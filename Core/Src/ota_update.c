#include "ota_update.h"
#include "stm32f4xx_hal.h"
#include <stddef.h>
#include <string.h>

static uint32_t OTA_MetadataCRC(const OTA_Metadata_t *metadata)
{
    const uint8_t *start = (const uint8_t *)&metadata->format_version;
    uint32_t length = (uint32_t)(offsetof(OTA_Metadata_t, metadata_crc32) -
                                 offsetof(OTA_Metadata_t, format_version));

    return OTA_CRC32_Calculate(start, length);
}

static uint8_t OTA_BufferIsZero(const uint8_t *buffer, uint32_t length)
{
    uint32_t index;
    uint8_t value = 0U;

    for (index = 0U; index < length; index++)
    {
        value |= buffer[index];
    }
    return value == 0U ? 1U : 0U;
}

static uint8_t OTA_BuffersEqual(const uint8_t *left,
                                const uint8_t *right,
                                uint32_t length)
{
    uint32_t index;
    uint8_t difference = 0U;

    if (left == NULL || right == NULL)
    {
        return 0U;
    }

    for (index = 0U; index < length; index++)
    {
        difference |= left[index] ^ right[index];
    }
    return difference == 0U ? 1U : 0U;
}

static uint8_t OTA_ErrorIsRetryableRequestFailure(OTA_Error_t error)
{
    return (error == OTA_ERROR_WIFI ||
            error == OTA_ERROR_PREFLIGHT) ? 1U : 0U;
}

static uint8_t OTA_URLIsSafe(const char *url, size_t length)
{
    size_t index;

    if (url == NULL || length == 0U || length >= OTA_URL_MAX_LENGTH ||
        (strncmp(url, "http://", 7U) != 0 &&
         strncmp(url, "https://", 8U) != 0))
    {
        return 0U;
    }

    for (index = 0U; index < length; index++)
    {
        uint8_t value = (uint8_t)url[index];
        if (value < 0x21U || value > 0x7EU || value == '"' ||
            value == '#' || value == '\\')
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t OTA_RecordIsErased(uint32_t address)
{
    uint32_t index;

    for (index = 0U; index < (OTA_METADATA_RECORD_SIZE / sizeof(uint32_t)); index++)
    {
        if (*(const volatile uint32_t *)(address + index * sizeof(uint32_t)) !=
            0xFFFFFFFFUL)
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t OTA_PrimaryHasValidRecord(void)
{
    uint32_t index;

    for (index = 0U; index < OTA_METADATA_RECORD_COUNT; index++)
    {
        const OTA_Metadata_t *candidate =
            (const OTA_Metadata_t *)(OTA_METADATA_ADDRESS +
                                     index * OTA_METADATA_RECORD_SIZE);
        if (OTA_Metadata_IsValid(candidate))
        {
            return 1U;
        }
    }
    return 0U;
}

static uint8_t OTA_EraseSectorUnlocked(uint32_t sector)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = 0U;

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Banks = FLASH_BANK_1;
    erase.Sector = sector;
    erase.NbSectors = 1U;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    return HAL_FLASHEx_Erase(&erase, &sector_error) == HAL_OK ? 0U : 1U;
}

static uint8_t OTA_ProgramRecord(uint32_t address, const OTA_Metadata_t *metadata)
{
    uint32_t index;
    uint32_t word;

    /* Commit magic last so an interrupted record can never become valid. */
    for (index = 1U; index < (sizeof(*metadata) / sizeof(uint32_t)); index++)
    {
        memcpy(&word, ((const uint8_t *)metadata) + index * sizeof(uint32_t),
               sizeof(word));
        if (word != 0xFFFFFFFFUL &&
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD,
                              address + index * sizeof(uint32_t), word) != HAL_OK)
        {
            return 1U;
        }
        if (*(const volatile uint32_t *)(address + index * sizeof(uint32_t)) != word)
        {
            return 1U;
        }
    }

    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, metadata->magic) != HAL_OK ||
        *(const volatile uint32_t *)address != metadata->magic)
    {
        return 1U;
    }
    return 0U;
}

