#ifndef FIRMWARE_VERSION_H
#define FIRMWARE_VERSION_H

#include <stdint.h>

#define FW_VERSION_MAJOR 1U
#define FW_VERSION_MINOR 0U
#define FW_VERSION_PATCH 0U

#define FW_VERSION_NUMBER_MAKE(major, minor, patch) \
    ((((uint32_t)(major) & 0xFFUL) << 16U) | \
     (((uint32_t)(minor) & 0xFFUL) << 8U) | \
     ((uint32_t)(patch) & 0xFFUL))

#define FW_VERSION_NUMBER \
    FW_VERSION_NUMBER_MAKE(FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH)
#define FW_VERSION_STRING "1.0.0"

#define FW_INFO_MAGIC            0x46575632UL /* "FWV2" */
#define FW_INFO_FORMAT_VERSION   1UL
#define FW_INFO_SIZE             32UL
#define FW_INFO_OFFSET           0x00000200UL
#define FW_INFO_ADDRESS          0x08020200UL

typedef struct
{
    uint32_t magic;
    uint32_t format_version;
    uint32_t info_size;
    uint32_t board_id;
    uint32_t firmware_version;
    uint32_t application_address;
    uint32_t reserved[2];
} FirmwareVersionInfo_t;

#if defined(__cplusplus)
static_assert(sizeof(FirmwareVersionInfo_t) == FW_INFO_SIZE,
              "Firmware version info layout changed");
#else
_Static_assert(sizeof(FirmwareVersionInfo_t) == FW_INFO_SIZE,
               "Firmware version info layout changed");
#endif

extern const FirmwareVersionInfo_t g_firmware_version_info;

#endif /* FIRMWARE_VERSION_H */
