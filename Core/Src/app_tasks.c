#include "app_tasks.h"

#include "adc.h"
#include "app_config.h"
#include "app_health.h"
#include "app_state.h"
#include "can_app.h"
#include "cmsis_os.h"
#include "dac.h"
#include "energy_lvgl_ui.h"
#include "energy_service.h"
#include "gy30.h"
#include "ina226.h"
#include "ina226_3.h"
#include "ina226_pv.h"
#include "main.h"
#include "soc_estimator.h"

#include <stdio.h>

#define APP_TASK_VERBOSE_LOG              0U
#define APP_TASK_HOME_AVERAGE_COUNT        8U
#define APP_TASK_POWER_AVERAGE_COUNT       8U

static float AppTasks_Average(const float *values, uint8_t count)
{
    float sum = 0.0f;
    uint8_t index;

    for (index = 0U; index < count; index++)
    {
        sum += values[index];
    }
    return count != 0U ? sum / (float)count : 0.0f;
}

void HomeSensorTask(void const *argument)
{
    INA226_Data measurement;
    float voltage_samples[APP_TASK_HOME_AVERAGE_COUNT] = {0.0f};
    float current_samples[APP_TASK_HOME_AVERAGE_COUNT] = {0.0f};
    float power_samples[APP_TASK_HOME_AVERAGE_COUNT] = {0.0f};
    uint8_t sample_index = 0U;
    uint8_t sample_count = 0U;
    uint8_t initial_samples_to_skip = 3U;

    (void)argument;
    osDelay(500U);
    INA226_Init();

    for (;;)
    {
        AppHealth_Heartbeat(APP_HEALTH_TASK_HOME_SENSOR);
        if (INA226_ReadData(&measurement) == 0U)
        {
            float average_voltage;
            float average_current;
            float average_power;
            float soc;

            if (initial_samples_to_skip != 0U)
            {
                initial_samples_to_skip--;
                osDelay(200U);
                continue;
            }

            voltage_samples[sample_index] = measurement.bus_voltage;
            current_samples[sample_index] = measurement.current;
            power_samples[sample_index] = measurement.power;
            sample_index = (uint8_t)((sample_index + 1U) %
                                     APP_TASK_HOME_AVERAGE_COUNT);
            if (sample_count < APP_TASK_HOME_AVERAGE_COUNT)
            {
                sample_count++;
            }

            average_voltage = AppTasks_Average(voltage_samples,
                                                sample_count);
            average_current = AppTasks_Average(current_samples,
                                                sample_count);
            average_power = AppTasks_Average(power_samples, sample_count);
            soc = SocEstimator_FromVoltage(average_voltage);

            (void)AppState_UpdateHome(average_voltage,
                                      measurement.shunt_voltage,
                                      average_current,
                                      average_power,
                                      soc,
                                      1U);
        }
        else
        {
            if (APP_TASK_VERBOSE_LOG)
            {
                printf("INA226 read FAILED!\r\n");
            }
            (void)AppState_SetHomeOk(0U);
        }

        osDelay(200U);
    }
}

void GasSensorTask(void const *argument)
{
    (void)argument;

    for (;;)
    {
        AppHealth_Heartbeat(APP_HEALTH_TASK_GAS_SENSOR);
        HAL_ADC_Start(&hadc1);
        if (HAL_ADC_PollForConversion(&hadc1, 100U) == HAL_OK)
        {
            uint16_t raw = (uint16_t)HAL_ADC_GetValue(&hadc1);
            float voltage = (float)raw / 4095.0f * 3.3f;

            HAL_ADC_Stop(&hadc1);
            (void)AppState_UpdateMq9(raw, voltage);
        }
        else
        {
            HAL_ADC_Stop(&hadc1);
        }
        osDelay(500U);
    }
}