static void OTA_PrepareRecord(OTA_Metadata_t *metadata, uint32_t sequence)
{
    metadata->magic = OTA_METADATA_MAGIC;
    metadata->format_version = OTA_METADATA_FORMAT_VERSION;
    metadata->record_size = OTA_METADATA_RECORD_SIZE;
    metadata->sequence = sequence;
    metadata->metadata_crc32 = OTA_MetadataCRC(metadata);
}

static uint8_t OTA_AppendMetadata(OTA_Metadata_t *metadata)
{
    OTA_Metadata_t current;
    OTA_Metadata_t preserved;
    uint32_t address = OTA_METADATA_ADDRESS;
    uint32_t index;
    uint8_t compact = 0U;
    uint8_t has_current = 0U;
    uint8_t preserve_current = 0U;
    uint8_t primary_has_valid = 0U;
    uint8_t result;

    if (metadata == NULL)
    {
        return 1U;
    }

    if (OTA_Metadata_Read(&current) == 0U)
    {
        has_current = 1U;
        if (current.sequence >= 0xFFFFFFFEUL)
        {
            return 1U;
        }
        else
        {
            metadata->sequence = current.sequence + 1U;
        }
    }
    else
    {
        metadata->sequence = 1U;
    }

    if (!compact)
    {
        for (index = 0U; index < OTA_METADATA_RECORD_COUNT; index++)
        {
            address = OTA_METADATA_ADDRESS + index * OTA_METADATA_RECORD_SIZE;
            if (OTA_RecordIsErased(address))
            {
                break;
            }
        }
        if (index == OTA_METADATA_RECORD_COUNT)
        {
            compact = 1U;
        }
    }

    if (compact && has_current)
    {
        if ((metadata->state == OTA_STATE_REQUESTED ||
             metadata->state == OTA_STATE_INSTALLING ||
             (metadata->state == OTA_STATE_CONFIRMED &&
              metadata->last_result == OTA_RESULT_FAILED &&
              (current.state == OTA_STATE_REQUESTED ||
               current.state == OTA_STATE_INSTALLING))) &&
            OTA_Metadata_ReadConfirmedInstalled(metadata->installed_version,
                                                &preserved) == 0U)
        {
            preserve_current = 1U;
        }
        else
        {
            preserved = current;
            preserve_current = 1U;
        }
    }
    OTA_PrepareRecord(metadata, metadata->sequence);
    if (!OTA_Metadata_IsValid(metadata) ||
        (preserve_current && !OTA_Metadata_IsValid(&preserved)))
    {
        return 1U;
    }

    if (compact && has_current)
    {
        primary_has_valid = OTA_PrimaryHasValidRecord();
    }

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return 1U;
    }

    if (compact)
    {
        if (has_current && primary_has_valid)
        {
            uint32_t backup_address = OTA_METADATA_BACKUP_ADDRESS;

            if (OTA_EraseSectorUnlocked(FLASH_SECTOR_3) != 0U)
            {
                HAL_FLASH_Lock();
                return 1U;
            }
            if (preserve_current && preserved.sequence != current.sequence)
            {
                if (OTA_ProgramRecord(backup_address, &preserved) != 0U)
                {
                    HAL_FLASH_Lock();
                    return 1U;
                }
                backup_address += OTA_METADATA_RECORD_SIZE;
            }
            if (OTA_ProgramRecord(backup_address, &current) != 0U)
            {
                HAL_FLASH_Lock();
                return 1U;
            }
        }

        address = OTA_METADATA_ADDRESS;
        if (OTA_EraseSectorUnlocked(FLASH_SECTOR_4) != 0U)
        {
            HAL_FLASH_Lock();
            return 1U;
        }

        if (preserve_current)
        {
            if (OTA_ProgramRecord(address, &preserved) != 0U)
            {
                HAL_FLASH_Lock();
                return 1U;
            }
            address += OTA_METADATA_RECORD_SIZE;
        }
    }

    result = OTA_ProgramRecord(address, metadata);
    if (result == 0U && compact && has_current)
    {
        /* The primary log is committed; stale backup cleanup is best effort. */
        (void)OTA_EraseSectorUnlocked(FLASH_SECTOR_3);
    }
    HAL_FLASH_Lock();
    return result;
}

