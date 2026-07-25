#include "esp8266_onenet_at.h"
#include "esp8266_udp.h"
#include "usart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ESP8266_ONENET_AT_DMA_RX_SIZE 1024
#define ESP8266_ONENET_AT_RSP_SIZE    768
#define ESP8266_ONENET_AT_VERBOSE_LOG 1U
#define ESP8266_ONENET_AT_BOOT_WAIT_MS 1500U
#define ESP8266_ONENET_AT_ESCAPE_WAIT_MS 1200U
#define ESP8266_ONENET_AT_READY_RETRY 5U
#define ESP8266_ONENET_TCP_STREAM_SIZE 2048U

typedef enum
{
    ESP8266_RAW_FIND_IPD = 0,
    ESP8266_RAW_READ_IPD_LENGTH,
    ESP8266_RAW_SKIP_IPD_HEADER,
    ESP8266_RAW_READ_IPD_PAYLOAD
} ESP8266_RawState_t;

static uint8_t g_onenet_dma_rx[ESP8266_ONENET_AT_DMA_RX_SIZE];
static uint16_t g_onenet_dma_old_pos = 0;
static uint8_t g_onenet_dma_started = 0;
static ESP8266_RawState_t g_onenet_raw_state = ESP8266_RAW_FIND_IPD;
static uint8_t g_onenet_ipd_prefix_pos = 0U;
static uint32_t g_onenet_ipd_remaining = 0U;
static uint8_t g_onenet_tcp_stream[ESP8266_ONENET_TCP_STREAM_SIZE];
static uint16_t g_onenet_tcp_head = 0U;
static uint16_t g_onenet_tcp_tail = 0U;
static uint16_t g_onenet_tcp_count = 0U;
static uint8_t g_onenet_tcp_overflow = 0U;
static char g_onenet_link_event[32];
static uint8_t g_onenet_link_event_len = 0U;
static uint8_t g_onenet_link_closed = 0U;

static void ESP8266_ONENET_AT_ResetStreamParser(void)
{
    g_onenet_raw_state = ESP8266_RAW_FIND_IPD;
    g_onenet_ipd_prefix_pos = 0U;
    g_onenet_ipd_remaining = 0U;
    g_onenet_tcp_head = 0U;
    g_onenet_tcp_tail = 0U;
    g_onenet_tcp_count = 0U;
    g_onenet_tcp_overflow = 0U;
    g_onenet_link_event_len = 0U;
    g_onenet_link_event[0] = '\0';
    g_onenet_link_closed = 0U;
}

static void ESP8266_ONENET_AT_RecordLinkByte(uint8_t byte)
{
    if (g_onenet_link_event_len < (sizeof(g_onenet_link_event) - 1U))
    {
        g_onenet_link_event[g_onenet_link_event_len++] = (char)byte;
    }
    else
    {
        memmove(g_onenet_link_event, &g_onenet_link_event[1],
                sizeof(g_onenet_link_event) - 2U);
        g_onenet_link_event[sizeof(g_onenet_link_event) - 2U] = (char)byte;
        g_onenet_link_event_len = sizeof(g_onenet_link_event) - 1U;
    }
    g_onenet_link_event[g_onenet_link_event_len] = '\0';

    if (strstr(g_onenet_link_event, "WIFI DISCONNECT") != NULL ||
        strstr(g_onenet_link_event, "CLOSED") != NULL ||
        strstr(g_onenet_link_event, "link is not valid") != NULL)
    {
        g_onenet_link_closed = 1U;
    }
}

static void ESP8266_ONENET_AT_PushTcpByte(uint8_t byte)
{
    if (g_onenet_tcp_count >= sizeof(g_onenet_tcp_stream))
    {
        g_onenet_tcp_overflow = 1U;
        return;
    }

    g_onenet_tcp_stream[g_onenet_tcp_head++] = byte;
    if (g_onenet_tcp_head >= sizeof(g_onenet_tcp_stream))
    {
        g_onenet_tcp_head = 0U;
    }
    g_onenet_tcp_count++;
}

