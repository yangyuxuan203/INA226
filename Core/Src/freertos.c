/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ina226.h"
#include "adc.h"
#include "gy30.h"
#include "can.h"
#include "lcd.h"
#include "touch.h"
#include <stdio.h>
#include <string.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* Shared sensor data structure */
typedef struct {
    float bus_voltage;
    float shunt_voltage;
    float current;
    float power;
    uint16_t mq9_adc;
    float mq9_voltage;
    float lux;
    uint8_t ina226_ok;
    uint8_t gy30_ok;
} SensorData_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* Shared data protected by mutex */
osMutexId g_mutex;
osSemaphoreId g_data_ready;
SensorData_t g_sensor = {0};

/* CAN receive queue */
QueueHandle_t g_can_rx_queue = NULL;

/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN Function Prototypes */

/* USER CODE END Function Prototypes */

/* Hook prototypes */
/* USER CODE BEGIN Application */

/* USER CODE END Application */

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/**
  * @brief  Default task: INA226 current/voltage sensor (software I2C on PB6/PB7)
  */
void StartDefaultTask(void const * argument)
{
  INA226_Data ina_data;
  INA226_Init();
  printf("INA226 initialized (PB6=SCL, PB7=SDA)\r\n");

  for(;;)
  {
    if (INA226_ReadData(&ina_data) == 0)
    {
      osMutexWait(g_mutex, osWaitForever);
      g_sensor.bus_voltage = ina_data.bus_voltage;
      g_sensor.shunt_voltage = ina_data.shunt_voltage;
      g_sensor.current = ina_data.current;
      g_sensor.power = ina_data.power;
      g_sensor.ina226_ok = 1;
      osMutexRelease(g_mutex);
      osSemaphoreRelease(g_data_ready);
    }
    else
    {
      osMutexWait(g_mutex, osWaitForever);
      g_sensor.ina226_ok = 0;
      osMutexRelease(g_mutex);
    }
    osDelay(200);
  }
}

/**
  * @brief  UART upload task: prints sensor data every 1000ms
  */
void StartTask01(void const * argument)
{
  SensorData_t local;

  for(;;)
  {
    osSemaphoreWait(g_data_ready, osWaitForever);

    osMutexWait(g_mutex, osWaitForever);
    local = g_sensor;
    osMutexRelease(g_mutex);

    if (local.ina226_ok)
    {
      printf("INA226: V=%.3fV I=%.3fmA P=%.3fmW\r\n",
             (double)local.bus_voltage,
             (double)(local.current * 1000.0f),
             (double)(local.power * 1000.0f));
    }
    if (local.gy30_ok)
    {
      printf("GY30: %.1f lux\r\n", (double)local.lux);
    }

    osDelay(1000);
  }
}

/**
  * @brief  ADC task: MQ9 gas sensor on PA1 (ADC1_CH1)
  */
void StartTask02(void const * argument)
{
  HAL_ADC_Start(&hadc1);

  for(;;)
  {
    if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
    {
      uint16_t raw = (uint16_t)HAL_ADC_GetValue(&hadc1);
      float voltage = (float)raw / 4095.0f * 3.3f;

      osMutexWait(g_mutex, osWaitForever);
      g_sensor.mq9_adc = raw;
      g_sensor.mq9_voltage = voltage;
      osMutexRelease(g_mutex);
    }
    osDelay(500);
  }
}

/**
  * @brief  GY30 light sensor task (software I2C on PC1/PC2)
  */
void StartTask03(void const * argument)
{
  float light;

  GY30_Init();
  printf("GY30 initialized (PC1=SCL, PC2=SDA)\r\n");

  for(;;)
  {
    if (GY30_ReadLight(&light) == 0)
    {
      printf("GY30: %.1f lux\r\n", (double)light);
      osMutexWait(g_mutex, osWaitForever);
      g_sensor.lux = light;
      g_sensor.gy30_ok = 1;
      osMutexRelease(g_mutex);
      osSemaphoreRelease(g_data_ready);
    }
    else
    {
      printf("GY30 read error\r\n");
      osMutexWait(g_mutex, osWaitForever);
      g_sensor.gy30_ok = 0;
      osMutexRelease(g_mutex);
    }
    osDelay(500);
  }
}