uint32_t OTA_CRC32_Update(uint32_t crc, const uint8_t *data, uint32_t length)
{
    uint32_t i;
    uint8_t bit;

    if (data == NULL)
    {
        return crc;
    }

    for (i = 0U; i < length; i++)
    {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320UL : 0UL);
        }
    }

    return crc;
}

uint32_t OTA_CRC32_Calculate(const uint8_t *data, uint32_t length)
{
    return OTA_CRC32_Update(0xFFFFFFFFUL, data, length) ^ 0xFFFFFFFFUL;
}

static uint32_t OTA_ManifestFingerprint(
    const char *url,
    uint32_t url_length,
    uint32_t image_size,
    uint32_t image_crc32,
    uint32_t target_version,
    uint32_t board_id,
    const uint8_t image_hash[OTA_IMAGE_HASH_SIZE],
    const uint8_t signature[OTA_SIGNATURE_SIZE])
{
    uint32_t fields[5];
    uint32_t crc = 0xFFFFFFFFUL;

    if (url == NULL || url_length == 0U || url_length >= OTA_URL_MAX_LENGTH ||
        image_hash == NULL || signature == NULL)
    {
        return 0U;
    }

    fields[0] = board_id;
    fields[1] = target_version;
    fields[2] = image_size;
    fields[3] = image_crc32;
    fields[4] = url_length;
    crc = OTA_CRC32_Update(crc, (const uint8_t *)fields, sizeof(fields));
    crc = OTA_CRC32_Update(crc, image_hash, OTA_IMAGE_HASH_SIZE);
    crc = OTA_CRC32_Update(crc, signature, OTA_SIGNATURE_SIZE);
    crc = OTA_CRC32_Update(crc, (const uint8_t *)url, url_length);
    return crc ^ 0xFFFFFFFFUL;
}

