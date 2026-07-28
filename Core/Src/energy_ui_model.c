#include "energy_lvgl_ui.h"

#include "app_config.h"
#include "app_state.h"
#include "can_app.h"
#include "energy_service.h"
#include "main.h"

#include <string.h>

#define ENERGY_UI_PREDICTION_VALID_MS 180000U

void EnergyLvgl_GetSnapshot(EnergyLvglSnapshot_t *snapshot)
{
    AppStateSnapshot_t state;
    EnergyOutputState_t outputs;
    CAN_BatteryData_t vehicle;

    if (snapshot == NULL)
    {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    if (AppState_GetSnapshot(&state) != 0U)
    {
        return;
    }

    EnergyService_GetOutputState(&outputs);
    CAN_App_GetBatterySnapshot(&vehicle);

    strncpy(snapshot->beijing_time, state.beijing_time,
            sizeof(snapshot->beijing_time) - 1U);
    snapshot->time_valid = state.beijing_clock.valid;
    snapshot->onenet_online = state.onenet_online;

    snapshot->home_v = state.sensor.bus_voltage;
    snapshot->home_i = state.sensor.current;
    snapshot->home_p = state.sensor.power;
    snapshot->home_soc = state.sensor.soc_pct;
    snapshot->load_v = state.sensor.ina3_voltage;
    snapshot->load_i = state.sensor.ina3_current;
    snapshot->load_p = state.sensor.ina3_power;
    snapshot->lux = state.sensor.lux;
    snapshot->pv_v = state.sensor.pv_voltage;
    snapshot->pv_i = state.sensor.pv_current;
    snapshot->pv_p = state.sensor.pv_power;
    snapshot->home_ok = state.sensor.ina226_ok;
    snapshot->load_ok = state.sensor.ina3_ok;
    snapshot->pv_ok = state.sensor.pv_ok;

    snapshot->pvsrc = outputs.pv_source;
    snapshot->hsrc = outputs.home_source;
    snapshot->rigid = outputs.rigid;
    snapshot->led = outputs.led;
    snapshot->fan = outputs.fan;
    snapshot->qi = outputs.qi;
    snapshot->hchg = outputs.home_charge;
    snapshot->cchg = outputs.car_charge;
    snapshot->v2h = outputs.v2h;

    snapshot->car_v = vehicle.voltage;
    snapshot->car_i_ma = vehicle.current;
    snapshot->car_temp = vehicle.temperature;
    snapshot->car_soc = (uint8_t)(vehicle.soc_pct + 0.5f);
    snapshot->car_status = (uint8_t)vehicle.status;

    snapshot->human_valid = state.wearable.valid;
    snapshot->human_v =
        state.wearable.valid ? state.wearable.bat_v : 0.0f;
    snapshot->human_soc =
        state.wearable.valid ? state.wearable.bat_pct : 0.0f;
    snapshot->human_hr =
        state.wearable.valid ? state.wearable.hr : 0U;
    snapshot->human_spo2 =
        state.wearable.valid ? state.wearable.spo2 : 0U;
    snapshot->human_state =
        state.wearable.valid ? state.wearable.state : 0U;

    snapshot->ai_valid =
        APP_LSTM_PREDICTION_ENABLE != 0U &&
        state.prediction.valid != 0U &&
        (HAL_GetTick() - state.prediction.tick_ms) <=
            ENERGY_UI_PREDICTION_VALID_MS ? 1U : 0U;
    snapshot->ai_pv_p = snapshot->ai_valid ?
        state.prediction.future_pv_p : 0.0f;
    snapshot->ai_load_p = snapshot->ai_valid ?
        state.prediction.future_load_p : 0.0f;
    snapshot->ai_home_soc = snapshot->ai_valid ?
        state.prediction.future_home_soc : 0.0f;
    snapshot->ai_raw_home_soc = snapshot->ai_valid ?
        state.prediction_raw_home_soc : 0.0f;
}
