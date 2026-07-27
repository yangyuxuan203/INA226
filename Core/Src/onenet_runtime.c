#include "onenet_runtime.h"

#include <stddef.h>
#include <string.h>

#define ONENET_RECONNECT_BASE_MS   5000U
#define ONENET_RECONNECT_MAX_MS   60000U
#define ONENET_STABLE_ONLINE_MS  120000U

static uint8_t OneNET_Runtime_TickReached(uint32_t now, uint32_t deadline)
{
    return ((int32_t)(now - deadline) >= 0) ? 1U : 0U;
}

static uint32_t OneNET_Runtime_BackoffMs(uint8_t failure_count)
{
    uint32_t delay_ms = ONENET_RECONNECT_BASE_MS;
    uint8_t step;

    for (step = 1U; step < failure_count; step++)
    {
        if (delay_ms >= (ONENET_RECONNECT_MAX_MS / 2U))
        {
            return ONENET_RECONNECT_MAX_MS;
        }
        delay_ms *= 2U;
    }

    return delay_ms > ONENET_RECONNECT_MAX_MS ?
           ONENET_RECONNECT_MAX_MS : delay_ms;
}

void OneNET_Runtime_Init(OneNET_Runtime_t *runtime,
                         uint32_t now,
                         uint32_t startup_delay_ms)
{
    if (runtime == NULL)
    {
        return;
    }

    memset(runtime, 0, sizeof(*runtime));
    runtime->state = ONENET_LINK_STARTUP_DELAY;
    runtime->next_connect_tick = now + startup_delay_ms;
}

uint8_t OneNET_Runtime_CanConnect(const OneNET_Runtime_t *runtime,
                                  uint32_t now)
{
    if (runtime == NULL ||
        (runtime->state != ONENET_LINK_STARTUP_DELAY &&
         runtime->state != ONENET_LINK_BACKOFF))
    {
        return 0U;
    }

    return OneNET_Runtime_TickReached(now, runtime->next_connect_tick);
}

void OneNET_Runtime_BeginConnect(OneNET_Runtime_t *runtime)
{
    if (runtime != NULL)
    {
        runtime->state = ONENET_LINK_CONNECTING;
    }
}

void OneNET_Runtime_MarkOnline(OneNET_Runtime_t *runtime, uint32_t now)
{
    if (runtime == NULL)
    {
        return;
    }

    runtime->state = ONENET_LINK_ONLINE;
    runtime->last_disconnect_reason = ONENET_DISCONNECT_NONE;
    runtime->online_since_tick = now;
    runtime->last_ping_tick = now;
    runtime->ping_sent_tick = 0U;
    runtime->ping_outstanding = 0U;
    runtime->protocol_failures = 0U;
    memset(runtime->operation_failures, 0,
           sizeof(runtime->operation_failures));
}

void OneNET_Runtime_ScheduleReconnect(OneNET_Runtime_t *runtime,
                                      uint32_t now,
                                      OneNET_DisconnectReason_t reason)
{
    uint32_t delay_ms;

    if (runtime == NULL)
    {
        return;
    }

    if (runtime->state == ONENET_LINK_ONLINE &&
        (uint32_t)(now - runtime->online_since_tick) >=
            ONENET_STABLE_ONLINE_MS)
    {
        runtime->reconnect_failures = 0U;
    }

    if (runtime->reconnect_failures < 0xFFU)
    {
        runtime->reconnect_failures++;
    }
    delay_ms = OneNET_Runtime_BackoffMs(runtime->reconnect_failures);

    runtime->state = ONENET_LINK_BACKOFF;
    runtime->last_disconnect_reason = reason;
    runtime->next_connect_tick = now + delay_ms;
    runtime->ping_outstanding = 0U;
    runtime->protocol_failures = 0U;
    memset(runtime->operation_failures, 0,
           sizeof(runtime->operation_failures));
    if (runtime->reconnect_count < 0xFFFFFFFFU)
    {
        runtime->reconnect_count++;
    }
}

