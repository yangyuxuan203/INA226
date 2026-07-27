#ifndef APP_HEALTH_H
#define APP_HEALTH_H

#include "FreeRTOS.h"
#include "task.h"

#include <stdint.h>

typedef enum
{
    APP_HEALTH_TASK_HOME_SENSOR = 0,
    APP_HEALTH_TASK_GAS_SENSOR,
    APP_HEALTH_TASK_LIGHT_SENSOR,
    APP_HEALTH_TASK_ENERGY_CONTROL,
    APP_HEALTH_TASK_UI,
    APP_HEALTH_TASK_ESP32,
    APP_HEALTH_TASK_ONENET,
    APP_HEALTH_TASK_COUNT
} AppHealthTaskId_t;

typedef enum
{
    APP_HEALTH_FAULT_NONE = 0U,
    APP_HEALTH_FAULT_MALLOC = (1UL << 0),
    APP_HEALTH_FAULT_STACK_OVERFLOW = (1UL << 1)
} AppHealthFault_t;

typedef enum
{
    APP_HEALTH_WARNING_NONE = 0U,
    APP_HEALTH_WARNING_TASK_STALE = (1UL << 0),
    APP_HEALTH_WARNING_LOW_HEAP = (1UL << 1),
    APP_HEALTH_WARNING_LOW_STACK = (1UL << 2)
} AppHealthWarning_t;

typedef struct
{
    uint32_t uptime_ms;
    uint32_t reset_flags;
    uint32_t registered_mask;
    uint32_t stale_mask;
    uint32_t warning_flags;
    uint32_t fault_flags;
    uint32_t free_heap_bytes;
    uint32_t minimum_ever_free_heap_bytes;
    uint16_t stack_high_water_words[APP_HEALTH_TASK_COUNT];
} AppHealthSnapshot_t;

void AppHealth_Init(void);
void AppHealth_RegisterTask(AppHealthTaskId_t id, TaskHandle_t handle,
                            uint32_t timeout_ms);
void AppHealth_Heartbeat(AppHealthTaskId_t id);
void AppHealth_ReportFault(AppHealthFault_t fault);
uint8_t AppHealth_GetSnapshot(AppHealthSnapshot_t *snapshot);
void AppHealth_Task(void const *argument);

#endif /* APP_HEALTH_H */
