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
#include "ina226_pv.h"
#include "ina226_3.h"
#include "adc.h"
#include "gy30.h"
#include "can.h"
#include "can_app.h"
#include "lcd.h"
#include "touch.h"
#include "dac.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
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
    float   soc_pct;        /* Battery SOC 0-100.0% */
    float   pv_voltage;     /* PV bus voltage (V) */
    float   pv_current;     /* PV current (A) */
    float   pv_power;       /* PV power (W) */
    uint8_t pv_ok;          /* PV INA226 status */
    float   ina3_voltage;   /* INA226 #3 bus voltage (V) */
    float   ina3_current;   /* INA226 #3 current (A) */
    float   ina3_power;     /* INA226 #3 power (W) */
    uint8_t ina3_ok;        /* INA226 #3 status */
} SensorData_t;

/* SOC calculation state (file scope) */
static float soc_coulomb_mah = 2200.0f;  /* Start at 100%, corrected on first voltage reading */
static uint8_t soc_initialized = 0;       /* 0 = not yet calibrated from voltage */

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
osSemaphoreId g_lcd_update;  /* LCD refresh signal: released by each sensor task */
SensorData_t g_sensor = {0};

/* STM32F1 received data via CAN */
CAN_CtrlCmd_t g_f1_cmd = {0};
volatile uint8_t g_f1_cmd_updated = 0;

/* DAC control flag: 1 = charging active (DAC follows light), 0 = all full (DAC = 0) */
volatile uint8_t g_charging_active = 1;

/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN Function Prototypes */

/* Relay control function */
void Relay_Control(float pv_power, float home_soc, float car_soc, uint8_t sensor_ok);

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
#define VOLTAGE_THRESHOLD_HIGH  8.4f   // High voltage threshold (V)

/**
  * @brief  Relay control function
  *         MOS1: PV -> home battery (F4)
  *         MOS2: home battery -> car battery (F1)
  *         When both full, DAC output = 0
  * @param  pv_power: solar PV power (mW), from INA226
  * @param  home_soc: home battery SOC (0-100), from INA226
  * @param  car_soc: car battery SOC (0-100), from CAN
  * @param  sensor_ok: INA226 sensor status (0=no data, safe default=OFF)
  */
void Relay_Control(float pv_power, float home_soc, float car_soc, uint8_t sensor_ok)
{
    if (!sensor_ok)
    {
        /* No sensor data: default OFF */
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_6, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, GPIO_PIN_RESET);
        g_charging_active = 0;
        return;
    }

    /* MOS1: PV -> home battery, when home < 90% */
    if (home_soc < 90.0f)
    {
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_6, GPIO_PIN_SET);    /* MOS1 ON */
    }
    else
    {
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_6, GPIO_PIN_RESET);  /* MOS1 OFF */
    }

    /* MOS2: home battery -> car battery, when home > 80% and car < 90% */
    if (home_soc > 80.0f && car_soc < 90.0f)
    {
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, GPIO_PIN_SET);    /* MOS2 ON */
    }
    else
    {
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, GPIO_PIN_RESET);  /* MOS2 OFF */
    }

    /* Both full -> DAC = 0 */
    if (home_soc >= 95.0f && car_soc >= 95.0f)
    {
        g_charging_active = 0;
    }
    else
    {
        g_charging_active = 1;
    }
}

/**
  * @brief  Voltage-to-SOC lookup table for 2S LiPo (6.0V ~ 8.4V)
  *         Piecewise linear interpolation between known breakpoints
  */
static uint8_t SOC_VoltageToPercent(float voltage)
{
    /* Voltage(mV) -> SOC(%) breakpoints */
    static const uint16_t v_mv[] = { 6000, 6400, 6800, 7200, 7600, 8000, 8400 };
    static const uint8_t  soc[]  = {    0,   10,   20,   40,   65,   85,  100 };
    const int n = sizeof(v_mv) / sizeof(v_mv[0]);

    int mv = (int)(voltage * 1000.0f);

    if (mv <= v_mv[0])   return 0;
    if (mv >= v_mv[n-1]) return 100;

    for (int i = 0; i < n - 1; i++) {
        if (mv >= v_mv[i] && mv <= v_mv[i+1]) {
            /* Linear interpolation */
            float ratio = (float)(mv - v_mv[i]) / (float)(v_mv[i+1] - v_mv[i]);
            return (uint8_t)(soc[i] + ratio * (soc[i+1] - soc[i]));
        }
    }
    return 0;
}

