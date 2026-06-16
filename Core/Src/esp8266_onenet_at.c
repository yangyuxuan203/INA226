#include "esp8266_onenet_at.h"
#include "esp8266_udp.h"
#include "usart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ESP8266_ONENET_AT_DMA_RX_SIZE 512
#define ESP8266_ONENET_AT_RSP_SIZE    256
#define ESP8266_ONENET_AT_VERBOSE_LOG 1U

static uint8_t g_onenet_dma_rx[ESP8266_ONENET_AT_DMA_RX_SIZE];
static uint16_t g_onenet_dma_old_pos = 0;
static uint8_t g_onenet_dma_started = 0;

uint8_t ESP8266_ONENET_AT_StartDma(void)
{
    if (g_onenet_dma_started)
    {
        return 0;
    }

    HAL_UART_AbortReceive(&huart2);
    HAL_UART_AbortTransmit(&huart2);
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    __HAL_UART_CLEAR_FEFLAG(&huart2);
    __HAL_UART_CLEAR_NEFLAG(&huart2);

    if (HAL_UART_Receive_DMA(&huart2, g_onenet_dma_rx, ESP8266_ONENET_AT_DMA_RX_SIZE) != HAL_OK)
    {
        return 1;
    }

    __HAL_DMA_DISABLE_IT(huart2.hdmarx, DMA_IT_HT);
    g_onenet_dma_old_pos = 0;
    g_onenet_dma_started = 1;
    return 0;
}

void ESP8266_ONENET_AT_ClearRx(void)
{
    uint16_t pos;

    if (!g_onenet_dma_started)
    {
        return;
    }

    pos = (uint16_t)(ESP8266_ONENET_AT_DMA_RX_SIZE - __HAL_DMA_GET_COUNTER(huart2.hdmarx));
    if (pos >= ESP8266_ONENET_AT_DMA_RX_SIZE)
    {
        pos = 0;
    }
    g_onenet_dma_old_pos = pos;
}

static int16_t ESP8266_ONENET_AT_ReadDmaByte(void)
{
    uint16_t pos;
    uint8_t ch;

    if (!g_onenet_dma_started)
    {
        return -1;
    }

    pos = (uint16_t)(ESP8266_ONENET_AT_DMA_RX_SIZE - __HAL_DMA_GET_COUNTER(huart2.hdmarx));
    if (pos >= ESP8266_ONENET_AT_DMA_RX_SIZE)
    {
        pos = 0;
    }

    if (g_onenet_dma_old_pos == pos)
    {
        return -1;
    }

    ch = g_onenet_dma_rx[g_onenet_dma_old_pos++];
    if (g_onenet_dma_old_pos >= ESP8266_ONENET_AT_DMA_RX_SIZE)
    {
        g_onenet_dma_old_pos = 0;
    }

    return ch;
}

uint8_t ESP8266_ONENET_AT_SendBytes(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0U)
    {
        return 0;
    }

    return HAL_UART_Transmit(&huart2, (uint8_t *)data, len, 1000U) == HAL_OK ? 0U : 1U;
}

uint8_t ESP8266_ONENET_AT_SendRaw(const char *s)
{
    if (s == NULL)
    {
        return 1;
    }
    return ESP8266_ONENET_AT_SendBytes((const uint8_t *)s, (uint16_t)strlen(s));
}

uint16_t ESP8266_ONENET_AT_ReadUntil(char *buf, uint16_t len, const char *expect, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint16_t pos = 0;
    int16_t ch;

    if (buf == NULL || len == 0U)
    {
        return 0;
    }
    buf[0] = '\0';

    while ((HAL_GetTick() - start) < timeout_ms && pos < (len - 1U))
    {
        ch = ESP8266_ONENET_AT_ReadDmaByte();
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
            HAL_Delay(1);
        }
    }

    return pos;
}

uint8_t ESP8266_ONENET_AT_SendCmd(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    char rx[ESP8266_ONENET_AT_RSP_SIZE];

    ESP8266_ONENET_AT_ClearRx();
    if (ESP8266_ONENET_AT_SendRaw(cmd) != 0 || ESP8266_ONENET_AT_SendRaw("\r\n") != 0)
    {
        return 1;
    }

    ESP8266_ONENET_AT_ReadUntil(rx, sizeof(rx), expect, timeout_ms);
    if (expect == NULL || strstr(rx, expect) != NULL)
    {
        return 0;
    }
    if (strcmp(cmd, ESP8266_AT_CMD_CLOSE) == 0 && strstr(rx, ESP8266_AT_RSP_ERROR) != NULL)
    {
        return 0;
    }

    if (ESP8266_ONENET_AT_VERBOSE_LOG) printf("ONENET AT fail cmd=[%s] rsp=[%s]\r\n", cmd, rx);
    return 1;
}