void LightSensorTask(void const *argument)
{
    float light;

    (void)argument;
    GY30_Init();
    HAL_DAC_Start(&hdac, DAC_CHANNEL_1);

    for (;;)
    {
        AppHealth_Heartbeat(APP_HEALTH_TASK_LIGHT_SENSOR);
        if (GY30_ReadLight(&light) == 0U)
        {
            uint32_t dac_value;

            if (light > 2500.0f)
            {
                light = 2500.0f;
            }
            (void)AppState_UpdateLight(light, 1U);

            if (EnergyService_IsChargingActive() != 0U)
            {
                dac_value = (uint32_t)(light * 4095.0f / 2500.0f);
                if (dac_value > 4095U)
                {
                    dac_value = 4095U;
                }
            }
            else
            {
                dac_value = 0U;
            }
            HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1,
                             DAC_ALIGN_12B_R, dac_value);
        }
        else
        {
            if (APP_TASK_VERBOSE_LOG)
            {
                printf("GY30 read error\r\n");
            }
            (void)AppState_SetLightOk(0U);
        }
        osDelay(500U);
    }
}

void EnergyControlTask(void const *argument)
{
    CAN_CtrlCmd_t received_command;
    CAN_BatteryData_t vehicle;
    AppStateSnapshot_t state;
    EnergyServiceInput_t service_input;
    INA226_Data pv_measurement;
    INA226_Data load_measurement;
    float pv_voltage_samples[APP_TASK_POWER_AVERAGE_COUNT] = {0.0f};
    float pv_current_samples[APP_TASK_POWER_AVERAGE_COUNT] = {0.0f};
    float pv_power_samples[APP_TASK_POWER_AVERAGE_COUNT] = {0.0f};
    float load_voltage_samples[APP_TASK_POWER_AVERAGE_COUNT] = {0.0f};
    float load_current_samples[APP_TASK_POWER_AVERAGE_COUNT] = {0.0f};
    float load_power_samples[APP_TASK_POWER_AVERAGE_COUNT] = {0.0f};
    uint8_t pv_sample_index = 0U;
    uint8_t pv_sample_count = 0U;
    uint8_t pv_read_divider = 0U;
    uint8_t load_sample_index = 0U;
    uint8_t load_sample_count = 0U;
    uint8_t load_read_divider = 0U;
#if APP_DATA_LOG_ENABLE
    uint32_t data_log_tick;
    uint8_t data_header_printed = 0U;
#endif

    (void)argument;
    INA226_PV_Init();
    INA226_3_Init();
    if (CAN_App_Init() != 0U)
    {
        Error_Handler();
    }
#if APP_DATA_LOG_ENABLE
    data_log_tick = HAL_GetTick();
#endif

    for (;;)
    {
        AppHealth_Heartbeat(APP_HEALTH_TASK_ENERGY_CONTROL);
        pv_read_divider++;
        if (pv_read_divider >= 20U)
        {
            pv_read_divider = 0U;
            if (INA226_PV_ReadData(&pv_measurement) == 0U)
            {
                pv_voltage_samples[pv_sample_index] =
                    pv_measurement.bus_voltage;
                pv_current_samples[pv_sample_index] = pv_measurement.current;
                pv_power_samples[pv_sample_index] = pv_measurement.power;
                pv_sample_index = (uint8_t)((pv_sample_index + 1U) %
                                            APP_TASK_POWER_AVERAGE_COUNT);
                if (pv_sample_count < APP_TASK_POWER_AVERAGE_COUNT)
                {
                    pv_sample_count++;
                }
                (void)AppState_UpdatePv(
                    AppTasks_Average(pv_voltage_samples, pv_sample_count),
                    AppTasks_Average(pv_current_samples, pv_sample_count),
                    AppTasks_Average(pv_power_samples, pv_sample_count),
                    1U);
            }
            else
            {
                (void)AppState_SetPvOk(0U);
            }
        }

        load_read_divider++;
        if (load_read_divider >= 20U)
        {
            load_read_divider = 0U;
            if (INA226_3_ReadData(&load_measurement) == 0U)
            {
                load_voltage_samples[load_sample_index] =
                    load_measurement.bus_voltage;
                load_current_samples[load_sample_index] =
                    load_measurement.current;
                load_power_samples[load_sample_index] = load_measurement.power;
                load_sample_index = (uint8_t)((load_sample_index + 1U) %
                                              APP_TASK_POWER_AVERAGE_COUNT);
                if (load_sample_count < APP_TASK_POWER_AVERAGE_COUNT)
                {
                    load_sample_count++;
                }
                (void)AppState_UpdateLoad(
                    AppTasks_Average(load_voltage_samples,
                                     load_sample_count),
                    AppTasks_Average(load_current_samples,
                                     load_sample_count),
                    AppTasks_Average(load_power_samples,
                                     load_sample_count),
                    1U);
            }
            else
            {
                (void)AppState_SetLoadOk(0U);
            }
        }

        if (CAN_App_TryReceiveCommand(&received_command) == 0U)
        {
            if (APP_TASK_VERBOSE_LOG)
            {
                printf("CAN CMD: cmd=%u param=%u\r\n",
                       received_command.cmd, received_command.param);
            }
        }

        if (AppState_GetSnapshot(&state) == 0U)
        {
            CAN_App_GetBatterySnapshot(&vehicle);

            service_input.pv_power_w = state.sensor.pv_power;
            service_input.home_load_power_w = state.sensor.ina3_ok ?
                state.sensor.ina3_power : 0.0f;
            service_input.pv_voltage_v = state.sensor.pv_voltage;
            service_input.home_battery_voltage_v =
                state.sensor.bus_voltage;
            service_input.car_battery_voltage_v = vehicle.voltage;
            service_input.home_soc = state.sensor.soc_pct;
            service_input.car_soc = (float)vehicle.soc_pct;
            service_input.human_soc = state.wearable.valid ?
                state.wearable.bat_pct : -1.0f;
            service_input.lux = state.sensor.lux;
            service_input.sensor_ok = state.sensor.ina226_ok;
            service_input.pv_ok = state.sensor.pv_ok;
            service_input.clock_valid = state.beijing_clock.valid;
            service_input.clock_year = state.beijing_clock.year;
            service_input.clock_month = state.beijing_clock.month;
            service_input.clock_day = state.beijing_clock.day;
            service_input.clock_hour = state.beijing_clock.hour;
            service_input.clock_minute = state.beijing_clock.minute;
            service_input.clock_second = state.beijing_clock.second;
            service_input.clock_tick_ms = state.beijing_clock.tick_ms;
            service_input.prediction = state.prediction;
#if !APP_LSTM_PREDICTION_ENABLE
            service_input.prediction.valid = 0U;
#endif
            EnergyService_Process(&service_input);

#if APP_DATA_LOG_ENABLE
            if (data_header_printed == 0U)
            {
                data_header_printed = 1U;
                printf("DATA,period_ms,real_hour_sin,real_hour_cos,lux,"
                       "pv_v,pv_p,"
                       "home_v,home_soc,load_p,car_soc,human_soc,pvsrc,"
                       "hsrc,rigid,led,fan,qi,hchg,cchg,v2h\r\n");
            }

            if ((HAL_GetTick() - data_log_tick) >= APP_DATA_LOG_PERIOD_MS)
            {
                EnergyLstmInput_t data_input;

                data_log_tick += APP_DATA_LOG_PERIOD_MS;
                EnergyService_BuildLstmInput(&data_input, &service_input);
                printf("DATA,%lu,%.6f,%.6f,%.1f,%.3f,%.3f,%.3f,%.1f,%.3f,"
                       "%.1f,%.1f,%u,%u,%u,%u,%u,%u,%u,%u,%u\r\n",
                       (unsigned long)APP_DATA_LOG_PERIOD_MS,
                       (double)data_input.real_hour_sin,
                       (double)data_input.real_hour_cos,
                       (double)data_input.lux,
                       (double)data_input.pv_v,
                       (double)data_input.pv_p,
                       (double)data_input.home_v,
                       (double)data_input.home_soc,
                       (double)data_input.load_p,
                       (double)data_input.car_soc,
                       (double)data_input.human_soc,
                       data_input.pvsrc,
                       data_input.hsrc,
                       data_input.rigid,
                       data_input.led,
                       data_input.fan,
                       data_input.qi,
                       data_input.hchg,
                       data_input.cchg,
                       data_input.v2h);
            }
#endif
        }

        osDelay(50U);
    }
}

void UiTask(void const *argument)
{
    EnergyLvgl_Task(argument);
}
