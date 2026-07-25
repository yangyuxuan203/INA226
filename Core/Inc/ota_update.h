#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* STM32F407ZET6, 512 KiB internal Flash layout. */
#define OTA_FLASH_BASE_ADDRESS       0x08000000UL
#define OTA_BOOT_ADDRESS             0x08000000UL
#define OTA_BOOT_SIZE                0x0000C000UL /* sectors 0-2: 48 KiB */
#define OTA_METADATA_BACKUP_ADDRESS  0x0800C000UL
#define OTA_METADATA_BACKUP_SIZE     0x00004000UL /* sector 3: 16 KiB */
#define OTA_METADATA_ADDRESS         0x08010000UL
#define OTA_METADATA_SIZE            0x00010000UL /* sector 4: 64 KiB */
#define OTA_APPLICATION_ADDRESS      0x08020000UL
#define OTA_APPLICATION_SIZE         0x00060000UL /* sectors 5-7: 384 KiB */
#define OTA_FLASH_END_ADDRESS        0x08080000UL

#define OTA_BOARD_ID                 0xF4070001UL
#define OTA_METADATA_MAGIC           0x4F544133UL /* "OTA3" */
#define OTA_METADATA_FORMAT_VERSION  3UL
#define OTA_METADATA_RECORD_SIZE     512U
#define OTA_METADATA_RECORD_COUNT    (OTA_METADATA_SIZE / OTA_METADATA_RECORD_SIZE)
#define OTA_METADATA_BACKUP_RECORD_COUNT 2U
#define OTA_URL_MAX_LENGTH           256U
#define OTA_IMAGE_HASH_SIZE          32U
#define OTA_SIGNATURE_SIZE           64U

#define OTA_MAX_INSTALL_ATTEMPTS     3U
#define OTA_MAX_TRIAL_ATTEMPTS       3U
#define OTA_MAX_RECOVERY_CYCLES      3U
#define OTA_MAX_FAILED_REQUEST_ATTEMPTS 3U

#define OTA_RECOVERY_RETRY_OK             0U
#define OTA_RECOVERY_RETRY_ERROR          1U
#define OTA_RECOVERY_RETRY_LIMIT_REACHED  2U

#define OTA_METADATA_FLAG_FLASH_STARTED  (1UL << 0U)
#define OTA_METADATA_FLAG_REINSTALL       (1UL << 1U)
#define OTA_METADATA_KNOWN_FLAGS          \
    (OTA_METADATA_FLAG_FLASH_STARTED | OTA_METADATA_FLAG_REINSTALL)

typedef enum
{
    OTA_STATE_CONFIRMED = 1,
    OTA_STATE_REQUESTED = 2,
    OTA_STATE_INSTALLING = 3,
    OTA_STATE_TRIAL = 4,
    OTA_STATE_RECOVERY = 5
} OTA_State_t;

typedef enum
{
    OTA_RESULT_NONE = 0,
    OTA_RESULT_IN_PROGRESS = 1,
    OTA_RESULT_SUCCESS = 2,
    OTA_RESULT_FAILED = 3
} OTA_Result_t;

typedef enum
{
    OTA_ERROR_NONE = 0,
    OTA_ERROR_INVALID_REQUEST = 1,
    OTA_ERROR_DOWNGRADE = 2,
    OTA_ERROR_WRONG_BOARD = 3,
    OTA_ERROR_SIGNATURE = 4,
    OTA_ERROR_WIFI = 5,
    OTA_ERROR_PREFLIGHT = 6,
    OTA_ERROR_FLASH_ERASE = 7,
    OTA_ERROR_DOWNLOAD = 8,
    OTA_ERROR_IMAGE_CRC = 9,
    OTA_ERROR_IMAGE_HASH = 10,
    OTA_ERROR_VECTOR = 11,
    OTA_ERROR_INSTALL_ATTEMPTS = 12,
    OTA_ERROR_TRIAL_ATTEMPTS = 13,
    OTA_ERROR_METADATA = 14
} OTA_Error_t;

/*
 * Sector 4 contains 128 append-only records; Sector 3 holds the current
 * record and the previous signed CONFIRMED record during compaction. The
 * magic word is programmed last, so an interrupted record is ignored on the
 * next boot.
 */
