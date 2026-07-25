#include "bl_flash.h"
#include "bl_crypto.h"
#include "firmware_version.h"
#include "ota_update.h"
#include "stm32f4xx_hal.h"
#include <string.h>

_Static_assert(FW_INFO_ADDRESS == (OTA_APPLICATION_ADDRESS + FW_INFO_OFFSET),
               "Firmware info address must follow the application layout");

typedef struct
{
    uint32_t address;
    uint32_t expected_size;
    uint32_t bytes_written;
    uint32_t crc32;
    BL_OTA_HashContext_t hash;
    uint8_t word[4];
    uint8_t word_length;
    uint8_t active;
    uint8_t failed;
} BL_FlashWriter_t;

static BL_FlashWriter_t s_writer;

static uint8_t BL_Flash_VectorIsValid(uint32_t image_size)
{
    uint32_t reset_handler = *(const uint32_t *)(OTA_APPLICATION_ADDRESS + 4U);
    uint32_t reset_address = reset_handler & ~1UL;

    return (OTA_ApplicationVectorIsValid() &&
            reset_address < (OTA_APPLICATION_ADDRESS + image_size)) ? 1U : 0U;
}

static uint8_t BL_Flash_VersionInfoIsValid(uint32_t image_size,
                                           uint32_t expected_version)
{
    const FirmwareVersionInfo_t *info =
        (const FirmwareVersionInfo_t *)FW_INFO_ADDRESS;

    if (expected_version == 0U ||
        image_size < (FW_INFO_OFFSET + sizeof(FirmwareVersionInfo_t)))
    {
        return 0U;
    }

    return (info->magic == FW_INFO_MAGIC &&
            info->format_version == FW_INFO_FORMAT_VERSION &&
            info->info_size == FW_INFO_SIZE &&
            info->board_id == OTA_BOARD_ID &&
            info->application_address == OTA_APPLICATION_ADDRESS &&
            info->firmware_version == expected_version) ? 1U : 0U;
}

uint8_t BL_Flash_ApplicationHeaderIsValid(uint32_t expected_version)
{
    return (expected_version != 0U &&
            BL_Flash_VectorIsValid(OTA_APPLICATION_SIZE) &&
            BL_Flash_VersionInfoIsValid(OTA_APPLICATION_SIZE,
                                        expected_version)) ? 1U : 0U;
}

static uint8_t BL_Flash_ProgramWord(const uint8_t word_bytes[4])
{
    uint32_t word;

    memcpy(&word, word_bytes, sizeof(word));
    if (s_writer.address > (OTA_FLASH_END_ADDRESS - sizeof(uint32_t)) ||
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, s_writer.address, word) != HAL_OK ||
        *(const uint32_t *)s_writer.address != word)
    {
        s_writer.failed = 1U;
        return 1U;
    }

    s_writer.address += sizeof(uint32_t);
    return 0U;
}

uint8_t BL_Flash_Begin(uint32_t expected_size)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t sector_error = 0U;

    if (expected_size < 8U || expected_size > OTA_APPLICATION_SIZE)
    {
        return 1U;
    }

    memset(&s_writer, 0, sizeof(s_writer));
    s_writer.address = OTA_APPLICATION_ADDRESS;
    s_writer.expected_size = expected_size;
    s_writer.crc32 = 0xFFFFFFFFUL;
    if (BL_OTA_HashInit(&s_writer.hash) != 0U)
    {
        return 1U;
    }

    if (HAL_FLASH_Unlock() != HAL_OK)
    {
        return 1U;
    }

    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Banks = FLASH_BANK_1;
    erase.Sector = FLASH_SECTOR_5;
    erase.NbSectors = 3U;
    erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return 1U;
    }

    s_writer.active = 1U;
    return 0U;
}

uint8_t BL_Flash_Write(const uint8_t *data, uint32_t length)
{
    uint32_t index;

    if (!s_writer.active || s_writer.failed || data == NULL ||
        length > (s_writer.expected_size - s_writer.bytes_written))
    {
        return 1U;
    }

    for (index = 0U; index < length; index++)
    {
        uint8_t byte = data[index];

        s_writer.crc32 = OTA_CRC32_Update(s_writer.crc32, &byte, 1U);
        if (BL_OTA_HashUpdate(&s_writer.hash, &byte, 1U) != 0U)
        {
            s_writer.failed = 1U;
            return 1U;
        }
        s_writer.word[s_writer.word_length++] = byte;
        s_writer.bytes_written++;

        if (s_writer.word_length == sizeof(s_writer.word))
        {
            if (BL_Flash_ProgramWord(s_writer.word) != 0U)
            {
                return 1U;
            }
            s_writer.word_length = 0U;
        }
    }

    return 0U;
}

