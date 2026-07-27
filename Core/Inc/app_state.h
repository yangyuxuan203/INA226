#ifndef APP_STATE_H
#define APP_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "energy_types.h"
#include <stdint.h>

#define APP_STATE_BEIJING_TIME_LENGTH 32U

typedef struct
{
    float bus_voltage;
    float shunt_voltage;
    float current;
    float power;
    uint16_t mq9_adc;
    float mq9_voltage;
    float lux;
    uint8_t ina226_ok;
    uint8_t gy30_ok;
    float soc_pct;
    float pv_voltage;
    float pv_current;
    float pv_power;
    uint8_t pv_ok;
    float ina3_voltage;
    float ina3_current;
    float ina3_power;
    uint8_t ina3_ok;
} SensorData_t;

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

typedef struct
{
    SensorData_t sensor;
    ESP32S3_Data_t wearable;
    EnergyLstmPrediction_t prediction;
    float prediction_raw_home_soc;
    BeijingClock_t beijing_clock;
    char beijing_time[APP_STATE_BEIJING_TIME_LENGTH];
    uint8_t onenet_online;
} AppStateSnapshot_t;

uint8_t AppState_Init(void);

uint8_t AppState_UpdateHome(float bus_voltage, float shunt_voltage,
                            float current, float power, float soc_pct,
                            uint8_t ok);
uint8_t AppState_SetHomeOk(uint8_t ok);
uint8_t AppState_UpdateMq9(uint16_t adc, float voltage);
uint8_t AppState_UpdateLight(float lux, uint8_t ok);
uint8_t AppState_SetLightOk(uint8_t ok);
uint8_t AppState_UpdatePv(float voltage, float current, float power,
                          uint8_t ok);
uint8_t AppState_SetPvOk(uint8_t ok);
uint8_t AppState_UpdateLoad(float voltage, float current, float power,
                            uint8_t ok);
uint8_t AppState_SetLoadOk(uint8_t ok);
uint8_t AppState_GetSensor(SensorData_t *sensor);

uint8_t AppState_SetWearable(const ESP32S3_Data_t *wearable);
uint8_t AppState_GetWearable(ESP32S3_Data_t *wearable);
uint8_t AppState_SetPrediction(const EnergyLstmPrediction_t *prediction,
                               float raw_home_soc);
uint8_t AppState_GetPrediction(EnergyLstmPrediction_t *prediction,
                               float *raw_home_soc);

uint8_t AppState_SetBeijingTime(const BeijingClock_t *clock,
                                const char *display_text);
uint8_t AppState_GetBeijingTime(BeijingClock_t *clock,
                                char *display_text,
                                uint32_t display_text_size);
uint8_t AppState_SetOneNETOnline(uint8_t online);
uint8_t AppState_GetOneNETOnline(uint8_t *online);

uint8_t AppState_GetSnapshot(AppStateSnapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* APP_STATE_H */
