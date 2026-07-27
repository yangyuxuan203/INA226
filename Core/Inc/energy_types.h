#ifndef ENERGY_TYPES_H
#define ENERGY_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct
{
    float bat_v;
    float bat_pct;
    uint16_t hr;
    uint16_t spo2;
    uint8_t state;
    uint8_t valid;
} ESP32S3_Data_t;

typedef struct
{
    float real_hour_sin;
    float real_hour_cos;
    float lux;
    float pv_v;
    float pv_p;
    float home_v;
    float home_soc;
    float load_p;
    float car_soc;
    float human_soc;
    uint8_t pvsrc;
    uint8_t hsrc;
    uint8_t rigid;
    uint8_t led;
    uint8_t fan;
    uint8_t qi;
    uint8_t hchg;
    uint8_t cchg;
    uint8_t v2h;
} EnergyLstmInput_t;

typedef struct
{
    float future_pv_p;
    float future_load_p;
    float future_home_soc;
    uint32_t tick_ms;
    uint8_t valid;
} EnergyLstmPrediction_t;

typedef struct
{
    int8_t fan;
    int8_t led;
    int8_t load;
    int8_t qi;
} EnergyActuatorCommand_t;

typedef struct
{
    uint8_t pv_source;
    uint8_t home_source;
    uint8_t rigid;
    uint8_t led;
    uint8_t fan;
    uint8_t qi;
    uint8_t home_charge;
    uint8_t car_charge;
    uint8_t v2h;
} EnergyOutputState_t;

typedef struct
{
    float pv_power_w;
    float home_load_power_w;
    float pv_voltage_v;
    float home_battery_voltage_v;
    float car_battery_voltage_v;
    float home_soc;
    float car_soc;
    float human_soc;
    float lux;
    uint8_t sensor_ok;
    uint8_t pv_ok;

    uint8_t clock_valid;
    uint16_t clock_year;
    uint8_t clock_month;
    uint8_t clock_day;
    uint8_t clock_hour;
    uint8_t clock_minute;
    uint8_t clock_second;
    uint32_t clock_tick_ms;

    EnergyLstmPrediction_t prediction;
} EnergyServiceInput_t;

#ifdef __cplusplus
}
#endif

#endif /* ENERGY_TYPES_H */
