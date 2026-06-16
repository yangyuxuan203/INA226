#ifndef __ESP8266_ONENET_H__
#define __ESP8266_ONENET_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "esp8266_udp.h"
#include <stdint.h>

#define ONENET_MQTT_HOST        "mqtts.heclouds.com"
#define ONENET_MQTT_PORT        1883
#define ONENET_PRODUCT_ID       "wqb2ND1yNn"
#define ONENET_ACCESS_KEY       "ZFJIYlJ3cWVwVnhVYjFIaUZ2bUw3dDB2Q1A1NWlTOVI="
#define ONENET_DEVICE_NAME      "XIAOMI"
#define ONENET_TOKEN_EXPIRE     1956499200U

typedef struct {
    uint8_t car_soc;
    uint8_t car_status;
    uint8_t home_feng;
    uint8_t home_led;
    uint8_t home_load;
    float home_soc;
    uint8_t home_status;
    uint16_t human_heart;
    float human_soc;
    uint16_t human_spo2;
    uint8_t human_status;
    float load_power;
    float lux;
    float pv_power;
    uint8_t qi;
} OneNET_UploadData_t;

typedef struct {
    int8_t home_feng;
    int8_t home_led;
    int8_t home_load;
    int8_t qi;
    uint8_t updated;
} OneNET_Control_t;

uint8_t OneNET_MQTT_Open(void);
uint8_t OneNET_MQTT_Connect(void);
uint8_t OneNET_MQTT_SubscribeControl(void);
uint8_t OneNET_MQTT_Ping(void);
uint8_t OneNET_MQTT_Process(OneNET_Control_t *ctrl, uint32_t timeout_ms);
uint8_t OneNET_Upload(const OneNET_UploadData_t *data);
uint8_t OneNET_UploadSwitchStates(uint8_t home_feng, uint8_t home_led,
                                  uint8_t home_load, uint8_t qi);

#ifdef __cplusplus
}
#endif

#endif /* __ESP8266_ONENET_H__ */
