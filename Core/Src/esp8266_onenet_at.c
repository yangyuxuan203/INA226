#include "esp8266_onenet_at.h"
#include "esp8266_udp.h"
#include "usart.h"
#include "cmsis_os.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ESP8266_ONENET_AT_DMA_RX_SIZE 2048U
#define ESP8266_ONENET_AT_RSP_SIZE    768
#ifndef ESP8266_ONENET_AT_VERBOSE_LOG
#define ESP8266_ONENET_AT_VERBOSE_LOG 0U
#endif
#define ESP8266_ONENET_AT_BOOT_WAIT_MS 1500U
#define ESP8266_ONENET_AT_ESCAPE_WAIT_MS 1200U
#define ESP8266_ONENET_AT_READY_RETRY 5U
#define ESP8266_ONENET_TCP_STREAM_SIZE 2048U
#define ESP8266_ONENET_SNTP_RETRY_COUNT 3U
#define ESP8266_ONENET_SNTP_READY_WAIT_MS 1500U
#define ESP8266_ONENET_SNTP_QUERY_TIMEOUT_MS 2500U

typedef enum
{
    ESP8266_RAW_FIND_IPD = 0,
    ESP8266_RAW_READ_IPD_LENGTH,
    ESP8266_RAW_SKIP_IPD_HEADER,
    ESP8266_RAW_READ_IPD_PAYLOAD
} ESP8266_RawState_t;

static uint8_t g_onenet_dma_rx[ESP8266_ONENET_AT_DMA_RX_SIZE];
static uint16_t g_onenet_dma_old_pos = 0;
static volatile uint32_t g_onenet_dma_wrap_count = 0U;
static uint32_t g_onenet_dma_old_wrap_count = 0U;
static uint8_t g_onenet_dma_overflow = 0U;
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
static uint8_t g_onenet_sntp_configured = 0U;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2)
    {
        g_onenet_dma_wrap_count++;
    }
}

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
    g_onenet_dma_started = 0U;
    HAL_UART_AbortReceive(&huart2);
    ESP8266_ONENET_AT_ClearUartErrors();

    g_onenet_dma_wrap_count = 0U;
    g_onenet_dma_old_wrap_count = 0U;
    g_onenet_dma_overflow = 0U;

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
        uint8_t was_started = g_onenet_dma_started;
        uint8_t result = ESP8266_ONENET_AT_RestartDma();
        if (result == 0U && was_started != 0U)
        {
            g_onenet_dma_overflow = 1U;
        }
        return result;
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
    uint32_t wrap_count;
    uint32_t wrap_count_after;

    if (ESP8266_ONENET_AT_EnsureDma() != 0)
    {
        return;
    }

    do
    {
        wrap_count = g_onenet_dma_wrap_count;
        pos = (uint16_t)(ESP8266_ONENET_AT_DMA_RX_SIZE -
                         __HAL_DMA_GET_COUNTER(huart2.hdmarx));
        if (pos >= ESP8266_ONENET_AT_DMA_RX_SIZE)
        {
            pos = 0U;
        }
        wrap_count_after = g_onenet_dma_wrap_count;
    }
    while (wrap_count != wrap_count_after);

    g_onenet_dma_old_pos = pos;
    g_onenet_dma_old_wrap_count = wrap_count;
    g_onenet_dma_overflow = 0U;
    ESP8266_ONENET_AT_ResetStreamParser();
}