uint8_t OTA_Metadata_IsValid(const OTA_Metadata_t *metadata)
{
    uint8_t has_payload;
    uint8_t has_failed_request;

    if (metadata == NULL ||
        metadata->magic != OTA_METADATA_MAGIC ||
        metadata->format_version != OTA_METADATA_FORMAT_VERSION ||
        metadata->record_size != OTA_METADATA_RECORD_SIZE ||
        metadata->sequence == 0U || metadata->sequence == 0xFFFFFFFFUL ||
        metadata->state < OTA_STATE_CONFIRMED ||
        metadata->state > OTA_STATE_RECOVERY ||
        metadata->last_result > OTA_RESULT_FAILED ||
        metadata->error_code < OTA_ERROR_NONE ||
        metadata->error_code > OTA_ERROR_METADATA ||
        metadata->board_id != OTA_BOARD_ID ||
        metadata->installed_version == 0U ||
        (metadata->flags & ~OTA_METADATA_KNOWN_FLAGS) != 0U ||
        metadata->install_attempts > OTA_MAX_INSTALL_ATTEMPTS ||
        metadata->trial_attempts > OTA_MAX_TRIAL_ATTEMPTS ||
        metadata->recovery_cycles > OTA_MAX_RECOVERY_CYCLES ||
        metadata->failed_request_attempts > OTA_MAX_FAILED_REQUEST_ATTEMPTS ||
        metadata->metadata_crc32 != OTA_MetadataCRC(metadata))
    {
        return 0U;
    }

    has_failed_request = metadata->failed_target_version != 0U ? 1U : 0U;
    if ((has_failed_request &&
         (metadata->state != OTA_STATE_CONFIRMED ||
          metadata->last_result != OTA_RESULT_FAILED ||
          metadata->error_code <= OTA_ERROR_NONE ||
          OTA_BufferIsZero(metadata->failed_signature,
                           OTA_SIGNATURE_SIZE) ||
          metadata->failed_request_attempts == 0U)) ||
        (!has_failed_request &&
         (!OTA_BufferIsZero(metadata->failed_signature,
                            OTA_SIGNATURE_SIZE) ||
          metadata->failed_manifest_crc32 != 0U ||
          metadata->failed_request_attempts != 0U)))
    {
        return 0U;
    }

    has_payload = metadata->target_version != 0U ? 1U : 0U;
    if (!has_payload)
    {
        return 0U;
    }

    if (metadata->image_size < 8U ||
        metadata->image_size > OTA_APPLICATION_SIZE ||
        metadata->url_length == 0U ||
        metadata->url_length >= OTA_URL_MAX_LENGTH ||
        metadata->url[metadata->url_length] != '\0' ||
        !OTA_URLIsSafe(metadata->url, metadata->url_length) ||
        OTA_BufferIsZero(metadata->image_hash, OTA_IMAGE_HASH_SIZE) ||
        OTA_BufferIsZero(metadata->signature, OTA_SIGNATURE_SIZE))
    {
        return 0U;
    }

    if (metadata->state == OTA_STATE_CONFIRMED &&
        metadata->last_result == OTA_RESULT_SUCCESS)
    {
        return metadata->installed_version == metadata->target_version ? 1U : 0U;
    }

    if (metadata->state == OTA_STATE_CONFIRMED &&
        metadata->last_result == OTA_RESULT_NONE &&
        metadata->error_code == OTA_ERROR_NONE)
    {
        return metadata->installed_version == metadata->target_version ? 1U : 0U;
    }

    if (metadata->state == OTA_STATE_CONFIRMED &&
        metadata->last_result == OTA_RESULT_FAILED &&
        metadata->error_code > OTA_ERROR_NONE &&
        metadata->target_version == metadata->installed_version)
    {
        return 1U;
    }

    if (metadata->state == OTA_STATE_RECOVERY)
    {
        return (metadata->last_result == OTA_RESULT_FAILED &&
                metadata->error_code > OTA_ERROR_NONE &&
                metadata->target_version >= metadata->installed_version) ? 1U : 0U;
    }

    if (metadata->target_version < metadata->installed_version ||
        (metadata->target_version == metadata->installed_version &&
         (metadata->flags & OTA_METADATA_FLAG_REINSTALL) == 0U))
    {
        return 0U;
    }

    if ((metadata->state == OTA_STATE_REQUESTED ||
         metadata->state == OTA_STATE_INSTALLING ||
         metadata->state == OTA_STATE_TRIAL) &&
        (metadata->last_result != OTA_RESULT_IN_PROGRESS ||
         metadata->error_code != OTA_ERROR_NONE))
    {
        return 0U;
    }

    if (metadata->state == OTA_STATE_CONFIRMED &&
        (metadata->last_result != OTA_RESULT_FAILED ||
         metadata->error_code <= OTA_ERROR_NONE))
    {
        return 0U;
    }

    return 1U;
}

uint8_t OTA_Metadata_Read(OTA_Metadata_t *metadata)
{
    OTA_Metadata_t candidate;
    uint32_t index;
    uint8_t found = 0U;

    if (metadata == NULL)
    {
        return 1U;
    }

    for (index = 0U; index < OTA_METADATA_BACKUP_RECORD_COUNT; index++)
    {
        memcpy(&candidate,
               (const void *)(OTA_METADATA_BACKUP_ADDRESS +
                              index * OTA_METADATA_RECORD_SIZE),
               sizeof(candidate));
        if (OTA_Metadata_IsValid(&candidate) &&
            (!found || (int32_t)(candidate.sequence - metadata->sequence) > 0))
        {
            *metadata = candidate;
            found = 1U;
        }
    }

    for (index = 0U; index < OTA_METADATA_RECORD_COUNT; index++)
    {
        memcpy(&candidate,
               (const void *)(OTA_METADATA_ADDRESS + index * OTA_METADATA_RECORD_SIZE),
               sizeof(candidate));
        if (OTA_Metadata_IsValid(&candidate) &&
            (!found || (int32_t)(candidate.sequence - metadata->sequence) > 0))
        {
            *metadata = candidate;
            found = 1U;
        }
    }

    return found ? 0U : 1U;
}