void OneNET_Runtime_Service(OneNET_Runtime_t *runtime, uint32_t now)
{
    if (runtime != NULL &&
        runtime->state == ONENET_LINK_ONLINE &&
        runtime->reconnect_failures != 0U &&
        (uint32_t)(now - runtime->online_since_tick) >=
            ONENET_STABLE_ONLINE_MS)
    {
        runtime->reconnect_failures = 0U;
    }
}

uint8_t OneNET_Runtime_IsOnline(const OneNET_Runtime_t *runtime)
{
    return (runtime != NULL && runtime->state == ONENET_LINK_ONLINE) ?
           1U : 0U;
}

uint32_t OneNET_Runtime_GetBackoffRemaining(const OneNET_Runtime_t *runtime,
                                            uint32_t now)
{
    if (runtime == NULL || runtime->state != ONENET_LINK_BACKOFF ||
        OneNET_Runtime_TickReached(now, runtime->next_connect_tick))
    {
        return 0U;
    }

    return runtime->next_connect_tick - now;
}

uint8_t OneNET_Runtime_RecordOperation(OneNET_Runtime_t *runtime,
                                       OneNET_Operation_t operation,
                                       uint8_t succeeded)
{
    uint8_t *failure_count;

    if (runtime == NULL || operation >= ONENET_OPERATION_COUNT)
    {
        return 0U;
    }

    failure_count = &runtime->operation_failures[operation];
    if (succeeded != 0U)
    {
        *failure_count = 0U;
        return 0U;
    }

    if (*failure_count < 0xFFU)
    {
        (*failure_count)++;
    }
    return *failure_count >= ONENET_RUNTIME_OPERATION_FAILURE_LIMIT ?
           1U : 0U;
}

uint8_t OneNET_Runtime_RecordProtocol(OneNET_Runtime_t *runtime,
                                      uint8_t succeeded)
{
    if (runtime == NULL)
    {
        return 0U;
    }

    if (succeeded != 0U)
    {
        runtime->protocol_failures = 0U;
        return 0U;
    }

    if (runtime->protocol_failures < 0xFFU)
    {
        runtime->protocol_failures++;
    }
    return runtime->protocol_failures >=
           ONENET_RUNTIME_OPERATION_FAILURE_LIMIT ? 1U : 0U;
}

uint8_t OneNET_Runtime_ShouldPing(const OneNET_Runtime_t *runtime,
                                  uint32_t now,
                                  uint32_t interval_ms)
{
    if (!OneNET_Runtime_IsOnline(runtime) ||
        runtime->ping_outstanding != 0U)
    {
        return 0U;
    }

    return (uint32_t)(now - runtime->last_ping_tick) >= interval_ms ?
           1U : 0U;
}

void OneNET_Runtime_MarkPingSent(OneNET_Runtime_t *runtime, uint32_t now)
{
    if (runtime == NULL)
    {
        return;
    }

    runtime->ping_outstanding = 1U;
    runtime->ping_sent_tick = now;
    runtime->last_ping_tick = now;
}

void OneNET_Runtime_MarkPingResponse(OneNET_Runtime_t *runtime,
                                     uint32_t now)
{
    if (runtime == NULL)
    {
        return;
    }

    runtime->ping_outstanding = 0U;
    runtime->ping_sent_tick = 0U;
    runtime->last_ping_tick = now;
    runtime->operation_failures[ONENET_OPERATION_PING] = 0U;
}

uint8_t OneNET_Runtime_IsPingTimedOut(const OneNET_Runtime_t *runtime,
                                      uint32_t now,
                                      uint32_t timeout_ms)
{
    return (runtime != NULL && runtime->ping_outstanding != 0U &&
            (uint32_t)(now - runtime->ping_sent_tick) >= timeout_ms) ?
           1U : 0U;
}