OTA_Error_t BL_Flash_Finish(uint32_t expected_crc32,
                            const uint8_t expected_hash[OTA_IMAGE_HASH_SIZE],
                            uint32_t expected_version)
{
    uint32_t actual_crc32;
    uint8_t actual_hash[BL_OTA_HASH_SIZE];

    if (!s_writer.active || s_writer.failed ||
        s_writer.bytes_written != s_writer.expected_size || expected_hash == NULL)
    {
        BL_Flash_Abort();
        return OTA_ERROR_DOWNLOAD;
    }

    if (s_writer.word_length != 0U)
    {
        while (s_writer.word_length < sizeof(s_writer.word))
        {
            s_writer.word[s_writer.word_length++] = 0xFFU;
        }
        if (BL_Flash_ProgramWord(s_writer.word) != 0U)
        {
            BL_Flash_Abort();
            return OTA_ERROR_DOWNLOAD;
        }
    }

    actual_crc32 = s_writer.crc32 ^ 0xFFFFFFFFUL;
    if (BL_OTA_HashFinish(&s_writer.hash, actual_hash) != 0U)
    {
        BL_Flash_Abort();
        return OTA_ERROR_IMAGE_HASH;
    }
    s_writer.active = 0U;
    HAL_FLASH_Lock();

    if (actual_crc32 != expected_crc32)
    {
        return OTA_ERROR_IMAGE_CRC;
    }
    if (!BL_OTA_HashMatches(actual_hash, expected_hash))
    {
        return OTA_ERROR_IMAGE_HASH;
    }
    if (!BL_Flash_VectorIsValid(s_writer.expected_size))
    {
        return OTA_ERROR_VECTOR;
    }
    if (!BL_Flash_VersionInfoIsValid(s_writer.expected_size, expected_version))
    {
        return OTA_ERROR_INVALID_REQUEST;
    }

    return OTA_ERROR_NONE;
}

OTA_Error_t BL_Flash_VerifyApplication(const OTA_Metadata_t *metadata)
{
    BL_OTA_HashContext_t hash_context;
    uint8_t actual_hash[BL_OTA_HASH_SIZE];
    uint32_t actual_crc32;

    if (metadata == NULL || metadata->image_size < 8U ||
        metadata->image_size > OTA_APPLICATION_SIZE ||
        metadata->target_version == 0U)
    {
        return OTA_ERROR_INVALID_REQUEST;
    }

    if (!BL_OTA_VerifySignature(metadata->board_id,
                                metadata->target_version,
                                metadata->image_size,
                                metadata->image_crc32,
                                metadata->image_hash,
                                metadata->url,
                                metadata->url_length,
                                metadata->signature))
    {
        return OTA_ERROR_SIGNATURE;
    }

    actual_crc32 = OTA_CRC32_Calculate(
        (const uint8_t *)OTA_APPLICATION_ADDRESS, metadata->image_size);
    if (actual_crc32 != metadata->image_crc32)
    {
        return OTA_ERROR_IMAGE_CRC;
    }

    if (BL_OTA_HashInit(&hash_context) != 0U ||
        BL_OTA_HashUpdate(&hash_context,
                          (const uint8_t *)OTA_APPLICATION_ADDRESS,
                          metadata->image_size) != 0U ||
        BL_OTA_HashFinish(&hash_context, actual_hash) != 0U ||
        !BL_OTA_HashMatches(actual_hash, metadata->image_hash))
    {
        return OTA_ERROR_IMAGE_HASH;
    }

    if (!BL_Flash_VectorIsValid(metadata->image_size))
    {
        return OTA_ERROR_VECTOR;
    }
    if (!BL_Flash_VersionInfoIsValid(metadata->image_size,
                                     metadata->target_version))
    {
        return OTA_ERROR_INVALID_REQUEST;
    }

    return OTA_ERROR_NONE;
}

void BL_Flash_Abort(void)
{
    s_writer.active = 0U;
    HAL_FLASH_Lock();
}

uint32_t BL_Flash_BytesWritten(void)
{
    return s_writer.bytes_written;
}
