#ifndef __ONENET_RUNTIME_H__
#define __ONENET_RUNTIME_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define ONENET_RUNTIME_OPERATION_FAILURE_LIMIT 3U

typedef enum
{
    ONENET_LINK_STARTUP_DELAY = 0,
    ONENET_LINK_BACKOFF,
    ONENET_LINK_CONNECTING,
    ONENET_LINK_ONLINE
} OneNET_LinkState_t;

typedef enum
{
    ONENET_DISCONNECT_NONE = 0,
    ONENET_DISCONNECT_WIFI_INIT,
    ONENET_DISCONNECT_TCP_OPEN,
    ONENET_DISCONNECT_MQTT_CONNECT,
    ONENET_DISCONNECT_SUBSCRIBE,
    ONENET_DISCONNECT_LINK_CLOSED,
    ONENET_DISCONNECT_PROTOCOL,
    ONENET_DISCONNECT_PING_SEND,
    ONENET_DISCONNECT_PING_TIMEOUT,
    ONENET_DISCONNECT_TELEMETRY,
    ONENET_DISCONNECT_SWITCH_SYNC
} OneNET_DisconnectReason_t;

typedef enum
{
    ONENET_OPERATION_PING = 0,
    ONENET_OPERATION_TELEMETRY,
    ONENET_OPERATION_SWITCH_SYNC,
    ONENET_OPERATION_CONTROL_REPLY,
    ONENET_OPERATION_COUNT
} OneNET_Operation_t;

typedef struct
{
    OneNET_LinkState_t state;
    OneNET_DisconnectReason_t last_disconnect_reason;
    uint32_t next_connect_tick;
    uint32_t online_since_tick;
    uint32_t last_ping_tick;
    uint32_t ping_sent_tick;
    uint32_t reconnect_count;
    uint8_t reconnect_failures;
    uint8_t protocol_failures;
    uint8_t operation_failures[ONENET_OPERATION_COUNT];
    uint8_t ping_outstanding;
} OneNET_Runtime_t;

void OneNET_Runtime_Init(OneNET_Runtime_t *runtime,
                         uint32_t now,
                         uint32_t startup_delay_ms);
uint8_t OneNET_Runtime_CanConnect(const OneNET_Runtime_t *runtime,
                                  uint32_t now);
void OneNET_Runtime_BeginConnect(OneNET_Runtime_t *runtime);
void OneNET_Runtime_MarkOnline(OneNET_Runtime_t *runtime, uint32_t now);
void OneNET_Runtime_ScheduleReconnect(OneNET_Runtime_t *runtime,
                                      uint32_t now,
                                      OneNET_DisconnectReason_t reason);
void OneNET_Runtime_Service(OneNET_Runtime_t *runtime, uint32_t now);
uint8_t OneNET_Runtime_IsOnline(const OneNET_Runtime_t *runtime);
uint32_t OneNET_Runtime_GetBackoffRemaining(const OneNET_Runtime_t *runtime,
                                            uint32_t now);

uint8_t OneNET_Runtime_RecordOperation(OneNET_Runtime_t *runtime,
                                       OneNET_Operation_t operation,
                                       uint8_t succeeded);
uint8_t OneNET_Runtime_RecordProtocol(OneNET_Runtime_t *runtime,
                                      uint8_t succeeded);

uint8_t OneNET_Runtime_ShouldPing(const OneNET_Runtime_t *runtime,
                                  uint32_t now,
                                  uint32_t interval_ms);
void OneNET_Runtime_MarkPingSent(OneNET_Runtime_t *runtime, uint32_t now);
void OneNET_Runtime_MarkPingResponse(OneNET_Runtime_t *runtime,
                                     uint32_t now);
uint8_t OneNET_Runtime_IsPingTimedOut(const OneNET_Runtime_t *runtime,
                                      uint32_t now,
                                      uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* __ONENET_RUNTIME_H__ */
