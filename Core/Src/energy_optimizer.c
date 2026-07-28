#include "energy_optimizer.h"

#include "app_config.h"

#include <float.h>
#include <math.h>
#include <string.h>

#define ENERGY_OPT_PRED_VALID_MS              180000U
#define ENERGY_OPT_PV_REFERENCE_W                8.50f
#define ENERGY_OPT_LOAD_REFERENCE_W              4.00f
#define ENERGY_OPT_MIN_PV_DENOMINATOR_W          0.25f
#define ENERGY_OPT_CHARGE_ABSORB_W                4.50f
#define ENERGY_OPT_HOME_CHARGE_GAIN_PCT           3.00f
#define ENERGY_OPT_CAR_CHARGE_GAIN_PCT            0.10f
#define ENERGY_OPT_SAVE_HOME_GAIN_PCT              1.00f
#define ENERGY_OPT_V2H_CAR_DROP_PCT                4.50f
#define ENERGY_OPT_HOME_RESERVE_PCT               20.00f
#define ENERGY_OPT_HOME_SAVE_PCT                  30.00f
#define ENERGY_OPT_HOME_CAR_START_PCT             90.00f
#define ENERGY_OPT_HOME_CAR_HOLD_PCT              85.00f
#define ENERGY_OPT_CAR_TARGET_PCT                 90.00f
#define ENERGY_OPT_CAR_RESERVE_PCT                20.00f
#define ENERGY_OPT_CAR_AVAILABLE_V                 1.00f
#define ENERGY_OPT_SOC_PRED_ERROR_PCT              3.32f
#define ENERGY_OPT_FORECAST_SURPLUS_MARGIN_W       0.30f

typedef struct
{
    EnergyOptimizerMode_t mode;
    uint8_t charge_target;
    uint8_t force_save_mode;
} EnergyOptimizerCandidate_t;

static const EnergyOptimizerCandidate_t g_candidates[] = {
    {ENERGY_OPT_MODE_HOLD, 0U, 0U},
    {ENERGY_OPT_MODE_HOME_CHARGE, 1U, 0U},
    {ENERGY_OPT_MODE_CAR_CHARGE, 2U, 0U},
    {ENERGY_OPT_MODE_SAVE, 0U, 1U}
};

static float EnergyOptimizer_Clamp(float value,
                                   float min_value,
                                   float max_value)
{
    if (value < min_value)
    {
        return min_value;
    }
    if (value > max_value)
    {
        return max_value;
    }
    return value;
}

static float EnergyOptimizer_Blend(float prediction,
                                   float measurement,
                                   float trust)
{
    return trust * prediction + (1.0f - trust) * measurement;
}

static uint8_t EnergyOptimizer_CandidateIsLegal(
    const EnergyOptimizerInput_t *input,
    const EnergyOptimizerCandidate_t *candidate,
    float forecast_pv_w,
    float forecast_load_w,
    float forecast_home_soc)
{
    const EnergyServiceInput_t *energy = input->energy;
    float forecast_surplus_w = forecast_pv_w - forecast_load_w;

    /* The charge FSM owns target transitions and hysteresis. */
    if (candidate->charge_target != 0U &&
        candidate->charge_target != input->allowed_charge_target)
    {
        return 0U;
    }

    if (candidate->charge_target != 0U &&
        (!input->pv_available ||
         forecast_surplus_w < ENERGY_OPT_FORECAST_SURPLUS_MARGIN_W))
    {
        return 0U;
    }

    if (candidate->charge_target == 2U)
    {
        if (energy->car_battery_voltage_v <= ENERGY_OPT_CAR_AVAILABLE_V)
        {
            return 0U;
        }
    }

    if (candidate->force_save_mode &&
        !(forecast_pv_w < forecast_load_w &&
          forecast_home_soc < ENERGY_OPT_HOME_SAVE_PCT))
    {
        return 0U;
    }

    return 1U;
}