uint8_t OTA_Metadata_ReadConfirmedInstalled(uint32_t installed_version,
                                            OTA_Metadata_t *metadata)
{
    OTA_Metadata_t candidate;
    uint32_t index;
    uint8_t found = 0U;

    if (installed_version == 0U || metadata == NULL)
    {
        return 1U;
    }

    for (index = 0U; index < OTA_METADATA_BACKUP_RECORD_COUNT; index++)
    {
        memcpy(&candidate,
               (const void *)(OTA_METADATA_BACKUP_ADDRESS +
                              index * OTA_METADATA_RECORD_SIZE),
               sizeof(candidate));
        if (OTA_Metadata_IsValid(&candidate) &&
            candidate.state == OTA_STATE_CONFIRMED &&
            candidate.installed_version == installed_version &&
            candidate.target_version == installed_version &&
            candidate.image_size >= 8U &&
            (!found ||
             (int32_t)(candidate.sequence - metadata->sequence) > 0))
        {
            *metadata = candidate;
            found = 1U;
        }
    }

    for (index = 0U; index < OTA_METADATA_RECORD_COUNT; index++)
    {
        memcpy(&candidate,
               (const void *)(OTA_METADATA_ADDRESS +
                              index * OTA_METADATA_RECORD_SIZE),
               sizeof(candidate));
        if (OTA_Metadata_IsValid(&candidate) &&
            candidate.state == OTA_STATE_CONFIRMED &&
            candidate.installed_version == installed_version &&
            candidate.target_version == installed_version &&
            candidate.image_size >= 8U &&
            (!found ||
             (int32_t)(candidate.sequence - metadata->sequence) > 0))
        {
            *metadata = candidate;
            found = 1U;
        }
    }

    return found ? 0U : 1U;
}

uint8_t OTA_Metadata_WriteRequest(const char *url,
                                  uint32_t image_size,
                                  uint32_t image_crc32,
                                  uint32_t target_version,
                                  uint32_t board_id,
                                  const uint8_t image_hash[OTA_IMAGE_HASH_SIZE],
                                  const uint8_t signature[OTA_SIGNATURE_SIZE],
                                  uint32_t current_version)
{
    OTA_Metadata_t previous;
    OTA_Metadata_t metadata;
    uint32_t installed_version = current_version;
    size_t url_length;

    if (url == NULL || image_hash == NULL || signature == NULL ||
        current_version == 0U || board_id != OTA_BOARD_ID ||
        image_size < 8U || image_size > OTA_APPLICATION_SIZE)
    {
        return 1U;
    }

    url_length = strlen(url);
    if (!OTA_URLIsSafe(url, url_length))
    {
        return 1U;
    }

    if (OTA_Metadata_Read(&previous) == 0U)
    {
        if (previous.state != OTA_STATE_CONFIRMED)
        {
            return 1U;
        }
        if (previous.installed_version > installed_version)
        {
            installed_version = previous.installed_version;
        }
    }

    if (target_version <= installed_version ||
        OTA_BufferIsZero(image_hash, OTA_IMAGE_HASH_SIZE) ||
        OTA_BufferIsZero(signature, OTA_SIGNATURE_SIZE))
    {
        return 1U;
    }

    memset(&metadata, 0, sizeof(metadata));
    metadata.state = OTA_STATE_REQUESTED;
    metadata.last_result = OTA_RESULT_IN_PROGRESS;
    metadata.error_code = OTA_ERROR_NONE;
    metadata.installed_version = installed_version;
    metadata.target_version = target_version;
    metadata.board_id = board_id;
    metadata.image_size = image_size;
    metadata.image_crc32 = image_crc32;
    metadata.url_length = (uint32_t)url_length;
    memcpy(metadata.image_hash, image_hash, sizeof(metadata.image_hash));
    memcpy(metadata.signature, signature, sizeof(metadata.signature));
    memcpy(metadata.url, url, url_length + 1U);
    return OTA_AppendMetadata(&metadata);
}

