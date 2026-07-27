#include "esp8266_udp.h"
#include "app_config.h"
#include "esp8266_at.h"
#include "cJSON.h"
#include "FreeRTOS.h"
#include <stdio.h>
#include <string.h>

#define ESP8266_UDP_VERBOSE_LOG 0U

static uint8_t g_cjson_hooks_ready = 0;
static char g_esp32s3_ip[32] = {0};
static uint16_t g_esp32s3_src_port = 0;
static uint32_t g_esp32s3_last_rx_tick = 0U;

static void *ESP8266_JSON_Malloc(size_t size)
{
    return pvPortMalloc(size);
}

static void ESP8266_JSON_Free(void *ptr)
{
    vPortFree(ptr);
}

static void ESP8266_JSON_InitHooks(void)
{
    cJSON_Hooks hooks;

    if (g_cjson_hooks_ready)
    {
        return;
    }

    hooks.malloc_fn = ESP8266_JSON_Malloc;
    hooks.free_fn = ESP8266_JSON_Free;
    cJSON_InitHooks(&hooks);
    g_cjson_hooks_ready = 1;
}

static uint8_t ESP8266_JSON_GetNumber(cJSON *root, const char *name, double *out)
{
    cJSON *item;

    if (root == NULL || name == NULL || out == NULL)
    {
        return 1;
    }

    item = cJSON_GetObjectItem(root, name);
    if (item == NULL || item->type != cJSON_Number)
    {
        return 1;
    }

    *out = item->valuedouble;
    return 0;
}

static uint8_t ESP8266_JSON_HasNumber(cJSON *root, const char *name)
{
    cJSON *item;

    if (root == NULL || name == NULL)
    {
        return 0;
    }

    item = cJSON_GetObjectItem(root, name);
    return (item != NULL && item->type == cJSON_Number) ? 1U : 0U;
}

uint8_t ESP8266_UDP_Init(void)
{
    char ip_rsp[256];

    ESP8266_JSON_InitHooks();
    ESP8266_UDP_ClearPeer();

    if (ESP8266_UDP_VERBOSE_LOG) printf("ESP8266: start DMA\r\n");
    if (ESP8266_AT_StartDma() != 0)
    {
        if (ESP8266_UDP_VERBOSE_LOG) printf("ESP8266: DMA start failed\r\n");
        return 1;
    }

    if (ESP8266_UDP_VERBOSE_LOG) printf("ESP8266: AT test\r\n");
    if (ESP8266_AT_SendCmd(ESP8266_AT_CMD_TEST, ESP8266_AT_RSP_OK, 1000) != 0)
    {
        if (ESP8266_UDP_VERBOSE_LOG) printf("ESP8266: AT no response\r\n");
        return 1;
    }

    ESP8266_AT_SendCmd(ESP8266_AT_CMD_ECHO_OFF, ESP8266_AT_RSP_OK, 1000);

    if (ESP8266_UDP_VERBOSE_LOG) printf("ESP8266: station mode\r\n");
    if (ESP8266_AT_SendCmd(ESP8266_AT_CMD_STATION_MODE, ESP8266_AT_RSP_OK, 1000) != 0)
    {
        if (ESP8266_UDP_VERBOSE_LOG) printf("ESP8266: CWMODE failed\r\n");
        return 1;
    }

    ESP8266_AT_SendCmd(ESP8266_AT_CMD_AUTO_CONN_OFF, ESP8266_AT_RSP_OK, 1000);
    ESP8266_AT_SendCmd(ESP8266_AT_CMD_QUIT_AP, ESP8266_AT_RSP_OK, 1000);
    ESP8266_AT_SendCmd(ESP8266_AT_CMD_DHCP_ON, ESP8266_AT_RSP_OK, 1000);
    if (ESP8266_UDP_VERBOSE_LOG) printf("ESP8266: join AP %s\r\n", ESP8266_WIFI_SSID);
    if (ESP8266_AT_JoinAp(ESP8266_WIFI_SSID, ESP8266_WIFI_PASSWORD) != 0)
    {
        if (ESP8266_UDP_VERBOSE_LOG) printf("ESP8266: join AP failed\r\n");
        return 1;
    }
    if (ESP8266_UDP_VERBOSE_LOG) printf("ESP8266: WiFi joined\r\n");
    ESP8266_AT_ClearRx();
    ESP8266_AT_SendRaw(ESP8266_AT_CMD_GET_IP);
    ESP8266_AT_SendRaw("\r\n");
    ESP8266_AT_ReadUntil(ip_rsp, sizeof(ip_rsp), ESP8266_AT_RSP_OK, 2000);
    if (ESP8266_UDP_VERBOSE_LOG) printf("ESP8266: IP rsp=[%s]\r\n", ip_rsp);

    ESP8266_AT_SendCmd(ESP8266_AT_CMD_NORMAL_MODE, ESP8266_AT_RSP_OK, 1000);
    ESP8266_AT_SendCmd(ESP8266_AT_CMD_SINGLE_CONN, ESP8266_AT_RSP_OK, 1000);
    ESP8266_AT_SendCmd(ESP8266_AT_CMD_IPDINFO_ON, ESP8266_AT_RSP_OK, 1000);
    ESP8266_AT_SendCmd(ESP8266_AT_CMD_CLOSE, ESP8266_AT_RSP_OK, 1000);

    if (ESP8266_UDP_VERBOSE_LOG)
    {
        printf("ESP8266: UDP local=%u peer_port=%u\r\n",
               ESP8266_UDP_PORT, ESP32S3_RX_PORT);
    }
    if (ESP8266_AT_StartUdp("255.255.255.255",
                            ESP32S3_RX_PORT,
                            ESP8266_UDP_PORT) != 0)
    {
        if (ESP8266_UDP_VERBOSE_LOG) printf("ESP8266: UDP start failed\r\n");
        return 1;
    }

    if (ESP8266_UDP_VERBOSE_LOG) printf("ESP8266: UDP started, wait ESP32-S3 packet\r\n");
    return 0;
}

