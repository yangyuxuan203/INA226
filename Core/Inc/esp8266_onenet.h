#ifndef __ESP8266_ONENET_H__
#define __ESP8266_ONENET_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "esp8266_udp.h"
#include "onenet_config.h"
#include "ota_update.h"
#include <stdint.h>

#define ONENET_REQUEST_ID_MAX_LENGTH 48U

typedef enum {
    ONENET_REQUEST_SOURCE_NONE = 0,
    ONENET_REQUEST_SOURCE_PROPERTY_SET,
    ONENET_REQUEST_SOURCE_DESIRED
} OneNET_RequestSource_t;

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
    char url[OTA_URL_MAX_LENGTH];
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t image_version;
    uint32_t board_id;
    uint8_t image_hash[OTA_IMAGE_HASH_SIZE];
    uint8_t signature[OTA_SIGNATURE_SIZE];
    char request_id[ONENET_REQUEST_ID_MAX_LENGTH];
    uint8_t requested;
    uint8_t ready;
} OneNET_OTACommand_t;

typedef struct {
    int8_t home_feng;
    int8_t home_led;
    int8_t home_load;
    int8_t qi;
    uint8_t updated;
    OneNET_OTACommand_t ota;
    char request_id[ONENET_REQUEST_ID_MAX_LENGTH];
    uint8_t request_received;
    uint8_t request_id_valid;
    OneNET_RequestSource_t request_source;
} OneNET_Control_t;

uint8_t OneNET_MQTT_Open(void);
uint8_t OneNET_MQTT_Connect(void);
uint8_t OneNET_MQTT_SubscribeControl(void);
uint8_t OneNET_MQTT_RequestOTADesired(uint8_t start_new_request);
void OneNET_MQTT_ClearOTADesiredRequest(void);
uint8_t OneNET_MQTT_Ping(void);
uint8_t OneNET_MQTT_Process(OneNET_Control_t *ctrl, uint32_t timeout_ms);
uint8_t OneNET_MQTT_ReplyPropertySet(const char *request_id,
                                    uint16_t code,
                                    const char *message);
uint8_t OneNET_Upload(const OneNET_UploadData_t *data);
uint8_t OneNET_UploadSwitchStates(uint8_t home_feng, uint8_t home_led,
                                  uint8_t home_load, uint8_t qi);
uint8_t OneNET_UploadOTAStatus(uint32_t current_version,
                               uint32_t target_version,
                               OTA_State_t state,
                               uint8_t progress,
                               OTA_Result_t result,
                               OTA_Error_t error);

#ifdef __cplusplus
}
#endif

#endif /* __ESP8266_ONENET_H__ */