static int16_t ESP8266_ONENET_AT_ReadDmaByte(void)
{
    uint16_t pos;
    uint8_t ch;
    uint32_t wrap_count;
    uint32_t wrap_count_after;
    uint32_t wrap_delta;
    uint32_t available;

    if (ESP8266_ONENET_AT_EnsureDma() != 0)
    {
        return -1;
    }

    do
    {
        wrap_count = g_onenet_dma_wrap_count;
        pos = (uint16_t)(ESP8266_ONENET_AT_DMA_RX_SIZE -
                         __HAL_DMA_GET_COUNTER(huart2.hdmarx));
        if (pos >= ESP8266_ONENET_AT_DMA_RX_SIZE)
        {
            pos = 0U;
        }
        wrap_count_after = g_onenet_dma_wrap_count;
    }
    while (wrap_count != wrap_count_after);

    wrap_delta = wrap_count - g_onenet_dma_old_wrap_count;
    if (wrap_delta == 0U)
    {
        if (pos < g_onenet_dma_old_pos)
        {
            g_onenet_dma_overflow = 1U;
            g_onenet_dma_old_pos = pos;
            return -1;
        }
        available = (uint32_t)(pos - g_onenet_dma_old_pos);
    }
    else
    {
        available = (wrap_delta - 1U) * ESP8266_ONENET_AT_DMA_RX_SIZE;
        available += ESP8266_ONENET_AT_DMA_RX_SIZE - g_onenet_dma_old_pos;
        available += pos;
    }

    if (available == 0U)
    {
        return -1;
    }
    if (available > ESP8266_ONENET_AT_DMA_RX_SIZE)
    {
        g_onenet_dma_overflow = 1U;
        g_onenet_dma_old_pos = pos;
        g_onenet_dma_old_wrap_count = wrap_count;
        return -1;
    }

    ch = g_onenet_dma_rx[g_onenet_dma_old_pos++];
    if (g_onenet_dma_old_pos >= ESP8266_ONENET_AT_DMA_RX_SIZE)
    {
        g_onenet_dma_old_pos = 0;
        g_onenet_dma_old_wrap_count++;
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
            osDelay(1U);
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
            osDelay(50U);
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
        osDelay(100U);
    }

    if (ESP8266_ONENET_AT_VERBOSE_LOG) printf("ONENET AT fail cmd=[%s] rsp=[%s]\r\n", cmd, rx);
    return 1;
}

static void ESP8266_ONENET_AT_PrepareBeforeInit(void)
{
    (void)ESP8266_ONENET_AT_StartDma();

    osDelay(ESP8266_ONENET_AT_BOOT_WAIT_MS);
    ESP8266_ONENET_AT_ClearRx();

    /* Leave possible transparent transmission mode from a previous run. */
    (void)ESP8266_ONENET_AT_SendRaw("+++");
    osDelay(ESP8266_ONENET_AT_ESCAPE_WAIT_MS);
    ESP8266_ONENET_AT_ClearRx();

    osDelay(200U);
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

        osDelay(1000U);
    }

    return ESP8266_ONENET_AT_SendCmd("AT+CWJAP?", ESP8266_AT_RSP_OK, 3000);
}

static uint8_t ESP8266_ONENET_AT_ResponseHasLine(const char *response,
                                                  const char *expected)
{
    const char *line;
    const char *end;
    size_t expected_len;

    if (response == NULL || expected == NULL || expected[0] == '\0')
    {
        return 0U;
    }

    expected_len = strlen(expected);
    line = response;
    while (*line != '\0')
    {
        while (*line == '\r' || *line == '\n')
        {
            line++;
        }
        if (*line == '\0')
        {
            break;
        }

        end = strpbrk(line, "\r\n");
        if (end == NULL)
        {
            end = line + strlen(line);
        }
        if ((size_t)(end - line) == expected_len &&
            memcmp(line, expected, expected_len) == 0)
        {
            return 1U;
        }
        line = end;
    }

    return 0U;
}

