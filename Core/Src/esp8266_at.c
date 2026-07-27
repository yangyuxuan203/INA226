#include "esp8266_at.h"
#include "cmsis_os.h"
#include "usart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ESP8266_AT_DMA_RX_SIZE 512
#define ESP8266_AT_RSP_SIZE    256
#define ESP8266_AT_VERBOSE_LOG 0U

static uint8_t g_esp8266_dma_rx[ESP8266_AT_DMA_RX_SIZE];
static uint16_t g_esp8266_dma_old_pos = 0;
static uint8_t g_esp8266_dma_started = 0;

uint8_t ESP8266_AT_StartDma(void)
{
    if (g_esp8266_dma_started)
    {
        return 0;
    }

    if (HAL_UART_Receive_DMA(&huart3, g_esp8266_dma_rx, ESP8266_AT_DMA_RX_SIZE) != HAL_OK)
    {
        return 1;
    }

    __HAL_DMA_DISABLE_IT(huart3.hdmarx, DMA_IT_HT);
    g_esp8266_dma_old_pos = 0;
    g_esp8266_dma_started = 1;
    return 0;
}

void ESP8266_AT_ClearRx(void)
{
    uint16_t pos;

    if (!g_esp8266_dma_started)
    {
        return;
    }

    pos = (uint16_t)(ESP8266_AT_DMA_RX_SIZE - __HAL_DMA_GET_COUNTER(huart3.hdmarx));
    if (pos >= ESP8266_AT_DMA_RX_SIZE)
    {
        pos = 0;
    }
    g_esp8266_dma_old_pos = pos;
}

static int16_t ESP8266_AT_ReadDmaByte(void)
{
    uint16_t pos;
    uint8_t ch;

    if (!g_esp8266_dma_started)
    {
        return -1;
    }

    pos = (uint16_t)(ESP8266_AT_DMA_RX_SIZE - __HAL_DMA_GET_COUNTER(huart3.hdmarx));
    if (pos >= ESP8266_AT_DMA_RX_SIZE)
    {
        pos = 0;
    }

    if (g_esp8266_dma_old_pos == pos)
    {
        return -1;
    }

    ch = g_esp8266_dma_rx[g_esp8266_dma_old_pos++];
    if (g_esp8266_dma_old_pos >= ESP8266_AT_DMA_RX_SIZE)
    {
        g_esp8266_dma_old_pos = 0;
    }

    return ch;
}

uint8_t ESP8266_AT_SendBytes(const uint8_t *data, uint16_t len)
{
    uint32_t start;

    if (data == NULL || len == 0)
    {
        return 0;
    }

    if (HAL_UART_Transmit_DMA(&huart3, (uint8_t *)data, len) != HAL_OK)
    {
        return 1;
    }

    start = HAL_GetTick();
    while (huart3.gState != HAL_UART_STATE_READY)
    {
        if ((HAL_GetTick() - start) > 1000U)
        {
            HAL_UART_AbortTransmit(&huart3);
            return 1;
        }
        osDelay(1U);
    }

    return 0;
}

uint8_t ESP8266_AT_SendRaw(const char *s)
{
    if (s == NULL)
    {
        return 1;
    }
    return ESP8266_AT_SendBytes((const uint8_t *)s, (uint16_t)strlen(s));
}

uint16_t ESP8266_AT_ReadUntil(char *buf, uint16_t len, const char *expect, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint16_t pos = 0;
    int16_t ch;

    if (buf == NULL || len == 0)
    {
        return 0;
    }
    buf[0] = '\0';

    while ((HAL_GetTick() - start) < timeout_ms && pos < (len - 1))
    {
        ch = ESP8266_AT_ReadDmaByte();
        if (ch >= 0)
        {
            buf[pos++] = (char)ch;
            buf[pos] = '\0';
            if (expect != NULL && strstr(buf, expect) != NULL)
            {
                break;
            }
        }
        else
        {
            osDelay(1U);
        }
    }

    return pos;
}

