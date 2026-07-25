#ifndef BL_FLASH_H
#define BL_FLASH_H

#include "ota_update.h"
#include <stdint.h>

uint8_t BL_Flash_Begin(uint32_t expected_size);
uint8_t BL_Flash_Write(const uint8_t *data, uint32_t length);
OTA_Error_t BL_Flash_Finish(uint32_t expected_crc32,
                            const uint8_t expected_hash[OTA_IMAGE_HASH_SIZE],
                            uint32_t expected_version);
OTA_Error_t BL_Flash_VerifyApplication(const OTA_Metadata_t *metadata);
uint8_t BL_Flash_ApplicationHeaderIsValid(uint32_t expected_version);
void BL_Flash_Abort(void);
uint32_t BL_Flash_BytesWritten(void);

#endif /* BL_FLASH_H */
