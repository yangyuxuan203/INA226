#ifndef __ESP8266_UDP_H__
#define __ESP8266_UDP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "wifi_config.h"
#include "energy_types.h"

/* Update these values to match your ESP32-S3 WiFi/UDP setup. */
#define ESP8266_WIFI_SSID        WIFI_SSID
#define ESP8266_WIFI_PASSWORD    WIFI_PASSWORD
#define ESP8266_UDP_PORT         8888
#define ESP32S3_RX_PORT          8889

uint8_t ESP8266_UDP_Init(void);
uint8_t ESP8266_UDP_PollReceive(ESP32S3_Data_t *data, uint32_t timeout_ms);
uint8_t ESP8266_UDP_PollReceiveEx(ESP32S3_Data_t *data,
                                  EnergyLstmPrediction_t *pred,
                                  uint8_t *packet_flags,
                                  uint32_t timeout_ms);
uint8_t ESP8266_UDP_SendTelemetry(float lux, float home_load_power_w, float pv_power_w);
uint8_t ESP8266_UDP_SendLstmInput(const EnergyLstmInput_t *input);
uint8_t ESP8266_UDP_HasPeer(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESP8266_UDP_H__ */