uint8_t ESP8266_AT_SendCmd(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    char rx[ESP8266_AT_RSP_SIZE];

    ESP8266_AT_ClearRx();
    if (ESP8266_AT_SendRaw(cmd) != 0 || ESP8266_AT_SendRaw("\r\n") != 0)
    {
        return 1;
    }

    ESP8266_AT_ReadUntil(rx, sizeof(rx), expect, timeout_ms);
    if (expect == NULL || strstr(rx, expect) != NULL)
    {
        return 0;
    }

    if (ESP8266_AT_VERBOSE_LOG) printf("ESP8266 AT fail cmd=[%s] rsp=[%s]\r\n", cmd, rx);
    return 1;
}

uint8_t ESP8266_AT_JoinAp(const char *ssid, const char *password)
{
    char cmd[160];
    char rx[ESP8266_AT_RSP_SIZE];
    uint8_t retry;

    if (ssid == NULL || password == NULL)
    {
        return 1;
    }

    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, password);

    for (retry = 0; retry < 3; retry++)
    {
        ESP8266_AT_ClearRx();
        if (ESP8266_AT_VERBOSE_LOG) printf("ESP8266: CWJAP try %u\r\n", retry + 1);
        if (ESP8266_AT_SendRaw(cmd) != 0 || ESP8266_AT_SendRaw("\r\n") != 0)
        {
            continue;
        }

        ESP8266_AT_ReadUntil(rx, sizeof(rx), ESP8266_AT_RSP_OK, 20000);
        if (ESP8266_AT_VERBOSE_LOG) printf("ESP8266: CWJAP rsp=[%s]\r\n", rx);

        if (strstr(rx, ESP8266_AT_RSP_GOT_IP) != NULL ||
            strstr(rx, ESP8266_AT_RSP_WIFI_CONNECTED) != NULL ||
            strstr(rx, ESP8266_AT_RSP_OK) != NULL)
        {
            return 0;
        }

        osDelay(1000U);
    }

    return ESP8266_AT_SendCmd("AT+CWJAP?", ESP8266_AT_RSP_OK, 3000);
}

uint8_t ESP8266_AT_StartUdp(const char *remote_ip, uint16_t remote_port, uint16_t local_port)
{
    char cmd[128];

    if (remote_ip == NULL)
    {
        return 1;
    }

    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"UDP\",\"%s\",%u,%u,0",
             remote_ip, remote_port, local_port);
    if (ESP8266_AT_SendCmd(cmd, ESP8266_AT_RSP_OK, 3000) == 0)
    {
        return 0;
    }

    return ESP8266_AT_SendCmd(cmd, ESP8266_AT_RSP_CONNECT, 3000);
}

uint8_t ESP8266_AT_StartTcp(const char *host, uint16_t port)
{
    char cmd[128];

    if (host == NULL)
    {
        return 1;
    }

    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u", host, port);
    if (ESP8266_AT_SendCmd(cmd, ESP8266_AT_RSP_OK, 5000) == 0)
    {
        return 0;
    }

    return ESP8266_AT_SendCmd(cmd, ESP8266_AT_RSP_CONNECT, 5000);
}