uint8_t ESP8266_ONENET_AT_JoinAp(const char *ssid, const char *password)
{
    char cmd[160];
    char rx[ESP8266_ONENET_AT_RSP_SIZE];
    uint8_t retry;

    if (ssid == NULL || password == NULL)
    {
        return 1;
    }

    snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, password);

    for (retry = 0; retry < 3U; retry++)
    {
        ESP8266_ONENET_AT_ClearRx();
        if (ESP8266_ONENET_AT_VERBOSE_LOG) printf("ONENET ESP8266: CWJAP try %u\r\n", retry + 1U);
        if (ESP8266_ONENET_AT_SendRaw(cmd) != 0 || ESP8266_ONENET_AT_SendRaw("\r\n") != 0)
        {
            continue;
        }

        ESP8266_ONENET_AT_ReadUntil(rx, sizeof(rx), ESP8266_AT_RSP_OK, 20000);
        if (ESP8266_ONENET_AT_VERBOSE_LOG) printf("ONENET ESP8266: CWJAP rsp=[%s]\r\n", rx);

        if (strstr(rx, ESP8266_AT_RSP_GOT_IP) != NULL ||
            strstr(rx, ESP8266_AT_RSP_WIFI_CONNECTED) != NULL ||
            strstr(rx, ESP8266_AT_RSP_OK) != NULL)
        {
            return 0;
        }

        HAL_Delay(1000);
    }

    return ESP8266_ONENET_AT_SendCmd("AT+CWJAP?", ESP8266_AT_RSP_OK, 3000);
}

uint8_t ESP8266_ONENET_AT_StartTcp(const char *host, uint16_t port)
{
    char cmd[128];

    if (host == NULL)
    {
        return 1;
    }

    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u", host, port);
    if (ESP8266_ONENET_AT_SendCmd(cmd, ESP8266_AT_RSP_OK, 5000) == 0)
    {
        return 0;
    }

    return ESP8266_ONENET_AT_SendCmd(cmd, ESP8266_AT_RSP_CONNECT, 5000);
}

uint8_t ESP8266_ONENET_AT_SendData(const uint8_t *data, uint16_t len)
{
    char cmd[32];
    char rx[ESP8266_ONENET_AT_RSP_SIZE];

    if (data == NULL || len == 0U)
    {
        return 1;
    }

    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", len);

    ESP8266_ONENET_AT_ClearRx();
    if (ESP8266_ONENET_AT_SendRaw(cmd) != 0 || ESP8266_ONENET_AT_SendRaw("\r\n") != 0)
    {
        return 1;
    }

    ESP8266_ONENET_AT_ReadUntil(rx, sizeof(rx), ESP8266_AT_RSP_SEND_PROMPT, 2000);
    if (strchr(rx, '>') == NULL)
    {
        return 1;
    }

    ESP8266_ONENET_AT_ClearRx();
    if (ESP8266_ONENET_AT_SendBytes(data, len) != 0)
    {
        return 1;
    }

    ESP8266_ONENET_AT_ReadUntil(rx, sizeof(rx), ESP8266_AT_RSP_SEND_OK, 3000);
    return strstr(rx, ESP8266_AT_RSP_SEND_OK) != NULL ? 0 : 1;
}

uint8_t ESP8266_ONENET_AT_WaitTcpPacket(uint8_t *payload, uint16_t len, uint16_t *out_len, uint32_t timeout_ms)
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

    if (payload == NULL || len == 0U || out_len == NULL)
    {
        return 1;
    }

    *out_len = 0;

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        ch = ESP8266_ONENET_AT_ReadDmaByte();
        if (ch < 0)
        {
            HAL_Delay(1);
            continue;
        }

        if (hpos < (sizeof(header) - 1U))
        {
            header[hpos++] = (char)ch;
            header[hpos] = '\0';
        }
        else
        {
            memmove(header, &header[1], sizeof(header) - 2U);
            header[sizeof(header) - 2U] = (char)ch;
            header[sizeof(header) - 1U] = '\0';
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
        if (packet_len == 0U)
        {
            return 1;
        }
        if (packet_len >= len)
        {
            packet_len = (uint16_t)(len - 1U);
        }

        for (i = 0; i < packet_len; )
        {
            if ((HAL_GetTick() - start) >= timeout_ms)
            {
                return 1;
            }
            ch = ESP8266_ONENET_AT_ReadDmaByte();
            if (ch < 0)
            {
                HAL_Delay(1);
                continue;
            }
            payload[i++] = (uint8_t)ch;
        }

        *out_len = packet_len;
        return 0;
    }

    return 1;
}

