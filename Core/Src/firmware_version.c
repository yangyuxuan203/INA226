#include "firmware_version.h"
#include "ota_update.h"

__attribute__((used, section(".firmware_info")))
const FirmwareVersionInfo_t g_firmware_version_info = {
    .magic = FW_INFO_MAGIC,
    .format_version = FW_INFO_FORMAT_VERSION,
    .info_size = FW_INFO_SIZE,
    .board_id = OTA_BOARD_ID,
    .firmware_version = FW_VERSION_NUMBER,
    .application_address = OTA_APPLICATION_ADDRESS,
    .reserved = {0U, 0U},
};
