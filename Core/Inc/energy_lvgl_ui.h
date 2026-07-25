#ifndef ENERGY_LVGL_UI_H
#define ENERGY_LVGL_UI_H

#include <stdint.h>

typedef struct
{
    char beijing_time[32];
    uint8_t time_valid;
    uint8_t onenet_online;

    float home_v;
    float home_i;
    float home_p;
    float home_soc;
    float load_v;
    float load_i;
    float load_p;
    float lux;
    float pv_v;
    float pv_i;
    float pv_p;
    uint8_t home_ok;
    uint8_t load_ok;
    uint8_t pv_ok;

    uint8_t pvsrc;
    uint8_t hsrc;
    uint8_t rigid;
    uint8_t led;
    uint8_t fan;
    uint8_t qi;
    uint8_t hchg;
    uint8_t cchg;
    uint8_t v2h;

    float car_v;
    float car_i_ma;
    float car_temp;
    uint8_t car_soc;
    uint8_t car_status;

    uint8_t human_valid;
    float human_v;
    float human_soc;
    uint16_t human_hr;
    uint16_t human_spo2;
    uint8_t human_state;

    uint8_t ai_valid;
    float ai_pv_p;
    float ai_load_p;
    float ai_home_soc;
    float ai_raw_home_soc;
} EnergyLvglSnapshot_t;

void EnergyLvgl_GetSnapshot(EnergyLvglSnapshot_t *snapshot);
void EnergyLvgl_Task(void const *argument);

#endif
