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
#include "esp8266_udp.h"
#include "esp8266_onenet.h"
#include "esp8266_onenet_at.h"
#include "esp8266_at.h"
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
volatile uint8_t g_v2h_active = 0;

/* ESP32-S3 data received through ESP8266 UDP */
ESP32S3_Data_t g_esp32s3_data = {0};
volatile uint8_t g_esp32s3_updated = 0;
EnergyLstmPrediction_t g_energy_lstm_pred = {0};
volatile uint8_t g_energy_lstm_pred_updated = 0;

/* OneNET cloud control and Beijing time */
OneNET_Control_t g_onenet_ctrl = {-1, -1, -1, -1, 0};
char g_beijing_time[32] = "--:--:--";
volatile uint8_t g_onenet_online = 0;

/* USER CODE END Variables */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN Function Prototypes */

/* Home energy dispatch function */
void Relay_Control(float pv_power_w, float home_load_power_w,
                   float pv_voltage_v, float home_battery_voltage_v,
                   float car_battery_voltage_v,
                   float home_soc, float car_soc,
                   float human_soc, float lux,
                   uint8_t sensor_ok, uint8_t pv_ok);
void StartTask06(void const * argument);
void StartTask07(void const * argument);

/* USER CODE END Function Prototypes */

/* Hook prototypes */
/* USER CODE BEGIN Application */

/* USER CODE END Application */

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* Home dispatch MOS outputs. */
#define MOS_PV_SRC_PORT       GPIOE
#define MOS_PV_SRC_PIN        GPIO_PIN_0  /* PE0: PV 5V source -> home load bus */
#define MOS_RIGID_LOAD_PORT   GPIOE
#define MOS_RIGID_LOAD_PIN    GPIO_PIN_1  /* PE1: home rigid load branch enable */
#define MOS_HOME_SRC_PORT     GPIOB
#define MOS_HOME_SRC_PIN      GPIO_PIN_14 /* PB14: home battery 5V source -> home load bus */
#define MOS_LED_PORT          GPIOC
#define MOS_LED_PIN           GPIO_PIN_10 /* PC10: controllable LED load */
#define MOS_FAN_PORT          GPIOD
#define MOS_FAN_PIN           GPIO_PIN_2  /* PD2: fan load */
#define MOS_QI_PORT           GPIOD
#define MOS_QI_PIN            GPIO_PIN_13 /* PD13: Qi transmitter */

/* Local keys, active low.
 * Actual wiring:
 * PE2 / KEY2 -> LED
 * PE3 / KEY1 -> fan
 * PE4 / KEY0 -> Qi
 */
#define KEY_LED_PORT          GPIOE
#define KEY_LED_PIN           GPIO_PIN_2  /* PE2: KEY2 toggles LED request */
#define KEY_FAN_PORT          GPIOE
#define KEY_FAN_PIN           GPIO_PIN_3  /* PE3: KEY1 toggles fan request */
#define KEY_QI_PORT           GPIOE
#define KEY_QI_PIN            GPIO_PIN_4  /* PE4: KEY0 toggles Qi request */

/* Charging MOS outputs kept from the previous design. */
#define MOS_HOME_CHARGE_PIN   GPIO_PIN_6  /* PD6: PV -> home battery charge */
#define MOS_CAR_CHARGE_PIN    GPIO_PIN_7  /* PD7: PV -> car battery charge */
#define MOS_CHARGE_PORT       GPIOD
#define MOS_CHARGE_ALL_PINS   (MOS_HOME_CHARGE_PIN | MOS_CAR_CHARGE_PIN)

#define VOLTAGE_THRESHOLD_LOW   6.5f   // Low voltage cutoff threshold (V)
#define VOLTAGE_THRESHOLD_HIGH  8.4f   // High voltage threshold (V)

#define PV_VALID_POWER_W        0.25f
#define PV_OFF_POWER_W          0.10f
#define PV_LOAD_MARGIN_W        0.20f
#define PV_DIRECT_MIN_POWER_W   2.00f
#define PV_START_VOLTAGE_V      6.20f
#define PV_STOP_VOLTAGE_V       6.00f
#define PV_PROBE_MIN_VOLTAGE_V  5.20f
#define PV_BAD_SAMPLE_LIMIT     3U
#define PV_LOADED_MIN_VOLTAGE_V 5.50f
#define PV_WAKE_LUX_THRESHOLD   120.0f
#define PV_PROBE_HOLD_MS        8000U
#define PV_RAMP_EVAL_MS         5000U
#define PV_COOLDOWN_MS          10000U
#define PV_CHARGE_MIN_POWER_W   0.60f
#define PV_CHARGE_MARGIN_W      0.30f
#define PV_CHARGE_VOLT_MARGIN_V 0.50f
#define PV_CHARGE_HOLD_MARGIN_V 0.10f
#define HOME_SOC_RESERVE_PCT    20.0f
#define HOME_SOC_SAVE_PCT       30.0f
#define HOME_SOC_COMFORT_PCT    50.0f
#define HOME_SOC_CHARGE_FULL    90.0f
#define CAR_SOC_CHARGE_FULL     90.0f
#define HOME_SOC_V2H_START_PCT  20.0f
#define HOME_SOC_V2H_STOP_PCT   35.0f
#define CAR_SOC_V2H_START_PCT   30.0f
#define CAR_SOC_V2H_STOP_PCT    20.0f
#define HUMAN_SOC_QI_REQ_PCT    30.0f
#define NIGHT_LUX_THRESHOLD     80.0f
#define DAY_LUX_THRESHOLD       300.0f
#define HUMAN_SOC_QI_STOP_PCT   80.0f
#define KEY_DEBOUNCE_MS         50U

#define CAN_CMD_V2H_ON          0x01U
#define CAN_CMD_V2H_OFF         0x02U
#define CAN_CMD_CAR_CHARGE_ON   0x03U
#define CAN_CMD_CAR_CHARGE_OFF  0x04U
#define CAN_V2H_POWER_LIMIT     80U
#define CAN_CAR_CHARGE_REPORT_MS 1000U

#define SERIAL_DATA_LOG         1U
#define SERIAL_VERBOSE_LOG      0U
#define COMM_VERBOSE_LOG        0U
#define DATA_LOG_PERIOD_MS      10000U
#define DATA_SCENE_ID           0U
#define ONENET_FULL_UPLOAD_MS    5000U
#define ONENET_SWITCH_SYNC_MS    500U
#define ONENET_CTRL_APPLY_DELAY_MS 1000U
#define ONENET_SWITCH_HEARTBEAT_MS 5000U
#define ONENET_PING_INTERVAL_MS  60000U  /* MQTT keepalive ping every 60s */
#define ONENET_STARTUP_DELAY_MS  12000U  /* wait ESP8266 power/WiFi module startup */
#define PV_PROBE_CHARGE_MS      10000U  /* probe charge duration: 10 seconds */
#define PV_PROBE_SETTLE_MS      5000U   /* wait current/power to rise after charge MOS on */
#define PV_ACTIVE_STABILIZE_MS  2000U   /* wait 2s after ACTIVE before probe */
#define ENERGY_LSTM_UDP_PERIOD_MS 10000U  /* ESP32 averages 6 frames into one 60s LSTM sample */
#define ENERGY_LSTM_DISPATCH_ENABLE 1U
#define ENERGY_LSTM_SOC_MAX_DROP_PCT 3.0f
#define ENERGY_LSTM_SOC_MAX_RISE_PCT 3.0f
#define ENERGY_LSTM_PRED_VALID_MS 180000U
#define ENERGY_LSTM_SOC_FALL_WARN_PCT 2.0f
#define ENERGY_LSTM_HOME_CHARGE_MIN_SURPLUS_W 0.30f
#define ENERGY_LSTM_CAR_CHARGE_MIN_SURPLUS_W 1.00f
#define ENERGY_LSTM_CAR_MIN_FUTURE_HOME_SOC 88.0f
#define ENERGY_PI_F             3.14159265358979323846f

typedef struct
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint32_t tick_ms;
    uint8_t valid;
} BeijingClock_t;

static BeijingClock_t g_bj_clock = {0};

static uint8_t Beijing_GetLedPeriod(uint8_t hour)
{
    if (hour < 6U)
    {
        return 0U;  /* 00:00-06:00 */
    }
    if (hour < 18U)
    {
        return 1U;  /* 06:00-18:00 */
    }
    if (hour < 22U)
    {
        return 2U;  /* 18:00-22:00 */
    }
    return 3U;      /* 22:00-24:00 */
}

