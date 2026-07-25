#include "esp8266_onenet.h"
#include "esp8266_onenet_at.h"
#include "MqttKit.h"
#include "base64.h"
#include "hmac_sha1.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

#define ONENET_VERBOSE_LOG 1U
#define ONENET_RX_BUF_SIZE 768U

static uint16_t s_onenet_msg_id = 1U;

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
    char token[180];

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

    if (rx_len >= 4U && MQTT_UnPacketRecv(rx) == MQTT_PKT_CONNACK &&
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
    char topic[96];
    const int8 *topics[1];

    snprintf(topic, sizeof(topic), "$sys/%s/%s/thing/property/set",
             ONENET_PRODUCT_ID, ONENET_DEVICE_NAME);
    topics[0] = (const int8 *)topic;

    if (MQTT_PacketSubscribe(OneNET_NextPacketId(), MQTT_QOS_LEVEL0, topics, 1, &pkt) != 0)
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

    if (rx_len > 0U && MQTT_UnPacketRecv(rx) == MQTT_PKT_SUBACK)
    {
        if (ONENET_VERBOSE_LOG) printf("ONENET: subscribed %s\r\n", topic);
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
    if (root == NULL)
    {
        return 1;
    }

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

uint8_t OneNET_MQTT_Process(OneNET_Control_t *ctrl, uint32_t timeout_ms)
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
    uint8_t ret = 1;

    if (ctrl == NULL)
    {
        return 1;
    }

    ret = ESP8266_ONENET_AT_WaitTcpPacket(rx, sizeof(rx), &rx_len, timeout_ms);
    if (ret != 0)
    {
        return ret;
    }
    rx[rx_len] = 0U;
    ret = 1;

    type = MQTT_UnPacketRecv(rx);
    if (type == MQTT_PKT_PUBLISH)
    {
        if (MQTT_UnPacketPublish(rx, &topic, &topic_len, &payload, &payload_len, &qos, &pkt_id) == 0)
        {
            if (payload != NULL)
            {
                if (ONENET_VERBOSE_LOG) printf("ONENET: CTRL topic=%s payload=%s\r\n", topic, payload);
                ret = OneNET_ParseControlPayload((const char *)payload, ctrl);
                if (ONENET_VERBOSE_LOG)
                {
                    printf("ONENET: CTRL parse ret=%u feng=%d led=%d load=%d qi=%d updated=%u\r\n",
                           ret, ctrl->home_feng, ctrl->home_led,
                           ctrl->home_load, ctrl->qi, ctrl->updated);
                }
            }
        }
    }
    else if (type == MQTT_PKT_PINGRESP || type == MQTT_PKT_PUBACK || type == MQTT_PKT_SUBACK)
    {
        ret = 0;
    }

    if (topic != NULL)
    {
        MQTT_FreeBuffer(topic);
    }
    if (payload != NULL)
    {
        MQTT_FreeBuffer(payload);
    }

    return ret;
}

static uint8_t OneNET_PublishPropertyPayload(const char *payload)
{
    MQTT_PACKET_STRUCTURE pkt = {NULL, 0, 0, 0};
    uint16_t body_len;

    if (payload == NULL)
    {
        return 1;
    }

    body_len = (uint16_t)strlen(payload);
    if (body_len == 0U)
    {
        return 1;
    }

    if (MQTT_PacketSaveData((const int8 *)ONENET_PRODUCT_ID,
                            ONENET_DEVICE_NAME,
                            (int16)body_len,
                            NULL,
                            &pkt) != 0)
    {
        return 1;
    }

    if ((pkt._len + body_len) > pkt._size)
    {
        MQTT_DeleteBuffer(&pkt);
        return 1;
    }

    memcpy(&pkt._data[pkt._len], payload, body_len);
    pkt._len += body_len;

    if (ONENET_VERBOSE_LOG)
    {
        printf("ONENET TX JSON len=%u\r\n", body_len);
    }

    uint8_t send_ret = OneNET_SendMqttPacket(&pkt);


    
    if (ONENET_VERBOSE_LOG)
    {
        printf("ONENET TX ret=%u\r\n", send_ret);
    }
    return send_ret;
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
