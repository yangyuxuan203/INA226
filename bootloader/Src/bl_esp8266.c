#include "bl_esp8266.h"
#include "bl_board.h"
#include "bl_flash.h"
#include "wifi_config.h"
#include "stm32f4xx_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BL_AT_RESPONSE_TIMEOUT_MS  5000U
#define BL_HTTP_IDLE_TIMEOUT_MS    15000U
#define BL_HTTP_HEADER_SIZE        1024U
#define BL_HTTP_HOST_SIZE          96U
#define BL_HTTP_PATH_SIZE          384U

typedef struct
{
    uint8_t use_ssl;
    uint16_t port;
    char host[BL_HTTP_HOST_SIZE];
    char path[BL_HTTP_PATH_SIZE];
} BL_URL_t;

typedef enum
{
    BL_TCP_FIND_PREFIX = 0,
    BL_TCP_READ_LENGTH,
    BL_TCP_SKIP_HEADER,
    BL_TCP_READ_PAYLOAD
} BL_TCPState_t;

static BL_TCPState_t s_tcp_state = BL_TCP_FIND_PREFIX;
static uint8_t s_tcp_prefix_position = 0U;
static uint32_t s_tcp_payload_remaining = 0U;

static void BL_TCP_Reset(void)
{
    s_tcp_state = BL_TCP_FIND_PREFIX;
    s_tcp_prefix_position = 0U;
    s_tcp_payload_remaining = 0U;
}

static uint8_t BL_WriteString(const char *text)
{
    if (text == NULL)
    {
        return 1U;
    }
    return BL_UART_Write((const uint8_t *)text, (uint16_t)strlen(text), 3000U);
}

static uint8_t BL_WaitFor(const char *expected, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint32_t match = 0U;
    uint32_t expected_length;
    uint8_t byte;

    if (expected == NULL || expected[0] == '\0')
    {
        return 1U;
    }
    expected_length = (uint32_t)strlen(expected);

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        if (BL_UART_ReadByte(&byte, 10U) != 0U)
        {
            continue;
        }

        if (byte == (uint8_t)expected[match])
        {
            match++;
            if (match == expected_length)
            {
                return 0U;
            }
        }
        else
        {
            match = (byte == (uint8_t)expected[0]) ? 1U : 0U;
        }
    }

    return 1U;
}

static uint8_t BL_SendCommand(const char *command, const char *expected, uint32_t timeout_ms)
{
    BL_UART_Flush();
    if (BL_WriteString(command) != 0U || BL_WriteString("\r\n") != 0U)
    {
        return 1U;
    }
    return BL_WaitFor(expected, timeout_ms);
}