static void EnergyOptimizer_ScoreCandidate(
    const EnergyOptimizerInput_t *input,
    const EnergyOptimizerCandidate_t *candidate,
    float forecast_pv_w,
    float forecast_load_w,
    float forecast_home_soc,
    EnergyOptimizerDecision_t *score)
{
    const EnergyServiceInput_t *energy = input->energy;
    float home_soc = forecast_home_soc;
    float car_soc = energy->car_soc;
    float pv_surplus_w;
    float absorbed_w = 0.0f;
    float wasted_w;
    float home_low_penalty;
    float home_high_penalty;
    float car_low_penalty;
    float car_high_penalty;
    float boundary_penalty;
    float car_charge_reserve_penalty = 0.0f;
    float throughput_penalty;
    float flexible_request_weight = 0.0f;
    float load_denominator;
    float switches = 0.0f;
    float weight_sum;
    uint8_t future_source_available;

    memset(score, 0, sizeof(*score));
    score->mode = candidate->mode;
    score->charge_target = candidate->charge_target;
    score->force_save_mode = candidate->force_save_mode;
    score->forecast_pv_w = forecast_pv_w;
    score->forecast_load_w = forecast_load_w;

    home_soc += ((candidate->charge_target == 1U) ? 1.0f : 0.0f) *
                    ENERGY_OPT_HOME_CHARGE_GAIN_PCT -
                ((input->current_output.home_charge != 0U) ? 1.0f : 0.0f) *
                    ENERGY_OPT_HOME_CHARGE_GAIN_PCT;
    if (candidate->force_save_mode)
    {
        home_soc += ENERGY_OPT_SAVE_HOME_GAIN_PCT;
    }
    home_soc = EnergyOptimizer_Clamp(home_soc, 0.0f, 100.0f);
    score->forecast_home_soc = home_soc;

    if (candidate->charge_target == 2U)
    {
        car_soc += ENERGY_OPT_CAR_CHARGE_GAIN_PCT;
    }
    if (input->v2h_active)
    {
        car_soc -= ENERGY_OPT_V2H_CAR_DROP_PCT;
    }
    car_soc = EnergyOptimizer_Clamp(car_soc, 0.0f, 100.0f);

    pv_surplus_w = fmaxf(forecast_pv_w - forecast_load_w, 0.0f);
    if (candidate->charge_target != 0U)
    {
        absorbed_w = fminf(pv_surplus_w, ENERGY_OPT_CHARGE_ABSORB_W);
    }
    wasted_w = fmaxf(pv_surplus_w - absorbed_w, 0.0f);
    if (forecast_pv_w > ENERGY_OPT_MIN_PV_DENOMINATOR_W)
    {
        score->pv_waste_score = EnergyOptimizer_Clamp(
            wasted_w / fmaxf(forecast_pv_w, ENERGY_OPT_PV_REFERENCE_W),
            0.0f,
            1.0f);
    }

    home_low_penalty = EnergyOptimizer_Clamp(
        (ENERGY_OPT_HOME_SAVE_PCT - home_soc) /
            (ENERGY_OPT_HOME_SAVE_PCT - ENERGY_OPT_HOME_RESERVE_PCT),
        0.0f,
        1.0f);
    home_high_penalty = EnergyOptimizer_Clamp(
        (home_soc - ENERGY_OPT_HOME_CAR_START_PCT) /
            (100.0f - ENERGY_OPT_HOME_CAR_START_PCT),
        0.0f,
        1.0f);
    car_low_penalty = EnergyOptimizer_Clamp(
        (30.0f - car_soc) / (30.0f - ENERGY_OPT_CAR_RESERVE_PCT),
        0.0f,
        1.0f);
    car_high_penalty = EnergyOptimizer_Clamp(
        (car_soc - ENERGY_OPT_CAR_TARGET_PCT) /
            (100.0f - ENERGY_OPT_CAR_TARGET_PCT),
        0.0f,
        1.0f);
    boundary_penalty = EnergyOptimizer_Clamp(
        0.55f * fmaxf(home_low_penalty, home_high_penalty) +
        0.45f * fmaxf(car_low_penalty, car_high_penalty),
        0.0f,
        1.0f);
    if (candidate->charge_target == 2U)
    {
        car_charge_reserve_penalty = EnergyOptimizer_Clamp(
            (ENERGY_OPT_HOME_CAR_HOLD_PCT +
             ENERGY_OPT_SOC_PRED_ERROR_PCT - home_soc) /
                ENERGY_OPT_SOC_PRED_ERROR_PCT,
            0.0f,
            1.0f);
        boundary_penalty = fmaxf(boundary_penalty,
                                 car_charge_reserve_penalty);
    }
    throughput_penalty = candidate->charge_target != 0U ? 0.25f : 0.0f;
    if (input->v2h_active)
    {
        throughput_penalty += 0.50f;
    }
    score->battery_stress_score = EnergyOptimizer_Clamp(
        0.70f * boundary_penalty + 0.30f * throughput_penalty,
        0.0f,
        1.0f);

    if (input->led_requested)
    {
        flexible_request_weight += 0.25f;
    }
    if (input->fan_requested)
    {
        flexible_request_weight += 0.35f;
    }
    if (input->qi_requested)
    {
        flexible_request_weight += 0.20f;
    }
    load_denominator = 1.0f + flexible_request_weight;
    future_source_available =
        (forecast_pv_w >= forecast_load_w ||
         home_soc > ENERGY_OPT_HOME_RESERVE_PCT ||
         (input->v2h_active && car_soc > ENERGY_OPT_CAR_RESERVE_PCT)) ?
        1U : 0U;
    if (!future_source_available)
    {
        score->load_loss_score = 1.0f;
    }
    else if (candidate->force_save_mode)
    {
        score->load_loss_score = EnergyOptimizer_Clamp(
            flexible_request_weight / load_denominator,
            0.0f,
            1.0f);
    }

    score->car_deficit_score = EnergyOptimizer_Clamp(
        (ENERGY_OPT_CAR_TARGET_PCT - car_soc) /
            (ENERGY_OPT_CAR_TARGET_PCT - ENERGY_OPT_CAR_RESERVE_PCT),
        0.0f,
        1.0f);

    if ((candidate->charge_target == 1U) !=
        (input->current_output.home_charge != 0U))
    {
        switches += 1.0f;
    }
    if ((candidate->charge_target == 2U) !=
        (input->current_output.car_charge != 0U))
    {
        switches += 1.0f;
    }
    if (candidate->force_save_mode)
    {
        switches += input->current_output.led ? 1.0f : 0.0f;
        switches += input->current_output.fan ? 1.0f : 0.0f;
        switches += input->current_output.qi ? 1.0f : 0.0f;
    }
    score->switch_score = EnergyOptimizer_Clamp(switches / 5.0f,
                                                 0.0f,
                                                 1.0f);

    weight_sum =
        APP_OPTIMIZER_WEIGHT_PV +
        APP_OPTIMIZER_WEIGHT_BATTERY +
        APP_OPTIMIZER_WEIGHT_LOAD +
        APP_OPTIMIZER_WEIGHT_CAR +
        APP_OPTIMIZER_WEIGHT_SWITCH;
    score->total_score =
        APP_OPTIMIZER_WEIGHT_PV * score->pv_waste_score +
        APP_OPTIMIZER_WEIGHT_BATTERY * score->battery_stress_score +
        APP_OPTIMIZER_WEIGHT_LOAD * score->load_loss_score +
        APP_OPTIMIZER_WEIGHT_CAR * score->car_deficit_score +
        APP_OPTIMIZER_WEIGHT_SWITCH * score->switch_score;
    if (weight_sum > 0.0f)
    {
        score->total_score /= weight_sum;
    }
}