static uint8_t ESP8266_ONENET_AT_FeedRawByte(uint8_t byte)
{
    static const char prefix[] = "+IPD,";

    if (g_onenet_raw_state == ESP8266_RAW_READ_IPD_PAYLOAD)
    {
        ESP8266_ONENET_AT_PushTcpByte(byte);
        if (--g_onenet_ipd_remaining == 0U)
        {
            g_onenet_raw_state = ESP8266_RAW_FIND_IPD;
            g_onenet_ipd_prefix_pos = 0U;
        }
        return 1U;
    }

    if (g_onenet_raw_state == ESP8266_RAW_FIND_IPD)
    {
        if (byte == (uint8_t)prefix[g_onenet_ipd_prefix_pos])
        {
            g_onenet_ipd_prefix_pos++;
            if (g_onenet_ipd_prefix_pos == (sizeof(prefix) - 1U))
            {
                g_onenet_raw_state = ESP8266_RAW_READ_IPD_LENGTH;
                g_onenet_ipd_remaining = 0U;
            }
            return 1U;
        }

        g_onenet_ipd_prefix_pos = (byte == (uint8_t)prefix[0]) ? 1U : 0U;
        ESP8266_ONENET_AT_RecordLinkByte(byte);
        return g_onenet_ipd_prefix_pos != 0U ? 1U : 0U;
    }

    if (g_onenet_raw_state == ESP8266_RAW_READ_IPD_LENGTH)
    {
        if (byte >= '0' && byte <= '9')
        {
            g_onenet_ipd_remaining = g_onenet_ipd_remaining * 10U + (uint32_t)(byte - '0');
            if (g_onenet_ipd_remaining > 65535U)
            {
                g_onenet_raw_state = ESP8266_RAW_FIND_IPD;
                g_onenet_ipd_prefix_pos = 0U;
                g_onenet_tcp_overflow = 1U;
            }
        }
        else if (byte == ':')
        {
            g_onenet_raw_state = g_onenet_ipd_remaining == 0U ?
                                  ESP8266_RAW_FIND_IPD : ESP8266_RAW_READ_IPD_PAYLOAD;
        }
        else if (byte == ',')
        {
            g_onenet_raw_state = ESP8266_RAW_SKIP_IPD_HEADER;
        }
        else
        {
            g_onenet_raw_state = ESP8266_RAW_FIND_IPD;
            g_onenet_ipd_prefix_pos = 0U;
        }
        return 1U;
    }

    if (g_onenet_raw_state == ESP8266_RAW_SKIP_IPD_HEADER && byte == ':')
    {
        g_onenet_raw_state = g_onenet_ipd_remaining == 0U ?
                              ESP8266_RAW_FIND_IPD : ESP8266_RAW_READ_IPD_PAYLOAD;
    }
    return 1U;
}

static uint8_t ESP8266_ONENET_AT_PeekTcp(uint16_t offset)
{
    uint16_t position = (uint16_t)(g_onenet_tcp_tail + offset);
    if (position >= sizeof(g_onenet_tcp_stream))
    {
        position = (uint16_t)(position - sizeof(g_onenet_tcp_stream));
    }
    return g_onenet_tcp_stream[position];
}

static void ESP8266_ONENET_AT_DropTcp(uint16_t length)
{
    g_onenet_tcp_tail = (uint16_t)((g_onenet_tcp_tail + length) % sizeof(g_onenet_tcp_stream));
    g_onenet_tcp_count = (uint16_t)(g_onenet_tcp_count - length);
}

