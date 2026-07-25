#ifndef BL_ESP8266_H
#define BL_ESP8266_H

#include "ota_update.h"
#include <stdint.h>

uint8_t BL_ESP8266_InitWiFi(void);
uint8_t BL_ESP8266_PreflightImage(const OTA_Metadata_t *metadata);
uint8_t BL_ESP8266_DownloadImage(const OTA_Metadata_t *metadata);

#endif /* BL_ESP8266_H */
