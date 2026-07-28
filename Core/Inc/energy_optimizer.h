#ifndef ENERGY_OPTIMIZER_H
#define ENERGY_OPTIMIZER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "energy_types.h"

typedef enum
{
    ENERGY_OPT_MODE_HOLD = 0,
    ENERGY_OPT_MODE_HOME_CHARGE,
    ENERGY_OPT_MODE_CAR_CHARGE,
    ENERGY_OPT_MODE_SAVE
} EnergyOptimizerMode_t;

typedef struct
{
    const EnergyServiceInput_t *energy;
    EnergyOutputState_t current_output;
    uint32_t now_tick_ms;
    uint8_t pv_available;
    uint8_t v2h_active;
    uint8_t allowed_charge_target;
    uint8_t led_requested;
    uint8_t fan_requested;
    uint8_t qi_requested;
} EnergyOptimizerInput_t;

typedef struct
{
    uint32_t sequence;
    uint32_t prediction_tick_ms;
    EnergyOptimizerMode_t mode;
    uint8_t valid;
    uint8_t prediction_used;
    uint8_t charge_target;
    uint8_t force_save_mode;
    float total_score;
    float pv_waste_score;
    float battery_stress_score;
    float load_loss_score;
    float car_deficit_score;
    float switch_score;
    float forecast_pv_w;
    float forecast_load_w;
    float forecast_home_soc;
} EnergyOptimizerDecision_t;

uint8_t EnergyOptimizer_Evaluate(const EnergyOptimizerInput_t *input,
                                 EnergyOptimizerDecision_t *decision);

#ifdef __cplusplus
}
#endif

#endif /* ENERGY_OPTIMIZER_H */