/* Returns 0 for a complete packet, 1 when more bytes are needed, 2 on corruption. */
static uint8_t ESP8266_ONENET_AT_TryPopMqtt(uint8_t *payload, uint16_t capacity,
                                            uint16_t *out_length)
{
    uint32_t remaining_length = 0U;
    uint32_t multiplier = 1U;
    uint32_t total_length;
    uint16_t offset = 1U;
    uint16_t index;
    uint8_t encoded;

    if (g_onenet_tcp_overflow)
    {
        ESP8266_ONENET_AT_ResetStreamParser();
        return 2U;
    }
    if (g_onenet_tcp_count < 2U)
    {
        return 1U;
    }

    do
    {
        if (offset >= g_onenet_tcp_count)
        {
            return 1U;
        }
        if (offset > 4U)
        {
            ESP8266_ONENET_AT_ResetStreamParser();
            return 2U;
        }
        encoded = ESP8266_ONENET_AT_PeekTcp(offset++);
        remaining_length += (uint32_t)(encoded & 0x7FU) * multiplier;
        multiplier *= 128U;
    } while ((encoded & 0x80U) != 0U);

    total_length = (uint32_t)offset + remaining_length;
    if (total_length > sizeof(g_onenet_tcp_stream))
    {
        ESP8266_ONENET_AT_ResetStreamParser();
        return 2U;
    }
    if (g_onenet_tcp_count < total_length)
    {
        return 1U;
    }
    if (total_length >= capacity)
    {
        ESP8266_ONENET_AT_DropTcp((uint16_t)total_length);
        return 2U;
    }

    for (index = 0U; index < total_length; index++)
    {
        payload[index] = ESP8266_ONENET_AT_PeekTcp(index);
    }
    ESP8266_ONENET_AT_DropTcp((uint16_t)total_length);
    *out_length = (uint16_t)total_length;
    return 0U;
}

static void ESP8266_ONENET_AT_ClearUartErrors(void)
{
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    __HAL_UART_CLEAR_FEFLAG(&huart2);
    __HAL_UART_CLEAR_NEFLAG(&huart2);
    __HAL_UART_CLEAR_PEFLAG(&huart2);
    huart2.ErrorCode = HAL_UART_ERROR_NONE;
}

static uint8_t ESP8266_ONENET_AT_RestartDma(void)
{
    HAL_UART_AbortReceive(&huart2);
    ESP8266_ONENET_AT_ClearUartErrors();

    if (HAL_UART_Receive_DMA(&huart2, g_onenet_dma_rx, ESP8266_ONENET_AT_DMA_RX_SIZE) != HAL_OK)
    {
        g_onenet_dma_started = 0;
        return 1;
    }

    __HAL_DMA_DISABLE_IT(huart2.hdmarx, DMA_IT_HT);
    g_onenet_dma_old_pos = 0;
    g_onenet_dma_started = 1;
    ESP8266_ONENET_AT_ResetStreamParser();
    return 0;
}

static uint8_t ESP8266_ONENET_AT_EnsureDma(void)
{
    if (!g_onenet_dma_started ||
        huart2.RxState != HAL_UART_STATE_BUSY_RX ||
        huart2.ErrorCode != HAL_UART_ERROR_NONE)
    {
        return ESP8266_ONENET_AT_RestartDma();
    }

    return 0;
}

uint8_t ESP8266_ONENET_AT_StartDma(void)
{
    if (g_onenet_dma_started)
    {
        return ESP8266_ONENET_AT_EnsureDma();
    }

    HAL_UART_AbortTransmit(&huart2);
    return ESP8266_ONENET_AT_RestartDma();
}

void ESP8266_ONENET_AT_ClearRx(void)
{
    uint16_t pos;

    if (ESP8266_ONENET_AT_EnsureDma() != 0)
    {
        return;
    }

    pos = (uint16_t)(ESP8266_ONENET_AT_DMA_RX_SIZE - __HAL_DMA_GET_COUNTER(huart2.hdmarx));
    if (pos >= ESP8266_ONENET_AT_DMA_RX_SIZE)
    {
        pos = 0;
    }
    g_onenet_dma_old_pos = pos;
    ESP8266_ONENET_AT_ResetStreamParser();
}

