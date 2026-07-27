#ifndef __ESP8266_ONENET_AT_H__
#define __ESP8266_ONENET_AT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "esp8266_at.h"
#include <stdint.h>

typedef enum
{
    ESP8266_ONENET_AT_OK = 0,
    ESP8266_ONENET_AT_TIMEOUT,
    ESP8266_ONENET_AT_LINK_CLOSED,
    ESP8266_ONENET_AT_RX_ERROR
} ESP8266_ONENET_AT_Result_t;

uint8_t ESP8266_ONENET_AT_InitWiFi(void);
uint8_t ESP8266_ONENET_AT_StartDma(void);
void ESP8266_ONENET_AT_ClearRx(void);
uint8_t ESP8266_ONENET_AT_SendRaw(const char *s);
uint8_t ESP8266_ONENET_AT_SendBytes(const uint8_t *data, uint16_t len);
uint16_t ESP8266_ONENET_AT_ReadUntil(char *buf, uint16_t len, const char *expect, uint32_t timeout_ms);
uint8_t ESP8266_ONENET_AT_SendCmd(const char *cmd, const char *expect, uint32_t timeout_ms);
uint8_t ESP8266_ONENET_AT_JoinAp(const char *ssid, const char *password);
uint8_t ESP8266_ONENET_AT_StartTcp(const char *host, uint16_t port);
uint8_t ESP8266_ONENET_AT_SendData(const uint8_t *data, uint16_t len);
ESP8266_ONENET_AT_Result_t ESP8266_ONENET_AT_WaitTcpPacket(
    uint8_t *payload, uint16_t len, uint16_t *out_len, uint32_t timeout_ms);
uint8_t ESP8266_ONENET_AT_GetSntpTime(char *buf, uint16_t len);
uint8_t ESP8266_ONENET_AT_QuerySntpTimeOnline(char *buf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __ESP8266_ONENET_AT_H__ */