uint8_t ESP8266_AT_SendDataTo(const uint8_t *data, uint16_t len, const char *remote_ip, uint16_t remote_port)
{
    char cmd[96];
    char rx[ESP8266_AT_RSP_SIZE];

    if (data == NULL || len == 0)
    {
        return 1;
    }

    if (remote_ip != NULL && remote_ip[0] != '\0' && remote_port != 0)
    {
        snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u,\"%s\",%u", len, remote_ip, remote_port);
    }
    else
    {
        snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", len);
    }

    ESP8266_AT_ClearRx();
    if (ESP8266_AT_SendRaw(cmd) != 0 || ESP8266_AT_SendRaw("\r\n") != 0)
    {
        return 1;
    }

    ESP8266_AT_ReadUntil(rx, sizeof(rx), ESP8266_AT_RSP_SEND_PROMPT, 2000);
    if (strchr(rx, '>') == NULL)
    {
        if (ESP8266_AT_VERBOSE_LOG)
        {
            printf("ESP8266 UDP CIPSEND no prompt cmd=[%s] rsp=[%s]\r\n", cmd, rx);
        }
        return 1;
    }

    ESP8266_AT_ClearRx();
    if (ESP8266_AT_SendBytes(data, len) != 0)
    {
        return 1;
    }

    ESP8266_AT_ReadUntil(rx, sizeof(rx), ESP8266_AT_RSP_SEND_OK, 3000);
    if (strstr(rx, ESP8266_AT_RSP_SEND_OK) != NULL)
    {
        return 0;
    }

    if (ESP8266_AT_VERBOSE_LOG)
    {
        printf("ESP8266 UDP SEND no OK cmd=[%s] rsp=[%s]\r\n", cmd, rx);
    }
    return 1;
}

uint8_t ESP8266_AT_SendData(const uint8_t *data, uint16_t len)
{
    return ESP8266_AT_SendDataTo(data, len, NULL, 0);
}

uint8_t ESP8266_AT_WaitTcpPacket(uint8_t *payload, uint16_t len, uint16_t *out_len, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    char header[48];
    uint16_t hpos = 0;
    uint16_t packet_len = 0;
    uint16_t i;
    int16_t ch;
    char *colon;
    char *ipd;
    char *len_ptr;

    if (payload == NULL || len == 0 || out_len == NULL)
    {
        return 1;
    }

    *out_len = 0;

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        ch = ESP8266_AT_ReadDmaByte();
        if (ch < 0)
        {
            osDelay(1U);
            continue;
        }

        if (hpos < (sizeof(header) - 1))
        {
            header[hpos++] = (char)ch;
            header[hpos] = '\0';
        }
        else
        {
            memmove(header, &header[1], sizeof(header) - 2);
            header[sizeof(header) - 2] = (char)ch;
            header[sizeof(header) - 1] = '\0';
        }

        colon = strchr(header, ':');
        if (colon == NULL || strstr(header, "+IPD,") == NULL)
        {
            continue;
        }

        ipd = strstr(header, "+IPD,");
        if (ipd == NULL)
        {
            continue;
        }

        len_ptr = ipd + strlen("+IPD,");
        if (*len_ptr < '0' || *len_ptr > '9')
        {
            return 1;
        }

        packet_len = (uint16_t)atoi(len_ptr);
        if (packet_len == 0)
        {
            return 1;
        }
        if (packet_len >= len)
        {
            packet_len = len - 1;
        }

        for (i = 0; i < packet_len; )
        {
            if ((HAL_GetTick() - start) >= timeout_ms)
            {
                return 1;
            }
            ch = ESP8266_AT_ReadDmaByte();
            if (ch < 0)
            {
                osDelay(1U);
                continue;
            }
            payload[i++] = (uint8_t)ch;
        }

        *out_len = packet_len;
        return 0;
    }

    return 1;
}

static uint8_t ESP8266_AT_ParseRemote(const char *rx, char *remote_ip,
                                      uint16_t remote_ip_len, uint16_t *remote_port)
{
    const char *p;
    const char *q;
    unsigned int port;
    uint16_t copy_len;

    if (rx == NULL || remote_ip == NULL || remote_ip_len == 0 || remote_port == NULL)
    {
        return 1;
    }

    p = strstr(rx, ESP8266_AT_IPD_PREFIX);
    if (p == NULL)
    {
        return 1;
    }

    p = strchr(p, ',');
    if (p == NULL) return 1;
    p++;

    p = strchr(p, ',');
    if (p == NULL) return 1;
    p++;

    if (*p == '"')
    {
        p++;
        q = strchr(p, '"');
        if (q == NULL) return 1;
        copy_len = (uint16_t)(q - p);
        if (copy_len >= remote_ip_len)
        {
            copy_len = remote_ip_len - 1;
        }
        memcpy(remote_ip, p, copy_len);
        remote_ip[copy_len] = '\0';

        p = strchr(q + 1, ',');
        if (p == NULL) return 1;
        p++;
        if (sscanf(p, "%u", &port) != 1) return 1;
        *remote_port = (uint16_t)port;
        return 0;
    }

    return 1;
}

