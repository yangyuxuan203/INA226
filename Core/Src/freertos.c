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
#include "can_app.h"
#include "lcd.h"
#include "touch.h"
#include "dac.h"
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

/* STM32F1 received data via CAN */
CAN_CtrlCmd_t g_f1_cmd = {0};
volatile uint8_t g_f1_cmd_updated = 0;

/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN Function Prototypes */

/* Relay control function */
void Relay_Control(float local_voltage, float can_voltage);

/* USER CODE END Function Prototypes */

/* Hook prototypes */
/* USER CODE BEGIN Application */

/* USER CODE END Application */

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* Relay control defines */
#define MOS1_PIN  GPIO_PIN_6  // PD6 - Solar/PV control
#define MOS2_PIN  GPIO_PIN_7  // PD7 - Battery charging control
#define MOS_PORT  GPIOD

#define VOLTAGE_THRESHOLD_LOW   6.0f   // Low voltage threshold (V)
#define VOLTAGE_THRESHOLD_HIGH  7.0f   // High voltage threshold (V)

/**
  * @brief  Relay control function
  * @param  local_voltage: INA226 measured voltage
  * @param  can_voltage: CAN received voltage from STM32F1
  */
void Relay_Control(float local_voltage, float can_voltage)
{
    /* MOS1 (PD6) control based on local voltage */
    if (local_voltage < 6.0f)
    {
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_6, GPIO_PIN_SET);
    }
    else if (local_voltage > 7.0f)
    {
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_6, GPIO_PIN_RESET);
    }

    /* MOS2 (PD7) control based on CAN voltage */
    if (can_voltage < 6.0f)
    {
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, GPIO_PIN_SET);
    }
    else if (can_voltage > 7.0f)
    {
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, GPIO_PIN_RESET);
    }
}

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
  for(;;)
  {
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
    {
      uint16_t raw = (uint16_t)HAL_ADC_GetValue(&hadc1);
      HAL_ADC_Stop(&hadc1);
      float voltage = (float)raw / 4095.0f * 3.3f;

      osMutexWait(g_mutex, osWaitForever);
      g_sensor.mq9_adc = raw;
      g_sensor.mq9_voltage = voltage;
      osMutexRelease(g_mutex);
    }
    else
    {
      HAL_ADC_Stop(&hadc1);
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
  printf("GY30 initialized (PC3=SCL, PC4=SDA)\r\n");

  /* Start DAC channel 1 (PA4) */
  HAL_DAC_Start(&hdac, DAC_CHANNEL_1);
  printf("DAC CH1 started (PA4)\r\n");

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

      /* Map lux 0-3000 to DAC 0-4095 (PA4) */
      uint32_t dac_val = (uint32_t)(light * 4095.0f / 3000.0f);
      if (dac_val > 4095) dac_val = 4095;
      HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, dac_val);
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
*        Receives battery data and commands from STM32F1.
*        Controls relay based on voltage levels.
* @param argument: Not used
* @retval None
*/
void StartTask04(void const * argument)
{
  CAN_CtrlCmd_t rx_cmd;
  uint32_t debug_tick = 0;
  SensorData_t local_data;

  CAN_App_Init();
  printf("CAN1 ready (PA11=RX, PA12=TX)\r\n");

  for(;;)
  {
    /* Read local sensor data */
    osMutexWait(g_mutex, osWaitForever);
    local_data = g_sensor;
    osMutexRelease(g_mutex);

    /* Check for received CAN command from STM32F1 */
    if (xCanRxQueue && xQueueReceive(xCanRxQueue, &rx_cmd, 0) == pdTRUE)
    {
      printf("CAN CMD: cmd=%d param=%d\r\n", rx_cmd.cmd, rx_cmd.param);

      /* Update global F1 command data */
      g_f1_cmd = rx_cmd;
      g_f1_cmd_updated = 1;
    }

    /* Relay control logic */
    if (local_data.ina226_ok)
    {
      float can_voltage = g_f1_battery.voltage;
      if (!g_f1_battery_updated)
      {
        can_voltage = 0.0f;
      }
      Relay_Control(local_data.bus_voltage, can_voltage);
    }

    /* Debug: print voltage values every 2 seconds */
    if ((HAL_GetTick() - debug_tick) >= 2000)
    {
      debug_tick = HAL_GetTick();
      printf("VOLTAGE: local=%.3fV CAN=%.3fV\r\n",
             local_data.bus_voltage, g_f1_battery.voltage);
    }

    osDelay(100);
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
  SensorData_t prev;
  uint8_t touch_ok;
  CAN_BatteryData_t prev_f1_battery;

  /* Initialize prev to invalid values so first update always draws */
  memset(&prev, 0xFF, sizeof(prev));
  memset(&prev_f1_battery, 0, sizeof(prev_f1_battery));

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
  lcd_show_string(10, 260, 200, 16, 16, "DAC Out:", BLUE);

  /* STM32F1 Battery Data section */
  lcd_show_string(10, 330, 200, 24, 24, "STM32F1 Battery", RED);
  lcd_show_string(10, 360, 200, 16, 16, "Voltage:", BLUE);
  lcd_show_string(10, 390, 200, 16, 16, "Current:", BLUE);
  lcd_show_string(10, 420, 200, 16, 16, "Temp:", BLUE);
  lcd_show_string(10, 450, 200, 16, 16, "Status:", BLUE);

  /* Relay Control section */
  lcd_show_string(10, 480, 200, 24, 24, "Relay Control", RED);
  lcd_show_string(10, 510, 200, 16, 16, "MOS1:", BLUE);
  lcd_show_string(10, 540, 200, 16, 16, "MOS2:", BLUE);

  /* Initialize touch screen */
  touch_ok = tp_init();
  if (touch_ok == 0) {
      lcd_show_string(10, 300, 300, 16, 16, "Touch: OK", GREEN);
      printf("Touch screen initialized\r\n");
  } else {
      lcd_show_string(10, 300, 300, 16, 16, "Touch: N/A", GRAY);
      printf("Touch screen not found\r\n");
  }

  for(;;)
  {
    /* Read shared sensor data */
    osMutexWait(g_mutex, osWaitForever);
    local = g_sensor;
    osMutexRelease(g_mutex);

    /* Update INA226 display values only when changed */
    if (local.ina226_ok != prev.ina226_ok ||
        local.bus_voltage != prev.bus_voltage ||
        local.current != prev.current ||
        local.power != prev.power)
    {
      if (local.ina226_ok)
      {
        int v_i = (int)local.bus_voltage;
        int v_f = (int)((local.bus_voltage - v_i) * 1000);
        if (v_f < 0) v_f = -v_f;
        lcd_fill(120, 40, 280, 56, WHITE);
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
    }

    /* Update MQ9 display values only when changed */
    if (local.mq9_adc != prev.mq9_adc || local.mq9_voltage != prev.mq9_voltage)
    {
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
    }

    /* Update GY30 light display only when changed */
    if (local.gy30_ok != prev.gy30_ok || local.lux != prev.lux)
    {
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

      /* Update DAC output display (depends on lux) */
      lcd_fill(120, 260, 280, 276, WHITE);
      if (local.gy30_ok)
      {
        uint32_t dac_val = (uint32_t)(local.lux * 4095.0f / 3000.0f);
        if (dac_val > 4095) dac_val = 4095;
        float dac_v = (float)dac_val / 4095.0f * 3.3f;
        int dv_i = (int)dac_v;
        int dv_f = (int)((dac_v - dv_i) * 100);
        if (dv_f < 0) dv_f = -dv_f;
        lcd_show_num(120, 260, dv_i, 1, 16, RED);
        lcd_show_char(128, 260, '.', 16, 0, RED);
        lcd_show_xnum(136, 260, dv_f, 2, 16, 0x80, RED);
        lcd_show_string(160, 260, 40, 16, 16, " V  ", RED);
      }
      else
      {
        lcd_show_string(120, 260, 80, 16, 16, "N/A", GRAY);
      }
    }

    /* Update STM32F1 battery data display - only when values change */
    if (g_f1_battery_updated &&
        (g_f1_battery.voltage != prev_f1_battery.voltage ||
         g_f1_battery.current != prev_f1_battery.current ||
         g_f1_battery.temperature != prev_f1_battery.temperature ||
         g_f1_battery.status != prev_f1_battery.status))
    {
      /* Voltage */
      lcd_fill(120, 360, 280, 376, WHITE);
      int bv_i = (int)g_f1_battery.voltage;
      int bv_f = (int)((g_f1_battery.voltage - bv_i) * 1000);
      if (bv_f < 0) bv_f = -bv_f;
      lcd_show_num(120, 360, bv_i, 1, 16, RED);
      lcd_show_char(128, 360, '.', 16, 0, RED);
      lcd_show_num(136, 360, bv_f, 3, 16, RED);
      lcd_show_string(170, 360, 40, 16, 16, " V  ", RED);

      /* Current */
      lcd_fill(120, 390, 280, 406, WHITE);
      int bc = (int)g_f1_battery.current;
      lcd_show_num(120, 390, bc, 5, 16, RED);
      lcd_show_string(168, 390, 50, 16, 16, " mA  ", RED);

      /* Temperature */
      lcd_fill(120, 420, 280, 436, WHITE);
      int bt_i = (int)g_f1_battery.temperature;
      int bt_f = (int)((g_f1_battery.temperature - bt_i) * 10);
      if (bt_f < 0) bt_f = -bt_f;
      lcd_show_num(120, 420, bt_i, 2, 16, RED);
      lcd_show_char(136, 420, '.', 16, 0, RED);
      lcd_show_num(144, 420, bt_f, 1, 16, RED);
      lcd_show_string(155, 420, 30, 16, 16, " C  ", RED);

      /* Status */
      lcd_fill(120, 450, 280, 466, WHITE);
      switch (g_f1_battery.status)
      {
        case BAT_STATUS_IDLE:      lcd_show_string(120, 450, 80, 16, 16, "IDLE    ", BLUE);   break;
        case BAT_STATUS_CHARGING:  lcd_show_string(120, 450, 80, 16, 16, "CHARGE  ", GREEN);  break;
        case BAT_STATUS_DISCHARGE: lcd_show_string(120, 450, 80, 16, 16, "DISCHRG ", YELLOW); break;
        case BAT_STATUS_FAULT:     lcd_show_string(120, 450, 80, 16, 16, "FAULT   ", RED);    break;
        case BAT_STATUS_TILTED:    lcd_show_string(120, 450, 80, 16, 16, "TILTED  ", RED);    break;
        default:                   lcd_show_string(120, 450, 80, 16, 16, "UNKNOWN ", GRAY);   break;
      }

      /* Save current values for next comparison */
      prev_f1_battery.voltage = g_f1_battery.voltage;
      prev_f1_battery.current = g_f1_battery.current;
      prev_f1_battery.temperature = g_f1_battery.temperature;
      prev_f1_battery.status = g_f1_battery.status;
      g_f1_battery_updated = 0;
    }

    /* Update MOS relay status display */
    static uint8_t prev_mos1_state = 0xFF;
    static uint8_t prev_mos2_state = 0xFF;
    uint8_t mos1 = HAL_GPIO_ReadPin(MOS_PORT, MOS1_PIN) == GPIO_PIN_SET ? 1 : 0;
    uint8_t mos2 = HAL_GPIO_ReadPin(MOS_PORT, MOS2_PIN) == GPIO_PIN_SET ? 1 : 0;

    if (mos1 != prev_mos1_state || mos2 != prev_mos2_state)
    {
      /* MOS1 (PD2) - Solar/PV control */
      lcd_fill(120, 510, 280, 526, WHITE);
      if (mos1)
        lcd_show_string(120, 510, 80, 16, 16, "ON ", GREEN);
      else
        lcd_show_string(120, 510, 80, 16, 16, "OFF", RED);

      /* MOS2 (PD3) - Battery charging */
      lcd_fill(120, 540, 280, 556, WHITE);
      if (mos2)
        lcd_show_string(120, 540, 80, 16, 16, "ON ", GREEN);
      else
        lcd_show_string(120, 540, 80, 16, 16, "OFF", RED);

      prev_mos1_state = mos1;
      prev_mos2_state = mos2;
    }

    /* Save current values for next comparison */
    prev = local;

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

  /* Define and create tasks */
  osThreadDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 256);
  osThreadCreate(osThread(defaultTask), NULL);

  osThreadDef(usart, StartTask01, osPriorityBelowNormal, 0, 256);
  osThreadCreate(osThread(usart), NULL);

  osThreadDef(adc_1, StartTask02, osPriorityBelowNormal, 0, 256);
  osThreadCreate(osThread(adc_1), NULL);

  osThreadDef(adc_2, StartTask03, osPriorityBelowNormal, 0, 256);
  osThreadCreate(osThread(adc_2), NULL);

  osThreadDef(adc_3, StartTask04, osPriorityBelowNormal, 0, 512);
  osThreadCreate(osThread(adc_3), NULL);

  osThreadDef(lcd, StartTask05, osPriorityBelowNormal, 0, 512);
  osThreadCreate(osThread(lcd), NULL);
}

/* USER CODE END Application */
