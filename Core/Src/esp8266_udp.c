#include "esp8266_udp.h"
#include "esp8266_at.h"
#include "cJSON.h"
#include "FreeRTOS.h"
#include <stdio.h>
#include <string.h>

#define ESP8266_UDP_VERBOSE_LOG 0U

static uint8_t g_cjson_hooks_ready = 0;
static char g_esp32s3_ip[32] = {0};
static uint16_t g_esp32s3_src_port = 0;

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

uint8_t ESP8266_UDP_Init(void)
{
    char ip_rsp[256];

    ESP8266_JSON_InitHooks();

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
    char payload[192];
    cJSON *root;
    double value;

    if (data == NULL)
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
        if (ESP8266_UDP_VERBOSE_LOG) printf("ESP8266: peer %s:%u\r\n", g_esp32s3_ip, g_esp32s3_src_port);
    }

    root = cJSON_Parse(payload);
    if (root == NULL)
    {
        return 1;
    }

    if (ESP8266_JSON_GetNumber(root, "bat_v", &value) != 0) goto parse_fail;
    data->bat_v = (float)value;

    if (ESP8266_JSON_GetNumber(root, "bat_pct", &value) != 0) goto parse_fail;
    data->bat_pct = (float)value;

    if (ESP8266_JSON_GetNumber(root, "hr", &value) != 0) goto parse_fail;
    if (value < 0.0) value = 0.0;
    if (value > 65535.0) value = 65535.0;
    data->hr = (uint16_t)(value + 0.5);

    if (ESP8266_JSON_GetNumber(root, "spo2", &value) != 0) goto parse_fail;
    if (value < 0.0) value = 0.0;
    if (value > 65535.0) value = 65535.0;
    data->spo2 = (uint16_t)(value + 0.5);

    if (ESP8266_JSON_GetNumber(root, "state", &value) != 0) goto parse_fail;
    if (value < 0.0) value = 0.0;
    if (value > 255.0) value = 255.0;
    data->state = (uint8_t)(value + 0.5);
    data->valid = 1;

    cJSON_Delete(root);
    return 0;

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

uint8_t ESP8266_UDP_HasPeer(void)
{
    return g_esp32s3_ip[0] != '\0';
}