uint8_t ESP8266_UDP_PollReceive(ESP32S3_Data_t *data, uint32_t timeout_ms)
{
    uint8_t flags = 0;

    return ESP8266_UDP_PollReceiveEx(data, NULL, &flags, timeout_ms);
}

uint8_t ESP8266_UDP_PollReceiveEx(ESP32S3_Data_t *data,
                                  EnergyLstmPrediction_t *pred,
                                  uint8_t *packet_flags,
                                  uint32_t timeout_ms)
{
    char payload[384];
    cJSON *root;
    double value;
    uint8_t parsed_any = 0;

    if (data == NULL && pred == NULL)
    {
        return 1;
    }

    ESP8266_JSON_InitHooks();

    if (ESP8266_AT_WaitUdpPayloadFrom(payload, sizeof(payload),
                                      g_esp32s3_ip, sizeof(g_esp32s3_ip),
                                      &g_esp32s3_src_port,
                                      timeout_ms) != 0)
    {
        return 1;
    }

    if (g_esp32s3_ip[0] != '\0')
    {
        g_esp32s3_last_rx_tick = HAL_GetTick();
        if (ESP8266_UDP_VERBOSE_LOG) printf("ESP8266: peer %s:%u\r\n", g_esp32s3_ip, g_esp32s3_src_port);
    }
    if (ESP8266_UDP_VERBOSE_LOG) printf("UDP RX: %s\r\n", payload);

    root = cJSON_Parse(payload);
    if (root == NULL)
    {
        return 1;
    }

    if (packet_flags != NULL)
    {
        *packet_flags = 0;
    }

    if (data != NULL &&
        ESP8266_JSON_HasNumber(root, "bat_v") &&
        ESP8266_JSON_HasNumber(root, "bat_pct") &&
        ESP8266_JSON_HasNumber(root, "state"))
    {
        if (ESP8266_JSON_GetNumber(root, "bat_v", &value) != 0) goto parse_fail;
        data->bat_v = (float)value;

        if (ESP8266_JSON_GetNumber(root, "bat_pct", &value) != 0) goto parse_fail;
        data->bat_pct = (float)value;

        if (ESP8266_JSON_GetNumber(root, "hr", &value) == 0)
        {
            if (value < 0.0) value = 0.0;
            if (value > 65535.0) value = 65535.0;
            data->hr = (uint16_t)(value + 0.5);
        }
        else
        {
            data->hr = 0;
        }

        if (ESP8266_JSON_GetNumber(root, "spo2", &value) == 0)
        {
            if (value < 0.0) value = 0.0;
            if (value > 65535.0) value = 65535.0;
            data->spo2 = (uint16_t)(value + 0.5);
        }
        else
        {
            data->spo2 = 0;
        }

        if (ESP8266_JSON_GetNumber(root, "state", &value) != 0) goto parse_fail;
        if (value < 0.0) value = 0.0;
        if (value > 255.0) value = 255.0;
        data->state = (uint8_t)(value + 0.5);
        data->valid = 1;
        parsed_any = 1;
        if (packet_flags != NULL) *packet_flags |= 0x01U;
        if (ESP8266_UDP_VERBOSE_LOG)
        {
            printf("UDP RX S3: bat=%.3fV soc=%.1f%% hr=%u spo2=%u state=%u\r\n",
                   (double)data->bat_v, (double)data->bat_pct,
                   data->hr, data->spo2, data->state);
        }
    }

    if (pred != NULL &&
        ESP8266_JSON_HasNumber(root, "future_pv_p") &&
        ESP8266_JSON_HasNumber(root, "future_load_p") &&
        ESP8266_JSON_HasNumber(root, "future_home_soc"))
    {
        if (ESP8266_JSON_GetNumber(root, "future_pv_p", &value) != 0) goto parse_fail;
        pred->future_pv_p = (float)value;

        if (ESP8266_JSON_GetNumber(root, "future_load_p", &value) != 0) goto parse_fail;
        pred->future_load_p = (float)value;

        if (ESP8266_JSON_GetNumber(root, "future_home_soc", &value) != 0) goto parse_fail;
        pred->future_home_soc = (float)value;

        pred->tick_ms = HAL_GetTick();
        pred->valid = 1U;
        parsed_any = 1;
        if (packet_flags != NULL) *packet_flags |= 0x02U;
        if (ESP8266_UDP_VERBOSE_LOG)
        {
            printf("UDP RX LSTM: pv=%.3fW load=%.3fW home_soc=%.1f%%\r\n",
                   (double)pred->future_pv_p,
                   (double)pred->future_load_p,
                   (double)pred->future_home_soc);
        }
    }

    cJSON_Delete(root);
    return parsed_any ? 0U : 1U;

parse_fail:
    cJSON_Delete(root);
    return 1;
}