uint8_t OTA_Metadata_BeginInstall(void)
{
    OTA_Metadata_t metadata;

    if (OTA_Metadata_Read(&metadata) != 0U ||
        (metadata.state != OTA_STATE_REQUESTED &&
         metadata.state != OTA_STATE_INSTALLING) ||
        metadata.install_attempts >= OTA_MAX_INSTALL_ATTEMPTS)
    {
        return 1U;
    }

    if (metadata.state == OTA_STATE_REQUESTED)
    {
        metadata.flags &= ~OTA_METADATA_FLAG_FLASH_STARTED;
    }
    metadata.state = OTA_STATE_INSTALLING;
    metadata.last_result = OTA_RESULT_IN_PROGRESS;
    metadata.error_code = OTA_ERROR_NONE;
    metadata.install_attempts++;
    return OTA_AppendMetadata(&metadata);
}

uint8_t OTA_Metadata_MarkFlashStarted(void)
{
    OTA_Metadata_t metadata;

    if (OTA_Metadata_Read(&metadata) != 0U ||
        metadata.state != OTA_STATE_INSTALLING)
    {
        return 1U;
    }

    if ((metadata.flags & OTA_METADATA_FLAG_FLASH_STARTED) != 0U)
    {
        return 0U;
    }

    metadata.flags |= OTA_METADATA_FLAG_FLASH_STARTED;
    return OTA_AppendMetadata(&metadata);
}

uint8_t OTA_Metadata_MarkTrial(void)
{
    OTA_Metadata_t metadata;

    if (OTA_Metadata_Read(&metadata) != 0U ||
        metadata.state != OTA_STATE_INSTALLING)
    {
        return 1U;
    }

    metadata.state = OTA_STATE_TRIAL;
    metadata.last_result = OTA_RESULT_IN_PROGRESS;
    metadata.error_code = OTA_ERROR_NONE;
    metadata.trial_attempts = 0U;
    metadata.flags &= ~OTA_METADATA_FLAG_FLASH_STARTED;
    return OTA_AppendMetadata(&metadata);
}

uint8_t OTA_Metadata_RecordTrialBoot(void)
{
    OTA_Metadata_t metadata;

    if (OTA_Metadata_Read(&metadata) != 0U ||
        metadata.state != OTA_STATE_TRIAL ||
        metadata.trial_attempts >= OTA_MAX_TRIAL_ATTEMPTS)
    {
        return 1U;
    }

    metadata.trial_attempts++;
    return OTA_AppendMetadata(&metadata);
}

