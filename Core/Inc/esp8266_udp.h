#ifndef __ESP8266_UDP_H__
#define __ESP8266_UDP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "wifi_config.h"

/* Update these values to match your ESP32-S3 WiFi/UDP setup. */
#define ESP8266_WIFI_SSID        WIFI_SSID
#define ESP8266_WIFI_PASSWORD    WIFI_PASSWORD
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

typedef struct {
    float real_hour_sin;
    float real_hour_cos;
    float lux;
    float pv_v;
    float pv_p;
    float home_v;
    float home_soc;
    float load_p;
    float car_soc;
    float human_soc;
    uint8_t pvsrc;
    uint8_t hsrc;
    uint8_t rigid;
    uint8_t led;
    uint8_t fan;
    uint8_t qi;
    uint8_t hchg;
    uint8_t cchg;
    uint8_t v2h;
} EnergyLstmInput_t;

typedef struct {
    float future_pv_p;
    float future_load_p;
    float future_home_soc;
    uint32_t tick_ms;
    uint8_t valid;
} EnergyLstmPrediction_t;

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