uint8_t ESP8266_ONENET_AT_GetSntpTime(char *buf, uint16_t len)
{
    char rx[256];
    char *p;
    char *e;
    uint16_t copy_len;
    uint8_t cfg_ok = 0;

    if (buf == NULL || len == 0U)
    {
        return 1;
    }
    buf[0] = '\0';

    if (ESP8266_ONENET_AT_SendCmd("AT+CIPSNTPCFG=1,8", ESP8266_AT_RSP_OK, 2000) == 0)
    {
        cfg_ok = 1;
    }
    else if (ESP8266_ONENET_AT_SendCmd("AT+CIPSNTPCFG=1,8,\"ntp.aliyun.com\",\"cn.ntp.org.cn\"",
                                       ESP8266_AT_RSP_OK, 2000) == 0)
    {
        cfg_ok = 1;
    }

    if (!cfg_ok)
    {
        ESP8266_ONENET_AT_ClearRx();
        if (ESP8266_ONENET_AT_SendRaw("AT+GMR\r\n") == 0)
        {
            ESP8266_ONENET_AT_ReadUntil(rx, sizeof(rx), ESP8266_AT_RSP_OK, 2000);
            if (ESP8266_ONENET_AT_VERBOSE_LOG) printf("ONENET ESP8266 GMR=[%s]\r\n", rx);
        }
        return 1;
    }

    HAL_Delay(3000);
    ESP8266_ONENET_AT_ClearRx();
    if (ESP8266_ONENET_AT_SendRaw("AT+CIPSNTPTIME?\r\n") != 0)
    {
        return 1;
    }
    ESP8266_ONENET_AT_ReadUntil(rx, sizeof(rx), ESP8266_AT_RSP_OK, 5000);

    p = strstr(rx, "+CIPSNTPTIME:");
    if (p == NULL)
    {
        if (ESP8266_ONENET_AT_VERBOSE_LOG) printf("ONENET SNTP raw=[%s]\r\n", rx);
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
        copy_len = (uint16_t)(len - 1U);
    }
    memcpy(buf, p, copy_len);
    buf[copy_len] = '\0';
    return 0;
}

uint8_t ESP8266_ONENET_AT_InitWiFi(void)
{
    if (ESP8266_ONENET_AT_StartDma() != 0)
    {
        return 1;
    }

    if (ESP8266_ONENET_AT_VERBOSE_LOG) printf("ONENET ESP8266: start on USART2 PA2/PA3\r\n");
    if (ESP8266_ONENET_AT_SendCmd(ESP8266_AT_CMD_TEST, ESP8266_AT_RSP_OK, 1000) != 0)
    {
        HAL_Delay(300U);
        if (ESP8266_ONENET_AT_SendCmd(ESP8266_AT_CMD_TEST, ESP8266_AT_RSP_OK, 1500) != 0)
        {
            if (ESP8266_ONENET_AT_VERBOSE_LOG)
            {
                printf("ONENET ESP8266: no AT response on USART2, check PA2->RX, PA3<-TX, GND and 3.3V power\r\n");
            }
            return 1;
        }
    }
    ESP8266_ONENET_AT_SendCmd(ESP8266_AT_CMD_ECHO_OFF, ESP8266_AT_RSP_OK, 1000);
    ESP8266_ONENET_AT_SendCmd(ESP8266_AT_CMD_STATION_MODE, ESP8266_AT_RSP_OK, 1000);
    ESP8266_ONENET_AT_SendCmd(ESP8266_AT_CMD_AUTO_CONN_OFF, ESP8266_AT_RSP_OK, 1000);
    ESP8266_ONENET_AT_SendCmd(ESP8266_AT_CMD_DHCP_ON, ESP8266_AT_RSP_OK, 1000);

    if (ESP8266_ONENET_AT_JoinAp(ESP8266_WIFI_SSID, ESP8266_WIFI_PASSWORD) != 0)
    {
        return 1;
    }

    ESP8266_ONENET_AT_SendCmd(ESP8266_AT_CMD_NORMAL_MODE, ESP8266_AT_RSP_OK, 1000);
    ESP8266_ONENET_AT_SendCmd(ESP8266_AT_CMD_SINGLE_CONN, ESP8266_AT_RSP_OK, 1000);
    ESP8266_ONENET_AT_SendCmd(ESP8266_AT_CMD_IPDINFO_ON, ESP8266_AT_RSP_OK, 1000);
    ESP8266_ONENET_AT_SendCmd(ESP8266_AT_CMD_CLOSE, ESP8266_AT_RSP_OK, 1000);
    return 0;
}