uint8_t ESP8266_ONENET_AT_StartTcp(const char *host, uint16_t port)
{
    char cmd[128];
    char rx[ESP8266_ONENET_AT_RSP_SIZE];

    if (host == NULL)
    {
        return 1;
    }

    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",%u", host, port);
    ESP8266_ONENET_AT_ClearRx();
    if (ESP8266_ONENET_AT_SendRaw(cmd) != 0U ||
        ESP8266_ONENET_AT_SendRaw("\r\n") != 0U)
    {
        return 1U;
    }

    (void)ESP8266_ONENET_AT_ReadUntil(rx, sizeof(rx),
                                      "CONNECT\r\n", 5000U);
    return (ESP8266_ONENET_AT_ResponseHasLine(
                rx, ESP8266_AT_RSP_CONNECT) != 0U ||
            ESP8266_ONENET_AT_ResponseHasLine(
                rx, "ALREADY CONNECTED") != 0U) ? 0U : 1U;
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

ESP8266_ONENET_AT_Result_t ESP8266_ONENET_AT_WaitTcpPacket(
    uint8_t *payload, uint16_t len, uint16_t *out_len, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    int16_t ch;
    uint8_t packet_result;

    if (payload == NULL || len == 0U || out_len == NULL)
    {
        return ESP8266_ONENET_AT_RX_ERROR;
    }

    *out_len = 0;

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        if (g_onenet_dma_overflow != 0U)
        {
            g_onenet_dma_overflow = 0U;
            ESP8266_ONENET_AT_ResetStreamParser();
            return ESP8266_ONENET_AT_RX_ERROR;
        }

        packet_result = ESP8266_ONENET_AT_TryPopMqtt(payload, len, out_len);
        if (packet_result == 0U)
        {
            return ESP8266_ONENET_AT_OK;
        }
        if (g_onenet_link_closed)
        {
            return ESP8266_ONENET_AT_LINK_CLOSED;
        }
        if (packet_result == 2U)
        {
            return ESP8266_ONENET_AT_RX_ERROR;
        }

        ch = ESP8266_ONENET_AT_ReadDmaByte();
        if (ch < 0)
        {
            osDelay(1U);
            continue;
        }

        (void)ESP8266_ONENET_AT_FeedRawByte((uint8_t)ch);
    }

    return ESP8266_ONENET_AT_TIMEOUT;
}

static uint8_t ESP8266_ONENET_AT_ParseSntpTime(const char *response,
                                                char *buf,
                                                uint16_t len)
{
    const char *start;
    const char *end;
    uint16_t copy_len;

    if (response == NULL || buf == NULL || len == 0U)
    {
        return 1U;
    }

    start = strstr(response, "CIPSNTPTIME:");
    if (start == NULL)
    {
        return 1U;
    }

    start += strlen("CIPSNTPTIME:");
    while (*start == ' ')
    {
        start++;
    }
    if (strstr(start, "1970") != NULL)
    {
        return 1U;
    }

    end = strpbrk(start, "\r\n");
    if (end == NULL)
    {
        end = start + strlen(start);
    }
    if (end == start)
    {
        return 1U;
    }

    copy_len = (uint16_t)(end - start);
    if (copy_len >= len)
    {
        copy_len = (uint16_t)(len - 1U);
    }
    memcpy(buf, start, copy_len);
    buf[copy_len] = '\0';
    return 0U;
}

uint8_t ESP8266_ONENET_AT_GetSntpTime(char *buf, uint16_t len)
{
    char rx[256];
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
        g_onenet_sntp_configured = 0U;
        ESP8266_ONENET_AT_ClearRx();
        if (ESP8266_ONENET_AT_SendRaw("AT+GMR\r\n") == 0)
        {
            ESP8266_ONENET_AT_ReadUntil(rx, sizeof(rx), ESP8266_AT_RSP_OK, 2000);
            if (ESP8266_ONENET_AT_VERBOSE_LOG) printf("ONENET SNTP cfg failed, GMR=[%s]\r\n", rx);
        }
        return 1;
    }
    g_onenet_sntp_configured = 1U;

    for (retry = 0U; retry < ESP8266_ONENET_SNTP_RETRY_COUNT; retry++)
    {
        osDelay(ESP8266_ONENET_SNTP_READY_WAIT_MS);
        ESP8266_ONENET_AT_ClearRx();
        if (ESP8266_ONENET_AT_SendRaw("AT+CIPSNTPTIME?\r\n") != 0)
        {
            return 1;
        }
        ESP8266_ONENET_AT_ReadUntil(rx, sizeof(rx), ESP8266_AT_RSP_OK,
                                    ESP8266_ONENET_SNTP_QUERY_TIMEOUT_MS);
        if (ESP8266_ONENET_AT_VERBOSE_LOG) printf("ONENET SNTP try %u raw=[%s]\r\n", retry + 1U, rx);

        if (ESP8266_ONENET_AT_ParseSntpTime(rx, buf, len) == 0U)
        {
            return 0U;
        }
    }

    return 1U;
}

