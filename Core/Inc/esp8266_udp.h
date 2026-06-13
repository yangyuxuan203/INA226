#ifndef __ESP8266_UDP_H__
#define __ESP8266_UDP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* Update these values to match your ESP32-S3 WiFi/UDP setup. */
#define ESP8266_WIFI_SSID        "wlh"
#define ESP8266_WIFI_PASSWORD    "20160912"
#define ESP8266_UDP_PORT         8888
#define ESP32S3_RX_PORT          8889

typedef struct {
    float bat_v;
    float bat_pct;
    uint16_t hr;
    uint16_t spo2;
    uint8_t state;
    uint8_t valid;
} ESP32S3_Data_t;

uint8_t ESP8266_UDP_Init(void);
uint8_t ESP8266_UDP_PollReceive(ESP32S3_Data_t *data, uint32_t timeout_ms);
uint8_t ESP8266_UDP_SendTelemetry(float lux, float home_load_power_w, float pv_power_w);
uint8_t ESP8266_UDP_HasPeer(void);

#ifdef __cplusplus
}
#endif

#endif /* __ESP8266_UDP_H__ */