static uint8_t ESP8266_AT_IsValidIpv4Text(const char *ip)
{
    unsigned int a, b, c, d;
    char tail;

    if (ip == NULL)
    {
        return 0;
    }

    if (sscanf(ip, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4)
    {
        return 0;
    }

    return (a <= 255U && b <= 255U && c <= 255U && d <= 255U) ? 1U : 0U;
}

uint8_t ESP8266_AT_WaitUdpPayload(char *payload, uint16_t len, uint32_t timeout_ms)
{
    return ESP8266_AT_WaitUdpPayloadFrom(payload, len, NULL, 0, NULL, timeout_ms);
}

uint8_t ESP8266_AT_WaitUdpPayloadFrom(char *payload, uint16_t len,
                                      char *remote_ip, uint16_t remote_ip_len,
                                      uint16_t *remote_port,
                                      uint32_t timeout_ms)
{
    char rx[ESP8266_AT_RSP_SIZE];
    char parsed_ip[32];
    uint16_t parsed_port = 0;
    char *start;
    char *end;
    uint16_t payload_len;

    if (payload == NULL || len == 0)
    {
        return 1;
    }
    payload[0] = '\0';

    ESP8266_AT_ReadUntil(rx, sizeof(rx), "}", timeout_ms);
    if (remote_ip != NULL && remote_ip_len > 0 && remote_port != NULL)
    {
        parsed_ip[0] = '\0';
        if (ESP8266_AT_ParseRemote(rx, parsed_ip, sizeof(parsed_ip), &parsed_port) == 0 &&
            ESP8266_AT_IsValidIpv4Text(parsed_ip))
        {
            strncpy(remote_ip, parsed_ip, remote_ip_len - 1U);
            remote_ip[remote_ip_len - 1U] = '\0';
            *remote_port = parsed_port;
        }
    }

    start = strchr(rx, '{');
    end = strchr(rx, '}');
    if (start == NULL || end == NULL || end < start)
    {
        return 1;
    }

    payload_len = (uint16_t)(end - start + 1);
    if (payload_len >= len)
    {
        payload_len = len - 1;
    }

    memcpy(payload, start, payload_len);
    payload[payload_len] = '\0';
    return 0;
}

uint8_t ESP8266_AT_GetSntpTime(char *buf, uint16_t len)
{
    char rx[256];
    char *p;
    char *e;
    uint16_t copy_len;

    if (buf == NULL || len == 0)
    {
        return 1;
    }
    buf[0] = '\0';

    ESP8266_AT_SendCmd("AT+CIPSNTPCFG=1,8,\"ntp.aliyun.com\",\"cn.ntp.org.cn\"", ESP8266_AT_RSP_OK, 2000);
    ESP8266_AT_ClearRx();
    if (ESP8266_AT_SendRaw("AT+CIPSNTPTIME?\r\n") != 0)
    {
        return 1;
    }
    ESP8266_AT_ReadUntil(rx, sizeof(rx), ESP8266_AT_RSP_OK, 3000);

    p = strstr(rx, "+CIPSNTPTIME:");
    if (p == NULL)
    {
        return 1;
    }
    p += strlen("+CIPSNTPTIME:");
    while (*p == ' ') p++;
    e = strpbrk(p, "\r\n");
    if (e == NULL)
    {
        e = p + strlen(p);
    }

    copy_len = (uint16_t)(e - p);
    if (copy_len >= len)
    {
        copy_len = len - 1;
    }
    memcpy(buf, p, copy_len);
    buf[copy_len] = '\0';
    return 0;
}