uint8_t ESP8266_ONENET_AT_QuerySntpTimeOnline(char *buf, uint16_t len)
{
    char rx[256];

    if (buf == NULL || len == 0U)
    {
        return 1U;
    }
    buf[0] = '\0';

    /* The normal online RX path has already drained pending AT text. Do not
     * call ClearRx here: ReadUntil keeps any concurrent +IPD payload queued
     * for the MQTT task instead of discarding it.
     */
    if (!g_onenet_sntp_configured)
    {
        if (ESP8266_ONENET_AT_SendRaw(
                "AT+CIPSNTPCFG=1,8,\"ntp.aliyun.com\",\"cn.ntp.org.cn\"\r\n") != 0U)
        {
            return 1U;
        }
        (void)ESP8266_ONENET_AT_ReadUntil(
            rx, sizeof(rx), ESP8266_AT_RSP_OK,
            ESP8266_ONENET_SNTP_QUERY_TIMEOUT_MS);
        if (strstr(rx, ESP8266_AT_RSP_OK) == NULL)
        {
            if (ESP8266_ONENET_AT_SendRaw(
                    "AT+CIPSNTPCFG=1,8\r\n") != 0U)
            {
                return 1U;
            }
            (void)ESP8266_ONENET_AT_ReadUntil(
                rx, sizeof(rx), ESP8266_AT_RSP_OK,
                ESP8266_ONENET_SNTP_QUERY_TIMEOUT_MS);
            if (strstr(rx, ESP8266_AT_RSP_OK) == NULL)
            {
                return 1U;
            }
        }
        g_onenet_sntp_configured = 1U;
        osDelay(ESP8266_ONENET_SNTP_READY_WAIT_MS);
    }

    if (ESP8266_ONENET_AT_SendRaw("AT+CIPSNTPTIME?\r\n") != 0U)
    {
        return 1U;
    }
    (void)ESP8266_ONENET_AT_ReadUntil(
        rx, sizeof(rx), ESP8266_AT_RSP_OK,
        ESP8266_ONENET_SNTP_QUERY_TIMEOUT_MS);
    if (ESP8266_ONENET_AT_ParseSntpTime(rx, buf, len) != 0U)
    {
        g_onenet_sntp_configured = 0U;
        return 1U;
    }
    return 0U;
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

        osDelay(500U);
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

    if (ESP8266_ONENET_AT_SendCmd(ESP8266_AT_CMD_NORMAL_MODE,
                                  ESP8266_AT_RSP_OK, 1000U) != 0U ||
        ESP8266_ONENET_AT_SendCmd(ESP8266_AT_CMD_SINGLE_CONN,
                                  ESP8266_AT_RSP_OK, 1000U) != 0U)
    {
        return 1U;
    }
    (void)ESP8266_ONENET_AT_SendCmd(ESP8266_AT_CMD_IPDINFO_ON,
                                     ESP8266_AT_RSP_OK, 1000U);
    ESP8266_ONENET_AT_SendCmd(ESP8266_AT_CMD_CLOSE, ESP8266_AT_RSP_OK, 1000);
    return 0;
}