uint8_t EnergyOptimizer_Evaluate(const EnergyOptimizerInput_t *input,
                                 EnergyOptimizerDecision_t *decision)
{
    const EnergyServiceInput_t *energy;
    EnergyOptimizerDecision_t candidate_score;
    float forecast_pv_w;
    float forecast_load_w;
    float forecast_home_soc;
    float best_score = FLT_MAX;
    uint32_t index;
    uint8_t found = 0U;

    if (input == NULL || decision == NULL || input->energy == NULL)
    {
        return 1U;
    }

    energy = input->energy;
    memset(decision, 0, sizeof(*decision));
    if (!energy->sensor_ok || !energy->prediction.valid ||
        (input->now_tick_ms - energy->prediction.tick_ms) >
            ENERGY_OPT_PRED_VALID_MS)
    {
        return 1U;
    }

    forecast_pv_w = EnergyOptimizer_Clamp(
        EnergyOptimizer_Blend(energy->prediction.future_pv_p,
                              energy->pv_power_w,
                              APP_OPTIMIZER_LSTM_PV_TRUST),
        0.0f,
        ENERGY_OPT_PV_REFERENCE_W);
    forecast_load_w = EnergyOptimizer_Clamp(
        EnergyOptimizer_Blend(energy->prediction.future_load_p,
                              energy->home_load_power_w,
                              APP_OPTIMIZER_LSTM_LOAD_TRUST),
        0.0f,
        ENERGY_OPT_LOAD_REFERENCE_W);
    forecast_home_soc = EnergyOptimizer_Clamp(
        EnergyOptimizer_Blend(energy->prediction.future_home_soc,
                              energy->home_soc,
                              APP_OPTIMIZER_LSTM_SOC_TRUST),
        0.0f,
        100.0f);

    for (index = 0U;
         index < (sizeof(g_candidates) / sizeof(g_candidates[0]));
         index++)
    {
        if (!EnergyOptimizer_CandidateIsLegal(input,
                                               &g_candidates[index],
                                               forecast_pv_w,
                                               forecast_load_w,
                                               forecast_home_soc))
        {
            continue;
        }

        EnergyOptimizer_ScoreCandidate(input,
                                       &g_candidates[index],
                                       forecast_pv_w,
                                       forecast_load_w,
                                       forecast_home_soc,
                                       &candidate_score);
        if (!found || candidate_score.total_score < best_score)
        {
            *decision = candidate_score;
            best_score = candidate_score.total_score;
            found = 1U;
        }
    }

    if (!found)
    {
        return 1U;
    }

    decision->valid = 1U;
    decision->prediction_used = 1U;
    decision->prediction_tick_ms = energy->prediction.tick_ms;
    return 0U;
}