/**
* @brief CAN communication task.
*        Sends sensor data and receives commands every 1000ms.
* @param argument: Not used
* @retval None
*/
void StartTask04(void const * argument)
{
  SensorData_t local;
  uint8_t tx_data[8];
  CAN_Msg_t rx_msg;

  CAN_Start();
  printf("CAN1 ready (PA11=RX, PA12=TX)\r\n");

  for(;;)
  {
    /* Read shared data */
    osMutexWait(g_mutex, osWaitForever);
    local = g_sensor;
    osMutexRelease(g_mutex);

    /* Send sensor data via CAN (ID=0x100) */
    if (local.ina226_ok)
    {
      /* Pack bus voltage (mV) and current (mA) into 4 bytes */
      uint16_t mv = (uint16_t)(local.bus_voltage * 1000);
      uint16_t ma = (uint16_t)(local.current * 1000);
      tx_data[0] = (mv >> 8) & 0xFF;
      tx_data[1] = mv & 0xFF;
      tx_data[2] = (ma >> 8) & 0xFF;
      tx_data[3] = ma & 0xFF;
      tx_data[4] = local.gy30_ok ? 1 : 0;
      tx_data[5] = 0;
      tx_data[6] = 0;
      tx_data[7] = 0;
      CAN_Send(0x100, tx_data, 8);
    }

    /* Check for received CAN data via queue (non-blocking poll) */
    if (g_can_rx_queue && xQueueReceive(g_can_rx_queue, &rx_msg, 0) == pdTRUE)
    {
      printf("CAN RX: ID=0x%03lX len=%d data=", (unsigned long)rx_msg.id, rx_msg.len);
      for (int i = 0; i < rx_msg.len; i++)
          printf("%02X ", rx_msg.data[i]);
      printf("\r\n");
    }

    osDelay(1000);
  }
}

/**
* @brief LCD display and touch task.
*        Shows INA226 and MQ9 data on TFTLCD, handles touch input.
* @param argument: Not used
* @retval None
*/
void StartTask05(void const * argument)
{
  SensorData_t local;
  uint8_t touch_ok;

  /* Clear screen and draw static labels */
  lcd_clear(WHITE);
  lcd_show_string(10, 10, 200, 24, 24, "INA226 Monitor", RED);
  lcd_show_string(10, 40, 200, 16, 16, "Bus Voltage:", BLUE);
  lcd_show_string(10, 70, 200, 16, 16, "Current:", BLUE);
  lcd_show_string(10, 100, 200, 16, 16, "Power:", BLUE);
  lcd_show_string(10, 140, 200, 24, 24, "MQ9 Sensor", RED);
  lcd_show_string(10, 170, 200, 16, 16, "ADC Raw:", BLUE);
  lcd_show_string(10, 200, 200, 16, 16, "Voltage:", BLUE);
  lcd_show_string(10, 230, 200, 16, 16, "Light:", BLUE);

  /* Initialize touch screen */
  touch_ok = tp_init();
  if (touch_ok == 0) {
      lcd_show_string(10, 280, 300, 16, 16, "Touch: OK", GREEN);
      printf("Touch screen initialized\r\n");
  } else {
      lcd_show_string(10, 280, 300, 16, 16, "Touch: N/A", GRAY);
      printf("Touch screen not found\r\n");
  }

  for(;;)
  {
    /* Read shared sensor data */
    osMutexWait(g_mutex, osWaitForever);
    local = g_sensor;
    osMutexRelease(g_mutex);

    /* Update INA226 display values */
    if (local.ina226_ok)
    {
      int v_i = (int)local.bus_voltage;
      int v_f = (int)((local.bus_voltage - v_i) * 1000);
      if (v_f < 0) v_f = -v_f;
      lcd_fill(120, 40, 280, 56, WHITE);
      lcd_show_string(120, 40, 160, 16, 16, "        ", WHITE);
      lcd_show_num(120, 40, v_i, 1, 16, RED);
      lcd_show_char(128, 40, '.', 16, 0, RED);
      lcd_show_num(136, 40, v_f, 3, 16, RED);
      lcd_show_string(170, 40, 40, 16, 16, " V  ", RED);

      int ma = (int)(local.current * 1000.0f);
      lcd_fill(120, 70, 280, 86, WHITE);
      lcd_show_num(120, 70, ma, 5, 16, RED);
      lcd_show_string(168, 70, 50, 16, 16, " mA  ", RED);

      int mw = (int)(local.power * 1000.0f);
      lcd_fill(120, 100, 280, 116, WHITE);
      lcd_show_num(120, 100, mw, 6, 16, RED);
      lcd_show_string(180, 100, 60, 16, 16, " mW  ", RED);
    }
    else
    {
      lcd_fill(120, 40, 280, 116, WHITE);
      lcd_show_string(120, 40, 150, 16, 16, "ERR", RED);
    }

    /* Update MQ9 display values */
    lcd_fill(120, 170, 280, 186, WHITE);
    lcd_show_num(120, 170, local.mq9_adc, 4, 16, RED);

    int mv_i = (int)local.mq9_voltage;
    int mv_f = (int)((local.mq9_voltage - mv_i) * 100);
    if (mv_f < 0) mv_f = -mv_f;
    lcd_fill(120, 200, 280, 216, WHITE);
    lcd_show_num(120, 200, mv_i, 1, 16, RED);
    lcd_show_char(128, 200, '.', 16, 0, RED);
    lcd_show_num(136, 200, mv_f, 2, 16, RED);
    lcd_show_string(160, 200, 40, 16, 16, " V  ", RED);

    /* Update GY30 light display */
    lcd_fill(120, 230, 280, 246, WHITE);
    if (local.gy30_ok)
    {
      int lx = (int)local.lux;
      int lf = (int)((local.lux - lx) * 10);
      if (lf < 0) lf = -lf;
      lcd_show_num(120, 230, lx, 5, 16, RED);
      lcd_show_char(160, 230, '.', 16, 0, RED);
      lcd_show_num(168, 230, lf, 1, 16, RED);
      lcd_show_string(178, 230, 60, 16, 16, " lux", RED);
    }
    else
    {
      lcd_show_string(120, 230, 80, 16, 16, "N/A", GRAY);
    }

    /* Handle touch input */
    if (touch_ok == 0)
    {
      tp_dev.scan(0);
      if (tp_dev.sta & TP_PRES_DOWN)
      {
        uint16_t tx = tp_dev.x[0];
        uint16_t ty = tp_dev.y[0];
        lcd_draw_circle(tx, ty, 3, RED);
      }
    }

    osDelay(300);
  }
}

