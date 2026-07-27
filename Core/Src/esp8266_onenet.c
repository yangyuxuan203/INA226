#include "esp8266_onenet.h"
#include "esp8266_onenet_at.h"
#include "MqttKit.h"
#include "base64.h"
#include "hmac_sha1.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

#ifndef ONENET_VERBOSE_LOG
#define ONENET_VERBOSE_LOG 0U
#endif
#define ONENET_RX_BUF_SIZE 1280U

static uint16_t s_onenet_msg_id = 1U;

static void OneNET_BuildPropertySetTopic(char *topic, size_t topic_size)
{
    if (topic != NULL && topic_size > 0U)
    {
        snprintf(topic, topic_size, "$sys/%s/%s/thing/property/set",
                 ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    }
}

static uint16_t OneNET_NextPacketId(void)
{
    s_onenet_msg_id++;
    if (s_onenet_msg_id == 0U)
    {
        s_onenet_msg_id = 1U;
    }
    return s_onenet_msg_id;
}

static unsigned char OneNET_UrlEncode(char *sign)
{
    char sign_t[80];
    unsigned int i = 0;
    unsigned int j = 0;
    unsigned int sign_len;

    if (sign == NULL)
    {
        return 1;
    }

    sign_len = (unsigned int)strlen(sign);
    if (sign_len >= sizeof(sign_t))
    {
        return 1;
    }

    memcpy(sign_t, sign, sign_len + 1U);
    sign[0] = '\0';

    for (i = 0; i < sign_len; i++)
    {
        switch (sign_t[i])
        {
            case '+': strcat(sign + j, "%2B"); j += 3U; break;
            case ' ': strcat(sign + j, "%20"); j += 3U; break;
            case '/': strcat(sign + j, "%2F"); j += 3U; break;
            case '?': strcat(sign + j, "%3F"); j += 3U; break;
            case '%': strcat(sign + j, "%25"); j += 3U; break;
            case '#': strcat(sign + j, "%23"); j += 3U; break;
            case '&': strcat(sign + j, "%26"); j += 3U; break;
            case '=': strcat(sign + j, "%3D"); j += 3U; break;
            default:
                sign[j++] = sign_t[i];
                sign[j] = '\0';
                break;
        }
    }

    return 0;
}

static uint8_t OneNET_BuildToken(char *authorization_buf, uint16_t authorization_buf_len)
{
    size_t olen = 0;
    char sign_buf[96];
    char hmac_sha1_buf[32];
    char access_key_base64[64];
    char string_for_signature[96];

    if (authorization_buf == NULL || authorization_buf_len < 120U)
    {
        return 1;
    }

    memset(access_key_base64, 0, sizeof(access_key_base64));
    if (BASE64_Decode((unsigned char *)access_key_base64, sizeof(access_key_base64), &olen,
                      (const unsigned char *)ONENET_ACCESS_KEY, strlen(ONENET_ACCESS_KEY)) != 0)
    {
        return 1;
    }

    snprintf(string_for_signature, sizeof(string_for_signature),
             "%lu\nsha1\nproducts/%s/devices/%s\n2018-10-31",
             (unsigned long)ONENET_TOKEN_EXPIRE,
             ONENET_PRODUCT_ID,
             ONENET_DEVICE_NAME);

    memset(hmac_sha1_buf, 0, sizeof(hmac_sha1_buf));
    hmac_sha1((unsigned char *)access_key_base64, (int)strlen(access_key_base64),
              (unsigned char *)string_for_signature, (int)strlen(string_for_signature),
              (unsigned char *)hmac_sha1_buf);

    olen = 0;
    memset(sign_buf, 0, sizeof(sign_buf));
    if (BASE64_Encode((unsigned char *)sign_buf, sizeof(sign_buf), &olen,
                      (const unsigned char *)hmac_sha1_buf, 20U) != 0)
    {
        return 1;
    }

    if (OneNET_UrlEncode(sign_buf) != 0)
    {
        return 1;
    }

    snprintf(authorization_buf, authorization_buf_len,
             "version=2018-10-31&res=products%%2F%s%%2Fdevices%%2F%s&et=%lu&method=sha1&sign=%s",
             ONENET_PRODUCT_ID,
             ONENET_DEVICE_NAME,
             (unsigned long)ONENET_TOKEN_EXPIRE,
             sign_buf);
    return 0;
}

static uint8_t OneNET_SendMqttPacket(MQTT_PACKET_STRUCTURE *pkt)
{
    uint8_t ret;

    if (pkt == NULL || pkt->_data == NULL || pkt->_len == 0U)
    {
        return 1;
    }

    ret = ESP8266_ONENET_AT_SendData(pkt->_data, (uint16_t)pkt->_len);
    MQTT_DeleteBuffer(pkt);
    return ret;
}

uint8_t OneNET_MQTT_Open(void)
{
    if (ONENET_VERBOSE_LOG) printf("ONENET: TCP open %s:%u\r\n", ONENET_MQTT_HOST, ONENET_MQTT_PORT);
    ESP8266_ONENET_AT_SendCmd(ESP8266_AT_CMD_CLOSE, ESP8266_AT_RSP_OK, 1000);
    return ESP8266_ONENET_AT_StartTcp(ONENET_MQTT_HOST, ONENET_MQTT_PORT);
}

uint8_t OneNET_MQTT_Connect(void)
{
    MQTT_PACKET_STRUCTURE pkt = {NULL, 0, 0, 0};
    uint8_t rx[64];
    uint16_t rx_len = 0;
    char token[224];

    if (OneNET_BuildToken(token, sizeof(token)) != 0)
    {
        if (ONENET_VERBOSE_LOG) printf("ONENET: token build failed\r\n");
        return 1;
    }

    if (MQTT_PacketConnect((const int8 *)ONENET_PRODUCT_ID,
                           (const int8 *)token,
                           (const int8 *)ONENET_DEVICE_NAME,
                           300, 1, MQTT_QOS_LEVEL0,
                           NULL, NULL, 0, &pkt) != 0)
    {
        return 1;
    }

    if (ESP8266_ONENET_AT_SendData(pkt._data, (uint16_t)pkt._len) != 0)
    {
        MQTT_DeleteBuffer(&pkt);
        return 1;
    }
    MQTT_DeleteBuffer(&pkt);

    if (ESP8266_ONENET_AT_WaitTcpPacket(rx, sizeof(rx), &rx_len, 5000) != 0)
    {
        if (ONENET_VERBOSE_LOG) printf("ONENET: CONNACK timeout\r\n");
        return 1;
    }

    if (rx_len >= 4U &&
        MQTT_UnPacketRecvEx(rx, rx_len) == MQTT_PKT_CONNACK &&
        MQTT_UnPacketConnectAck(rx) == 0)
    {
        if (ONENET_VERBOSE_LOG) printf("ONENET: MQTT connected\r\n");
        return 0;
    }

    if (ONENET_VERBOSE_LOG) printf("ONENET: CONNACK invalid len=%u head=%02X\r\n", rx_len, rx[0]);
    return 1;
}

uint8_t OneNET_MQTT_SubscribeControl(void)
{
    MQTT_PACKET_STRUCTURE pkt = {NULL, 0, 0, 0};
    uint8_t rx[64];
    uint16_t rx_len = 0;
    char property_topic[128];
    const int8 *topics[1];
    uint16_t subscribe_id;

    OneNET_BuildPropertySetTopic(property_topic, sizeof(property_topic));
    topics[0] = (const int8 *)property_topic;
    subscribe_id = OneNET_NextPacketId();

    if (MQTT_PacketSubscribe(subscribe_id, MQTT_QOS_LEVEL0,
                             topics, 1, &pkt) != 0)
    {
        return 1;
    }

    if (ESP8266_ONENET_AT_SendData(pkt._data, (uint16_t)pkt._len) != 0)
    {
        MQTT_DeleteBuffer(&pkt);
        return 1;
    }
    MQTT_DeleteBuffer(&pkt);

    if (ESP8266_ONENET_AT_WaitTcpPacket(rx, sizeof(rx), &rx_len, 5000) != 0)
    {
        if (ONENET_VERBOSE_LOG) printf("ONENET: SUBACK timeout\r\n");
        return 1;
    }

    if (rx_len == 5U &&
        MQTT_UnPacketRecvEx(rx, rx_len) == MQTT_PKT_SUBACK &&
        rx[1] == 3U &&
        rx[2] == (uint8_t)(subscribe_id >> 8U) &&
        rx[3] == (uint8_t)subscribe_id &&
        rx[4] == (uint8_t)MQTT_QOS_LEVEL0)
    {
        if (ONENET_VERBOSE_LOG)
        {
            printf("ONENET: subscribed control\r\n");
        }
        return 0;
    }

    return 1;
}

uint8_t OneNET_MQTT_Ping(void)
{
    MQTT_PACKET_STRUCTURE pkt = {NULL, 0, 0, 0};

    if (MQTT_PacketPing(&pkt) != 0)
    {
        return 1;
    }
    return OneNET_SendMqttPacket(&pkt);
}

static int8_t OneNET_ParseSwitchValue(cJSON *item)
{
    cJSON *value;

    if (item == NULL)
    {
        return -1;
    }

    value = cJSON_GetObjectItem(item, "value");
    if (value != NULL)
    {
        item = value;
    }

    if (item->type == cJSON_True)
    {
        return 1;
    }
    if (item->type == cJSON_False)
    {
        return 0;
    }
    if (item->type == cJSON_Number)
    {
        return item->valueint ? 1 : 0;
    }
    if (item->type == cJSON_String && item->valuestring != NULL)
    {
        if (strcmp(item->valuestring, "on") == 0 ||
            strcmp(item->valuestring, "ON") == 0 ||
            strcmp(item->valuestring, "1") == 0 ||
            strcmp(item->valuestring, "true") == 0)
        {
            return 1;
        }
        if (strcmp(item->valuestring, "off") == 0 ||
            strcmp(item->valuestring, "OFF") == 0 ||
            strcmp(item->valuestring, "0") == 0 ||
            strcmp(item->valuestring, "false") == 0)
        {
            return 0;
        }
    }

    return -1;
}

static void OneNET_ParseRequestId(cJSON *root, OneNET_Control_t *ctrl)
{
    cJSON *id;
    int length;

    if (root == NULL || ctrl == NULL)
    {
        return;
    }

    id = cJSON_GetObjectItem(root, "id");
    if (id != NULL && id->type == cJSON_String && id->valuestring != NULL)
    {
        length = snprintf(ctrl->request_id, sizeof(ctrl->request_id), "%s", id->valuestring);
        if (length > 0 && (size_t)length < sizeof(ctrl->request_id))
        {
            ctrl->request_id_valid = 1U;
        }
    }
    else if (id != NULL && id->type == cJSON_Number &&
             id->valuedouble >= 0.0 && id->valuedouble <= 4294967295.0)
    {
        length = snprintf(ctrl->request_id, sizeof(ctrl->request_id), "%.0f", id->valuedouble);
        if (length > 0 && (size_t)length < sizeof(ctrl->request_id))
        {
            ctrl->request_id_valid = 1U;
        }
    }
}

static uint8_t OneNET_ParseControlPayload(const char *payload, OneNET_Control_t *ctrl)
{
    cJSON *root;
    cJSON *params;
    int8_t val;

    if (payload == NULL || ctrl == NULL)
    {
        return 1;
    }

    root = cJSON_Parse(payload);
    if (root == NULL || root->type != cJSON_Object)
    {
        cJSON_Delete(root);
        return 1;
    }

    ctrl->request_received = 1U;
    ctrl->request_source = ONENET_REQUEST_SOURCE_PROPERTY_SET;
    OneNET_ParseRequestId(root, ctrl);

    params = cJSON_GetObjectItem(root, "params");
    if (params == NULL)
    {
        params = root;
    }

    val = OneNET_ParseSwitchValue(cJSON_GetObjectItem(params, "home_feng"));
    if (val >= 0)
    {
        ctrl->home_feng = val;
        ctrl->updated = 1U;
    }

    val = OneNET_ParseSwitchValue(cJSON_GetObjectItem(params, "home_led"));
    if (val >= 0)
    {
        ctrl->home_led = val;
        ctrl->updated = 1U;
    }

    val = OneNET_ParseSwitchValue(cJSON_GetObjectItem(params, "home_load"));
    if (val >= 0)
    {
        ctrl->home_load = val;
        ctrl->updated = 1U;
    }

    val = OneNET_ParseSwitchValue(cJSON_GetObjectItem(params, "qi"));
    if (val >= 0)
    {
        ctrl->qi = val;
        ctrl->updated = 1U;
    }

    cJSON_Delete(root);
    return ctrl->updated ? 0U : 1U;
}

OneNET_MQTTRxResult_t OneNET_MQTT_Process(OneNET_Control_t *ctrl,
                                          uint32_t timeout_ms)
{
    uint8_t rx[ONENET_RX_BUF_SIZE];
    uint16_t rx_len = 0;
    uint8_t type;
    int8 *topic = NULL;
    int8 *payload = NULL;
    uint16 topic_len = 0;
    uint16 payload_len = 0;
    uint8 qos = 0;
    uint16 pkt_id = 0;
    uint8_t parse_result;
    ESP8266_ONENET_AT_Result_t transport_result;
    OneNET_MQTTRxResult_t result = ONENET_MQTT_RX_PROTOCOL_ERROR;
    char property_topic[128];

    if (ctrl == NULL)
    {
        return ONENET_MQTT_RX_PROTOCOL_ERROR;
    }

    transport_result = ESP8266_ONENET_AT_WaitTcpPacket(
        rx, sizeof(rx), &rx_len, timeout_ms);
    if (transport_result == ESP8266_ONENET_AT_TIMEOUT)
    {
        return ONENET_MQTT_RX_IDLE;
    }
    if (transport_result == ESP8266_ONENET_AT_LINK_CLOSED)
    {
        return ONENET_MQTT_RX_LINK_CLOSED;
    }
    if (transport_result != ESP8266_ONENET_AT_OK || rx_len == 0U)
    {
        return ONENET_MQTT_RX_PROTOCOL_ERROR;
    }

    rx[rx_len] = 0U;
    type = MQTT_UnPacketRecvEx(rx, rx_len);
    if (type == MQTT_PKT_PUBLISH)
    {
        if (MQTT_UnPacketPublishEx(rx, rx_len, &topic, &topic_len,
                                  &payload, &payload_len, &qos,
                                  &pkt_id) == 0U)
        {
            OneNET_BuildPropertySetTopic(property_topic, sizeof(property_topic));
            if (topic != NULL && payload != NULL &&
                strcmp((const char *)topic, property_topic) == 0)
            {
                if (ONENET_VERBOSE_LOG) printf("ONENET: CTRL topic=%s payload=%s\r\n", topic, payload);
                parse_result = OneNET_ParseControlPayload(
                    (const char *)payload, ctrl);
                result = parse_result == 0U ?
                         ONENET_MQTT_RX_PROPERTY_SET :
                         ONENET_MQTT_RX_PAYLOAD_ERROR;
                if (ONENET_VERBOSE_LOG)
                {
                    printf("ONENET: CTRL parse ret=%u feng=%d led=%d load=%d qi=%d updated=%u\r\n",
                           parse_result, ctrl->home_feng, ctrl->home_led,
                           ctrl->home_load, ctrl->qi, ctrl->updated);
                }
            }
            else
            {
                result = ONENET_MQTT_RX_IGNORED;
            }
        }
    }
    else if (type == MQTT_PKT_PINGRESP)
    {
        result = ONENET_MQTT_RX_PING_RESPONSE;
    }
    else if (type == MQTT_PKT_PUBACK || type == MQTT_PKT_SUBACK)
    {
        result = ONENET_MQTT_RX_ACK;
    }

    if (topic != NULL)
    {
        MQTT_FreeBuffer(topic);
    }
    if (payload != NULL)
    {
        MQTT_FreeBuffer(payload);
    }

    return result;
}

static uint8_t OneNET_PublishTopic(const char *topic,
                                   const char *payload,
                                   enum MqttQosLevel qos)
{
    MQTT_PACKET_STRUCTURE pkt = {NULL, 0, 0, 0};
    size_t payload_length;

    if (topic == NULL || payload == NULL)
    {
        return 1U;
    }

    payload_length = strlen(payload);
    if (payload_length == 0U || payload_length > 65535U)
    {
        return 1U;
    }

    if (MQTT_PacketPublish(OneNET_NextPacketId(),
                           (const int8 *)topic,
                           (const int8 *)payload,
                           (uint32)payload_length,
                           qos, 0, 1, &pkt) != 0)
    {
        return 1U;
    }

    return OneNET_SendMqttPacket(&pkt);
}

uint8_t OneNET_MQTT_ReplyPropertySet(const char *request_id,
                                    uint16_t code,
                                    const char *message)
{
    cJSON *root;
    char *payload;
    char topic[104];
    uint8_t result;

    if (request_id == NULL || request_id[0] == '\0' || message == NULL)
    {
        return 1U;
    }

    root = cJSON_CreateObject();
    if (root == NULL)
    {
        return 1U;
    }
    cJSON_AddStringToObject(root, "id", request_id);
    cJSON_AddNumberToObject(root, "code", code);
    cJSON_AddStringToObject(root, "msg", message);
    payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL)
    {
        return 1U;
    }

    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/set_reply",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    result = OneNET_PublishTopic(topic, payload, MQTT_QOS_LEVEL0);
    cJSON_Free(payload);
    return result;
}