uint8_t OTA_Metadata_MarkFailure(OTA_Error_t error, uint8_t application_intact)
{
    OTA_Metadata_t metadata;
    OTA_Metadata_t installed;
    uint32_t failed_target_version;
    uint32_t failed_manifest_crc32;
    uint32_t failed_request_attempts;
    uint8_t failed_signature[OTA_SIGNATURE_SIZE];

    if (error <= OTA_ERROR_NONE || error > OTA_ERROR_METADATA ||
        OTA_Metadata_Read(&metadata) != 0U)
    {
        return 1U;
    }

    if (application_intact)
    {
        if (OTA_Metadata_ReadConfirmedInstalled(metadata.installed_version,
                                                &installed) != 0U)
        {
            /* Never manufacture a CONFIRMED record without a signed image
               description. Continue in fail-closed RECOVERY instead. */
            application_intact = 0U;
        }
        else
        {
            failed_target_version = metadata.target_version;
            memcpy(failed_signature, metadata.signature,
                   sizeof(failed_signature));
            failed_manifest_crc32 = OTA_ManifestFingerprint(
                metadata.url, metadata.url_length, metadata.image_size,
                metadata.image_crc32, metadata.target_version,
                metadata.board_id, metadata.image_hash, metadata.signature);
            if (installed.failed_target_version == failed_target_version &&
                installed.failed_manifest_crc32 == failed_manifest_crc32 &&
                OTA_BuffersEqual(installed.failed_signature, failed_signature,
                                 sizeof(failed_signature)))
            {
                failed_request_attempts = installed.failed_request_attempts;
                if (failed_request_attempts < OTA_MAX_FAILED_REQUEST_ATTEMPTS)
                {
                    failed_request_attempts++;
                }
            }
            else
            {
                failed_request_attempts = 1U;
            }
            metadata = installed;
            metadata.state = OTA_STATE_CONFIRMED;
            metadata.last_result = OTA_RESULT_FAILED;
            metadata.error_code = error;
            metadata.flags = 0U;
            metadata.failed_target_version = failed_target_version;
            memcpy(metadata.failed_signature, failed_signature,
                   sizeof(metadata.failed_signature));
            metadata.failed_manifest_crc32 = failed_manifest_crc32;
            metadata.failed_request_attempts = failed_request_attempts;
            return OTA_AppendMetadata(&metadata);
        }
    }

    if (!application_intact && metadata.state == OTA_STATE_CONFIRMED &&
        metadata.target_version == metadata.installed_version &&
        metadata.image_size >= 8U)
    {
        metadata.flags |= OTA_METADATA_FLAG_REINSTALL;
    }
    if (!application_intact)
    {
        metadata.failed_target_version = 0U;
        memset(metadata.failed_signature, 0,
               sizeof(metadata.failed_signature));
        metadata.failed_manifest_crc32 = 0U;
        metadata.failed_request_attempts = 0U;
    }
    metadata.state = application_intact ? OTA_STATE_CONFIRMED : OTA_STATE_RECOVERY;
    metadata.last_result = OTA_RESULT_FAILED;
    metadata.error_code = error;
    return OTA_AppendMetadata(&metadata);
}

uint8_t OTA_Metadata_RetryRecovery(void)
{
    OTA_Metadata_t metadata;

    if (OTA_Metadata_Read(&metadata) != 0U ||
        metadata.state != OTA_STATE_RECOVERY ||
        metadata.target_version < metadata.installed_version ||
        (metadata.target_version == metadata.installed_version &&
         (metadata.flags & OTA_METADATA_FLAG_REINSTALL) == 0U) ||
        metadata.image_size < 8U)
    {
        return OTA_RECOVERY_RETRY_ERROR;
    }

    if (metadata.recovery_cycles >= OTA_MAX_RECOVERY_CYCLES)
    {
        return OTA_RECOVERY_RETRY_LIMIT_REACHED;
    }

    metadata.state = OTA_STATE_INSTALLING;
    metadata.last_result = OTA_RESULT_IN_PROGRESS;
    metadata.error_code = OTA_ERROR_NONE;
    metadata.install_attempts = 0U;
    metadata.recovery_cycles++;
    metadata.flags |= OTA_METADATA_FLAG_FLASH_STARTED;
    return OTA_AppendMetadata(&metadata) == 0U ?
           OTA_RECOVERY_RETRY_OK : OTA_RECOVERY_RETRY_ERROR;
}