/**
  * @brief  Return the CAN RX queue handle (used by can.c ISR callback)
  */
QueueHandle_t CAN_GetRxQueueHandle(void)
{
  return g_can_rx_queue;
}

/**
  * @brief  FreeRTOS idle/timer task memory allocation (required by configSUPPORT_STATIC_ALLOCATION)
  */
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
  static StaticTask_t idle_tcb;
  static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
  *ppxIdleTaskTCBBuffer = &idle_tcb;
  *ppxIdleTaskStackBuffer = idle_stack;
  *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize)
{
  static StaticTask_t timer_tcb;
  static StackType_t timer_stack[256];
  *ppxTimerTaskTCBBuffer = &timer_tcb;
  *ppxTimerTaskStackBuffer = timer_stack;
  *pulTimerTaskStackSize = 256;
}

/**
  * @brief  FreeRTOS initialization - creates mutex, semaphore, queue and tasks
  */
void MX_FREERTOS_Init(void)
{
  /* Create mutex for shared sensor data */
  osMutexDef(g_mutex);
  g_mutex = osMutexCreate(osMutex(g_mutex));

  /* Create semaphore for data-ready signaling */
  osSemaphoreDef(g_data_ready);
  g_data_ready = osSemaphoreCreate(osSemaphore(g_data_ready), 1);
  osSemaphoreWait(g_data_ready, 0); /* consume initial token */

  /* Create CAN receive queue */
  g_can_rx_queue = xQueueCreate(4, sizeof(CAN_Msg_t));

  /* Define and create tasks */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 256);
  osThreadCreate(osThread(defaultTask), NULL);

  osThreadDef(usart, StartTask01, osPriorityBelowNormal, 0, 256);
  osThreadCreate(osThread(usart), NULL);

  osThreadDef(adc_1, StartTask02, osPriorityBelowNormal, 0, 256);
  osThreadCreate(osThread(adc_1), NULL);

  osThreadDef(adc_2, StartTask03, osPriorityBelowNormal, 0, 256);
  osThreadCreate(osThread(adc_2), NULL);

  osThreadDef(adc_3, StartTask04, osPriorityBelowNormal, 0, 256);
  osThreadCreate(osThread(adc_3), NULL);

  osThreadDef(lcd, StartTask05, osPriorityBelowNormal, 0, 512);
  osThreadCreate(osThread(lcd), NULL);
}

/* USER CODE END Application */