static uint8_t OneNET_PublishPropertyPayload(const char *payload)
{
    char topic[104];
    int topic_len;

    if (payload == NULL || payload[0] == '\0')
    {
        return 1U;
    }

    topic_len = snprintf(topic, sizeof(topic),
                         "$sys/%s/%s/thing/property/post",
                         ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    if (topic_len <= 0 || (size_t)topic_len >= sizeof(topic))
    {
        return 1U;
    }

    /* Periodic properties are QoS 0. The legacy helper used QoS 1 with a
     * fixed packet id but did not serialize or match PUBACK responses. */
    return OneNET_PublishTopic(topic, payload, MQTT_QOS_LEVEL0);
}

static unsigned int OneNET_FloatToIntRange(float value, unsigned int max_value)
{
    if (value <= 0.0f)
    {
        return 0U;
    }
    if (value >= (float)max_value)
    {
        return max_value;
    }
    return (unsigned int)(value + 0.5f);
}

static unsigned int OneNET_WattToMilliwattRange(float value_w, unsigned int max_value_mw)
{
    return OneNET_FloatToIntRange(value_w * 1000.0f, max_value_mw);
}

static uint16_t OneNET_BuildTelemetryJson(const OneNET_UploadData_t *data, char *buf, uint16_t len)
{
    if (data == NULL || buf == NULL || len == 0U)
    {
        return 0;
    }

    return (uint16_t)snprintf(buf, len,
        "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{"
        "\"car_soc\":{\"value\":%u},"
        "\"car_status\":{\"value\":%u},"
        "\"home_feng\":{\"value\":%s},"
        "\"home_led\":{\"value\":%s},"
        "\"home_load\":{\"value\":%s},"
        "\"home_soc\":{\"value\":%u},"
        "\"home_status\":{\"value\":%u},"
        "\"human_heart\":{\"value\":%u},"
        "\"human_soc\":{\"value\":%u},"
        "\"human_spo2\":{\"value\":%u},"
        "\"human_status\":{\"value\":%u},"
        "\"load_power\":{\"value\":%u},"
        "\"lux\":{\"value\":%u},"
        "\"pv_power\":{\"value\":%u},"
        "\"qi\":{\"value\":%s}"
        "}}",
        (unsigned long)HAL_GetTick(),
        data->car_soc,
        data->car_status,
        data->home_feng ? "true" : "false",
        data->home_led ? "true" : "false",
        data->home_load ? "true" : "false",
        OneNET_FloatToIntRange(data->home_soc, 100U),
        data->home_status,
        data->human_heart,
        OneNET_FloatToIntRange(data->human_soc, 100U),
        data->human_spo2,
        data->human_status,
        OneNET_WattToMilliwattRange(data->load_power, 10000U),
        OneNET_FloatToIntRange(data->lux, 10000U),
        OneNET_WattToMilliwattRange(data->pv_power, 10000U),
        data->qi ? "true" : "false");
}

uint8_t OneNET_Upload(const OneNET_UploadData_t *data)
{
    char payload[768];
    uint16_t body_len;

    body_len = OneNET_BuildTelemetryJson(data, payload, sizeof(payload));
    if (body_len == 0U || body_len >= sizeof(payload))
    {
        return 1;
    }

    return OneNET_PublishPropertyPayload(payload);
}

uint8_t OneNET_UploadSwitchStates(uint8_t home_feng, uint8_t home_led,
                                  uint8_t home_load, uint8_t qi)
{
    char payload[256];
    uint16_t body_len;

    body_len = (uint16_t)snprintf(payload, sizeof(payload),
        "{\"id\":\"%lu\",\"version\":\"1.0\",\"params\":{"
        "\"home_feng\":{\"value\":%s},"
        "\"home_led\":{\"value\":%s},"
        "\"home_load\":{\"value\":%s},"
        "\"qi\":{\"value\":%s}"
        "}}",
        (unsigned long)HAL_GetTick(),
        home_feng ? "true" : "false",
        home_led ? "true" : "false",
        home_load ? "true" : "false",
        qi ? "true" : "false");

    if (body_len == 0U || body_len >= sizeof(payload))
    {
        return 1;
    }

    return OneNET_PublishPropertyPayload(payload);
}
