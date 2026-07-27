/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : RTOS resources and task scheduling
  ******************************************************************************
  */
/* USER CODE END Header */

#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

#include "app_health.h"
#include "app_state.h"
#include "app_tasks.h"
#include "energy_service.h"
#include "esp32_service.h"
#include "onenet_service.h"

#include <stdio.h>

#define HEALTH_HOME_TIMEOUT_MS       2000U
#define HEALTH_GAS_TIMEOUT_MS        3000U
#define HEALTH_LIGHT_TIMEOUT_MS      3000U
#define HEALTH_ENERGY_TIMEOUT_MS     2000U
#define HEALTH_UI_TIMEOUT_MS         5000U
#define HEALTH_ESP32_TIMEOUT_MS     30000U
#define HEALTH_ONENET_TIMEOUT_MS    90000U

static void FreeRTOS_RequireCreated(const void *handle, const char *name)
{
    if (handle == NULL)
    {
        printf("FreeRTOS: failed to create %s\r\n", name);
        Error_Handler();
    }
}

static void FreeRTOS_RequireInitialized(uint8_t result, const char *name)
{
    if (result != 0U)
    {
        printf("FreeRTOS: failed to initialize %s\r\n", name);
        Error_Handler();
    }
}

void vApplicationGetIdleTaskMemory(StaticTask_t **task_buffer,
                                   StackType_t **stack_buffer,
                                   uint32_t *stack_size)
{
    static StaticTask_t idle_tcb;
    static StackType_t idle_stack[configMINIMAL_STACK_SIZE];

    *task_buffer = &idle_tcb;
    *stack_buffer = idle_stack;
    *stack_size = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **task_buffer,
                                    StackType_t **stack_buffer,
                                    uint32_t *stack_size)
{
    static StaticTask_t timer_tcb;
    static StackType_t timer_stack[256];

    *task_buffer = &timer_tcb;
    *stack_buffer = timer_stack;
    *stack_size = 256U;
}

void vApplicationMallocFailedHook(void)
{
    AppHealth_ReportFault(APP_HEALTH_FAULT_MALLOC);
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}

void vApplicationStackOverflowHook(TaskHandle_t task, char *task_name)
{
    (void)task;
    (void)task_name;
    AppHealth_ReportFault(APP_HEALTH_FAULT_STACK_OVERFLOW);
    taskDISABLE_INTERRUPTS();
    for (;;)
    {
    }
}

void MX_FREERTOS_Init(void)
{
    osThreadId thread_id;

    AppHealth_Init();
    FreeRTOS_RequireInitialized(AppState_Init(), "app_state");
    FreeRTOS_RequireInitialized(EnergyService_Init(), "energy_service");

    osThreadDef(homeSensor, HomeSensorTask, osPriorityNormal, 0, 256);
    thread_id = osThreadCreate(osThread(homeSensor), NULL);
    FreeRTOS_RequireCreated(thread_id, "homeSensor");
    AppHealth_RegisterTask(APP_HEALTH_TASK_HOME_SENSOR,
                           (TaskHandle_t)thread_id,
                           HEALTH_HOME_TIMEOUT_MS);

    osThreadDef(gasSensor, GasSensorTask,
                osPriorityBelowNormal, 0, 256);
    thread_id = osThreadCreate(osThread(gasSensor), NULL);
    FreeRTOS_RequireCreated(thread_id, "gasSensor");
    AppHealth_RegisterTask(APP_HEALTH_TASK_GAS_SENSOR,
                           (TaskHandle_t)thread_id,
                           HEALTH_GAS_TIMEOUT_MS);

    osThreadDef(lightSensor, LightSensorTask,
                osPriorityBelowNormal, 0, 256);
    thread_id = osThreadCreate(osThread(lightSensor), NULL);
    FreeRTOS_RequireCreated(thread_id, "lightSensor");
    AppHealth_RegisterTask(APP_HEALTH_TASK_LIGHT_SENSOR,
                           (TaskHandle_t)thread_id,
                           HEALTH_LIGHT_TIMEOUT_MS);

    osThreadDef(energyControl, EnergyControlTask,
                osPriorityBelowNormal, 0, 512);
    thread_id = osThreadCreate(osThread(energyControl), NULL);
    FreeRTOS_RequireCreated(thread_id, "energyControl");
    AppHealth_RegisterTask(APP_HEALTH_TASK_ENERGY_CONTROL,
                           (TaskHandle_t)thread_id,
                           HEALTH_ENERGY_TIMEOUT_MS);

    osThreadDef(ui, UiTask, osPriorityBelowNormal, 0, 1024);
    thread_id = osThreadCreate(osThread(ui), NULL);
    FreeRTOS_RequireCreated(thread_id, "ui");
    AppHealth_RegisterTask(APP_HEALTH_TASK_UI,
                           (TaskHandle_t)thread_id,
                           HEALTH_UI_TIMEOUT_MS);

    osThreadDef(esp8266, ESP32_ServiceTask,
                osPriorityBelowNormal, 0, 1024);
    thread_id = osThreadCreate(osThread(esp8266), NULL);
    FreeRTOS_RequireCreated(thread_id, "esp8266");
    AppHealth_RegisterTask(APP_HEALTH_TASK_ESP32,
                           (TaskHandle_t)thread_id,
                           HEALTH_ESP32_TIMEOUT_MS);

    osThreadDef(onenet, OneNET_ServiceTask,
                osPriorityBelowNormal, 0, 1280);
    thread_id = osThreadCreate(osThread(onenet), NULL);
    FreeRTOS_RequireCreated(thread_id, "onenet");
    AppHealth_RegisterTask(APP_HEALTH_TASK_ONENET,
                           (TaskHandle_t)thread_id,
                           HEALTH_ONENET_TIMEOUT_MS);

    osThreadDef(health, AppHealth_Task, osPriorityAboveNormal, 0, 384);
    thread_id = osThreadCreate(osThread(health), NULL);
    FreeRTOS_RequireCreated(thread_id, "health");
}