typedef struct
{
    uint32_t magic;
    uint32_t format_version;
    uint32_t record_size;
    uint32_t sequence;
    uint32_t state;
    uint32_t last_result;
    int32_t error_code;
    uint32_t install_attempts;
    uint32_t trial_attempts;
    uint32_t installed_version;
    uint32_t target_version;
    uint32_t board_id;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t url_length;
    uint8_t image_hash[OTA_IMAGE_HASH_SIZE];
    uint8_t signature[OTA_SIGNATURE_SIZE];
    char url[OTA_URL_MAX_LENGTH];
    uint32_t flags;
    uint32_t recovery_cycles;
    uint32_t failed_target_version;
    uint8_t failed_signature[OTA_SIGNATURE_SIZE];
    uint32_t failed_manifest_crc32;
    uint32_t failed_request_attempts;
    uint8_t reserved[12];
    uint32_t metadata_crc32;
} OTA_Metadata_t;

#if defined(__cplusplus)
static_assert(sizeof(OTA_Metadata_t) == OTA_METADATA_RECORD_SIZE,
              "OTA metadata record must be exactly 512 bytes");
static_assert((OTA_METADATA_SIZE % OTA_METADATA_RECORD_SIZE) == 0U,
              "OTA metadata records must fill the Flash sector");
static_assert((OTA_METADATA_RECORD_SIZE * OTA_METADATA_BACKUP_RECORD_COUNT) <=
                  OTA_METADATA_BACKUP_SIZE,
              "OTA metadata backup sector is too small");
#else
_Static_assert(sizeof(OTA_Metadata_t) == OTA_METADATA_RECORD_SIZE,
               "OTA metadata record must be exactly 512 bytes");
_Static_assert((OTA_METADATA_SIZE % OTA_METADATA_RECORD_SIZE) == 0U,
               "OTA metadata records must fill the Flash sector");
_Static_assert((OTA_METADATA_RECORD_SIZE * OTA_METADATA_BACKUP_RECORD_COUNT) <=
                   OTA_METADATA_BACKUP_SIZE,
               "OTA metadata backup sector is too small");
#endif

uint32_t OTA_CRC32_Update(uint32_t crc, const uint8_t *data, uint32_t length);
uint32_t OTA_CRC32_Calculate(const uint8_t *data, uint32_t length);
uint8_t OTA_Metadata_IsValid(const OTA_Metadata_t *metadata);
uint8_t OTA_Metadata_Read(OTA_Metadata_t *metadata);
uint8_t OTA_Metadata_WriteRequest(const char *url,
                                  uint32_t image_size,
                                  uint32_t image_crc32,
                                  uint32_t target_version,
                                  uint32_t board_id,
                                  const uint8_t image_hash[OTA_IMAGE_HASH_SIZE],
                                  const uint8_t signature[OTA_SIGNATURE_SIZE],
                                  uint32_t current_version);
uint8_t OTA_Metadata_BeginInstall(void);
uint8_t OTA_Metadata_MarkFlashStarted(void);
uint8_t OTA_Metadata_MarkTrial(void);
uint8_t OTA_Metadata_RecordTrialBoot(void);
uint8_t OTA_Metadata_MarkFailure(OTA_Error_t error, uint8_t application_intact);
uint8_t OTA_Metadata_RetryRecovery(void);
uint8_t OTA_Metadata_ConfirmApplication(uint32_t current_version);
uint8_t OTA_Metadata_ReadConfirmedInstalled(uint32_t installed_version,
                                            OTA_Metadata_t *metadata);
uint8_t OTA_Metadata_IsPermanentlyRejected(
    const OTA_Metadata_t *metadata,
    const char *url,
    uint32_t image_size,
    uint32_t image_crc32,
    uint32_t target_version,
    uint32_t board_id,
    const uint8_t image_hash[OTA_IMAGE_HASH_SIZE],
    const uint8_t signature[OTA_SIGNATURE_SIZE]);
uint8_t OTA_Metadata_Clear(void);
uint8_t OTA_ApplicationVectorIsValid(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_UPDATE_H */