/**
  * @brief  Default task: INA226 current/voltage sensor (software I2C on PB6/PB7)
  *         Also computes SOC via coulomb counting + voltage calibration
  */
void StartDefaultTask(void const * argument)
{
  INA226_Data ina_data;
  uint32_t dbg_tick = 0;

  #define INA1_AVG_N 8
  float ina1_v_buf[INA1_AVG_N] = {0};
  float ina1_i_buf[INA1_AVG_N] = {0};
  float ina1_p_buf[INA1_AVG_N] = {0};
  uint8_t ina1_avg_idx = 0;
  uint8_t ina1_avg_cnt = 0;

  /* Wait for power rail stable before I2C */
  osDelay(500);
  INA226_Init();
  printf("INA226 initialized (PB6=SCL, PB7=SDA)\r\n");

  uint8_t init_skip = 3;  /* skip first few readings for sensor stabilize */

  for(;;)
  {
    if (INA226_ReadData(&ina_data) == 0)
    {
      /* Skip first few readings to let sensor stabilize */
      if (init_skip > 0) {
          init_skip--;
          printf("INA226: skipping initial read V=%.3f\r\n", (double)ina_data.bus_voltage);
          osDelay(200);
          continue;
      }

      /* Moving average filter for INA226 #1 */
      ina1_v_buf[ina1_avg_idx] = ina_data.bus_voltage;
      ina1_i_buf[ina1_avg_idx] = ina_data.current;
      ina1_p_buf[ina1_avg_idx] = ina_data.power;
      ina1_avg_idx = (ina1_avg_idx + 1) % INA1_AVG_N;
      if (ina1_avg_cnt < INA1_AVG_N) ina1_avg_cnt++;

      float avg_v = 0, avg_i = 0, avg_p = 0;
      for (int j = 0; j < ina1_avg_cnt; j++) {
          avg_v += ina1_v_buf[j];
          avg_i += ina1_i_buf[j];
          avg_p += ina1_p_buf[j];
      }
      avg_v /= ina1_avg_cnt;
      avg_i /= ina1_avg_cnt;
      avg_p /= ina1_avg_cnt;

      float current_mA = avg_i * 1000.0f;

      /* --- SOC Calculation --- */
      /* 0. Over-discharge / battery disconnected: V < 6.0V → force 0%, reset init */
      if (avg_v < 6.0f) {
          soc_coulomb_mah = 0.0f;
          soc_initialized = 0;

          osMutexWait(g_mutex, osWaitForever);
          g_sensor.bus_voltage = avg_v;
          g_sensor.current = avg_i;
          g_sensor.power = avg_p;
          g_sensor.soc_pct = 0.0f;
          g_sensor.ina226_ok = 1;
          osMutexRelease(g_mutex);
          osSemaphoreRelease(g_data_ready);
          osSemaphoreRelease(g_lcd_update);

          if ((HAL_GetTick() - dbg_tick) >= 3000) {
              dbg_tick = HAL_GetTick();
              printf("SOC: V=%.3fV < 6.0V -> 0%%\r\n", (double)avg_v);
          }
          osDelay(200);
          continue;
      }

      /* 1. First reading: init coulomb counter directly from voltage */
      if (!soc_initialized) {
          uint8_t v_soc = SOC_VoltageToPercent(avg_v);
          soc_coulomb_mah = (float)v_soc / 100.0f * 2200.0f;
          soc_initialized = 1;
          printf("SOC init: V=%.3fV -> %d%% (%.1fmAh)\r\n",
                 (double)avg_v, v_soc, (double)soc_coulomb_mah);
      }

      /* 2. Coulomb counting: integrate current over 200ms interval */
      soc_coulomb_mah -= current_mA * (200.0f / 3600000.0f);

      /* 3. Voltage calibration */
      static float last_cal_voltage = 0.0f;
      float voltage_soc_mah = (float)SOC_VoltageToPercent(avg_v)
                              / 100.0f * 2200.0f;

      if (current_mA > -5.0f && current_mA < 5.0f) {
          soc_coulomb_mah = soc_coulomb_mah * 0.9f + voltage_soc_mah * 0.1f;
      }
      else if (last_cal_voltage > 0.0f &&
               fabsf(avg_v - last_cal_voltage) > 0.05f) {
          soc_coulomb_mah = voltage_soc_mah;
          printf("SOC recal: V %.3f->%.3fV, SOC=%d%%\r\n",
                 (double)last_cal_voltage, (double)avg_v,
                 SOC_VoltageToPercent(avg_v));
      }
      last_cal_voltage = avg_v;

      /* 4. Clamp to 0~2200mAh */
      if (soc_coulomb_mah < 0.0f)    soc_coulomb_mah = 0.0f;
      if (soc_coulomb_mah > 2200.0f)  soc_coulomb_mah = 2200.0f;

      /* 5. Convert to percentage */
      float soc = soc_coulomb_mah / 2200.0f * 100.0f;
      if (soc > 100.0f) soc = 100.0f;
      if (soc < 0.0f)   soc = 0.0f;
      /* --- End SOC Calculation --- */

      /* Debug: print SOC detail every 3 seconds */
      if ((HAL_GetTick() - dbg_tick) >= 3000) {
          dbg_tick = HAL_GetTick();
          printf("SOC: V=%.3fV I=%.3fmAh mah=%.1f soc=%.1f%%\r\n",
                 (double)avg_v, (double)current_mA,
                 (double)soc_coulomb_mah, (double)soc);
      }

      osMutexWait(g_mutex, osWaitForever);
      g_sensor.bus_voltage = avg_v;
      g_sensor.current = avg_i;
      g_sensor.power = avg_p;
      g_sensor.soc_pct = soc;
      g_sensor.ina226_ok = 1;
      osMutexRelease(g_mutex);
      osSemaphoreRelease(g_data_ready);
      osSemaphoreRelease(g_lcd_update);
    }
    else
    {
      printf("INA226 read FAILED!\r\n");
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
      printf("INA226: V=%.3fV I=%.3fmA P=%.3fmW SOC=%.1f%%\r\n",
             (double)local.bus_voltage,
             (double)(local.current * 1000.0f),
             (double)(local.power * 1000.0f),
             (double)local.soc_pct);
    }
    if (local.gy30_ok)
    {
      printf("GY30: %.1f lux\r\n", (double)local.lux);
    }

    osDelay(1000);
  }
}