uint8_t ESP8266_UDP_SendTelemetry(float lux, float home_load_power_w, float pv_power_w)
{
    cJSON *root;
    char *json;
    uint8_t ret;

    ESP8266_JSON_InitHooks();

    root = cJSON_CreateObject();
    if (root == NULL)
    {
        return 1;
    }

    cJSON_AddNumberToObject(root, "lux", lux);
    cJSON_AddNumberToObject(root, "home_load_p", home_load_power_w);
    cJSON_AddNumberToObject(root, "pv_p", pv_power_w);

    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL)
    {
        return 1;
    }

    if (g_esp32s3_ip[0] == '\0')
    {
        vPortFree(json);
        return 1;
    }

    ret = ESP8266_AT_SendDataTo((const uint8_t *)json, (uint16_t)strlen(json),
                                g_esp32s3_ip, ESP32S3_RX_PORT);
    vPortFree(json);
    return ret;
}

uint8_t ESP8266_UDP_SendLstmInput(const EnergyLstmInput_t *input)
{
    cJSON *root;
    char *json;
    const char *dst_ip;
    uint8_t ret;

    if (input == NULL)
    {
        return 1;
    }

    ESP8266_JSON_InitHooks();

    root = cJSON_CreateObject();
    if (root == NULL)
    {
        return 1;
    }

    cJSON_AddStringToObject(root, "type", "lstm_input");
    cJSON_AddBoolToObject(root, "prediction_enable",
                          APP_LSTM_PREDICTION_ENABLE != 0U);
    cJSON_AddNumberToObject(root, "real_hour_sin", input->real_hour_sin);
    cJSON_AddNumberToObject(root, "real_hour_cos", input->real_hour_cos);
    cJSON_AddNumberToObject(root, "lux", input->lux);
    cJSON_AddNumberToObject(root, "pv_v", input->pv_v);
    cJSON_AddNumberToObject(root, "pv_p", input->pv_p);
    cJSON_AddNumberToObject(root, "home_v", input->home_v);
    cJSON_AddNumberToObject(root, "home_soc", input->home_soc);
    cJSON_AddNumberToObject(root, "load_p", input->load_p);
    cJSON_AddNumberToObject(root, "car_soc", input->car_soc);
    cJSON_AddNumberToObject(root, "human_soc", input->human_soc);
    cJSON_AddNumberToObject(root, "pvsrc", input->pvsrc);
    cJSON_AddNumberToObject(root, "hsrc", input->hsrc);
    cJSON_AddNumberToObject(root, "rigid", input->rigid);
    cJSON_AddNumberToObject(root, "led", input->led);
    cJSON_AddNumberToObject(root, "fan", input->fan);
    cJSON_AddNumberToObject(root, "qi", input->qi);
    cJSON_AddNumberToObject(root, "hchg", input->hchg);
    cJSON_AddNumberToObject(root, "cchg", input->cchg);
    cJSON_AddNumberToObject(root, "v2h", input->v2h);

    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL)
    {
        return 1;
    }

    dst_ip = (g_esp32s3_ip[0] != '\0') ? g_esp32s3_ip : "255.255.255.255";

    if (g_esp32s3_ip[0] != '\0')
    {
        ret = ESP8266_AT_SendDataTo((const uint8_t *)json, (uint16_t)strlen(json),
                                    dst_ip, ESP32S3_RX_PORT);
    }
    else
    {
        ret = ESP8266_AT_SendData((const uint8_t *)json, (uint16_t)strlen(json));
    }
    if (ESP8266_UDP_VERBOSE_LOG)
    {
        printf("UDP TX LSTM ret=%u dst=%s:%u peer_port=%u json=%s\r\n",
               ret, dst_ip, ESP32S3_RX_PORT, g_esp32s3_src_port, json);
    }
    vPortFree(json);
    return ret;
}

uint8_t ESP8266_UDP_HasPeer(void)
{
    return g_esp32s3_ip[0] != '\0';
}

uint8_t ESP8266_UDP_IsPeerAlive(uint32_t timeout_ms)
{
    if (timeout_ms == 0U || ESP8266_UDP_HasPeer() == 0U)
    {
        return 0U;
    }

    return ((HAL_GetTick() - g_esp32s3_last_rx_tick) <= timeout_ms) ?
           1U : 0U;
}

void ESP8266_UDP_ClearPeer(void)
{
    g_esp32s3_ip[0] = '\0';
    g_esp32s3_src_port = 0U;
    g_esp32s3_last_rx_tick = 0U;
}