static uint8_t BL_ParseURL(const char *url, BL_URL_t *parsed)
{
    const char *host_start;
    const char *path_start;
    const char *query_start;
    const char *host_end;
    const char *port_separator = NULL;
    size_t host_length;
    size_t path_length;

    if (url == NULL || parsed == NULL)
    {
        return 1U;
    }

    memset(parsed, 0, sizeof(*parsed));
    if (strncmp(url, "http://", 7U) == 0)
    {
        parsed->port = 80U;
        parsed->use_ssl = 0U;
        host_start = url + 7U;
    }
    else if (strncmp(url, "https://", 8U) == 0)
    {
        parsed->port = 443U;
        parsed->use_ssl = 1U;
        host_start = url + 8U;
    }
    else
    {
        return 1U;
    }

    path_start = strchr(host_start, '/');
    query_start = strchr(host_start, '?');
    if (path_start == NULL ||
        (query_start != NULL && query_start < path_start))
    {
        host_end = query_start != NULL ? query_start : host_start + strlen(host_start);
        path_start = NULL;
    }
    else
    {
        host_end = path_start;
    }
    for (const char *cursor = host_start; cursor < host_end; cursor++)
    {
        if (*cursor == ':')
        {
            port_separator = cursor;
        }
    }

    if (port_separator != NULL)
    {
        char *port_end;
        unsigned long port = strtoul(port_separator + 1, &port_end, 10);
        if (port == 0UL || port > 65535UL || port_end != host_end)
        {
            return 1U;
        }
        parsed->port = (uint16_t)port;
        host_end = port_separator;
    }

    host_length = (size_t)(host_end - host_start);
    if (host_length == 0U || host_length >= sizeof(parsed->host))
    {
        return 1U;
    }
    for (const char *cursor = host_start; cursor < host_end; cursor++)
    {
        char value = *cursor;
        if (!((value >= 'a' && value <= 'z') ||
              (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') ||
              value == '.' || value == '-'))
        {
            return 1U;
        }
    }
    if (host_start[0] == '.' || host_start[0] == '-' ||
        host_end[-1] == '.' || host_end[-1] == '-')
    {
        return 1U;
    }
    memcpy(parsed->host, host_start, host_length);
    parsed->host[host_length] = '\0';

    if (path_start == NULL)
    {
        if (query_start == NULL)
        {
            strcpy(parsed->path, "/");
        }
        else
        {
            path_length = strlen(query_start);
            if ((path_length + 1U) >= sizeof(parsed->path))
            {
                return 1U;
            }
            parsed->path[0] = '/';
            memcpy(&parsed->path[1], query_start, path_length + 1U);
        }
    }
    else
    {
        path_length = strlen(path_start);
        if (path_length >= sizeof(parsed->path))
        {
            return 1U;
        }
        memcpy(parsed->path, path_start, path_length + 1U);
    }

    return 0U;
}

static const char *BL_FindHeader(const char *header, const char *name)
{
    size_t name_length = strlen(name);
    const char *line = header;

    while (line != NULL && *line != '\0')
    {
        const char *line_end = strstr(line, "\r\n");
        size_t line_length = line_end != NULL ? (size_t)(line_end - line) : strlen(line);
        size_t i;

        if (line_length <= name_length)
        {
            line = line_end != NULL ? line_end + 2U : NULL;
            continue;
        }
        for (i = 0U; i < name_length; i++)
        {
            char left = line[i];
            char right = name[i];
            if (left >= 'A' && left <= 'Z') left = (char)(left + ('a' - 'A'));
            if (right >= 'A' && right <= 'Z') right = (char)(right + ('a' - 'A'));
            if (left != right) break;
        }
        if (i == name_length && line[i] == ':')
        {
            line += name_length + 1U;
            while (*line == ' ' || *line == '\t') line++;
            return line;
        }
        line = line_end != NULL ? line_end + 2U : NULL;
    }

    return NULL;
}

static uint8_t BL_HTTPHeaderIsValid(const char *header, uint32_t expected_size)
{
    const char *content_length;
    const char *transfer_encoding;
    char *length_end;
    unsigned long length;

    if ((strncmp(header, "HTTP/1.1 200", 12U) != 0 &&
         strncmp(header, "HTTP/1.0 200", 12U) != 0))
    {
        return 0U;
    }

    transfer_encoding = BL_FindHeader(header, "Transfer-Encoding");
    if (transfer_encoding != NULL)
    {
        return 0U;
    }

    content_length = BL_FindHeader(header, "Content-Length");
    if (content_length == NULL)
    {
        return 0U;
    }

    length = strtoul(content_length, &length_end, 10);
    if (length_end == content_length)
    {
        return 0U;
    }
    while (*length_end == ' ' || *length_end == '\t')
    {
        length_end++;
    }
    if (*length_end != '\r')
    {
        return 0U;
    }

    return length == expected_size ? 1U : 0U;
}

static uint8_t BL_TCP_ReadByte(uint8_t *payload_byte, uint32_t timeout_ms)
{
    static const char prefix[] = "+IPD,";
    uint32_t start = HAL_GetTick();
    uint8_t byte;

    while ((HAL_GetTick() - start) < timeout_ms)
    {
        if (BL_UART_ReadByte(&byte, 10U) != 0U)
        {
            continue;
        }

        if (s_tcp_state == BL_TCP_READ_PAYLOAD)
        {
            *payload_byte = byte;
            s_tcp_payload_remaining--;
            if (s_tcp_payload_remaining == 0U)
            {
                s_tcp_state = BL_TCP_FIND_PREFIX;
                s_tcp_prefix_position = 0U;
            }
            return 0U;
        }

        if (s_tcp_state == BL_TCP_FIND_PREFIX)
        {
            if (byte == (uint8_t)prefix[s_tcp_prefix_position])
            {
                s_tcp_prefix_position++;
                if (s_tcp_prefix_position == (sizeof(prefix) - 1U))
                {
                    s_tcp_state = BL_TCP_READ_LENGTH;
                    s_tcp_payload_remaining = 0U;
                }
            }
            else
            {
                s_tcp_prefix_position = (byte == (uint8_t)prefix[0]) ? 1U : 0U;
            }
            continue;
        }

        if (s_tcp_state == BL_TCP_READ_LENGTH)
        {
            if (byte >= '0' && byte <= '9')
            {
                s_tcp_payload_remaining = s_tcp_payload_remaining * 10U + (uint32_t)(byte - '0');
                if (s_tcp_payload_remaining > 65535U)
                {
                    BL_TCP_Reset();
                    return 1U;
                }
            }
            else if (byte == ':')
            {
                if (s_tcp_payload_remaining == 0U)
                {
                    BL_TCP_Reset();
                    return 1U;
                }
                s_tcp_state = BL_TCP_READ_PAYLOAD;
            }
            else if (byte == ',')
            {
                s_tcp_state = BL_TCP_SKIP_HEADER;
            }
            else
            {
                BL_TCP_Reset();
            }
            continue;
        }

        if (s_tcp_state == BL_TCP_SKIP_HEADER && byte == ':')
        {
            s_tcp_state = s_tcp_payload_remaining == 0U ?
                          BL_TCP_FIND_PREFIX : BL_TCP_READ_PAYLOAD;
        }
    }

    return 1U;
}

static void BL_HTTP_Close(void)
{
    (void)BL_SendCommand("AT+CIPCLOSE", "OK", 2000U);
    BL_TCP_Reset();
}

static uint8_t BL_HTTP_OpenAndSend(const BL_URL_t *url, const char *method)
{
    char command[192];
    char request[640];
    char host_header[112];
    int command_length;
    int request_length;
    uint8_t default_port;

    if (url == NULL || method == NULL)
    {
        return 1U;
    }

    command_length = snprintf(command, sizeof(command),
                              "AT+CIPSTART=\"%s\",\"%s\",%u",
                              url->use_ssl ? "SSL" : "TCP", url->host, url->port);
    if (command_length <= 0 || (size_t)command_length >= sizeof(command) ||
        BL_SendCommand(command, "OK", 12000U) != 0U)
    {
        return 1U;
    }

    default_port = ((!url->use_ssl && url->port == 80U) ||
                    (url->use_ssl && url->port == 443U)) ? 1U : 0U;
    if (default_port)
    {
        command_length = snprintf(host_header, sizeof(host_header), "%s", url->host);
    }
    else
    {
        command_length = snprintf(host_header, sizeof(host_header), "%s:%u",
                                  url->host, url->port);
    }
    if (command_length <= 0 || (size_t)command_length >= sizeof(host_header))
    {
        BL_HTTP_Close();
        return 1U;
    }

    request_length = snprintf(request, sizeof(request),
                              "%s %s HTTP/1.1\r\n"
                              "Host: %s\r\n"
                              "Accept: application/octet-stream\r\n"
                              "Connection: close\r\n\r\n",
                              method, url->path, host_header);
    if (request_length <= 0 || (size_t)request_length >= sizeof(request))
    {
        BL_HTTP_Close();
        return 1U;
    }

    command_length = snprintf(command, sizeof(command), "AT+CIPSEND=%d", request_length);
    if (command_length <= 0 || (size_t)command_length >= sizeof(command))
    {
        BL_HTTP_Close();
        return 1U;
    }

    BL_TCP_Reset();
    BL_UART_Flush();
    if (BL_WriteString(command) != 0U || BL_WriteString("\r\n") != 0U ||
        BL_WaitFor(">", BL_AT_RESPONSE_TIMEOUT_MS) != 0U ||
        BL_UART_Write((const uint8_t *)request, (uint16_t)request_length, 5000U) != 0U)
    {
        BL_HTTP_Close();
        return 1U;
    }

    return 0U;
}

static uint8_t BL_HTTP_ReadHeader(char *header, uint32_t capacity)
{
    uint32_t header_length = 0U;
    uint8_t byte;

    if (header == NULL || capacity < 5U)
    {
        return 1U;
    }

    header[0] = '\0';
    while (header_length < (capacity - 1U))
    {
        if (BL_TCP_ReadByte(&byte, BL_HTTP_IDLE_TIMEOUT_MS) != 0U)
        {
            return 1U;
        }

        header[header_length++] = (char)byte;
        header[header_length] = '\0';
        if (header_length >= 4U &&
            memcmp(&header[header_length - 4U], "\r\n\r\n", 4U) == 0)
        {
            return 0U;
        }
    }

    return 1U;
}

uint8_t BL_ESP8266_InitWiFi(void)
{
    char command[192];

    HAL_Delay(1500U);
    BL_UART_Flush();
    (void)BL_WriteString("+++");
    HAL_Delay(1200U);
    BL_UART_Flush();

    if (BL_SendCommand("AT", "OK", 2000U) != 0U ||
        BL_SendCommand("ATE0", "OK", 2000U) != 0U ||
        BL_SendCommand("AT+CWMODE=1", "OK", 2000U) != 0U)
    {
        return 1U;
    }

    snprintf(command, sizeof(command), "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PASSWORD);
    if (BL_SendCommand(command, "OK", 25000U) != 0U)
    {
        return 1U;
    }

    if (BL_SendCommand("AT+CIPMODE=0", "OK", 2000U) != 0U ||
        BL_SendCommand("AT+CIPMUX=0", "OK", 2000U) != 0U ||
        BL_SendCommand("AT+CIPDINFO=0", "OK", 2000U) != 0U)
    {
        return 1U;
    }

    (void)BL_SendCommand("AT+CIPCLOSE", "OK", 2000U);
    return 0U;
}

uint8_t BL_ESP8266_PreflightImage(const OTA_Metadata_t *metadata)
{
    BL_URL_t url;
    char header[BL_HTTP_HEADER_SIZE];
    uint8_t result = 1U;

    if (metadata == NULL || BL_ParseURL(metadata->url, &url) != 0U)
    {
        return 1U;
    }

    if (BL_HTTP_OpenAndSend(&url, "HEAD") == 0U &&
        BL_HTTP_ReadHeader(header, sizeof(header)) == 0U &&
        BL_HTTPHeaderIsValid(header, metadata->image_size))
    {
        result = 0U;
    }

    BL_HTTP_Close();

    /* Some object stores reject HEAD. Validate a GET response header without
       consuming or writing its body, then close the connection immediately. */
    if (result != 0U)
    {
        if (BL_HTTP_OpenAndSend(&url, "GET") == 0U &&
            BL_HTTP_ReadHeader(header, sizeof(header)) == 0U &&
            BL_HTTPHeaderIsValid(header, metadata->image_size))
        {
            result = 0U;
        }
        BL_HTTP_Close();
    }

    return result;
}

uint8_t BL_ESP8266_DownloadImage(const OTA_Metadata_t *metadata)
{
    BL_URL_t url;
    char header[BL_HTTP_HEADER_SIZE];
    uint8_t byte;
    uint8_t result = 1U;

    if (metadata == NULL || BL_ParseURL(metadata->url, &url) != 0U)
    {
        return 1U;
    }

    if (BL_HTTP_OpenAndSend(&url, "GET") != 0U ||
        BL_HTTP_ReadHeader(header, sizeof(header)) != 0U ||
        !BL_HTTPHeaderIsValid(header, metadata->image_size))
    {
        goto done;
    }

    while (BL_Flash_BytesWritten() < metadata->image_size)
    {
        if (BL_TCP_ReadByte(&byte, BL_HTTP_IDLE_TIMEOUT_MS) != 0U)
        {
            goto done;
        }

        if (BL_Flash_Write(&byte, 1U) != 0U)
        {
            goto done;
        }
    }

    result = 0U;

done:
    BL_HTTP_Close();
    return result;
}
