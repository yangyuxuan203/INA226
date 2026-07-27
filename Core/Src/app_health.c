#include "app_health.h"

#include "cmsis_os.h"
#include "main.h"

#include <stdio.h>
#include <string.h>

#define APP_HEALTH_CHECK_PERIOD_MS       2000U
#define APP_HEALTH_LOG_PERIOD_MS        30000U
#define APP_HEALTH_LOW_HEAP_BYTES        4096U
#define APP_HEALTH_LOW_STACK_WORDS         64U

#ifndef APP_HEALTH_LOG
#ifdef NDEBUG
#define APP_HEALTH_LOG 0U
#else
#define APP_HEALTH_LOG 1U
#endif
#endif

typedef struct
{
    TaskHandle_t handle;
    volatile uint32_t last_heartbeat_ms;
    uint32_t timeout_ms;
} AppHealthTaskEntry_t;

static AppHealthTaskEntry_t s_tasks[APP_HEALTH_TASK_COUNT];
static AppHealthSnapshot_t s_snapshot;
static volatile uint32_t s_fault_flags;

static const char *const s_task_names[APP_HEALTH_TASK_COUNT] = {
    "homeSensor",
    "gasSensor",
    "lightSensor",
    "energyControl",
    "ui",
    "esp8266",
    "onenet",
};

static uint32_t AppHealth_ResetFlags(void)
{
    return RCC->CSR & (RCC_CSR_LPWRRSTF |
                       RCC_CSR_WWDGRSTF |
                       RCC_CSR_IWDGRSTF |
                       RCC_CSR_SFTRSTF |
                       RCC_CSR_PORRSTF |
                       RCC_CSR_PINRSTF |
                       RCC_CSR_BORRSTF);
}

void AppHealth_Init(void)
{
    memset(s_tasks, 0, sizeof(s_tasks));
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.reset_flags = AppHealth_ResetFlags();
    s_fault_flags = APP_HEALTH_FAULT_NONE;
    __HAL_RCC_CLEAR_RESET_FLAGS();
}

void AppHealth_RegisterTask(AppHealthTaskId_t id, TaskHandle_t handle,
                            uint32_t timeout_ms)
{
    if (id >= APP_HEALTH_TASK_COUNT || handle == NULL || timeout_ms == 0U)
    {
        return;
    }

    s_tasks[id].handle = handle;
    s_tasks[id].last_heartbeat_ms = HAL_GetTick();
    s_tasks[id].timeout_ms = timeout_ms;
}

void AppHealth_Heartbeat(AppHealthTaskId_t id)
{
    if (id >= APP_HEALTH_TASK_COUNT)
    {
        return;
    }

    s_tasks[id].last_heartbeat_ms = HAL_GetTick();
}

void AppHealth_ReportFault(AppHealthFault_t fault)
{
    s_fault_flags |= (uint32_t)fault;
    s_snapshot.fault_flags = s_fault_flags;
}

uint8_t AppHealth_GetSnapshot(AppHealthSnapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return 1U;
    }

    taskENTER_CRITICAL();
    *snapshot = s_snapshot;
    taskEXIT_CRITICAL();
    return 0U;
}

void AppHealth_Task(void const *argument)
{
    uint32_t previous_stale_mask = 0U;
    uint32_t previous_warning_flags = 0U;
    uint32_t previous_fault_flags = 0U;
    uint32_t last_log_ms = 0U;

    (void)argument;

    for (;;)
    {
        AppHealthSnapshot_t next;
        uint32_t now = HAL_GetTick();
        uint32_t index;

        memset(&next, 0, sizeof(next));
        next.uptime_ms = now;
        next.reset_flags = s_snapshot.reset_flags;
        next.free_heap_bytes = (uint32_t)xPortGetFreeHeapSize();
        next.minimum_ever_free_heap_bytes =
            (uint32_t)xPortGetMinimumEverFreeHeapSize();
        next.fault_flags = s_fault_flags;

        if (next.free_heap_bytes < APP_HEALTH_LOW_HEAP_BYTES)
        {
            next.warning_flags |= APP_HEALTH_WARNING_LOW_HEAP;
        }

        for (index = 0U; index < APP_HEALTH_TASK_COUNT; index++)
        {
            AppHealthTaskEntry_t *entry = &s_tasks[index];
            UBaseType_t stack_words;

            if (entry->handle == NULL)
            {
                continue;
            }

            next.registered_mask |= (1UL << index);
            stack_words = uxTaskGetStackHighWaterMark(entry->handle);
            next.stack_high_water_words[index] =
                stack_words > UINT16_MAX ? UINT16_MAX :
                (uint16_t)stack_words;
            if (stack_words < APP_HEALTH_LOW_STACK_WORDS)
            {
                next.warning_flags |= APP_HEALTH_WARNING_LOW_STACK;
            }

            if ((now - entry->last_heartbeat_ms) > entry->timeout_ms)
            {
                next.stale_mask |= (1UL << index);
            }
        }

        if (next.stale_mask != 0U)
        {
            next.warning_flags |= APP_HEALTH_WARNING_TASK_STALE;
        }

        taskENTER_CRITICAL();
        s_snapshot = next;
        taskEXIT_CRITICAL();

        if (APP_HEALTH_LOG &&
            (next.stale_mask != previous_stale_mask ||
             next.warning_flags != previous_warning_flags ||
             next.fault_flags != previous_fault_flags ||
             (now - last_log_ms) >= APP_HEALTH_LOG_PERIOD_MS))
        {
            printf("HEALTH: warn=%08lX fault=%08lX stale=%08lX "
                   "heap=%lu min_heap=%lu\r\n",
                   (unsigned long)next.warning_flags,
                   (unsigned long)next.fault_flags,
                   (unsigned long)next.stale_mask,
                   (unsigned long)next.free_heap_bytes,
                   (unsigned long)next.minimum_ever_free_heap_bytes);
            for (index = 0U; index < APP_HEALTH_TASK_COUNT; index++)
            {
                if ((next.stale_mask & (1UL << index)) != 0U)
                {
                    printf("HEALTH: stale task=%s stack_free=%u words\r\n",
                           s_task_names[index],
                           next.stack_high_water_words[index]);
                }
            }
            last_log_ms = now;
        }

        previous_stale_mask = next.stale_mask;
        previous_warning_flags = next.warning_flags;
        previous_fault_flags = next.fault_flags;
        osDelay(APP_HEALTH_CHECK_PERIOD_MS);
    }
}