uint8_t OTA_Metadata_ConfirmApplication(uint32_t current_version)
{
    OTA_Metadata_t metadata;

    if (current_version == 0U)
    {
        return 1U;
    }

    if (OTA_Metadata_Read(&metadata) != 0U)
    {
        return 1U;
    }

    if (metadata.state == OTA_STATE_CONFIRMED)
    {
        return metadata.installed_version == current_version ? 0U : 1U;
    }

    if (metadata.state != OTA_STATE_TRIAL ||
        metadata.target_version != current_version ||
        metadata.trial_attempts == 0U)
    {
        return 1U;
    }

    metadata.state = OTA_STATE_CONFIRMED;
    metadata.last_result = OTA_RESULT_SUCCESS;
    metadata.error_code = OTA_ERROR_NONE;
    metadata.installed_version = current_version;
    metadata.flags = 0U;
    metadata.recovery_cycles = 0U;
    metadata.failed_target_version = 0U;
    memset(metadata.failed_signature, 0, sizeof(metadata.failed_signature));
    metadata.failed_manifest_crc32 = 0U;
    metadata.failed_request_attempts = 0U;
    return OTA_AppendMetadata(&metadata);
}

static uint8_t OTA_Metadata_FailedRequestMatches(
    const OTA_Metadata_t *metadata,
    const char *url,
    uint32_t image_size,
    uint32_t image_crc32,
    uint32_t target_version,
    uint32_t board_id,
    const uint8_t image_hash[OTA_IMAGE_HASH_SIZE],
    const uint8_t signature[OTA_SIGNATURE_SIZE])
{
    uint32_t url_length;
    uint32_t fingerprint;

    if (metadata == NULL || url == NULL || image_hash == NULL ||
        signature == NULL || target_version == 0U ||
        metadata->state != OTA_STATE_CONFIRMED ||
        metadata->last_result != OTA_RESULT_FAILED ||
        metadata->failed_target_version != target_version)
    {
        return 0U;
    }

    url_length = (uint32_t)strlen(url);
    fingerprint = OTA_ManifestFingerprint(
        url, url_length, image_size, image_crc32, target_version, board_id,
        image_hash, signature);
    return (metadata->failed_manifest_crc32 == fingerprint &&
            OTA_BuffersEqual(metadata->failed_signature, signature,
                             OTA_SIGNATURE_SIZE)) ? 1U : 0U;
}

uint8_t OTA_Metadata_IsPermanentlyRejected(
    const OTA_Metadata_t *metadata,
    const char *url,
    uint32_t image_size,
    uint32_t image_crc32,
    uint32_t target_version,
    uint32_t board_id,
    const uint8_t image_hash[OTA_IMAGE_HASH_SIZE],
    const uint8_t signature[OTA_SIGNATURE_SIZE])
{
    if (!OTA_Metadata_FailedRequestMatches(
            metadata, url, image_size, image_crc32, target_version, board_id,
            image_hash, signature))
    {
        return 0U;
    }

    return (!OTA_ErrorIsRetryableRequestFailure(
                (OTA_Error_t)metadata->error_code) ||
            metadata->failed_request_attempts >=
                OTA_MAX_FAILED_REQUEST_ATTEMPTS) ? 1U : 0U;
}

uint8_t OTA_Metadata_Clear(void)
{
    uint8_t result;

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return 1U;
    }
    result = OTA_EraseSectorUnlocked(FLASH_SECTOR_3);
    if (result == 0U)
    {
        result = OTA_EraseSectorUnlocked(FLASH_SECTOR_4);
    }
    HAL_FLASH_Lock();
    return result;
}

uint8_t OTA_ApplicationVectorIsValid(void)
{
    uint32_t initial_sp = *(const uint32_t *)OTA_APPLICATION_ADDRESS;
    uint32_t reset_handler = *(const uint32_t *)(OTA_APPLICATION_ADDRESS + 4U);
    uint32_t reset_address = reset_handler & ~1UL;
    uint8_t sp_in_sram =
        ((initial_sp >= 0x20000000UL && initial_sp <= 0x20020000UL) ||
         (initial_sp >= 0x10000000UL && initial_sp <= 0x10010000UL)) ? 1U : 0U;

    return (sp_in_sram &&
            (reset_handler & 1UL) != 0U &&
            reset_address >= OTA_APPLICATION_ADDRESS &&
            reset_address < (OTA_APPLICATION_ADDRESS + OTA_APPLICATION_SIZE)) ? 1U : 0U;
}