static void Mos_Write(GPIO_TypeDef *port, uint16_t pin, uint8_t on)
{
    HAL_GPIO_WritePin(port, pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static uint8_t Key_IsPressed(GPIO_TypeDef *port, uint16_t pin)
{
    return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET;
}

static void Energy_GetRealHourSinCos(float *hour_sin, float *hour_cos)
{
    float real_hour;

    if (hour_sin == NULL || hour_cos == NULL)
    {
        return;
    }

    if (g_bj_clock.valid)
    {
        uint32_t elapsed_s = (HAL_GetTick() - g_bj_clock.tick_ms) / 1000U;
        uint32_t seconds_of_day =
            ((uint32_t)g_bj_clock.hour * 3600U) +
            ((uint32_t)g_bj_clock.minute * 60U) +
            (uint32_t)g_bj_clock.second + elapsed_s;
        seconds_of_day %= 86400U;
        real_hour = (float)seconds_of_day / 3600.0f;
    }
    else
    {
        real_hour = (float)((HAL_GetTick() / 1000U) % 86400U) / 3600.0f;
    }

    *hour_sin = sinf(2.0f * ENERGY_PI_F * real_hour / 24.0f);
    *hour_cos = cosf(2.0f * ENERGY_PI_F * real_hour / 24.0f);
}

static void Energy_FillLstmInput(EnergyLstmInput_t *input,
                                 const SensorData_t *local,
                                 const ESP32S3_Data_t *s3)
{
    if (input == NULL || local == NULL || s3 == NULL)
    {
        return;
    }

    memset(input, 0, sizeof(*input));
    Energy_GetRealHourSinCos(&input->real_hour_sin, &input->real_hour_cos);
    input->lux = local->lux;
    input->pv_v = local->pv_ok ? local->pv_voltage : 0.0f;
    input->pv_p = local->pv_ok ? local->pv_power : 0.0f;
    input->home_v = local->ina226_ok ? local->bus_voltage : 0.0f;
    input->home_soc = local->ina226_ok ? local->soc_pct : 0.0f;
    input->load_p = local->ina3_ok ? local->ina3_power : 0.0f;
    input->car_soc = (float)g_f1_battery.soc_pct;
    input->human_soc = s3->valid ? s3->bat_pct : -1.0f;
    input->pvsrc = HAL_GPIO_ReadPin(MOS_PV_SRC_PORT, MOS_PV_SRC_PIN) == GPIO_PIN_SET ? 1U : 0U;
    input->hsrc = HAL_GPIO_ReadPin(MOS_HOME_SRC_PORT, MOS_HOME_SRC_PIN) == GPIO_PIN_SET ? 1U : 0U;
    input->rigid = HAL_GPIO_ReadPin(MOS_RIGID_LOAD_PORT, MOS_RIGID_LOAD_PIN) == GPIO_PIN_SET ? 1U : 0U;
    input->led = HAL_GPIO_ReadPin(MOS_LED_PORT, MOS_LED_PIN) == GPIO_PIN_SET ? 1U : 0U;
    input->fan = HAL_GPIO_ReadPin(MOS_FAN_PORT, MOS_FAN_PIN) == GPIO_PIN_SET ? 1U : 0U;
    input->qi = HAL_GPIO_ReadPin(MOS_QI_PORT, MOS_QI_PIN) == GPIO_PIN_SET ? 1U : 0U;
    input->hchg = HAL_GPIO_ReadPin(MOS_CHARGE_PORT, MOS_HOME_CHARGE_PIN) == GPIO_PIN_SET ? 1U : 0U;
    input->cchg = HAL_GPIO_ReadPin(MOS_CHARGE_PORT, MOS_CAR_CHARGE_PIN) == GPIO_PIN_SET ? 1U : 0U;
    input->v2h = g_v2h_active ? 1U : 0U;
}

static float Energy_ClampFloat(float value, float min_value, float max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static void Energy_ClampLstmPrediction(EnergyLstmPrediction_t *pred,
                                       const SensorData_t *local)
{
    float soc_now;
    float soc_min;
    float soc_max;

    if (pred == NULL || local == NULL || !pred->valid || !local->ina226_ok)
    {
        return;
    }

    soc_now = local->soc_pct;
    soc_min = Energy_ClampFloat(soc_now - ENERGY_LSTM_SOC_MAX_DROP_PCT, 0.0f, 100.0f);
    soc_max = Energy_ClampFloat(soc_now + ENERGY_LSTM_SOC_MAX_RISE_PCT, 0.0f, 100.0f);
    pred->future_home_soc = Energy_ClampFloat(pred->future_home_soc, soc_min, soc_max);
}

typedef enum
{
    KEY_FSM_IDLE = 0,
    KEY_FSM_DEBOUNCE_PRESS,
    KEY_FSM_PRESSED,
    KEY_FSM_DEBOUNCE_RELEASE
} KeyFsmState_t;

typedef enum
{
    PV_FSM_IDLE = 0,
    PV_FSM_PROBE,
    PV_FSM_RAMP,
    PV_FSM_ACTIVE,
    PV_FSM_COOLDOWN
} PvFsmState_t;

typedef struct
{
    KeyFsmState_t state;
    uint32_t tick;
} KeyDebounce_t;

static uint8_t Key_GetPressedEvent(KeyDebounce_t *key, uint8_t raw_pressed, uint32_t now_tick)
{
    uint8_t event = 0;

    switch (key->state)
    {
        case KEY_FSM_IDLE:
            if (raw_pressed)
            {
                key->state = KEY_FSM_DEBOUNCE_PRESS;
                key->tick = now_tick;
            }
            break;

        case KEY_FSM_DEBOUNCE_PRESS:
            if (!raw_pressed)
            {
                key->state = KEY_FSM_IDLE;
            }
            else if ((now_tick - key->tick) >= KEY_DEBOUNCE_MS)
            {
                key->state = KEY_FSM_PRESSED;
                event = 1;
            }
            break;

        case KEY_FSM_PRESSED:
            if (!raw_pressed)
            {
                key->state = KEY_FSM_DEBOUNCE_RELEASE;
                key->tick = now_tick;
            }
            break;

        case KEY_FSM_DEBOUNCE_RELEASE:
            if (raw_pressed)
            {
                key->state = KEY_FSM_PRESSED;
            }
            else if ((now_tick - key->tick) >= KEY_DEBOUNCE_MS)
            {
                key->state = KEY_FSM_IDLE;
            }
            break;

        default:
            key->state = KEY_FSM_IDLE;
            break;
    }

    return event;
}

static void SendCarV2HCommand(uint8_t enable)
{
    CAN_CtrlCmd_t cmd = {0};

    cmd.cmd = enable ? CAN_CMD_V2H_ON : CAN_CMD_V2H_OFF;
    cmd.param = enable ? CAN_V2H_POWER_LIMIT : 0U;

    if (CAN_App_Send(CAN_ID_CTRL_CMD, (uint8_t *)&cmd, sizeof(cmd)) != 0)
    {
        if (SERIAL_VERBOSE_LOG) printf("CAN V2H %s send failed\r\n", enable ? "ON" : "OFF");
    }
}

static void SendCarChargeCommand(uint8_t enable, float charge_power_w)
{
    CAN_CtrlCmd_t cmd = {0};
    int power_0p1w;

    cmd.cmd = enable ? CAN_CMD_CAR_CHARGE_ON : CAN_CMD_CAR_CHARGE_OFF;
    if (enable)
    {
        if (charge_power_w < 0.0f)
        {
            charge_power_w = 0.0f;
        }
        power_0p1w = (int)(charge_power_w * 10.0f + 0.5f);
        if (power_0p1w > 255)
        {
            power_0p1w = 255;
        }
        cmd.param = (uint8_t)power_0p1w;
    }

    if (CAN_App_Send(CAN_ID_CTRL_CMD, (uint8_t *)&cmd, sizeof(cmd)) != 0)
    {
        if (SERIAL_VERBOSE_LOG) printf("CAN car charge %s send failed\r\n", enable ? "ON" : "OFF");
    }
}

/**
  * @brief  Bottom-layer home energy dispatch.
  *         PE0: PV 5V source connects to the home load bus when stable enough.
  *         PE1: rigid home load branch enable, normally on when any source is available.
  *         PB14: home battery 5V source connects to the home load bus as fallback.
  *         PD11/PD2/PD13 are low-priority loads controlled by energy margin.
  *         PE2/PE3/PE4 are local key inputs for LED/fan/Qi requests.
  *         PD6/PD7 charge home/car batteries from stable PV surplus.
  * @param  pv_power_w: measured PV output power (W), low when PV is open-circuit
  * @param  home_load_power_w: measured total home load power (W)
  * @param  pv_voltage_v: measured PV voltage (V), used to start a probe
  * @param  home_battery_voltage_v: measured home battery voltage (V)
  * @param  car_battery_voltage_v: measured car battery voltage from CAN (V)
  * @param  home_soc: home battery SOC (0-100)
  * @param  car_soc: car battery SOC (0-100), reserved for V2H policy
  * @param  human_soc: wearable/human node SOC (0-100), NAN-like invalid is not used
  * @param  lux: ambient light from GY30/BH1750
  * @param  sensor_ok: home battery sensor status
  * @param  pv_ok: PV sensor status
  */
void Relay_Control(float pv_power_w, float home_load_power_w,
                   float pv_voltage_v, float home_battery_voltage_v,
                   float car_battery_voltage_v,
                   float home_soc, float car_soc,
                   float human_soc, float lux,
                   uint8_t sensor_ok, uint8_t pv_ok)
{
    static PvFsmState_t pv_state = PV_FSM_IDLE;
    static uint32_t pv_state_tick = 0;
    static uint8_t pv_bad_count = 0;
    static uint8_t led_manual_override = 0;
    static uint8_t led_manual_on = 0;
    static uint16_t led_manual_year = 0;
    static uint8_t led_manual_month = 0;
    static uint8_t led_manual_day = 0;
    static uint8_t led_manual_period = 0xFFU;
    static uint8_t fan_user_req = 0;
    static uint32_t fan_local_override_tick = 0;
    static uint8_t qi_user_req = 0;
    static uint8_t qi_active = 0;
    static uint8_t rigid_user_req = 1;
    static uint8_t light_user_req = 1;
    static uint8_t prev_led_out = 0xFFU;
    static uint8_t prev_fan_out = 0xFFU;
    static uint8_t pv_charge_probed = 0;  /* 0=idle, 1=probing, 2=done */
    static uint8_t pv_probe_target = 0;   /* 0=none, 1=home battery, 2=car battery */
    static float pv_probe_threshold = 0.0f;
    static uint32_t pv_probe_start_tick = 0;
    static uint8_t pv_charging_active = 0;  /* hysteresis: 1=charging, 0=not charging */
    static uint8_t car_charge_reported = 0;
    static uint32_t car_charge_report_tick = 0;
    static KeyDebounce_t led_key_fsm = {KEY_FSM_IDLE, 0};
    static KeyDebounce_t fan_key_fsm = {KEY_FSM_IDLE, 0};
    static KeyDebounce_t qi_key_fsm = {KEY_FSM_IDLE, 0};
    uint32_t now_tick = HAL_GetTick();

    if (!sensor_ok)
    {
        if (g_v2h_active)
        {
            SendCarV2HCommand(0);
            g_v2h_active = 0;
        }
        Mos_Write(MOS_PV_SRC_PORT, MOS_PV_SRC_PIN, 0);
        Mos_Write(MOS_HOME_SRC_PORT, MOS_HOME_SRC_PIN, 0);
        Mos_Write(MOS_RIGID_LOAD_PORT, MOS_RIGID_LOAD_PIN, 0);
        Mos_Write(MOS_LED_PORT, MOS_LED_PIN, 0);
        Mos_Write(MOS_FAN_PORT, MOS_FAN_PIN, 0);
        Mos_Write(MOS_QI_PORT, MOS_QI_PIN, 0);
        HAL_GPIO_WritePin(MOS_CHARGE_PORT, MOS_CHARGE_ALL_PINS, GPIO_PIN_RESET);
        pv_state = PV_FSM_IDLE;
        pv_bad_count = 0;
        pv_charge_probed = 0;
        pv_probe_target = 0;
        pv_probe_threshold = 0.0f;
        pv_probe_start_tick = 0;
        pv_charging_active = 0;
        if (car_charge_reported)
        {
            SendCarChargeCommand(0, 0.0f);
            car_charge_reported = 0;
        }
        g_charging_active = 0;
        return;
    }

    uint8_t pv_start_condition =
        pv_ok &&
        (pv_voltage_v > PV_PROBE_MIN_VOLTAGE_V) &&
        ((pv_voltage_v > PV_START_VOLTAGE_V) ||
         (lux > PV_WAKE_LUX_THRESHOLD));

    switch (pv_state)
    {
        case PV_FSM_IDLE:
            pv_bad_count = 0;
            if (pv_charge_probed == 1U)
            {
                pv_charge_probed = 0;
            }
            pv_probe_start_tick = 0;
            if (pv_start_condition)
            {
                pv_state = PV_FSM_PROBE;
                pv_state_tick = now_tick;
                // if (SERIAL_VERBOSE_LOG)
                // {
                //     printf("PV FSM: IDLE->PROBE V=%.3f P=%.3f lux=%.1f\r\n",
                //            (double)pv_voltage_v, (double)pv_power_w, (double)lux);
                // }
            }
            break;

        case PV_FSM_PROBE:
            if (!pv_ok || pv_voltage_v < PV_STOP_VOLTAGE_V)
            {
                pv_state = PV_FSM_COOLDOWN;
                pv_state_tick = now_tick;
                pv_bad_count = 0;
            }
            else if ((now_tick - pv_state_tick) >= PV_PROBE_HOLD_MS)
            {
                pv_state = PV_FSM_RAMP;
                pv_state_tick = now_tick;
                pv_bad_count = 0;
            }
            break;

        case PV_FSM_RAMP:
            if (!pv_ok || pv_voltage_v < PV_STOP_VOLTAGE_V)
            {
                if (pv_bad_count < PV_BAD_SAMPLE_LIMIT) pv_bad_count++;
                if (pv_bad_count >= PV_BAD_SAMPLE_LIMIT)
                {
                    pv_state = PV_FSM_COOLDOWN;
                    pv_state_tick = now_tick;
                    pv_bad_count = 0;
                }
            }
            else
            {
                pv_bad_count = 0;
                if ((now_tick - pv_state_tick) >= PV_RAMP_EVAL_MS)
                {
                    if (pv_voltage_v > PV_LOADED_MIN_VOLTAGE_V &&
                        (pv_power_w > PV_VALID_POWER_W ||
                         pv_voltage_v > PV_START_VOLTAGE_V))
                    {
                        pv_state = PV_FSM_ACTIVE;
                        pv_state_tick = now_tick;
                    }
                    else
                    {
                        pv_state = PV_FSM_COOLDOWN;
                        pv_state_tick = now_tick;
                    }
                }
            }
            break;

        case PV_FSM_ACTIVE:
            if (!pv_ok || pv_voltage_v < PV_STOP_VOLTAGE_V)
            {
                if (pv_bad_count < PV_BAD_SAMPLE_LIMIT) pv_bad_count++;
                if (pv_bad_count >= PV_BAD_SAMPLE_LIMIT)
                {
                    pv_state = PV_FSM_COOLDOWN;
                    pv_state_tick = now_tick;
                    pv_bad_count = 0;
                }
            }
            else
            {
                pv_bad_count = 0;
            }
            break;

        case PV_FSM_COOLDOWN:
            if ((now_tick - pv_state_tick) >= PV_COOLDOWN_MS)
            {
                pv_state = pv_start_condition ? PV_FSM_PROBE : PV_FSM_IDLE;
                pv_state_tick = now_tick;
                pv_bad_count = 0;
                if (pv_charge_probed == 1U)
                {
                    pv_charge_probed = 0;
                }
                pv_probe_start_tick = 0;
            }
            break;

        default:
            pv_state = PV_FSM_IDLE;
            pv_bad_count = 0;
            break;
    }

    uint8_t pv_source_enabled =
        (pv_state == PV_FSM_PROBE ||
         pv_state == PV_FSM_RAMP ||
         pv_state == PV_FSM_ACTIVE) ? 1U : 0U;
    uint8_t pv_available = (pv_state == PV_FSM_ACTIVE) ? 1U : 0U;
    uint8_t enough_for_pv_load = pv_ok && pv_source_enabled;
    uint8_t home_bat_allowed = home_soc > HOME_SOC_RESERVE_PCT;
    uint8_t low_energy_mode = (!pv_available && home_soc < HOME_SOC_SAVE_PCT);
    uint8_t time_valid = g_bj_clock.valid;
    uint16_t bj_year = g_bj_clock.year;
    uint8_t bj_month = g_bj_clock.month;
    uint8_t bj_day = g_bj_clock.day;
    uint8_t bj_hour = g_bj_clock.hour;
    uint8_t bj_led_period = Beijing_GetLedPeriod(bj_hour);
    uint8_t led_auto_request;
    float pv_surplus_w = pv_power_w - home_load_power_w;
    uint8_t charge_target = 0; /* 0=none, 1=home, 2=car */
    float charge_target_voltage_v = 0.0f;
    EnergyLstmPrediction_t ai_pred;
    float ai_future_surplus_w = 0.0f;
    float ai_future_soc_delta = 0.0f;
    uint8_t ai_home_soc_falling = 0U;
    uint8_t ai_allow_new_charge_probe = 1U;
    uint8_t ai_allow_home_charge = 1U;
    uint8_t ai_allow_car_charge = 1U;

    osMutexWait(g_mutex, osWaitForever);
    ai_pred = g_energy_lstm_pred;
    osMutexRelease(g_mutex);
    if (ENERGY_LSTM_DISPATCH_ENABLE &&
        ai_pred.valid && (now_tick - ai_pred.tick_ms) <= ENERGY_LSTM_PRED_VALID_MS)
    {
        ai_future_surplus_w = ai_pred.future_pv_p - ai_pred.future_load_p;
        ai_future_soc_delta = ai_pred.future_home_soc - home_soc;
        ai_home_soc_falling = ai_future_soc_delta < -ENERGY_LSTM_SOC_FALL_WARN_PCT ? 1U : 0U;

        if (ai_future_surplus_w < ENERGY_LSTM_HOME_CHARGE_MIN_SURPLUS_W)
        {
            ai_allow_new_charge_probe = 0U;
            ai_allow_home_charge = pv_charging_active ? 1U : 0U;
        }

        if (ai_home_soc_falling ||
            ai_future_surplus_w < ENERGY_LSTM_CAR_CHARGE_MIN_SURPLUS_W ||
            ai_pred.future_home_soc < ENERGY_LSTM_CAR_MIN_FUTURE_HOME_SOC)
        {
            ai_allow_car_charge = 0U;
        }

        if (ai_home_soc_falling && !pv_available)
        {
            low_energy_mode = 1U;
        }
    }

    if (home_soc < HOME_SOC_CHARGE_FULL)
    {
        charge_target = 1U;
        charge_target_voltage_v = home_battery_voltage_v;
    }
    else if (car_soc < CAR_SOC_CHARGE_FULL && car_battery_voltage_v > 1.0f)
    {
        charge_target = 2U;
        charge_target_voltage_v = car_battery_voltage_v;
    }

    if (charge_target != pv_probe_target)
    {
        pv_charge_probed = 0;
        pv_probe_target = charge_target;
        pv_probe_threshold = 0.0f;
        pv_probe_start_tick = 0;
    }

    /* Hysteresis: start charging at 0.5W surplus, stop at 0.2W.
     * Prevents MOS toggling when surplus hovers around the threshold. */
    uint8_t can_charge_from_pv =
        pv_available &&
        (pv_surplus_w > (pv_charging_active ? 0.20f : (PV_CHARGE_MIN_POWER_W + PV_CHARGE_MARGIN_W)));
    uint8_t pv_load_priority_ok =
        pv_available &&
        (pv_voltage_v > PV_LOADED_MIN_VOLTAGE_V) &&
        (pv_power_w >= (home_load_power_w + (pv_charging_active ? 0.0f : PV_LOAD_MARGIN_W)));
    /* Probe charging:
     * The INA226 only sees the true PV charging capability after the charge MOS
     * is briefly enabled. Use one shared probe policy for home and car battery:
     * home is selected first; car is selected only after home SOC is high.
     */
    uint8_t pv_probe_charge = 0;
    if (charge_target != 0U && pv_probe_threshold <= 0.0f)
    {
        pv_probe_threshold = charge_target_voltage_v + PV_CHARGE_VOLT_MARGIN_V;
    }
    /* Start probe: PV in ACTIVE, waited 2s to stabilize, voltage enough for target.
     * Do not require pv_load_priority_ok here: before the charge MOS is enabled,
     * the measured PV power may only reflect the home load, not the charge branch.
     */
    if (charge_target != 0U &&
        pv_available && pv_charge_probed == 0 &&
        ai_allow_new_charge_probe &&
        (charge_target != 2U || ai_allow_car_charge) &&
        (pv_voltage_v > pv_probe_threshold) &&
        (now_tick - pv_state_tick) >= PV_ACTIVE_STABILIZE_MS)
    {
        pv_charge_probed = 1;  /* 1 = probing in progress */
        pv_probe_start_tick = now_tick;
    }
    /* During probe: keep charging, then judge after the PV/battery voltage settles. */
    if (pv_charge_probed == 1)
    {
        uint32_t probe_elapsed_ms = now_tick - pv_probe_start_tick;
        uint8_t probe_settled = probe_elapsed_ms >= PV_PROBE_SETTLE_MS;

        pv_probe_charge = 1;
        /* The PV voltage normally drops as soon as the charge MOS is enabled.
         * Check loaded voltage and power only after INA226 and the battery branch settle.
         */
        if (charge_target == 0U ||
            (probe_settled &&
             (!pv_load_priority_ok ||
              pv_voltage_v <= (charge_target_voltage_v + PV_CHARGE_HOLD_MARGIN_V))))
        {
            pv_probe_threshold = pv_voltage_v + PV_CHARGE_VOLT_MARGIN_V;
            pv_charge_probed = 0;  /* fail, wait for recovery */
        }
        /* Success: probe time complete and loaded PV voltage stayed above battery. */
        else if (probe_elapsed_ms >= PV_PROBE_CHARGE_MS)
        {
            pv_charge_probed = 2;  /* success, use normal logic */
        }
    }
    /* Charging allowed when:
     * - Probe in progress, OR
     * - Normal charging: surplus power enough AND PV voltage > target battery voltage */
    uint8_t pv_can_charge_home =
        (charge_target == 1U) &&
        ai_allow_home_charge &&
        (pv_probe_charge ||
         (can_charge_from_pv &&
          pv_load_priority_ok &&
          (pv_voltage_v > (home_battery_voltage_v +
                           (pv_charging_active ? PV_CHARGE_HOLD_MARGIN_V : PV_CHARGE_VOLT_MARGIN_V)))));
    uint8_t pv_can_charge_car =
        (charge_target == 2U) &&
        ai_allow_car_charge &&
        (pv_probe_charge ||
         (can_charge_from_pv &&
          pv_load_priority_ok &&
          (pv_voltage_v > (car_battery_voltage_v +
                           (pv_charging_active ? PV_CHARGE_HOLD_MARGIN_V : PV_CHARGE_VOLT_MARGIN_V)))));
    uint8_t v2h_should_start =
        (!enough_for_pv_load &&
         home_soc <= HOME_SOC_V2H_START_PCT &&
         car_soc >= CAR_SOC_V2H_START_PCT);
    uint8_t v2h_should_stop =
        (enough_for_pv_load ||
         home_soc >= HOME_SOC_V2H_STOP_PCT ||
         car_soc <= CAR_SOC_V2H_STOP_PCT);
    uint8_t led_key = Key_IsPressed(KEY_LED_PORT, KEY_LED_PIN);
    uint8_t fan_key = Key_IsPressed(KEY_FAN_PORT, KEY_FAN_PIN);
    uint8_t qi_key = Key_IsPressed(KEY_QI_PORT, KEY_QI_PIN);
    OneNET_Control_t cloud_ctrl;

    osMutexWait(g_mutex, osWaitForever);
    cloud_ctrl = g_onenet_ctrl;
    if (g_onenet_ctrl.updated)
    {
        g_onenet_ctrl.updated = 0;
    }
    osMutexRelease(g_mutex);

    if (cloud_ctrl.updated)
    {
        if (cloud_ctrl.home_led >= 0)
        {
            led_manual_override = 1;
            led_manual_on = (uint8_t)cloud_ctrl.home_led;
            led_manual_year = bj_year;
            led_manual_month = bj_month;
            led_manual_day = bj_day;
            led_manual_period = bj_led_period;
        }
        if (cloud_ctrl.home_feng >= 0 &&
            (HAL_GetTick() - fan_local_override_tick) > 8000U)
        {
            fan_user_req = (uint8_t)cloud_ctrl.home_feng;
        }
        if (cloud_ctrl.home_load >= 0)
        {
            rigid_user_req = (uint8_t)cloud_ctrl.home_load;
        }
        if (cloud_ctrl.qi >= 0)
        {
            qi_user_req = (uint8_t)cloud_ctrl.qi;
            qi_active = (uint8_t)cloud_ctrl.qi;
        }
    }

    if (time_valid && led_manual_override &&
        (led_manual_year != bj_year ||
         led_manual_month != bj_month ||
         led_manual_day != bj_day ||
         led_manual_period != bj_led_period))
    {
        led_manual_override = 0;
    }

    if (Key_GetPressedEvent(&led_key_fsm, led_key, now_tick))
    {
        uint8_t led_now =
            HAL_GPIO_ReadPin(MOS_LED_PORT, MOS_LED_PIN) == GPIO_PIN_SET ? 1U : 0U;
        led_manual_override = 1;
        led_manual_on = led_now ? 0U : 1U;
        led_manual_year = bj_year;
        led_manual_month = bj_month;
        led_manual_day = bj_day;
        led_manual_period = bj_led_period;
        // if (SERIAL_VERBOSE_LOG) printf("KEY2 LED request=%d\r\n", led_manual_on);
    }
    if (Key_GetPressedEvent(&fan_key_fsm, fan_key, now_tick))
    {
        fan_user_req = !fan_user_req;
        fan_local_override_tick = now_tick;
        // if (SERIAL_VERBOSE_LOG) printf("KEY1 FAN request=%d\r\n", fan_user_req);
    }
    if (Key_GetPressedEvent(&qi_key_fsm, qi_key, now_tick))
    {
        qi_user_req = qi_user_req ? 0U : 1U;
        qi_active = qi_user_req;
        // if (SERIAL_VERBOSE_LOG) printf("KEY0 QI request=%d\r\n", qi_user_req);
    }

    /*
     * V2H fallback:
     * - Start only when PV cannot cover the rigid home load, home SOC is low,
     *   and car SOC has enough reserve.
     * - Stop with hysteresis when PV recovers, home SOC recovers, or car SOC is low.
     * Handle this before source selection so PB14 and car-side V2H are not
     * intentionally enabled together.
     */
    if (!g_v2h_active && v2h_should_start)
    {
        SendCarV2HCommand(1);
        g_v2h_active = 1;
    }
    else if (g_v2h_active && v2h_should_stop)
    {
        SendCarV2HCommand(0);
        g_v2h_active = 0;
    }

    /*
     * Home 5V source selection:
     * - PE0 connects PV 5V to the home load bus when PV is stable enough.
     * - PB14 connects the home battery 5V source only when PV is not enough
     *   and V2H is not active. Schottky diodes provide hardware anti-backfeed;
     *   software still prefers one source at a time.
     * - The car-side V2H MOS is controlled by F1 after the CAN request.
     * - PE1 only enables the rigid-load branch; LED/fan/Qi have their own
     *   branch MOS outputs but still require a valid 5V source on the bus.
     */
    uint8_t use_pv_source = enough_for_pv_load;
    uint8_t use_home_source = (!use_pv_source && home_bat_allowed && !g_v2h_active);
    uint8_t home_source_available = use_pv_source || use_home_source || g_v2h_active;

    Mos_Write(MOS_PV_SRC_PORT, MOS_PV_SRC_PIN, use_pv_source);
    Mos_Write(MOS_HOME_SRC_PORT, MOS_HOME_SRC_PIN, use_home_source);
    Mos_Write(MOS_RIGID_LOAD_PORT, MOS_RIGID_LOAD_PIN,
              home_source_available && rigid_user_req);

    /*
     * Charging priority:
     * 1) Current home load is protected first.
     * 2) Stable PV surplus charges the home battery first.
     * 3) Only after home SOC is high enough, PV surplus charges the car battery.
     */
    if (pv_can_charge_home && home_soc < HOME_SOC_CHARGE_FULL)
    {
        HAL_GPIO_WritePin(MOS_CHARGE_PORT, MOS_HOME_CHARGE_PIN, GPIO_PIN_SET);
        HAL_GPIO_WritePin(MOS_CHARGE_PORT, MOS_CAR_CHARGE_PIN, GPIO_PIN_RESET);
        pv_charging_active = 1;
    }
    else if (pv_can_charge_car && ai_allow_car_charge &&
             home_soc >= HOME_SOC_CHARGE_FULL &&
             car_soc < CAR_SOC_CHARGE_FULL)
    {
        HAL_GPIO_WritePin(MOS_CHARGE_PORT, MOS_HOME_CHARGE_PIN, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(MOS_CHARGE_PORT, MOS_CAR_CHARGE_PIN, GPIO_PIN_SET);
        pv_charging_active = 1;
    }
    else
    {
        HAL_GPIO_WritePin(MOS_CHARGE_PORT, MOS_CHARGE_ALL_PINS, GPIO_PIN_RESET);
        pv_charging_active = 0;
    }

    uint8_t car_charge_on =
        HAL_GPIO_ReadPin(MOS_CHARGE_PORT, MOS_CAR_CHARGE_PIN) == GPIO_PIN_SET ? 1U : 0U;
    if (car_charge_on)
    {
        if (!car_charge_reported ||
            (now_tick - car_charge_report_tick) >= CAN_CAR_CHARGE_REPORT_MS)
        {
            SendCarChargeCommand(1, pv_power_w);
            car_charge_report_tick = now_tick;
            car_charge_reported = 1U;
        }
    }
    else if (car_charge_reported)
    {
        SendCarChargeCommand(0, 0.0f);
        car_charge_report_tick = now_tick;
        car_charge_reported = 0U;
    }

    if (time_valid)
    {
        if (bj_hour < 6U)
        {
            led_auto_request = 0U;
        }
        else if (bj_hour >= 18U && bj_hour < 22U)
        {
            led_auto_request = 1U;
        }
        else if (bj_hour >= 22U)
        {
            led_auto_request = 0U;
        }
        else
        {
            led_auto_request = (lux < NIGHT_LUX_THRESHOLD) ? 1U : 0U;
        }
    }
    else
    {
        led_auto_request = (lux < NIGHT_LUX_THRESHOLD) ? 1U : 0U;
    }

    /* LED: Beijing time auto by default; key/cloud control locks auto until next day. */
    uint8_t led_request = led_manual_override ? led_manual_on : led_auto_request;
    uint8_t led_out = home_source_available &&
                      led_request &&
                      !low_energy_mode &&
                      home_bat_allowed;
    Mos_Write(MOS_LED_PORT, MOS_LED_PIN, led_out);
    if (led_out != prev_led_out)
    {
        // printf("LED: req=%u out=%u src=%u low=%u soc_ok=%u key=%u manual=%u\r\n",
        //        led_request, led_out, home_source_available, low_energy_mode,
        //        home_bat_allowed, led_key, led_manual_override);
        prev_led_out = led_out;
    }

    /* Fan: PE3 local request, allowed only when PV exists or home SOC is healthy. */
    uint8_t fan_out = fan_user_req && home_source_available && !low_energy_mode;
    Mos_Write(MOS_FAN_PORT, MOS_FAN_PIN, fan_out);
    if (fan_out != prev_fan_out)
    {
        // printf("FAN: req=%u out=%u src=%u low=%u key=%u\r\n",
        //        fan_user_req, fan_out, home_source_available, low_energy_mode, fan_key);
        prev_fan_out = fan_out;
    }

    /* Qi: key/cloud request wins; energy protection can still force the output off. */
    qi_active = (qi_user_req && home_source_available && !low_energy_mode) ? 1U : 0U;
    Mos_Write(MOS_QI_PORT, MOS_QI_PIN, qi_active);

    /* Keep the DAC-driven PV simulator active when light exists and energy is useful. */
    if (!light_user_req)
    {
        g_charging_active = 0;
    }
    else if (lux > DAY_LUX_THRESHOLD &&
        (home_soc < 95.0f || home_load_power_w > 0.10f ||
         (human_soc >= 0.0f && human_soc < HUMAN_SOC_QI_REQ_PCT)))
    {
        g_charging_active = 1;
    }
    else if (!pv_available && lux < NIGHT_LUX_THRESHOLD)
    {
        g_charging_active = 0;
    }
}

/**
  * @brief  Voltage-to-SOC lookup table for 2S LiPo (6.5V cutoff ~ 8.4V full)
  *         Piecewise linear interpolation between known breakpoints
  */
static uint8_t SOC_VoltageToPercent(float voltage)
{
    /* Voltage(mV) -> SOC(%) breakpoints */
    static const uint16_t v_mv[] = { 6500, 6800, 7200, 7600, 8000, 8400 };
    static const uint8_t  soc[]  = {    0,   20,   40,   65,   90,  100 };
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
  // if (SERIAL_VERBOSE_LOG) printf("INA226 initialized (PB6=SCL, PB7=SDA)\r\n");

  uint8_t init_skip = 3;  /* skip first few readings for sensor stabilize */

  for(;;)
  {
    if (INA226_ReadData(&ina_data) == 0)
    {
      /* Skip first few readings to let sensor stabilize */
      if (init_skip > 0) {
          init_skip--;
          // if (SERIAL_VERBOSE_LOG) printf("INA226: skipping initial read V=%.3f\r\n", (double)ina_data.bus_voltage);
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
      /* 0. Over-discharge / battery cutoff: V < 6.5V -> force 0%, reset init */
      if (avg_v < VOLTAGE_THRESHOLD_LOW) {
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
              // if (SERIAL_VERBOSE_LOG)
              // {
              //     printf("SOC: V=%.3fV < 6.5V -> 0%%\r\n", (double)avg_v);
              // }
          }
          osDelay(200);
          continue;
      }

      /* 1. First reading: init coulomb counter directly from voltage */
      if (!soc_initialized) {
          uint8_t v_soc = SOC_VoltageToPercent(avg_v);
          soc_coulomb_mah = (float)v_soc / 100.0f * 2200.0f;
          soc_initialized = 1;
          // if (SERIAL_VERBOSE_LOG)
          // {
          //     printf("SOC init: V=%.3fV -> %d%% (%.1fmAh)\r\n",
          //            (double)avg_v, v_soc, (double)soc_coulomb_mah);
          // }
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
          // if (SERIAL_VERBOSE_LOG)
          // {
          //     printf("SOC recal: V %.3f->%.3fV, SOC=%d%%\r\n",
          //            (double)last_cal_voltage, (double)avg_v,
          //            SOC_VoltageToPercent(avg_v));
          // }
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
          // if (SERIAL_VERBOSE_LOG)
          // {
          //     printf("SOC: V=%.3fV I=%.3fmAh mah=%.1f soc=%.1f%%\r\n",
          //            (double)avg_v, (double)current_mA,
          //            (double)soc_coulomb_mah, (double)soc);
          // }
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
      if (SERIAL_VERBOSE_LOG) printf("INA226 read FAILED!\r\n");
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
  (void)local;  /* used when debug printf is enabled */

  for(;;)
  {
    osSemaphoreWait(g_data_ready, osWaitForever);

    // osMutexWait(g_mutex, osWaitForever);
    // local = g_sensor;
    // osMutexRelease(g_mutex);

    // if (SERIAL_VERBOSE_LOG && local.ina226_ok)
    // {
    //   printf("INA226: V=%.3fV I=%.3fmA P=%.3fmW SOC=%.1f%%\r\n",
    //          (double)local.bus_voltage,
    //          (double)(local.current * 1000.0f),
    //          (double)(local.power * 1000.0f),
    //          (double)local.soc_pct);
    // }
    // if (SERIAL_VERBOSE_LOG && local.gy30_ok)
    // {
    //   printf("GY30: %.1f lux\r\n", (double)local.lux);
    // }

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
  if (SERIAL_VERBOSE_LOG) printf("GY30 initialized (PC3=SCL, PC4=SDA)\r\n");

  /* Start DAC channel 1 (PA4) */
  HAL_DAC_Start(&hdac, DAC_CHANNEL_1);
  if (SERIAL_VERBOSE_LOG) printf("DAC CH1 started (PA4)\r\n");

  for(;;)
  {
    if (GY30_ReadLight(&light) == 0)
    {
      if (SERIAL_VERBOSE_LOG) printf("GY30: %.1f lux\r\n", (double)light);
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
      if (SERIAL_VERBOSE_LOG) printf("GY30 read error\r\n");
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
  uint32_t data_log_tick = 0;
  uint8_t data_header_printed = 0;
  SensorData_t local_data;
  INA226_Data pv_data;
  INA226_Data ina3_data;
  ESP32S3_Data_t local_s3;
  EnergyLstmPrediction_t local_pred;

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
  if (SERIAL_VERBOSE_LOG) printf("CAN1 ready (PA11=RX, PA12=TX)\r\n");

  for(;;)
  {
    /* Read PV INA226 (PB13/PB12) about once per second with moving average. */
    pv_read_div++;
    if (pv_read_div >= 20)
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

    /* Read INA226 #3 home load (PC11=SCL, PC12=SDA) about once per second. */
    ina3_read_div++;
    if (ina3_read_div >= 20)
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
    local_s3 = g_esp32s3_data;
    local_pred = g_energy_lstm_pred;
    osMutexRelease(g_mutex);

    /* Check for received CAN command from STM32F1 */
    if (xCanRxQueue && xQueueReceive(xCanRxQueue, &rx_cmd, 0) == pdTRUE)
    {
      if (SERIAL_VERBOSE_LOG) printf("CAN CMD: cmd=%d param=%d\r\n", rx_cmd.cmd, rx_cmd.param);

      /* Update global F1 command data */
      g_f1_cmd = rx_cmd;
      g_f1_cmd_updated = 1;
    }

    /* Bottom-layer dispatch logic (always call; sensor_ok=0 -> default OFF). */
    Relay_Control(local_data.pv_power,
                  local_data.ina3_ok ? local_data.ina3_power : 0.0f,
                  local_data.pv_voltage,
                  local_data.bus_voltage,
                  g_f1_battery.voltage,
                  local_data.soc_pct, g_f1_battery.soc_pct,
                  local_s3.valid ? local_s3.bat_pct : -1.0f,
                  local_data.lux,
                  local_data.ina226_ok, local_data.pv_ok);

#if SERIAL_DATA_LOG
    if (!data_header_printed)
    {
      data_header_printed = 1;
      printf("DATA,tick_ms,scene,lux,pv_ok,pv_v,pv_i,pv_p,home_ok,home_v,home_i,home_p,home_soc,load_ok,load_v,load_i,load_p,car_soc,s3_valid,s3_v,s3_soc,s3_hr,s3_spo2,s3_state,pvsrc,hsrc,rigid,led,fan,qi,hchg,cchg,v2h,ai_valid,ai_pv_p,ai_load_p,ai_home_soc\r\n");
    }

    if ((HAL_GetTick() - data_log_tick) >= DATA_LOG_PERIOD_MS)
    {
      data_log_tick = HAL_GetTick();
      printf("DATA,%lu,%u,%.1f,%u,%.3f,%.3f,%.3f,%u,%.3f,%.3f,%.3f,%.1f,%u,%.3f,%.3f,%.3f,%u,%u,%.3f,%.1f,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.3f,%.3f,%.1f\r\n",
             (unsigned long)data_log_tick,
             DATA_SCENE_ID,
             (double)local_data.lux,
             local_data.pv_ok,
             (double)local_data.pv_voltage,
             (double)local_data.pv_current,
             (double)local_data.pv_power,
             local_data.ina226_ok,
             (double)local_data.bus_voltage,
             (double)local_data.current,
             (double)local_data.power,
             (double)local_data.soc_pct,
             local_data.ina3_ok,
             (double)local_data.ina3_voltage,
             (double)local_data.ina3_current,
             (double)local_data.ina3_power,
             g_f1_battery.soc_pct,
             local_s3.valid,
             (double)(local_s3.valid ? local_s3.bat_v : -1.0f),
             (double)(local_s3.valid ? local_s3.bat_pct : -1.0f),
             local_s3.valid ? local_s3.hr : 0U,
             local_s3.valid ? local_s3.spo2 : 0U,
             local_s3.valid ? local_s3.state : 0U,
             HAL_GPIO_ReadPin(MOS_PV_SRC_PORT, MOS_PV_SRC_PIN) == GPIO_PIN_SET,
             HAL_GPIO_ReadPin(MOS_HOME_SRC_PORT, MOS_HOME_SRC_PIN) == GPIO_PIN_SET,
             HAL_GPIO_ReadPin(MOS_RIGID_LOAD_PORT, MOS_RIGID_LOAD_PIN) == GPIO_PIN_SET,
             HAL_GPIO_ReadPin(MOS_LED_PORT, MOS_LED_PIN) == GPIO_PIN_SET,
             HAL_GPIO_ReadPin(MOS_FAN_PORT, MOS_FAN_PIN) == GPIO_PIN_SET,
             HAL_GPIO_ReadPin(MOS_QI_PORT, MOS_QI_PIN) == GPIO_PIN_SET,
             HAL_GPIO_ReadPin(MOS_CHARGE_PORT, MOS_HOME_CHARGE_PIN) == GPIO_PIN_SET,
             HAL_GPIO_ReadPin(MOS_CHARGE_PORT, MOS_CAR_CHARGE_PIN) == GPIO_PIN_SET,
             g_v2h_active,
             local_pred.valid,
             (double)(local_pred.valid ? local_pred.future_pv_p : -1.0f),
             (double)(local_pred.valid ? local_pred.future_load_p : -1.0f),
             (double)(local_pred.valid ? local_pred.future_home_soc : -1.0f));
    }
#endif

    osDelay(50);
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
  ESP32S3_Data_t local_esp32s3;
  ESP32S3_Data_t prev_esp32s3;
  EnergyLstmPrediction_t local_pred;
  EnergyLstmPrediction_t prev_pred;
  char local_time[32];
  char prev_time[32] = "";
  char pred_line[24];

  /* Initialize prev to invalid values so first update always draws */
  memset(&prev, 0xFF, sizeof(prev));
  memset(&prev_f1_battery, 0, sizeof(prev_f1_battery));
  memset(&prev_esp32s3, 0xFF, sizeof(prev_esp32s3));
  memset(&prev_pred, 0xFF, sizeof(prev_pred));

  /* Clear screen and draw static labels */
  lcd_clear(WHITE);

  uint16_t lcd_max_x = (lcddev.width > 0U) ? (lcddev.width - 1U) : 239U;
  uint16_t lcd_max_y = (lcddev.height > 0U) ? (lcddev.height - 1U) : 479U;
  uint8_t use_bottom_area = (lcddev.height >= 700U) ? 1U : 0U;
  uint8_t use_right_area = ((lcddev.width >= 700U) && !use_bottom_area) ? 1U : 0U;
  uint16_t mos_base_x = use_right_area ? 260U : 8U;
  uint16_t mos_base_y = use_bottom_area ? 430U : (use_right_area ? 20U : 424U);
  uint16_t mos_grid_y = use_bottom_area ? 460U : (use_right_area ? 50U : 424U);
  uint16_t mos_col_w = (use_bottom_area || use_right_area) ? 150U : 78U;
  uint16_t mos_row_h = (use_bottom_area || use_right_area) ? 28U : 18U;
  uint16_t s3_base_x = use_bottom_area ? 10U : (use_right_area ? 260U : 142U);
  uint16_t s3_base_y = use_bottom_area ? 560U : (use_right_area ? 160U : 382U);
  uint16_t s3_value_x = s3_base_x + 54U;
  uint16_t time_base_x = use_right_area ? 520U : s3_base_x;
  uint16_t time_base_y = use_bottom_area ? (uint16_t)(s3_base_y + 150U) :
                         (use_right_area ? 160U : 360U);
  uint16_t pred_base_x = use_bottom_area ? 220U : (use_right_area ? 520U : 124U);
  uint16_t pred_base_y = use_bottom_area ? s3_base_y : (use_right_area ? 240U : 310U);
  uint16_t pred_clear_x2 = use_bottom_area ? lcd_max_x : (use_right_area ? lcd_max_x : 239U);
  uint16_t pred_clear_y2 = (uint16_t)(pred_base_y + 80U);
  uint16_t s3_clear_x2 = use_bottom_area ? lcd_max_x : (use_right_area ? lcd_max_x : 240U);
  uint16_t s3_clear_y2 = use_bottom_area ? (s3_base_y + 132U) :
                         (use_right_area ? (s3_base_y + 132U) : (s3_base_y + 96U));
  if (s3_clear_y2 > lcd_max_y) s3_clear_y2 = lcd_max_y;
  if (pred_clear_y2 > lcd_max_y) pred_clear_y2 = lcd_max_y;

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

  /* Section 6: Home dispatch MOS states */
  if (use_bottom_area)
  {
    lcd_fill(0, 420, lcd_max_x, lcd_max_y, WHITE);
  }
  else if (use_right_area)
  {
    lcd_fill(250, 0, lcd_max_x, lcd_max_y, WHITE);
  }
  else
  {
    lcd_fill(0, 420, 240, 479, WHITE);
  }
  lcd_show_string(mos_base_x, mos_base_y, 180, 16, 16, "MOS State", RED);
  lcd_show_string(time_base_x, time_base_y, 120, 16, 16, "BJ Time", RED);
  lcd_show_string(s3_base_x, s3_base_y, 120, 16, 16, "ESP32-S3", RED);
  lcd_show_string(pred_base_x, pred_base_y, 120, 16, 16, "AI Pred", RED);

  /* Initialize touch screen */
  touch_ok = tp_init();
  if (touch_ok == 0) {
      if (SERIAL_VERBOSE_LOG) printf("Touch screen initialized\r\n");
  } else {
      if (SERIAL_VERBOSE_LOG) printf("Touch screen not found\r\n");
  }

  for(;;)
  {
    /* Read shared sensor data */
    osMutexWait(g_mutex, osWaitForever);
    local = g_sensor;
    local_esp32s3 = g_esp32s3_data;
    local_pred = g_energy_lstm_pred;
    strncpy(local_time, g_beijing_time, sizeof(local_time) - 1U);
    local_time[sizeof(local_time) - 1U] = '\0';
    osMutexRelease(g_mutex);

    if (strncmp(prev_time, local_time, sizeof(prev_time)) != 0)
    {
      uint16_t clear_y2 = (uint16_t)(time_base_y + 38U);
      if (clear_y2 > lcd_max_y) clear_y2 = lcd_max_y;
      lcd_fill(time_base_x, (uint16_t)(time_base_y + 20U), s3_clear_x2, clear_y2, WHITE);
      lcd_show_string(time_base_x, (uint16_t)(time_base_y + 20U), 220, 16, 16, local_time, BLUE);
      strncpy(prev_time, local_time, sizeof(prev_time) - 1U);
      prev_time[sizeof(prev_time) - 1U] = '\0';
    }

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
        int hv_f = (int)((local.bus_voltage - hv_i) * 100);
        if (hv_f < 0) hv_f = -hv_f;
        lcd_show_num(30, 28, hv_i, 2, 16, RED);
        lcd_show_char(46, 28, '.', 16, 0, RED);
        lcd_show_xnum(54, 28, hv_f, 2, 16, 0x80, RED);
        lcd_show_string(78, 28, 20, 16, 16, "V", RED);
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
        int n3_f = (int)((local.ina3_voltage - n3_i) * 100);
        if (n3_f < 0) n3_f = -n3_f;
        lcd_show_num(30, 118, n3_i, 2, 16, RED);
        lcd_show_char(46, 118, '.', 16, 0, RED);
        lcd_show_xnum(54, 118, n3_f, 2, 16, 0x80, RED);
        lcd_show_string(78, 118, 20, 16, 16, "V", RED);

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
        int pv_f = (int)((local.pv_voltage - pv_i) * 100);
        if (pv_f < 0) pv_f = -pv_f;
        lcd_show_num(30, 190, pv_i, 2, 16, RED);
        lcd_show_char(46, 190, '.', 16, 0, RED);
        lcd_show_xnum(54, 190, pv_f, 2, 16, 0x80, RED);
        lcd_show_string(78, 190, 20, 16, 16, "V", RED);

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
      lcd_show_xnum(116, 250, mv_f, 2, 16, 0x80, RED);
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
      int bv_f = (int)((g_f1_battery.voltage - bv_i) * 100);
      if (bv_f < 0) bv_f = -bv_f;
      lcd_show_num(30, 328, bv_i, 2, 16, RED);
      lcd_show_char(46, 328, '.', 16, 0, RED);
      lcd_show_xnum(54, 328, bv_f, 2, 16, 0x80, RED);
      lcd_show_string(78, 328, 20, 16, 16, "V", RED);
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

    /* === Section 5b: LSTM prediction from ESP32-S3 === */
    if (local_pred.valid != prev_pred.valid ||
        local_pred.future_pv_p != prev_pred.future_pv_p ||
        local_pred.future_load_p != prev_pred.future_load_p ||
        local_pred.future_home_soc != prev_pred.future_home_soc)
    {
      lcd_fill(pred_base_x, (uint16_t)(pred_base_y + 18U), pred_clear_x2, pred_clear_y2, WHITE);
      if (local_pred.valid)
      {
        snprintf(pred_line, sizeof(pred_line), "PV:%4.1fW", (double)local_pred.future_pv_p);
        lcd_show_string(pred_base_x, (uint16_t)(pred_base_y + 20U), 110, 16, 16, pred_line, BLUE);
        snprintf(pred_line, sizeof(pred_line), "LD:%4.1fW", (double)local_pred.future_load_p);
        lcd_show_string(pred_base_x, (uint16_t)(pred_base_y + 38U), 110, 16, 16, pred_line, BLUE);
        snprintf(pred_line, sizeof(pred_line), "SOC:%4.1f%%", (double)local_pred.future_home_soc);
        lcd_show_string(pred_base_x, (uint16_t)(pred_base_y + 56U), 110, 16, 16, pred_line, BLUE);
      }
      else
      {
        lcd_show_string(pred_base_x, (uint16_t)(pred_base_y + 20U), 110, 16, 16, "WAIT AI", GRAY);
      }
      prev_pred = local_pred;
    }

    /* === Section 6: Home dispatch MOS states === */
    static uint8_t prev_mos_state[9] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                        0xFF, 0xFF, 0xFF, 0xFF};
    static const char *mos_label[9] = {
      "PV", "LD", "HS", "LED", "FAN", "QI", "HC", "CC", "V2H"
    };
    uint8_t mos_state[9];
    mos_state[0] = HAL_GPIO_ReadPin(MOS_PV_SRC_PORT, MOS_PV_SRC_PIN) == GPIO_PIN_SET ? 1 : 0;
    mos_state[1] = HAL_GPIO_ReadPin(MOS_RIGID_LOAD_PORT, MOS_RIGID_LOAD_PIN) == GPIO_PIN_SET ? 1 : 0;
    mos_state[2] = HAL_GPIO_ReadPin(MOS_HOME_SRC_PORT, MOS_HOME_SRC_PIN) == GPIO_PIN_SET ? 1 : 0;
    mos_state[3] = HAL_GPIO_ReadPin(MOS_LED_PORT, MOS_LED_PIN) == GPIO_PIN_SET ? 1 : 0;
    mos_state[4] = HAL_GPIO_ReadPin(MOS_FAN_PORT, MOS_FAN_PIN) == GPIO_PIN_SET ? 1 : 0;
    mos_state[5] = HAL_GPIO_ReadPin(MOS_QI_PORT, MOS_QI_PIN) == GPIO_PIN_SET ? 1 : 0;
    mos_state[6] = HAL_GPIO_ReadPin(MOS_CHARGE_PORT, MOS_HOME_CHARGE_PIN) == GPIO_PIN_SET ? 1 : 0;
    mos_state[7] = HAL_GPIO_ReadPin(MOS_CHARGE_PORT, MOS_CAR_CHARGE_PIN) == GPIO_PIN_SET ? 1 : 0;
    mos_state[8] = g_v2h_active ? 1 : 0;

    for (int i = 0; i < 9; i++)
    {
      if (mos_state[i] != prev_mos_state[i])
      {
        uint16_t x = (uint16_t)(mos_base_x + (i % 3) * mos_col_w);
        uint16_t y = (uint16_t)(mos_grid_y + (i / 3) * mos_row_h);
        uint16_t clear_x2 = (uint16_t)(x + mos_col_w - 6U);
        uint16_t clear_y2 = (uint16_t)(y + 16U);
        if (clear_x2 > lcd_max_x) clear_x2 = lcd_max_x;
        if (clear_y2 > lcd_max_y) clear_y2 = lcd_max_y;
        lcd_fill(x, y, clear_x2, clear_y2, WHITE);
        lcd_show_string(x, y, 40, 16, 16, (char *)mos_label[i], BLUE);
        lcd_show_char(x + 32, y, ':', 16, 0, BLUE);
        lcd_show_string(x + 40, y, 30, 16, 16,
                        mos_state[i] ? "ON " : "OFF",
                        mos_state[i] ? GREEN : RED);
        prev_mos_state[i] = mos_state[i];
      }
    }

    /* === Section 7: ESP32-S3 wearable data === */
    if (local_esp32s3.valid != prev_esp32s3.valid ||
        local_esp32s3.bat_v != prev_esp32s3.bat_v ||
        local_esp32s3.bat_pct != prev_esp32s3.bat_pct ||
        local_esp32s3.hr != prev_esp32s3.hr ||
        local_esp32s3.spo2 != prev_esp32s3.spo2 ||
        local_esp32s3.state != prev_esp32s3.state)
    {
      lcd_fill(s3_base_x, (uint16_t)(s3_base_y + 20U), s3_clear_x2, s3_clear_y2, WHITE);
      if (local_esp32s3.valid)
      {
        int s3_v_i = (int)local_esp32s3.bat_v;
        int s3_v_f = (int)((local_esp32s3.bat_v - s3_v_i) * 100);
        int s3_soc = (int)local_esp32s3.bat_pct;
        if (s3_v_f < 0) s3_v_f = -s3_v_f;

        lcd_show_string(s3_base_x, (uint16_t)(s3_base_y + 24U), 40, 16, 16, "V:", BLUE);
        lcd_show_num(s3_value_x, (uint16_t)(s3_base_y + 24U), s3_v_i, 1, 16, RED);
        lcd_show_char((uint16_t)(s3_value_x + 8U), (uint16_t)(s3_base_y + 24U), '.', 16, 0, RED);
        lcd_show_xnum((uint16_t)(s3_value_x + 16U), (uint16_t)(s3_base_y + 24U), s3_v_f, 2, 16, 0x80, RED);
        lcd_show_string((uint16_t)(s3_value_x + 36U), (uint16_t)(s3_base_y + 24U), 16, 16, 16, "V", RED);
        lcd_show_string(s3_base_x, (uint16_t)(s3_base_y + 46U), 40, 16, 16, "SOC:", BLUE);
        lcd_show_num(s3_value_x, (uint16_t)(s3_base_y + 46U), s3_soc, 3, 16, RED);
        lcd_show_string((uint16_t)(s3_value_x + 28U), (uint16_t)(s3_base_y + 46U), 16, 16, 16, "%", RED);
        lcd_show_string(s3_base_x, (uint16_t)(s3_base_y + 68U), 40, 16, 16, "HR:", BLUE);
        lcd_show_num(s3_value_x, (uint16_t)(s3_base_y + 68U), local_esp32s3.hr, 3, 16, RED);
        lcd_show_string(s3_base_x, (uint16_t)(s3_base_y + 90U), 48, 16, 16, "O2:", BLUE);
        lcd_show_num(s3_value_x, (uint16_t)(s3_base_y + 90U), local_esp32s3.spo2, 3, 16, RED);
        lcd_show_string((uint16_t)(s3_value_x + 28U), (uint16_t)(s3_base_y + 90U), 16, 16, 16, "%", RED);
        lcd_show_string((uint16_t)(s3_value_x + 56U), (uint16_t)(s3_base_y + 90U), 36, 16, 16,
                        local_esp32s3.state == 4 ? "FALL" : "OK  ",
                        local_esp32s3.state == 4 ? RED : GREEN);
        lcd_show_string(s3_base_x, (uint16_t)(s3_base_y + 112U), 48, 16, 16, "ST:", BLUE);
        lcd_show_num(s3_value_x, (uint16_t)(s3_base_y + 112U), local_esp32s3.state, 3, 16, RED);
      }
      else
      {
        lcd_show_string(s3_base_x, (uint16_t)(s3_base_y + 24U), 120, 16, 16,
                        ESP8266_UDP_HasPeer() ? "NO JSON" : "WAIT S3",
                        GRAY);
      }
      prev_esp32s3 = local_esp32s3;
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

static void OneNET_FillUploadData(OneNET_UploadData_t *upload,
                                  const SensorData_t *local,
                                  const ESP32S3_Data_t *s3_snapshot)
{
  uint8_t pv_src_on = HAL_GPIO_ReadPin(MOS_PV_SRC_PORT, MOS_PV_SRC_PIN) == GPIO_PIN_SET ? 1U : 0U;
  uint8_t home_src_on = HAL_GPIO_ReadPin(MOS_HOME_SRC_PORT, MOS_HOME_SRC_PIN) == GPIO_PIN_SET ? 1U : 0U;
  uint8_t home_chg_on = HAL_GPIO_ReadPin(MOS_CHARGE_PORT, MOS_HOME_CHARGE_PIN) == GPIO_PIN_SET ? 1U : 0U;
  uint8_t car_chg_on = HAL_GPIO_ReadPin(MOS_CHARGE_PORT, MOS_CAR_CHARGE_PIN) == GPIO_PIN_SET ? 1U : 0U;

  if (upload == NULL || local == NULL || s3_snapshot == NULL)
  {
    return;
  }

  upload->car_soc = g_f1_battery.soc_pct;
  upload->car_status = (uint8_t)g_f1_battery.status;
  upload->home_feng = HAL_GPIO_ReadPin(MOS_FAN_PORT, MOS_FAN_PIN) == GPIO_PIN_SET ? 1U : 0U;
  upload->home_led = HAL_GPIO_ReadPin(MOS_LED_PORT, MOS_LED_PIN) == GPIO_PIN_SET ? 1U : 0U;
  upload->home_load = HAL_GPIO_ReadPin(MOS_RIGID_LOAD_PORT, MOS_RIGID_LOAD_PIN) == GPIO_PIN_SET ? 1U : 0U;
  upload->home_soc = local->soc_pct;

  if (pv_src_on && home_chg_on) upload->home_status = 6U;
  else if (pv_src_on && car_chg_on) upload->home_status = 7U;
  else if (home_chg_on) upload->home_status = 1U;
  else if (car_chg_on) upload->home_status = 2U;
  else if (pv_src_on) upload->home_status = 3U;
  else if (home_src_on) upload->home_status = 4U;
  else if (g_v2h_active) upload->home_status = 5U;
  else upload->home_status = 0U;

  upload->human_heart = s3_snapshot->valid ? s3_snapshot->hr : 0U;
  upload->human_soc = s3_snapshot->valid ? s3_snapshot->bat_pct : -1.0f;
  upload->human_spo2 = s3_snapshot->valid ? s3_snapshot->spo2 : 0U;
  upload->human_status = s3_snapshot->valid ? s3_snapshot->state : 0U;
  upload->load_power = local->ina3_power;
  upload->lux = local->lux;
  upload->pv_power = local->pv_power;
  upload->qi = HAL_GPIO_ReadPin(MOS_QI_PORT, MOS_QI_PIN) == GPIO_PIN_SET ? 1U : 0U;
}

static uint8_t Beijing_MonthFromText(const char *mon)
{
  static const char *names[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };

  for (uint8_t i = 0; i < 12U; i++)
  {
    if (strncmp(mon, names[i], 3U) == 0)
    {
      return (uint8_t)(i + 1U);
    }
  }
  return 0;
}

static uint8_t Beijing_IsLeapYear(uint16_t year)
{
  return ((year % 4U == 0U && year % 100U != 0U) || (year % 400U == 0U)) ? 1U : 0U;
}

static uint8_t Beijing_DaysInMonth(uint16_t year, uint8_t month)
{
  static const uint8_t days[12] = {
    31U, 28U, 31U, 30U, 31U, 30U,
    31U, 31U, 30U, 31U, 30U, 31U
  };

  if (month == 2U && Beijing_IsLeapYear(year))
  {
    return 29U;
  }
  if (month >= 1U && month <= 12U)
  {
    return days[month - 1U];
  }
  return 31U;
}

static void Beijing_FormatTime(char *buf, uint16_t len)
{
  if (buf == NULL || len == 0U)
  {
    return;
  }

  if (!g_bj_clock.valid)
  {
    snprintf(buf, len, "--:--:--");
    return;
  }

  snprintf(buf, len, "%04u-%02u-%02u %02u:%02u:%02u",
           g_bj_clock.year, g_bj_clock.month, g_bj_clock.day,
           g_bj_clock.hour, g_bj_clock.minute, g_bj_clock.second);
}

static uint8_t Beijing_ParseSntpTime(const char *raw)
{
  char dow[4] = {0};
  char mon_txt[4] = {0};
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;

  if (raw == NULL)
  {
    return 1;
  }

  if (sscanf(raw, "%3s %3s %d %d:%d:%d %d",
             dow, mon_txt, &day, &hour, &minute, &second, &year) == 7)
  {
    month = Beijing_MonthFromText(mon_txt);
  }
  else if (sscanf(raw, "%d-%d-%d %d:%d:%d",
                  &year, &month, &day, &hour, &minute, &second) != 6)
  {
    return 1;
  }

  if (year < 2020 || month < 1 || month > 12 ||
      day < 1 || day > Beijing_DaysInMonth((uint16_t)year, (uint8_t)month) ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
  {
    return 1;
  }

  g_bj_clock.year = (uint16_t)year;
  g_bj_clock.month = (uint8_t)month;
  g_bj_clock.day = (uint8_t)day;
  g_bj_clock.hour = (uint8_t)hour;
  g_bj_clock.minute = (uint8_t)minute;
  g_bj_clock.second = (uint8_t)second;
  g_bj_clock.tick_ms = HAL_GetTick();
  g_bj_clock.valid = 1U;
  return 0;
}

static void Beijing_IncrementOneSecond(void)
{
  g_bj_clock.second++;
  if (g_bj_clock.second < 60U) return;
  g_bj_clock.second = 0U;

  g_bj_clock.minute++;
  if (g_bj_clock.minute < 60U) return;
  g_bj_clock.minute = 0U;

  g_bj_clock.hour++;
  if (g_bj_clock.hour < 24U) return;
  g_bj_clock.hour = 0U;

  g_bj_clock.day++;
  if (g_bj_clock.day <= Beijing_DaysInMonth(g_bj_clock.year, g_bj_clock.month)) return;
  g_bj_clock.day = 1U;

  g_bj_clock.month++;
  if (g_bj_clock.month <= 12U) return;
  g_bj_clock.month = 1U;
  g_bj_clock.year++;
}

static void BeijingClock_Service(void)
{
  char time_text[32];
  uint8_t changed = 0;

  if (!g_bj_clock.valid)
  {
    return;
  }

  while ((HAL_GetTick() - g_bj_clock.tick_ms) >= 1000U)
  {
    g_bj_clock.tick_ms += 1000U;
    Beijing_IncrementOneSecond();
    changed = 1U;
  }

  if (changed)
  {
    Beijing_FormatTime(time_text, sizeof(time_text));
    osMutexWait(g_mutex, osWaitForever);
    strncpy(g_beijing_time, time_text, sizeof(g_beijing_time) - 1U);
    g_beijing_time[sizeof(g_beijing_time) - 1U] = '\0';
    osMutexRelease(g_mutex);
  }
}

static void OneNET_UpdateBeijingTimeBeforeMqtt(void)
{
  char time_buf[32];
  char time_text[32];

  if (ESP8266_ONENET_AT_GetSntpTime(time_buf, sizeof(time_buf)) == 0)
  {
    if (Beijing_ParseSntpTime(time_buf) == 0)
    {
      Beijing_FormatTime(time_text, sizeof(time_text));
    }
    else
    {
      strncpy(time_text, time_buf, sizeof(time_text) - 1U);
      time_text[sizeof(time_text) - 1U] = '\0';
    }

    osMutexWait(g_mutex, osWaitForever);
    strncpy(g_beijing_time, time_text, sizeof(g_beijing_time) - 1U);
    g_beijing_time[sizeof(g_beijing_time) - 1U] = '\0';
    osMutexRelease(g_mutex);
    if (COMM_VERBOSE_LOG) printf("ONENET: SNTP %s\r\n", time_text);
  }
  else
  {
    if (COMM_VERBOSE_LOG) printf("ONENET: SNTP failed before MQTT\r\n");
  }
}

/**
* @brief ESP8266 UDP task on USART3 (PB10=TX, PB11=RX).
*        Receives ESP32-S3 health/battery data without touching the cloud link.
*/
void StartTask06(void const * argument)
{
  ESP32S3_Data_t rx;
  EnergyLstmPrediction_t pred;
  EnergyLstmInput_t lstm_input;
  SensorData_t local;
  ESP32S3_Data_t s3_snapshot;
  uint8_t packet_flags = 0;
  uint32_t last_udp_retry_tick = 0;
  uint32_t last_lstm_input_tick = 0;
  float raw_pred_home_soc = 0.0f;
  uint8_t udp_ready = 0;

  if (COMM_VERBOSE_LOG) printf("ESP8266 UDP task start\r\n");
  osDelay(2000);
  if (ESP8266_UDP_Init() == 0)
  {
    if (COMM_VERBOSE_LOG) printf("ESP8266 UDP ready (USART3 PB10/PB11)\r\n");
    udp_ready = 1;
  }
  else
  {
    if (COMM_VERBOSE_LOG) printf("ESP8266 UDP init failed\r\n");
    udp_ready = 0;
  }

  for (;;)
  {
    if (!udp_ready && (HAL_GetTick() - last_udp_retry_tick) >= 5000U)
    {
      last_udp_retry_tick = HAL_GetTick();
      if (ESP8266_AT_StartUdp("255.255.255.255",
                              ESP32S3_RX_PORT,
                              ESP8266_UDP_PORT) == 0)
      {
        udp_ready = 1;
      }
    }

    memset(&rx, 0, sizeof(rx));
    memset(&pred, 0, sizeof(pred));
    packet_flags = 0;
    raw_pred_home_soc = 0.0f;
    if (udp_ready && ESP8266_UDP_PollReceiveEx(&rx, &pred, &packet_flags, 200) == 0)
    {
      osMutexWait(g_mutex, osWaitForever);
      if (packet_flags & 0x01U)
      {
        g_esp32s3_data = rx;
        g_esp32s3_updated = 1;
      }
      if (packet_flags & 0x02U)
      {
        raw_pred_home_soc = pred.future_home_soc;
        Energy_ClampLstmPrediction(&pred, &g_sensor);
        g_energy_lstm_pred = pred;
        g_energy_lstm_pred_updated = 1;
      }
      osMutexRelease(g_mutex);
      if (COMM_VERBOSE_LOG && (packet_flags & 0x01U))
      {
        printf("ESP32S3 DATA saved: bat=%.3fV %.1f%% hr=%u spo2=%u state=%u\r\n",
               (double)rx.bat_v, (double)rx.bat_pct,
               rx.hr, rx.spo2, rx.state);
      }
      if (COMM_VERBOSE_LOG && (packet_flags & 0x02U))
      {
        printf("LSTM PRED saved: pv=%.3fW load=%.3fW home_soc=%.1f%% raw=%.1f%%\r\n",
               (double)pred.future_pv_p,
               (double)pred.future_load_p,
               (double)pred.future_home_soc,
               (double)raw_pred_home_soc);
      }
    }

    if (udp_ready &&
        (HAL_GetTick() - last_lstm_input_tick) >= ENERGY_LSTM_UDP_PERIOD_MS)
    {
      last_lstm_input_tick = HAL_GetTick();
      osMutexWait(g_mutex, osWaitForever);
      local = g_sensor;
      s3_snapshot = g_esp32s3_data;
      osMutexRelease(g_mutex);

      Energy_FillLstmInput(&lstm_input, &local, &s3_snapshot);
      if (COMM_VERBOSE_LOG)
      {
        printf("LSTM INPUT send: lux=%.1f pv=%.3fV/%.3fW home=%.3fV/%.1f%% load=%.3fW car=%u%% human=%.1f%%\r\n",
               (double)lstm_input.lux,
               (double)lstm_input.pv_v,
               (double)lstm_input.pv_p,
               (double)lstm_input.home_v,
               (double)lstm_input.home_soc,
               (double)lstm_input.load_p,
               g_f1_battery.soc_pct,
               (double)lstm_input.human_soc);
      }
      (void)ESP8266_UDP_SendLstmInput(&lstm_input);
    }

    osDelay(20);
  }
}

/**
* @brief ESP8266 OneNET cloud task on USART2 (PA2=TX, PA3=RX).
*        Keeps MQTT online while USART3 continues UDP reception independently.
*/
void StartTask07(void const * argument)
{
  SensorData_t local;
  ESP32S3_Data_t s3_snapshot;
  OneNET_UploadData_t upload;
  OneNET_Control_t ctrl;
  uint32_t last_upload_tick = 0;
  uint32_t last_switch_sync_tick = 0;
  uint32_t last_switch_heartbeat_tick = 0;
  uint8_t prev_fan_on = 0xFFU;
  uint8_t prev_led_on = 0xFFU;
  uint8_t prev_rigid_on = 0xFFU;
  uint8_t prev_qi_on = 0xFFU;
  uint8_t pending_switch_ack = 0;
  uint8_t publish_fail_count = 0;
  uint8_t mqtt_ok = 0;
  uint32_t last_ping_tick = 0;

  if (COMM_VERBOSE_LOG) printf("ONENET: wait module startup %ums\r\n", ONENET_STARTUP_DELAY_MS);
  osDelay(ONENET_STARTUP_DELAY_MS);

  for (;;)
  {
    BeijingClock_Service();

    /* MQTT keepalive ping — must run before any continue */
    if (mqtt_ok && (HAL_GetTick() - last_ping_tick) >= ONENET_PING_INTERVAL_MS)
    {
      last_ping_tick = HAL_GetTick();
      if (OneNET_MQTT_Ping() != 0)
      {
        if (++publish_fail_count >= 3U)
        {
          mqtt_ok = 0;
          publish_fail_count = 0;
        }
      }
      else
      {
        publish_fail_count = 0;
      }
    }

    if (!mqtt_ok)
    {
      g_onenet_online = 0;
      if (COMM_VERBOSE_LOG) printf("ONENET: init USART2 cloud link\r\n");
      if (ESP8266_ONENET_AT_InitWiFi() == 0)
      {
        /* Get SNTP while no MQTT TCP connection is open. Some ESP8266 AT
         * firmware cannot return SNTP reliably during an active TCP link.
         */
        OneNET_UpdateBeijingTimeBeforeMqtt();
      }
      else
      {
        if (COMM_VERBOSE_LOG) printf("ONENET: WiFi init failed on USART2\r\n");
        osDelay(5000);
        continue;
      }

      if (OneNET_MQTT_Open() == 0 &&
          OneNET_MQTT_Connect() == 0 &&
          OneNET_MQTT_SubscribeControl() == 0)
      {
        mqtt_ok = 1;
        g_onenet_online = 1;
        last_upload_tick = 0;
        last_switch_sync_tick = 0;
        last_switch_heartbeat_tick = 0;
        prev_fan_on = 0xFFU;
        prev_led_on = 0xFFU;
        prev_rigid_on = 0xFFU;
        prev_qi_on = 0xFFU;
        pending_switch_ack = 1U;
        publish_fail_count = 0;
        last_ping_tick = HAL_GetTick();
        if (COMM_VERBOSE_LOG) printf("ONENET: mqtt online on USART2\r\n");
      }
      else
      {
        if (COMM_VERBOSE_LOG) printf("ONENET: mqtt setup failed on USART2\r\n");
        ESP8266_ONENET_AT_SendCmd(ESP8266_AT_CMD_CLOSE, ESP8266_AT_RSP_OK, 1000);
        osDelay(5000);
        continue;
      }
    }

    ctrl.home_feng = -1;
    ctrl.home_led = -1;
    ctrl.home_load = -1;
    ctrl.qi = -1;
    ctrl.updated = 0;
    if (OneNET_MQTT_Process(&ctrl, 100) == 0 && ctrl.updated)
    {
      osMutexWait(g_mutex, osWaitForever);
      g_onenet_ctrl = ctrl;
      osMutexRelease(g_mutex);
      last_switch_sync_tick = HAL_GetTick();
      pending_switch_ack = 1U;
    }

    if ((HAL_GetTick() - last_upload_tick) >= ONENET_FULL_UPLOAD_MS)
    {
      last_upload_tick = HAL_GetTick();
      osMutexWait(g_mutex, osWaitForever);
      local = g_sensor;
      s3_snapshot = g_esp32s3_data;
      osMutexRelease(g_mutex);

      OneNET_FillUploadData(&upload, &local, &s3_snapshot);
      if (OneNET_Upload(&upload) != 0)
      {
        if (COMM_VERBOSE_LOG) printf("ONENET: full upload failed count=%u\r\n", publish_fail_count + 1U);
        if (++publish_fail_count >= 3U)
        {
          mqtt_ok = 0;
          publish_fail_count = 0;
        }
        continue;
      }
      publish_fail_count = 0;
    }

    if ((pending_switch_ack &&
         (HAL_GetTick() - last_switch_sync_tick) >= ONENET_SWITCH_SYNC_MS) ||
        ((HAL_GetTick() - last_switch_heartbeat_tick) >= ONENET_SWITCH_HEARTBEAT_MS))
    {
      uint8_t fan_on = HAL_GPIO_ReadPin(MOS_FAN_PORT, MOS_FAN_PIN) == GPIO_PIN_SET ? 1U : 0U;
      uint8_t led_on = HAL_GPIO_ReadPin(MOS_LED_PORT, MOS_LED_PIN) == GPIO_PIN_SET ? 1U : 0U;
      uint8_t rigid_on = HAL_GPIO_ReadPin(MOS_RIGID_LOAD_PORT, MOS_RIGID_LOAD_PIN) == GPIO_PIN_SET ? 1U : 0U;
      uint8_t qi_on = HAL_GPIO_ReadPin(MOS_QI_PORT, MOS_QI_PIN) == GPIO_PIN_SET ? 1U : 0U;

      if (fan_on != prev_fan_on ||
          led_on != prev_led_on ||
          rigid_on != prev_rigid_on ||
          qi_on != prev_qi_on ||
          last_switch_sync_tick == 0U ||
          (HAL_GetTick() - last_switch_heartbeat_tick) >= ONENET_SWITCH_HEARTBEAT_MS)
      {
        if (OneNET_UploadSwitchStates(fan_on, led_on, rigid_on, qi_on) != 0)
        {
          if (COMM_VERBOSE_LOG) printf("ONENET: switch upload failed count=%u\r\n", publish_fail_count + 1U);
          if (++publish_fail_count >= 3U)
          {
            mqtt_ok = 0;
            publish_fail_count = 0;
          }
          continue;
        }
        publish_fail_count = 0;
        prev_fan_on = fan_on;
        prev_led_on = led_on;
        prev_rigid_on = rigid_on;
        prev_qi_on = qi_on;
      }

      last_switch_sync_tick = HAL_GetTick();
      last_switch_heartbeat_tick = HAL_GetTick();
      pending_switch_ack = 0U;
    }

    osDelay(100);
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

  osThreadDef(esp8266, StartTask06, osPriorityBelowNormal, 0, 1024);
  osThreadCreate(osThread(esp8266), NULL);
  // if (osThreadCreate(osThread(esp8266), NULL) == NULL)
  // {
  //   if (SERIAL_VERBOSE_LOG) printf("ESP8266 task create failed\r\n");
  // }

  osThreadDef(onenet, StartTask07, osPriorityBelowNormal, 0, 1024);
  osThreadCreate(osThread(onenet), NULL);
  // if (osThreadCreate(osThread(onenet), NULL) == NULL)
  // {
  //   if (SERIAL_VERBOSE_LOG) printf("OneNET task create failed\r\n");
  // }
}

/* USER CODE END Application */