static int16_t ESP8266_ONENET_AT_ReadDmaByte(void)
{
    uint16_t pos;
    uint8_t ch;

    if (ESP8266_ONENET_AT_EnsureDma() != 0)
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
    uint8_t ret;

    if (data == NULL || len == 0U)
    {
        return 0;
    }

    (void)ESP8266_ONENET_AT_EnsureDma();
    ret = HAL_UART_Transmit(&huart2, (uint8_t *)data, len, 1000U) == HAL_OK ? 0U : 1U;
    if (huart2.ErrorCode != HAL_UART_ERROR_NONE)
    {
        (void)ESP8266_ONENET_AT_RestartDma();
    }
    return ret;
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
            if (ESP8266_ONENET_AT_FeedRawByte((uint8_t)ch) == 0U)
            {
                buf[pos++] = (char)ch;
                buf[pos] = '\0';
                if (expect != NULL && strstr(buf, expect) != NULL)
                {
                    break;
                }
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
    uint16_t rx_len;
    uint8_t retry;

    rx[0] = '\0';
    for (retry = 0; retry < 2U; retry++)
    {
        ESP8266_ONENET_AT_ClearRx();
        if (ESP8266_ONENET_AT_SendRaw(cmd) != 0 || ESP8266_ONENET_AT_SendRaw("\r\n") != 0)
        {
            (void)ESP8266_ONENET_AT_RestartDma();
            HAL_Delay(50);
            continue;
        }

        rx_len = ESP8266_ONENET_AT_ReadUntil(rx, sizeof(rx), expect, timeout_ms);
        if (expect == NULL || strstr(rx, expect) != NULL)
        {
            return 0;
        }
        if (strcmp(cmd, ESP8266_AT_CMD_CLOSE) == 0 && strstr(rx, ESP8266_AT_RSP_ERROR) != NULL)
        {
            return 0;
        }

        if (rx_len != 0U || retry != 0U)
        {
            break;
        }

        if (ESP8266_ONENET_AT_VERBOSE_LOG)
        {
            printf("ONENET AT empty rsp, restart USART2 RX DMA and retry cmd=[%s]\r\n", cmd);
        }
        (void)ESP8266_ONENET_AT_RestartDma();
        HAL_Delay(100);
    }

    if (ESP8266_ONENET_AT_VERBOSE_LOG) printf("ONENET AT fail cmd=[%s] rsp=[%s]\r\n", cmd, rx);
    return 1;
}

static void ESP8266_ONENET_AT_PrepareBeforeInit(void)
{
    (void)ESP8266_ONENET_AT_StartDma();

    HAL_Delay(ESP8266_ONENET_AT_BOOT_WAIT_MS);
    ESP8266_ONENET_AT_ClearRx();

    /* Leave possible transparent transmission mode from a previous run. */
    (void)ESP8266_ONENET_AT_SendRaw("+++");
    HAL_Delay(ESP8266_ONENET_AT_ESCAPE_WAIT_MS);
    ESP8266_ONENET_AT_ClearRx();

    HAL_Delay(200U);
    ESP8266_ONENET_AT_ClearRx();
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

    if (ESP8266_ONENET_AT_SendRaw(cmd) != 0 || ESP8266_ONENET_AT_SendRaw("\r\n") != 0)
    {
        return 1;
    }

    ESP8266_ONENET_AT_ReadUntil(rx, sizeof(rx), ESP8266_AT_RSP_SEND_PROMPT, 2000);
    if (strchr(rx, '>') == NULL)
    {
        return 1;
    }

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
    int16_t ch;
    uint8_t packet_result;

    if (payload == NULL || len == 0U || out_len == NULL)
    {
        return 1;
    }

    *out_len = 0;

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        packet_result = ESP8266_ONENET_AT_TryPopMqtt(payload, len, out_len);
        if (packet_result == 0U)
        {
            return 0U;
        }
        if (packet_result == 2U || g_onenet_link_closed)
        {
            return 2U;
        }

        ch = ESP8266_ONENET_AT_ReadDmaByte();
        if (ch < 0)
        {
            HAL_Delay(1);
            continue;
        }

        (void)ESP8266_ONENET_AT_FeedRawByte((uint8_t)ch);
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
    uint8_t retry;

    if (buf == NULL || len == 0U)
    {
        return 1;
    }
    buf[0] = '\0';

    if (ESP8266_ONENET_AT_SendCmd("AT+CIPSNTPCFG=1,8,\"ntp.aliyun.com\",\"cn.ntp.org.cn\"",
                                  ESP8266_AT_RSP_OK, 3000) == 0)
    {
        cfg_ok = 1;
    }
    else if (ESP8266_ONENET_AT_SendCmd("AT+CIPSNTPCFG=1,8", ESP8266_AT_RSP_OK, 3000) == 0)
    {
        cfg_ok = 1;
    }

    if (!cfg_ok)
    {
        ESP8266_ONENET_AT_ClearRx();
        if (ESP8266_ONENET_AT_SendRaw("AT+GMR\r\n") == 0)
        {
            ESP8266_ONENET_AT_ReadUntil(rx, sizeof(rx), ESP8266_AT_RSP_OK, 2000);
            if (ESP8266_ONENET_AT_VERBOSE_LOG) printf("ONENET SNTP cfg failed, GMR=[%s]\r\n", rx);
        }
        return 1;
    }

    for (retry = 0; retry < 5U; retry++)
    {
        HAL_Delay(3000);
        ESP8266_ONENET_AT_ClearRx();
        if (ESP8266_ONENET_AT_SendRaw("AT+CIPSNTPTIME?\r\n") != 0)
        {
            return 1;
        }
        ESP8266_ONENET_AT_ReadUntil(rx, sizeof(rx), ESP8266_AT_RSP_OK, 5000);
        if (ESP8266_ONENET_AT_VERBOSE_LOG) printf("ONENET SNTP try %u raw=[%s]\r\n", retry + 1U, rx);

        p = strstr(rx, "CIPSNTPTIME:");
        if (p == NULL)
        {
            continue;
        }
        p += strlen("CIPSNTPTIME:");
        while (*p == ' ') p++;
        e = strpbrk(p, "\r\n");
        if (e == NULL)
        {
            e = p + strlen(p);
        }

        if (strstr(p, "1970") != NULL)
        {
            continue;
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

    return 1;
}

uint8_t ESP8266_ONENET_AT_InitWiFi(void)
{
    uint8_t retry;
    uint8_t at_ready = 0U;

    if (ESP8266_ONENET_AT_StartDma() != 0)
    {
        return 1;
    }

    ESP8266_ONENET_AT_PrepareBeforeInit();

    if (ESP8266_ONENET_AT_VERBOSE_LOG)
    {
        printf("ONENET ESP8266: wait ready on USART2 PA2/PA3\r\n");
    }

    for (retry = 0U; retry < ESP8266_ONENET_AT_READY_RETRY; retry++)
    {
        uint32_t timeout_ms = 1000U + (uint32_t)retry * 500U;

        if (ESP8266_ONENET_AT_SendCmd(ESP8266_AT_CMD_TEST, ESP8266_AT_RSP_OK, timeout_ms) == 0)
        {
            at_ready = 1U;
            break;
        }

        HAL_Delay(500U);
        ESP8266_ONENET_AT_ClearRx();
    }

    if (!at_ready)
    {
        if (ESP8266_ONENET_AT_VERBOSE_LOG)
        {
            printf("ONENET ESP8266: no AT response after warmup, check USART2 wiring/power\r\n");
        }
        return 1;
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
