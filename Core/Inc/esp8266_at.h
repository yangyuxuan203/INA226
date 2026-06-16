#ifndef __ESP8266_AT_H__
#define __ESP8266_AT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

#define ESP8266_AT_CMD_TEST              "AT"
#define ESP8266_AT_CMD_ECHO_OFF          "ATE0"
#define ESP8266_AT_CMD_STATION_MODE      "AT+CWMODE=1"
#define ESP8266_AT_CMD_DHCP_ON           "AT+CWDHCP=1,1"
#define ESP8266_AT_CMD_AUTO_CONN_OFF     "AT+CWAUTOCONN=0"
#define ESP8266_AT_CMD_QUIT_AP           "AT+CWQAP"
#define ESP8266_AT_CMD_GET_IP            "AT+CIFSR"
#define ESP8266_AT_CMD_SINGLE_CONN       "AT+CIPMUX=0"
#define ESP8266_AT_CMD_NORMAL_MODE       "AT+CIPMODE=0"
#define ESP8266_AT_CMD_IPDINFO_ON        "AT+CIPDINFO=1"
#define ESP8266_AT_CMD_CLOSE             "AT+CIPCLOSE"

#define ESP8266_AT_RSP_OK                "OK"
#define ESP8266_AT_RSP_READY             "ready"
#define ESP8266_AT_RSP_WIFI_CONNECTED    "WIFI CONNECTED"
#define ESP8266_AT_RSP_GOT_IP            "WIFI GOT IP"
#define ESP8266_AT_RSP_CONNECT           "CONNECT"
#define ESP8266_AT_RSP_SEND_PROMPT       ">"
#define ESP8266_AT_RSP_SEND_OK           "SEND OK"
#define ESP8266_AT_RSP_ERROR             "ERROR"
#define ESP8266_AT_RSP_CLOSED            "CLOSED"
#define ESP8266_AT_IPD_PREFIX            "+IPD,"

uint8_t ESP8266_AT_StartDma(void);
void ESP8266_AT_ClearRx(void);
uint8_t ESP8266_AT_SendRaw(const char *s);
uint8_t ESP8266_AT_SendBytes(const uint8_t *data, uint16_t len);
uint16_t ESP8266_AT_ReadUntil(char *buf, uint16_t len, const char *expect, uint32_t timeout_ms);
uint8_t ESP8266_AT_SendCmd(const char *cmd, const char *expect, uint32_t timeout_ms);
uint8_t ESP8266_AT_JoinAp(const char *ssid, const char *password);
uint8_t ESP8266_AT_StartUdp(const char *remote_ip, uint16_t remote_port, uint16_t local_port);
uint8_t ESP8266_AT_StartTcp(const char *host, uint16_t port);
uint8_t ESP8266_AT_SendData(const uint8_t *data, uint16_t len);
uint8_t ESP8266_AT_SendDataTo(const uint8_t *data, uint16_t len, const char *remote_ip, uint16_t remote_port);
uint8_t ESP8266_AT_WaitTcpPacket(uint8_t *payload, uint16_t len, uint16_t *out_len, uint32_t timeout_ms);
uint8_t ESP8266_AT_WaitUdpPayload(char *payload, uint16_t len, uint32_t timeout_ms);
uint8_t ESP8266_AT_WaitUdpPayloadFrom(char *payload, uint16_t payload_len,
                                      char *remote_ip, uint16_t remote_ip_len,
                                      uint16_t *remote_port,
                                      uint32_t timeout_ms);
uint8_t ESP8266_AT_GetSntpTime(char *buf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __ESP8266_AT_H__ */