/**
  * @brief  ADC task: MQ9 gas sensor on PA7 (ADC1_CH7)
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
  * @brief  GY30 light sensor task (software I2C on PC3/PC4)
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
      if (light > 2500.0f) light = 2500.0f;
      g_sensor.lux = light;
      g_sensor.gy30_ok = 1;
      osMutexRelease(g_mutex);
      osSemaphoreRelease(g_data_ready);
      osSemaphoreRelease(g_lcd_update);

      /* Map lux 0-3000 to DAC 0-4095 (PA4), or 0 if charging complete */
      if (g_charging_active)
      {
        uint32_t dac_val = (uint32_t)(light * 4095.0f / 2500.0f);
        if (dac_val > 4095) dac_val = 4095;
        HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, dac_val);
      }
      else
      {
        HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 0);
      }
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
  INA226_Data pv_data;
  INA226_Data ina3_data;

  #define PV_AVG_N 8

  /* INA3 moving average filter (8 samples) */
  float ina3_v_buf[PV_AVG_N] = {0};
  float ina3_i_buf[PV_AVG_N] = {0};
  float ina3_p_buf[PV_AVG_N] = {0};
  uint8_t ina3_avg_idx = 0;
  uint8_t ina3_avg_cnt = 0;
  uint8_t ina3_read_div = 0;

  /* PV moving average filter (8 samples) */
  float pv_v_buf[PV_AVG_N] = {0};
  float pv_i_buf[PV_AVG_N] = {0};
  float pv_p_buf[PV_AVG_N] = {0};
  uint8_t pv_avg_idx = 0;
  uint8_t pv_avg_cnt = 0;
  uint8_t pv_read_div = 0;  /* divider to slow PV read rate */

  INA226_PV_Init();
  INA226_3_Init();
  CAN_App_Init();
  printf("CAN1 ready (PA11=RX, PA12=TX)\r\n");

  for(;;)
  {
    /* Read PV INA226 (PB13/PB12) - every 5th loop (1s) with moving average */
    pv_read_div++;
    if (pv_read_div >= 5)
    {
      pv_read_div = 0;
      if (INA226_PV_ReadData(&pv_data) == 0)
      {
        /* Add to circular buffer */
        pv_v_buf[pv_avg_idx] = pv_data.bus_voltage;
        pv_i_buf[pv_avg_idx] = pv_data.current;
        pv_p_buf[pv_avg_idx] = pv_data.power;
        pv_avg_idx = (pv_avg_idx + 1) % PV_AVG_N;
        if (pv_avg_cnt < PV_AVG_N) pv_avg_cnt++;

        /* Calculate average */
        float sum_v = 0, sum_i = 0, sum_p = 0;
        for (int j = 0; j < pv_avg_cnt; j++) {
            sum_v += pv_v_buf[j];
            sum_i += pv_i_buf[j];
            sum_p += pv_p_buf[j];
        }
        osMutexWait(g_mutex, osWaitForever);
        g_sensor.pv_voltage = sum_v / pv_avg_cnt;
        g_sensor.pv_current = sum_i / pv_avg_cnt;
        g_sensor.pv_power   = sum_p / pv_avg_cnt;
        g_sensor.pv_ok = 1;
        osMutexRelease(g_mutex);
      }
      else
      {
        osMutexWait(g_mutex, osWaitForever);
        g_sensor.pv_ok = 0;
        osMutexRelease(g_mutex);
      }
    }

    /* Read INA226 #3 (PB3/PB2) - every 5th loop (1s) with moving average */
    ina3_read_div++;
    if (ina3_read_div >= 5)
    {
      ina3_read_div = 0;
      if (INA226_3_ReadData(&ina3_data) == 0)
      {
        ina3_v_buf[ina3_avg_idx] = ina3_data.bus_voltage;
        ina3_i_buf[ina3_avg_idx] = ina3_data.current;
        ina3_p_buf[ina3_avg_idx] = ina3_data.power;
        ina3_avg_idx = (ina3_avg_idx + 1) % PV_AVG_N;
        if (ina3_avg_cnt < PV_AVG_N) ina3_avg_cnt++;

        float sum_v = 0, sum_i = 0, sum_p = 0;
        for (int j = 0; j < ina3_avg_cnt; j++) {
            sum_v += ina3_v_buf[j];
            sum_i += ina3_i_buf[j];
            sum_p += ina3_p_buf[j];
        }
        osMutexWait(g_mutex, osWaitForever);
        g_sensor.ina3_voltage = sum_v / ina3_avg_cnt;
        g_sensor.ina3_current = sum_i / ina3_avg_cnt;
        g_sensor.ina3_power   = sum_p / ina3_avg_cnt;
        g_sensor.ina3_ok = 1;
        osMutexRelease(g_mutex);
      }
      else
      {
        osMutexWait(g_mutex, osWaitForever);
        g_sensor.ina3_ok = 0;
        osMutexRelease(g_mutex);
      }
    }

    /* Signal LCD to update */
    osSemaphoreRelease(g_lcd_update);

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

    /* Relay control logic (always call; sensor_ok=0 -> default OFF) */
    Relay_Control(local_data.pv_power * 1000.0f, local_data.soc_pct,
                  g_f1_battery.soc_pct, local_data.ina226_ok);

    /* Debug: print all sensor status every 2 seconds */
    if ((HAL_GetTick() - debug_tick) >= 2000)
    {
      debug_tick = HAL_GetTick();
      printf("INA1: ok=%d V=%.3f I=%.3f P=%.3f SOC=%.1f%%\r\n",
             local_data.ina226_ok, (double)local_data.bus_voltage,
             (double)local_data.current, (double)local_data.power,
             (double)local_data.soc_pct);
      printf("PV:   ok=%d V=%.3f I=%.3f P=%.3f\r\n",
             local_data.pv_ok, (double)local_data.pv_voltage,
             (double)local_data.pv_current, (double)local_data.pv_power);
      printf("INA3: ok=%d V=%.3f I=%.3f P=%.3f\r\n",
             local_data.ina3_ok, (double)local_data.ina3_voltage,
             (double)local_data.ina3_current, (double)local_data.ina3_power);
      printf("GY30: ok=%d lux=%.1f F1_SOC=%d%% chg=%d\r\n",
             local_data.gy30_ok, (double)local_data.lux,
             g_f1_battery.soc_pct, g_charging_active);
    }

    osDelay(200);
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

  /* Section 1: Home Battery INA226 */
  lcd_show_string(10, 10, 200, 16, 16, "Home Battery", RED);
  lcd_show_string(10, 28, 100, 16, 16, "V:", BLUE);
  lcd_show_string(10, 46, 100, 16, 16, "I:", BLUE);
  lcd_show_string(10, 64, 100, 16, 16, "P:", BLUE);
  lcd_show_string(10, 82, 100, 16, 16, "SOC:", BLUE);

  /* Section 2: Home Load (INA226 #3) */
  lcd_show_string(10, 100, 200, 16, 16, "Home Load", RED);
  lcd_show_string(10, 118, 100, 16, 16, "V:", BLUE);
  lcd_show_string(10, 136, 100, 16, 16, "I:", BLUE);
  lcd_show_string(10, 154, 100, 16, 16, "P:", BLUE);

  /* Section 3: Solar PV (INA226 #2) */
  lcd_show_string(10, 172, 200, 16, 16, "Solar PV", RED);
  lcd_show_string(10, 190, 100, 16, 16, "V:", BLUE);
  lcd_show_string(10, 208, 100, 16, 16, "I:", BLUE);
  lcd_show_string(10, 226, 100, 16, 16, "P:", BLUE);

  /* Section 4: MQ9 + GY30 */
  lcd_show_string(10, 250, 200, 16, 16, "MQ9:", BLUE);
  lcd_show_string(10, 268, 200, 16, 16, "Light:", BLUE);
  lcd_show_string(10, 286, 200, 16, 16, "DAC:", BLUE);

  /* Section 5: STM32F1 Battery */
  lcd_show_string(10, 310, 200, 16, 16, "Car Battery (F1)", RED);
  lcd_show_string(10, 328, 100, 16, 16, "V:", BLUE);
  lcd_show_string(10, 346, 100, 16, 16, "I:", BLUE);
  lcd_show_string(10, 364, 100, 16, 16, "T:", BLUE);
  lcd_show_string(10, 382, 100, 16, 16, "S:", BLUE);
  lcd_show_string(10, 400, 100, 16, 16, "SOC:", BLUE);

  /* Section 6: Relay */
  lcd_show_string(10, 424, 200, 16, 16, "MOS1(PV):", BLUE);
  lcd_show_string(10, 442, 200, 16, 16, "MOS2(Car):", BLUE);

  /* Initialize touch screen */
  touch_ok = tp_init();
  if (touch_ok == 0) {
      printf("Touch screen initialized\r\n");
  } else {
      printf("Touch screen not found\r\n");
  }

  for(;;)
  {
    /* Read shared sensor data */
    osMutexWait(g_mutex, osWaitForever);
    local = g_sensor;
    osMutexRelease(g_mutex);

    /* === Section 1: Home Battery (INA226 #1) === */
    if (local.ina226_ok != prev.ina226_ok ||
        local.bus_voltage != prev.bus_voltage ||
        local.current != prev.current ||
        local.power != prev.power ||
        local.soc_pct != prev.soc_pct)
    {
      if (local.ina226_ok)
      {
        lcd_fill(30, 28, 240, 100, WHITE);
        /* V */
        int hv_i = (int)local.bus_voltage;
        int hv_f = (int)((local.bus_voltage - hv_i) * 1000);
        if (hv_f < 0) hv_f = -hv_f;
        lcd_show_num(30, 28, hv_i, 1, 16, RED);
        lcd_show_char(38, 28, '.', 16, 0, RED);
        lcd_show_num(46, 28, hv_f, 3, 16, RED);
        lcd_show_string(80, 28, 20, 16, 16, "V", RED);
        /* I */
        int hma = (int)(local.current * 1000.0f);
        lcd_show_num(30, 46, hma, 5, 16, RED);
        lcd_show_string(78, 46, 30, 16, 16, "mA", RED);
        /* P */
        int hpw = (int)(local.power * 1000.0f);
        lcd_show_num(30, 64, hpw, 5, 16, RED);
        lcd_show_string(78, 64, 30, 16, 16, "mW", RED);
        /* SOC */
        int hs_i = (int)local.soc_pct;
        int hs_f = (int)((local.soc_pct - hs_i) * 10);
        if (hs_f < 0) hs_f = -hs_f;
        lcd_show_num(30, 82, hs_i, 3, 16, RED);
        lcd_show_char(54, 82, '.', 16, 0, RED);
        lcd_show_num(62, 82, hs_f, 1, 16, RED);
        lcd_show_string(72, 82, 16, 16, 16, "%", RED);
      }
      else
      {
        lcd_fill(30, 28, 240, 100, WHITE);
        lcd_show_string(30, 28, 40, 16, 16, "ERR", RED);
      }
    }

    /* === Section 2: Home Load (INA226 #3) === */
    if (local.ina3_ok != prev.ina3_ok ||
        local.ina3_voltage != prev.ina3_voltage ||
        local.ina3_current != prev.ina3_current ||
        local.ina3_power != prev.ina3_power)
    {
      lcd_fill(30, 118, 240, 172, WHITE);
      if (local.ina3_ok)
      {
        int n3_i = (int)local.ina3_voltage;
        int n3_f = (int)((local.ina3_voltage - n3_i) * 1000);
        if (n3_f < 0) n3_f = -n3_f;
        lcd_show_num(30, 118, n3_i, 1, 16, RED);
        lcd_show_char(38, 118, '.', 16, 0, RED);
        lcd_show_num(46, 118, n3_f, 3, 16, RED);
        lcd_show_string(80, 118, 20, 16, 16, "V", RED);

        int n3ma = (int)(local.ina3_current * 1000.0f);
        lcd_show_num(30, 136, n3ma, 5, 16, RED);
        lcd_show_string(78, 136, 30, 16, 16, "mA", RED);

        int n3pw = (int)(local.ina3_power * 1000.0f);
        lcd_show_num(30, 154, n3pw, 5, 16, RED);
        lcd_show_string(78, 154, 30, 16, 16, "mW", RED);
      }
      else
      {
        lcd_show_string(30, 118, 40, 16, 16, "N/A", GRAY);
      }
    }

    /* === Section 3: Solar PV (INA226 #2) === */
    if (local.pv_ok != prev.pv_ok ||
        local.pv_voltage != prev.pv_voltage ||
        local.pv_current != prev.pv_current ||
        local.pv_power != prev.pv_power)
    {
      lcd_fill(30, 190, 240, 244, WHITE);
      if (local.pv_ok)
      {
        int pv_i = (int)local.pv_voltage;
        int pv_f = (int)((local.pv_voltage - pv_i) * 1000);
        if (pv_f < 0) pv_f = -pv_f;
        lcd_show_num(30, 190, pv_i, 1, 16, RED);
        lcd_show_char(38, 190, '.', 16, 0, RED);
        lcd_show_num(46, 190, pv_f, 3, 16, RED);
        lcd_show_string(80, 190, 20, 16, 16, "V", RED);

        int pi_ma = (int)(local.pv_current * 1000.0f);
        lcd_show_num(30, 208, pi_ma, 5, 16, RED);
        lcd_show_string(78, 208, 30, 16, 16, "mA", RED);

        int pp_mw = (int)(local.pv_power * 1000.0f);
        lcd_show_num(30, 226, pp_mw, 5, 16, RED);
        lcd_show_string(78, 226, 30, 16, 16, "mW", RED);
      }
      else
      {
        lcd_show_string(30, 190, 40, 16, 16, "N/A", GRAY);
      }
    }

    /* === Section 4a: MQ9 === */
    if (local.mq9_adc != prev.mq9_adc || local.mq9_voltage != prev.mq9_voltage)
    {
      lcd_fill(50, 250, 160, 266, WHITE);
      lcd_show_num(50, 250, local.mq9_adc, 4, 16, RED);
      int mv_i = (int)local.mq9_voltage;
      int mv_f = (int)((local.mq9_voltage - mv_i) * 100);
      if (mv_f < 0) mv_f = -mv_f;
      lcd_show_num(100, 250, mv_i, 1, 16, RED);
      lcd_show_char(108, 250, '.', 16, 0, RED);
      lcd_show_num(116, 250, mv_f, 2, 16, RED);
      lcd_show_string(136, 250, 20, 16, 16, "V", RED);
    }

    /* === Section 4b: GY30 Light === */
    if (local.gy30_ok != prev.gy30_ok || local.lux != prev.lux)
    {
      lcd_fill(50, 268, 140, 284, WHITE);
      if (local.gy30_ok)
      {
        int lx = (int)local.lux;
        int lf = (int)((local.lux - lx) * 10);
        if (lf < 0) lf = -lf;
        lcd_show_num(50, 268, lx, 5, 16, RED);
        lcd_show_char(90, 268, '.', 16, 0, RED);
        lcd_show_num(98, 268, lf, 1, 16, RED);
        lcd_show_string(108, 268, 30, 16, 16, "lux", RED);
      }
      else
      {
        lcd_show_string(50, 268, 40, 16, 16, "N/A", GRAY);
      }
    }

    /* === Section 4c: DAC === */
    if (local.gy30_ok != prev.gy30_ok || local.lux != prev.lux)
    {
      lcd_fill(50, 286, 110, 302, WHITE);
      if (local.gy30_ok)
      {
        uint32_t dac_val = (uint32_t)(local.lux * 4095.0f / 3000.0f);
        if (dac_val > 4095) dac_val = 4095;
        float dac_v = (float)dac_val / 4095.0f * 3.3f;
        int dv_i = (int)dac_v;
        int dv_f = (int)((dac_v - dv_i) * 100);
        if (dv_f < 0) dv_f = -dv_f;
        lcd_show_num(50, 286, dv_i, 1, 16, RED);
        lcd_show_char(58, 286, '.', 16, 0, RED);
        lcd_show_xnum(66, 286, dv_f, 2, 16, 0x80, RED);
        lcd_show_string(86, 286, 20, 16, 16, "V", RED);
      }
      else
      {
        lcd_show_string(50, 286, 40, 16, 16, "N/A", GRAY);
      }
    }

    /* === Section 5: Car Battery (F1) === */
    if (g_f1_battery_updated &&
        (g_f1_battery.voltage != prev_f1_battery.voltage ||
         g_f1_battery.current != prev_f1_battery.current ||
         g_f1_battery.temperature != prev_f1_battery.temperature ||
         g_f1_battery.status != prev_f1_battery.status ||
         g_f1_battery.soc_pct != prev_f1_battery.soc_pct))
    {
      lcd_fill(30, 328, 240, 418, WHITE);
      /* V */
      int bv_i = (int)g_f1_battery.voltage;
      int bv_f = (int)((g_f1_battery.voltage - bv_i) * 1000);
      if (bv_f < 0) bv_f = -bv_f;
      lcd_show_num(30, 328, bv_i, 1, 16, RED);
      lcd_show_char(38, 328, '.', 16, 0, RED);
      lcd_show_num(46, 328, bv_f, 3, 16, RED);
      lcd_show_string(80, 328, 20, 16, 16, "V", RED);
      /* I */
      int bc = (int)g_f1_battery.current;
      lcd_show_num(30, 346, bc, 5, 16, RED);
      lcd_show_string(78, 346, 30, 16, 16, "mA", RED);
      /* T */
      int bt_i = (int)g_f1_battery.temperature;
      int bt_f = (int)((g_f1_battery.temperature - bt_i) * 10);
      if (bt_f < 0) bt_f = -bt_f;
      lcd_show_num(30, 364, bt_i, 2, 16, RED);
      lcd_show_char(46, 364, '.', 16, 0, RED);
      lcd_show_num(54, 364, bt_f, 1, 16, RED);
      lcd_show_string(64, 364, 16, 16, 16, "C", RED);
      /* Status */
      switch (g_f1_battery.status)
      {
        case BAT_STATUS_IDLE:      lcd_show_string(30, 382, 60, 16, 16, "IDLE  ", BLUE);   break;
        case BAT_STATUS_CHARGING:  lcd_show_string(30, 382, 60, 16, 16, "CHARGE", GREEN);  break;
        case BAT_STATUS_DISCHARGE: lcd_show_string(30, 382, 60, 16, 16, "DISCHG", YELLOW); break;
        case BAT_STATUS_FAULT:     lcd_show_string(30, 382, 60, 16, 16, "FAULT ", RED);    break;
        case BAT_STATUS_TILTED:    lcd_show_string(30, 382, 60, 16, 16, "TILTED", RED);    break;
        default:                   lcd_show_string(30, 382, 60, 16, 16, "UNKN  ", GRAY);   break;
      }
      /* SOC */
      lcd_show_num(30, 400, g_f1_battery.soc_pct, 3, 16, RED);
      lcd_show_string(58, 400, 12, 16, 16, "%", RED);

      prev_f1_battery.voltage = g_f1_battery.voltage;
      prev_f1_battery.current = g_f1_battery.current;
      prev_f1_battery.temperature = g_f1_battery.temperature;
      prev_f1_battery.status = g_f1_battery.status;
      prev_f1_battery.soc_pct = g_f1_battery.soc_pct;
      g_f1_battery_updated = 0;
    }

    /* === Section 6: Relay === */
    static uint8_t prev_mos1_state = 0xFF;
    static uint8_t prev_mos2_state = 0xFF;
    uint8_t mos1 = HAL_GPIO_ReadPin(MOS_PORT, MOS1_PIN) == GPIO_PIN_SET ? 1 : 0;
    uint8_t mos2 = HAL_GPIO_ReadPin(MOS_PORT, MOS2_PIN) == GPIO_PIN_SET ? 1 : 0;

    if (mos1 != prev_mos1_state || mos2 != prev_mos2_state)
    {
      lcd_fill(100, 424, 200, 458, WHITE);
      lcd_show_string(100, 424, 40, 16, 16, mos1 ? "ON " : "OFF", mos1 ? GREEN : RED);
      lcd_show_string(100, 442, 40, 16, 16, mos2 ? "ON " : "OFF", mos2 ? GREEN : RED);

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

    /* Wait for sensor data update, or timeout after 500ms (for touch) */
    osSemaphoreWait(g_lcd_update, 500);
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

  /* Create semaphore for LCD update signaling */
  osSemaphoreDef(g_lcd_update);
  g_lcd_update = osSemaphoreCreate(osSemaphore(g_lcd_update), 1);
  osSemaphoreWait(g_lcd_update, 0); /* consume initial token */

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

  osThreadDef(lcd, StartTask05, osPriorityBelowNormal, 0, 768);
  osThreadCreate(osThread(lcd), NULL);
}

/* USER CODE END Application */
