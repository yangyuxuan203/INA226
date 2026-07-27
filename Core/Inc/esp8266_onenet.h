#ifndef __ESP8266_ONENET_H__
#define __ESP8266_ONENET_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "esp8266_udp.h"
#include "onenet_config.h"
#include <stdint.h>

#define ONENET_REQUEST_ID_MAX_LENGTH 48U

typedef enum {
    ONENET_REQUEST_SOURCE_NONE = 0,
    ONENET_REQUEST_SOURCE_PROPERTY_SET
} OneNET_RequestSource_t;

typedef enum {
    ONENET_MQTT_RX_IDLE = 0,
    ONENET_MQTT_RX_PROPERTY_SET,
    ONENET_MQTT_RX_PING_RESPONSE,
    ONENET_MQTT_RX_ACK,
    ONENET_MQTT_RX_IGNORED,
    ONENET_MQTT_RX_PAYLOAD_ERROR,
    ONENET_MQTT_RX_PROTOCOL_ERROR,
    ONENET_MQTT_RX_LINK_CLOSED
} OneNET_MQTTRxResult_t;

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
    char request_id[ONENET_REQUEST_ID_MAX_LENGTH];
    uint8_t request_received;
    uint8_t request_id_valid;
    OneNET_RequestSource_t request_source;
} OneNET_Control_t;

uint8_t OneNET_MQTT_Open(void);
uint8_t OneNET_MQTT_Connect(void);
uint8_t OneNET_MQTT_SubscribeControl(void);
uint8_t OneNET_MQTT_Ping(void);
OneNET_MQTTRxResult_t OneNET_MQTT_Process(OneNET_Control_t *ctrl,
                                          uint32_t timeout_ms);
uint8_t OneNET_MQTT_ReplyPropertySet(const char *request_id,
                                    uint16_t code,
                                    const char *message);
uint8_t OneNET_Upload(const OneNET_UploadData_t *data);
uint8_t OneNET_UploadSwitchStates(uint8_t home_feng, uint8_t home_led,
                                  uint8_t home_load, uint8_t qi);

#ifdef __cplusplus
}
#endif

#endif /* __ESP8266_ONENET_H__ */
