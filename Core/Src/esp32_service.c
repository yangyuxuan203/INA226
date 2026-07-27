#include "esp32_service.h"

#include "app_health.h"
#include "app_state.h"
#include "can_app.h"
#include "cmsis_os.h"
#include "energy_service.h"
#include "esp8266_udp.h"
#include "main.h"

#include <stdio.h>
#include <string.h>

#define ESP32_SERVICE_VERBOSE_LOG       0U
#define ESP32_SERVICE_RETRY_MS       5000U
#define ESP32_SERVICE_LSTM_PERIOD_MS 10000U
#define ESP32_SERVICE_RX_TIMEOUT_MS    200U
#define ESP32_SERVICE_LOOP_MS           20U

static uint8_t ESP32_ServiceBuildEnergyInput(EnergyServiceInput_t *input,
                                             AppStateSnapshot_t *state)
{
    CAN_BatteryData_t vehicle;

    if (input == NULL || state == NULL ||
        AppState_GetSnapshot(state) != 0U)
    {
        return 1U;
    }

    CAN_App_GetBatterySnapshot(&vehicle);
    memset(input, 0, sizeof(*input));
    input->pv_power_w = state->sensor.pv_power;
    input->home_load_power_w = state->sensor.ina3_ok ?
        state->sensor.ina3_power : 0.0f;
    input->pv_voltage_v = state->sensor.pv_voltage;
    input->home_battery_voltage_v = state->sensor.bus_voltage;
    input->car_battery_voltage_v = vehicle.voltage;
    input->home_soc = state->sensor.soc_pct;
    input->car_soc = (float)vehicle.soc_pct;
    input->human_soc = state->wearable.valid ?
        state->wearable.bat_pct : -1.0f;
    input->lux = state->sensor.lux;
    input->sensor_ok = state->sensor.ina226_ok;
    input->pv_ok = state->sensor.pv_ok;
    input->clock_valid = state->beijing_clock.valid;
    input->clock_year = state->beijing_clock.year;
    input->clock_month = state->beijing_clock.month;
    input->clock_day = state->beijing_clock.day;
    input->clock_hour = state->beijing_clock.hour;
    input->clock_minute = state->beijing_clock.minute;
    input->clock_second = state->beijing_clock.second;
    input->clock_tick_ms = state->beijing_clock.tick_ms;
    input->prediction = state->prediction;
    return 0U;
}

void ESP32_ServiceTask(void const *argument)
{
    ESP32S3_Data_t received_wearable;
    EnergyLstmPrediction_t prediction;
    EnergyLstmInput_t lstm_input;
    EnergyServiceInput_t energy_input;
    AppStateSnapshot_t state;
    uint8_t packet_flags;
    uint32_t last_retry_tick = 0U;
    uint32_t last_lstm_input_tick = 0U;
    float raw_prediction_home_soc;
    uint8_t udp_ready = 0U;

    (void)argument;

    if (ESP32_SERVICE_VERBOSE_LOG)
    {
        printf("ESP8266 UDP task start\r\n");
    }
    osDelay(2000U);
    if (ESP8266_UDP_Init() == 0U)
    {
        udp_ready = 1U;
        if (ESP32_SERVICE_VERBOSE_LOG)
        {
            printf("ESP8266 UDP ready (USART3 PB10/PB11)\r\n");
        }
    }
    else if (ESP32_SERVICE_VERBOSE_LOG)
    {
        printf("ESP8266 UDP init failed\r\n");
    }

    for (;;)
    {
        AppHealth_Heartbeat(APP_HEALTH_TASK_ESP32);
        if (udp_ready == 0U &&
            (HAL_GetTick() - last_retry_tick) >= ESP32_SERVICE_RETRY_MS)
        {
            last_retry_tick = HAL_GetTick();
            if (ESP8266_UDP_Init() == 0U)
            {
                udp_ready = 1U;
            }
        }

        memset(&received_wearable, 0, sizeof(received_wearable));
        memset(&prediction, 0, sizeof(prediction));
        packet_flags = 0U;
        raw_prediction_home_soc = 0.0f;
        if (udp_ready != 0U &&
            ESP8266_UDP_PollReceiveEx(&received_wearable,
                                      &prediction,
                                      &packet_flags,
                                      ESP32_SERVICE_RX_TIMEOUT_MS) == 0U)
        {
            if ((packet_flags & 0x01U) != 0U)
            {
                (void)AppState_SetWearable(&received_wearable);
            }
            if ((packet_flags & 0x02U) != 0U)
            {
                raw_prediction_home_soc = prediction.future_home_soc;
                if (ESP32_ServiceBuildEnergyInput(&energy_input, &state) ==
                    0U)
                {
                    EnergyService_ClampPrediction(&prediction,
                                                  &energy_input);
                }
                (void)AppState_SetPrediction(&prediction,
                                             raw_prediction_home_soc);
            }

            if (ESP32_SERVICE_VERBOSE_LOG &&
                (packet_flags & 0x01U) != 0U)
            {
                printf("ESP32S3 DATA saved: bat=%.3fV %.1f%% hr=%u "
                       "spo2=%u state=%u\r\n",
                       (double)received_wearable.bat_v,
                       (double)received_wearable.bat_pct,
                       received_wearable.hr,
                       received_wearable.spo2,
                       received_wearable.state);
            }
            if (ESP32_SERVICE_VERBOSE_LOG &&
                (packet_flags & 0x02U) != 0U)
            {
                printf("LSTM PRED saved: pv=%.3fW load=%.3fW "
                       "home_soc=%.1f%% raw=%.1f%%\r\n",
                       (double)prediction.future_pv_p,
                       (double)prediction.future_load_p,
                       (double)prediction.future_home_soc,
                       (double)raw_prediction_home_soc);
            }
        }

        if (udp_ready != 0U &&
            (HAL_GetTick() - last_lstm_input_tick) >=
                ESP32_SERVICE_LSTM_PERIOD_MS)
        {
            last_lstm_input_tick = HAL_GetTick();
            if (ESP32_ServiceBuildEnergyInput(&energy_input, &state) == 0U)
            {
                EnergyService_BuildLstmInput(&lstm_input,
                                              &energy_input);
                if (ESP32_SERVICE_VERBOSE_LOG)
                {
                    printf("LSTM INPUT send: lux=%.1f pv=%.3fV/%.3fW "
                           "home=%.3fV/%.1f%% load=%.3fW car=%.1f%% "
                           "human=%.1f%%\r\n",
                           (double)lstm_input.lux,
                           (double)lstm_input.pv_v,
                           (double)lstm_input.pv_p,
                           (double)lstm_input.home_v,
                           (double)lstm_input.home_soc,
                           (double)lstm_input.load_p,
                           (double)lstm_input.car_soc,
                           (double)lstm_input.human_soc);
                }
                (void)ESP8266_UDP_SendLstmInput(&lstm_input);
            }
        }

        osDelay(ESP32_SERVICE_LOOP_MS);
    }
}
