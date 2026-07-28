#include "energy_service.h"

#include "app_config.h"
#include "energy_io.h"
#include "energy_optimizer.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "main.h"
#include <math.h>
#include <string.h>

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
#define HOME_SOC_CAR_CHARGE_START_PCT 90.0f
#define HOME_SOC_CAR_CHARGE_STOP_PCT  85.0f
#define CAR_SOC_CHARGE_STOP_PCT        90.0f
#define CAR_SOC_CHARGE_RESTART_PCT     85.0f
#define HOME_SOC_V2H_START_PCT  20.0f
#define HOME_SOC_V2H_STOP_PCT   35.0f
#define CAR_SOC_V2H_START_PCT   30.0f
#define CAR_SOC_V2H_STOP_PCT    20.0f
#define HUMAN_SOC_QI_REQ_PCT    30.0f
#define NIGHT_LUX_THRESHOLD     80.0f
#define DAY_LUX_THRESHOLD       300.0f
#define HUMAN_SOC_QI_STOP_PCT   80.0f
#define KEY_DEBOUNCE_MS         50U

#define CAN_CAR_CHARGE_REPORT_MS 1000U
#define PV_PROBE_CHARGE_MS      10000U
#define PV_PROBE_SETTLE_MS      5000U
#define PV_ACTIVE_STABILIZE_MS  2000U
#define CHARGE_DROP_CONFIRM_MS  3000U
#define CHARGE_RESTART_HOLD_MS 10000U
#define CHARGE_START_CONFIRM_MS 2000U
#define CAR_SOC_JUMP_REJECT_PCT     3.0f
#define CAR_SOC_JUMP_CONFIRM_MS  5000U
#define CHARGE_SOC_RESTART_CONFIRM_MS 10000U

#define ENERGY_LSTM_SOC_MAX_DROP_PCT 3.0f
#define ENERGY_LSTM_SOC_MAX_RISE_PCT 3.0f
#define ENERGY_OPTIMIZER_DECISION_VALID_MS 180000U
#define ENERGY_PI_F 3.14159265358979323846f

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

typedef enum
{
    CHARGE_FSM_HOME_PRIORITY = 0,
    CHARGE_FSM_CAR_PRIORITY,
    CHARGE_FSM_CAR_FULL_HOME_RECOVERY,
    CHARGE_FSM_FULL_HOLD
} ChargeFsmState_t;

typedef struct
{
    KeyFsmState_t state;
    uint32_t tick;
} KeyDebounce_t;

typedef struct
{
    PvFsmState_t pv_state;
    uint32_t pv_state_tick;
    uint8_t pv_bad_count;
    uint8_t led_manual_override;
    uint8_t led_manual_on;
    uint16_t led_manual_year;
    uint8_t led_manual_month;
    uint8_t led_manual_day;
    uint8_t led_manual_period;
    uint8_t fan_user_req;
    uint32_t fan_local_override_tick;
    uint8_t qi_user_req;
    uint8_t qi_active;
    uint8_t rigid_user_req;
    uint8_t light_user_req;
    uint8_t prev_led_out;
    uint8_t prev_fan_out;
    uint8_t pv_charge_probed;
    uint8_t pv_probe_target;
    float pv_probe_threshold;
    uint32_t pv_probe_start_tick;
    uint8_t pv_charging_active;
    ChargeFsmState_t charge_state;
    uint8_t car_charge_reported;
    uint32_t car_charge_report_tick;
    uint8_t charge_start_candidate;
    uint8_t charge_drop_pending;
    uint32_t charge_start_candidate_tick;
    uint32_t charge_drop_tick;
    uint32_t charge_last_off_tick;
    float car_soc_control;
    float car_soc_candidate;
    uint32_t car_soc_candidate_tick;
    uint8_t car_soc_control_valid;
    uint8_t car_soc_candidate_valid;
    uint8_t car_soc_restart_pending;
    uint32_t car_soc_restart_tick;
    EnergyOptimizerDecision_t optimizer_decision;
    uint32_t optimizer_last_prediction_tick;
    uint32_t optimizer_sequence;
    uint8_t optimizer_has_prediction;
    uint8_t optimizer_fsm_target;
    KeyDebounce_t led_key_fsm;
    KeyDebounce_t fan_key_fsm;
    KeyDebounce_t qi_key_fsm;
    EnergyActuatorCommand_t pending_command;
    uint8_t pending_command_valid;
    volatile uint8_t charging_active;
    volatile uint8_t v2h_active;
} EnergyServiceContext_t;

static EnergyServiceContext_t g_energy = {
    .pv_state = PV_FSM_IDLE,
    .led_manual_period = 0xFFU,
    .rigid_user_req = 1U,
    .light_user_req = 1U,
    .prev_led_out = 0xFFU,
    .prev_fan_out = 0xFFU,
    .pending_command = {-1, -1, -1, -1},
    .charging_active = 1U
};

static QueueHandle_t g_command_queue = NULL;
static StaticQueue_t g_command_queue_buffer;
static uint8_t g_command_queue_storage[
    ENERGY_COMMAND_QUEUE_DEPTH * sizeof(EnergyActuatorCommand_t)];

static float Energy_ClampFloat(float value, float min_value, float max_value)
{
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static uint8_t Energy_GetLedPeriod(uint8_t hour)
{
    if (hour < 6U)
    {
        return 0U;
    }
    if (hour < 18U)
    {
        return 1U;
    }
    if (hour < 22U)
    {
        return 2U;
    }
    return 3U;
}

static uint8_t Energy_GetPressedEvent(KeyDebounce_t *key,
                                      uint8_t raw_pressed,
                                      uint32_t now_tick)
{
    uint8_t event = 0U;

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
                event = 1U;
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

static float Energy_UpdateCarSocControl(const EnergyServiceInput_t *input,
                                        uint32_t now_tick)
{
    float raw_soc;

    if (!isfinite(input->car_soc))
    {
        return g_energy.car_soc_control_valid ?
            g_energy.car_soc_control : 0.0f;
    }

    if (input->car_battery_voltage_v <= 1.0f)
    {
        g_energy.car_soc_control_valid = 0U;
        g_energy.car_soc_candidate_valid = 0U;
        return 0.0f;
    }

    raw_soc = Energy_ClampFloat(input->car_soc, 0.0f, 100.0f);
    if (!g_energy.car_soc_control_valid)
    {
        g_energy.car_soc_control = raw_soc;
        g_energy.car_soc_control_valid = 1U;
        return raw_soc;
    }

    /* A full indication must stop charging immediately. Downward voltage-SOC
       steps are qualified because removing charge voltage can cause a large
       apparent SOC drop without real battery discharge. */
    if (raw_soc >= CAR_SOC_CHARGE_STOP_PCT)
    {
        g_energy.car_soc_control = raw_soc;
        g_energy.car_soc_candidate_valid = 0U;
        return g_energy.car_soc_control;
    }

    if (fabsf(raw_soc - g_energy.car_soc_control) <=
        CAR_SOC_JUMP_REJECT_PCT)
    {
        g_energy.car_soc_control = raw_soc;
        g_energy.car_soc_candidate_valid = 0U;
        return g_energy.car_soc_control;
    }

    if (!g_energy.car_soc_candidate_valid ||
        fabsf(raw_soc - g_energy.car_soc_candidate) >
            CAR_SOC_JUMP_REJECT_PCT)
    {
        g_energy.car_soc_candidate = raw_soc;
        g_energy.car_soc_candidate_tick = now_tick;
        g_energy.car_soc_candidate_valid = 1U;
    }
    else if ((now_tick - g_energy.car_soc_candidate_tick) >=
             CAR_SOC_JUMP_CONFIRM_MS)
    {
        g_energy.car_soc_control = raw_soc;
        g_energy.car_soc_candidate_valid = 0U;
    }

    return g_energy.car_soc_control;
}

static uint8_t Energy_ConditionHeld(uint8_t condition,
                                    uint8_t *pending,
                                    uint32_t *start_tick,
                                    uint32_t now_tick,
                                    uint32_t confirm_ms)
{
    if (!condition)
    {
        *pending = 0U;
        return 0U;
    }

    if (!*pending)
    {
        *pending = 1U;
        *start_tick = now_tick;
        return 0U;
    }

    return (now_tick - *start_tick) >= confirm_ms ? 1U : 0U;
}

static void Energy_UpdateChargeState(const EnergyServiceInput_t *input,
                                     float car_soc_control,
                                     uint32_t now_tick)
{
    uint8_t car_available =
        input->car_battery_voltage_v > 1.0f ? 1U : 0U;
    uint8_t car_restart_ready = Energy_ConditionHeld(
        car_available && input->car_discharging &&
            car_soc_control <= CAR_SOC_CHARGE_RESTART_PCT,
        &g_energy.car_soc_restart_pending,
        &g_energy.car_soc_restart_tick,
        now_tick,
        CHARGE_SOC_RESTART_CONFIRM_MS);

    switch (g_energy.charge_state)
    {
        case CHARGE_FSM_HOME_PRIORITY:
            if (input->home_soc >= HOME_SOC_CAR_CHARGE_START_PCT)
            {
                if (car_available &&
                    car_soc_control < CAR_SOC_CHARGE_STOP_PCT)
                {
                    g_energy.charge_state = CHARGE_FSM_CAR_PRIORITY;
                }
                else
                {
                    g_energy.charge_state = CHARGE_FSM_FULL_HOLD;
                }
            }
            break;

        case CHARGE_FSM_CAR_PRIORITY:
            if (!car_available)
            {
                g_energy.charge_state =
                    input->home_soc < HOME_SOC_CAR_CHARGE_START_PCT ?
                    CHARGE_FSM_HOME_PRIORITY : CHARGE_FSM_FULL_HOLD;
            }
            else if (car_soc_control >= CAR_SOC_CHARGE_STOP_PCT)
            {
                g_energy.charge_state =
                    input->home_soc < HOME_SOC_CAR_CHARGE_START_PCT ?
                    CHARGE_FSM_CAR_FULL_HOME_RECOVERY :
                    CHARGE_FSM_FULL_HOLD;
            }
            else if (input->home_soc <= HOME_SOC_CAR_CHARGE_STOP_PCT)
            {
                g_energy.charge_state = CHARGE_FSM_HOME_PRIORITY;
            }
            break;

        case CHARGE_FSM_CAR_FULL_HOME_RECOVERY:
            if (!car_available)
            {
                g_energy.charge_state =
                    input->home_soc < HOME_SOC_CAR_CHARGE_START_PCT ?
                    CHARGE_FSM_HOME_PRIORITY : CHARGE_FSM_FULL_HOLD;
            }
            else if (car_restart_ready)
            {
                g_energy.charge_state =
                    input->home_soc < HOME_SOC_CAR_CHARGE_START_PCT ?
                    CHARGE_FSM_HOME_PRIORITY : CHARGE_FSM_CAR_PRIORITY;
            }
            else if (input->home_soc >= HOME_SOC_CAR_CHARGE_START_PCT)
            {
                g_energy.charge_state = CHARGE_FSM_FULL_HOLD;
            }
            break;

        case CHARGE_FSM_FULL_HOLD:
        default:
            if (input->home_soc <= HOME_SOC_CAR_CHARGE_STOP_PCT)
            {
                g_energy.charge_state = CHARGE_FSM_HOME_PRIORITY;
            }
            else if (car_restart_ready)
            {
                g_energy.charge_state = CHARGE_FSM_CAR_PRIORITY;
            }
            break;
    }
}

static uint8_t Energy_GetChargeOutputTarget(void)
{
    uint8_t home_on =
        EnergyIo_ReadOutput(ENERGY_OUTPUT_HOME_CHARGE);
    uint8_t car_on =
        EnergyIo_ReadOutput(ENERGY_OUTPUT_CAR_CHARGE);

    if (home_on && car_on)
    {
        return 3U;
    }
    if (home_on)
    {
        return 1U;
    }
    return car_on ? 2U : 0U;
}

static void Energy_RecordChargeOff(uint32_t now_tick)
{
    EnergyIo_DisableChargeOutputs();
    g_energy.charge_last_off_tick = now_tick;
    g_energy.charge_start_candidate = 0U;
    g_energy.charge_drop_pending = 0U;
}

static void Energy_ApplyChargeOutput(
    uint8_t requested_target,
    uint8_t selected_target,
    uint8_t fsm_target,
    const EnergyServiceInput_t *input,
    float car_soc_control,
    uint8_t pv_available,
    uint32_t now_tick)
{
    uint8_t active_target = Energy_GetChargeOutputTarget();
    uint8_t immediate_stop = 0U;
    float battery_voltage_v = 0.0f;

    if (active_target == 1U)
    {
        battery_voltage_v = input->home_battery_voltage_v;
        immediate_stop =
            fsm_target != 1U ||
            input->home_soc >= HOME_SOC_CAR_CHARGE_START_PCT;
    }
    else if (active_target == 2U)
    {
        battery_voltage_v = input->car_battery_voltage_v;
        immediate_stop =
            fsm_target != 2U ||
            input->car_battery_voltage_v <= 1.0f ||
            input->car_soc >= CAR_SOC_CHARGE_STOP_PCT ||
            car_soc_control >= CAR_SOC_CHARGE_STOP_PCT;
    }
    else if (active_target > 2U)
    {
        immediate_stop = 1U;
    }

    if (active_target != 0U &&
        (!input->sensor_ok || !input->pv_ok || !pv_available ||
         input->pv_voltage_v <= battery_voltage_v))
    {
        immediate_stop = 1U;
    }

    if (active_target != 0U)
    {
        g_energy.charge_start_candidate = 0U;
        if (immediate_stop)
        {
            Energy_RecordChargeOff(now_tick);
            return;
        }

        if (requested_target == active_target &&
            selected_target == active_target)
        {
            g_energy.charge_drop_pending = 0U;
            return;
        }

        if (!g_energy.charge_drop_pending)
        {
            g_energy.charge_drop_pending = 1U;
            g_energy.charge_drop_tick = now_tick;
        }
        else if ((now_tick - g_energy.charge_drop_tick) >=
                 CHARGE_DROP_CONFIRM_MS)
        {
            Energy_RecordChargeOff(now_tick);
        }
        return;
    }

    g_energy.charge_drop_pending = 0U;
    if (requested_target == 0U ||
        requested_target != selected_target ||
        requested_target != fsm_target)
    {
        g_energy.charge_start_candidate = 0U;
        return;
    }

    if (g_energy.charge_start_candidate != requested_target)
    {
        g_energy.charge_start_candidate = requested_target;
        g_energy.charge_start_candidate_tick = now_tick;
        return;
    }

    if ((now_tick - g_energy.charge_last_off_tick) <
            CHARGE_RESTART_HOLD_MS ||
        (now_tick - g_energy.charge_start_candidate_tick) <
            CHARGE_START_CONFIRM_MS)
    {
        return;
    }

    EnergyIo_WriteOutput(ENERGY_OUTPUT_HOME_CHARGE,
                         requested_target == 1U ? 1U : 0U);
    EnergyIo_WriteOutput(ENERGY_OUTPUT_CAR_CHARGE,
                         requested_target == 2U ? 1U : 0U);
    g_energy.charge_start_candidate = 0U;
}

static void Energy_ResetContext(void)
{
    memset(&g_energy, 0, sizeof(g_energy));
    g_energy.pv_state = PV_FSM_IDLE;
    g_energy.led_manual_period = 0xFFU;
    g_energy.rigid_user_req = 1U;
    g_energy.light_user_req = 1U;
    g_energy.prev_led_out = 0xFFU;
    g_energy.prev_fan_out = 0xFFU;
    g_energy.pending_command.fan = -1;
    g_energy.pending_command.led = -1;
    g_energy.pending_command.load = -1;
    g_energy.pending_command.qi = -1;
    g_energy.charge_state = CHARGE_FSM_HOME_PRIORITY;
    g_energy.charge_last_off_tick = HAL_GetTick();
    g_energy.charging_active = 1U;
}

uint8_t EnergyService_Init(void)
{
    Energy_ResetContext();
    g_command_queue = xQueueCreateStatic(
        ENERGY_COMMAND_QUEUE_DEPTH,
        sizeof(EnergyActuatorCommand_t),
        g_command_queue_storage,
        &g_command_queue_buffer);
    return g_command_queue == NULL ? 1U : 0U;
}

uint8_t EnergyService_SubmitCommand(const EnergyActuatorCommand_t *command)
{
    if (command == NULL || g_command_queue == NULL)
    {
        return 1U;
    }

    return xQueueSendToBack(g_command_queue, command, 0U) == pdTRUE ? 0U : 1U;
}

static void Energy_DrainCommands(void)
{
    EnergyActuatorCommand_t command;

    while (g_command_queue != NULL &&
           xQueueReceive(g_command_queue, &command, 0U) == pdTRUE)
    {
        if (command.fan >= 0)
            g_energy.pending_command.fan = command.fan;
        if (command.led >= 0)
            g_energy.pending_command.led = command.led;
        if (command.load >= 0)
            g_energy.pending_command.load = command.load;
        if (command.qi >= 0)
            g_energy.pending_command.qi = command.qi;
        g_energy.pending_command_valid = 1U;
    }
}

void EnergyService_Process(const EnergyServiceInput_t *input)
{
    uint32_t now_tick;
    uint8_t pv_start_condition;
    uint8_t pv_source_enabled;
    uint8_t pv_available;
    uint8_t enough_for_pv_load;
    uint8_t home_bat_allowed;
    uint8_t low_energy_mode;
    uint8_t led_period;
    uint8_t led_auto_request;
    float pv_surplus_w;
    float car_soc_control;
    uint8_t charge_target = 0U;
    uint8_t fsm_charge_target = 0U;
    uint8_t requested_charge_target = 0U;
    float charge_target_voltage_v = 0.0f;
    EnergyOutputState_t current_output;
    EnergyOptimizerInput_t optimizer_input;
    EnergyServiceInput_t optimizer_energy;
    uint8_t optimizer_active = 0U;
    uint8_t can_charge_from_pv;
    uint8_t pv_load_priority_ok;
    uint8_t pv_probe_charge = 0U;
    uint8_t pv_can_charge_home;
    uint8_t pv_can_charge_car;
    uint8_t v2h_should_start;
    uint8_t v2h_should_stop;
    EnergyKeyState_t keys;
    uint8_t use_pv_source;
    uint8_t use_home_source;
    uint8_t home_source_available;
    uint8_t car_charge_on;
    uint8_t led_request;
    uint8_t led_out;
    uint8_t fan_out;

    if (input == NULL)
    {
        return;
    }

    now_tick = HAL_GetTick();
    Energy_DrainCommands();

    if (!input->sensor_ok)
    {
        if (g_energy.v2h_active)
        {
            EnergyIo_SendCarV2HCommand(0U);
            g_energy.v2h_active = 0U;
        }
        Energy_RecordChargeOff(now_tick);
        EnergyIo_DisableAllOutputs();
        g_energy.pv_state = PV_FSM_IDLE;
        g_energy.pv_bad_count = 0U;
        g_energy.pv_charge_probed = 0U;
        g_energy.pv_probe_target = 0U;
        g_energy.pv_probe_threshold = 0.0f;
        g_energy.pv_probe_start_tick = 0U;
        g_energy.pv_charging_active = 0U;
        g_energy.charge_state = CHARGE_FSM_HOME_PRIORITY;
        g_energy.charge_last_off_tick = now_tick;
        g_energy.car_soc_control_valid = 0U;
        g_energy.car_soc_candidate_valid = 0U;
        g_energy.car_soc_restart_pending = 0U;
        memset(&g_energy.optimizer_decision, 0,
               sizeof(g_energy.optimizer_decision));
        g_energy.optimizer_has_prediction = 0U;
        if (g_energy.car_charge_reported)
        {
            EnergyIo_SendCarChargeCommand(0U, 0.0f);
            g_energy.car_charge_reported = 0U;
        }
        g_energy.charging_active = 0U;
        return;
    }

    pv_start_condition =
        input->pv_ok &&
        (input->pv_voltage_v > PV_PROBE_MIN_VOLTAGE_V) &&
        ((input->pv_voltage_v > PV_START_VOLTAGE_V) ||
         (input->lux > PV_WAKE_LUX_THRESHOLD));

    switch (g_energy.pv_state)
    {
        case PV_FSM_IDLE:
            g_energy.pv_bad_count = 0U;
            if (g_energy.pv_charge_probed == 1U)
            {
                g_energy.pv_charge_probed = 0U;
            }
            g_energy.pv_probe_start_tick = 0U;
            if (pv_start_condition)
            {
                g_energy.pv_state = PV_FSM_PROBE;
                g_energy.pv_state_tick = now_tick;
            }
            break;

        case PV_FSM_PROBE:
            if (!input->pv_ok || input->pv_voltage_v < PV_STOP_VOLTAGE_V)
            {
                g_energy.pv_state = PV_FSM_COOLDOWN;
                g_energy.pv_state_tick = now_tick;
                g_energy.pv_bad_count = 0U;
            }
            else if ((now_tick - g_energy.pv_state_tick) >= PV_PROBE_HOLD_MS)
            {
                g_energy.pv_state = PV_FSM_RAMP;
                g_energy.pv_state_tick = now_tick;
                g_energy.pv_bad_count = 0U;
            }
            break;

        case PV_FSM_RAMP:
            if (!input->pv_ok || input->pv_voltage_v < PV_STOP_VOLTAGE_V)
            {
                if (g_energy.pv_bad_count < PV_BAD_SAMPLE_LIMIT)
                    g_energy.pv_bad_count++;
                if (g_energy.pv_bad_count >= PV_BAD_SAMPLE_LIMIT)
                {
                    g_energy.pv_state = PV_FSM_COOLDOWN;
                    g_energy.pv_state_tick = now_tick;
                    g_energy.pv_bad_count = 0U;
                }
            }
            else
            {
                g_energy.pv_bad_count = 0U;
                if ((now_tick - g_energy.pv_state_tick) >= PV_RAMP_EVAL_MS)
                {
                    if (input->pv_voltage_v > PV_LOADED_MIN_VOLTAGE_V &&
                        (input->pv_power_w > PV_VALID_POWER_W ||
                         input->pv_voltage_v > PV_START_VOLTAGE_V))
                    {
                        g_energy.pv_state = PV_FSM_ACTIVE;
                        g_energy.pv_state_tick = now_tick;
                    }
                    else
                    {
                        g_energy.pv_state = PV_FSM_COOLDOWN;
                        g_energy.pv_state_tick = now_tick;
                    }
                }
            }
            break;

        case PV_FSM_ACTIVE:
            if (!input->pv_ok || input->pv_voltage_v < PV_STOP_VOLTAGE_V)
            {
                if (g_energy.pv_bad_count < PV_BAD_SAMPLE_LIMIT)
                    g_energy.pv_bad_count++;
                if (g_energy.pv_bad_count >= PV_BAD_SAMPLE_LIMIT)
                {
                    g_energy.pv_state = PV_FSM_COOLDOWN;
                    g_energy.pv_state_tick = now_tick;
                    g_energy.pv_bad_count = 0U;
                }
            }
            else
            {
                g_energy.pv_bad_count = 0U;
            }
            break;

        case PV_FSM_COOLDOWN:
            if ((now_tick - g_energy.pv_state_tick) >= PV_COOLDOWN_MS)
            {
                g_energy.pv_state =
                    pv_start_condition ? PV_FSM_PROBE : PV_FSM_IDLE;
                g_energy.pv_state_tick = now_tick;
                g_energy.pv_bad_count = 0U;
                if (g_energy.pv_charge_probed == 1U)
                {
                    g_energy.pv_charge_probed = 0U;
                }
                g_energy.pv_probe_start_tick = 0U;
            }
            break;

        default:
            g_energy.pv_state = PV_FSM_IDLE;
            g_energy.pv_bad_count = 0U;
            break;
    }

    pv_source_enabled =
        (g_energy.pv_state == PV_FSM_PROBE ||
         g_energy.pv_state == PV_FSM_RAMP ||
         g_energy.pv_state == PV_FSM_ACTIVE) ? 1U : 0U;
    pv_available = g_energy.pv_state == PV_FSM_ACTIVE ? 1U : 0U;
    enough_for_pv_load = input->pv_ok && pv_source_enabled;
    home_bat_allowed = input->home_soc > HOME_SOC_RESERVE_PCT;
    low_energy_mode =
        (!pv_available && input->home_soc < HOME_SOC_SAVE_PCT);
    led_period = Energy_GetLedPeriod(input->clock_hour);
    pv_surplus_w = input->pv_power_w - input->home_load_power_w;

    car_soc_control = Energy_UpdateCarSocControl(input, now_tick);
    Energy_UpdateChargeState(input, car_soc_control, now_tick);

    switch (g_energy.charge_state)
    {
        case CHARGE_FSM_HOME_PRIORITY:
        case CHARGE_FSM_CAR_FULL_HOME_RECOVERY:
            if (input->home_soc < HOME_SOC_CAR_CHARGE_START_PCT)
            {
                charge_target = 1U;
                charge_target_voltage_v = input->home_battery_voltage_v;
            }
            break;

        case CHARGE_FSM_CAR_PRIORITY:
            if (input->car_battery_voltage_v > 1.0f &&
                car_soc_control < CAR_SOC_CHARGE_STOP_PCT)
            {
                charge_target = 2U;
                charge_target_voltage_v = input->car_battery_voltage_v;
            }
            break;

        case CHARGE_FSM_FULL_HOLD:
        default:
            break;
    }
    fsm_charge_target = charge_target;

#if APP_OPTIMIZER_ENABLE
    EnergyIo_GetOutputState(&current_output);
    if (input->prediction.valid &&
        (!g_energy.optimizer_has_prediction ||
         input->prediction.tick_ms !=
             g_energy.optimizer_last_prediction_tick))
    {
        memset(&optimizer_input, 0, sizeof(optimizer_input));
        optimizer_energy = *input;
        optimizer_energy.car_soc = car_soc_control;
        optimizer_input.energy = &optimizer_energy;
        optimizer_input.current_output = current_output;
        optimizer_input.now_tick_ms = now_tick;
        optimizer_input.pv_available = pv_available;
        optimizer_input.v2h_active = g_energy.v2h_active;
        optimizer_input.allowed_charge_target = fsm_charge_target;
        optimizer_input.led_requested = current_output.led;
        optimizer_input.fan_requested = g_energy.fan_user_req;
        optimizer_input.qi_requested = g_energy.qi_user_req;

        if (EnergyOptimizer_Evaluate(
                &optimizer_input,
                &g_energy.optimizer_decision) == 0U)
        {
            g_energy.optimizer_sequence++;
            if (g_energy.optimizer_sequence == 0U)
            {
                g_energy.optimizer_sequence++;
            }
            g_energy.optimizer_decision.sequence =
                g_energy.optimizer_sequence;
        }
        else
        {
            memset(&g_energy.optimizer_decision, 0,
                   sizeof(g_energy.optimizer_decision));
        }
        g_energy.optimizer_last_prediction_tick =
            input->prediction.tick_ms;
        g_energy.optimizer_fsm_target = fsm_charge_target;
        g_energy.optimizer_has_prediction = 1U;
    }

    if (g_energy.optimizer_decision.valid &&
        g_energy.optimizer_fsm_target == fsm_charge_target &&
        (now_tick - g_energy.optimizer_decision.prediction_tick_ms) <=
            ENERGY_OPTIMIZER_DECISION_VALID_MS)
    {
        optimizer_active = 1U;
    }
    else
    {
        g_energy.optimizer_decision.valid = 0U;
    }

#if APP_OPTIMIZER_DISPATCH_ENABLE
    if (optimizer_active)
    {
        if (g_energy.optimizer_decision.charge_target == 0U ||
            g_energy.optimizer_decision.charge_target ==
                fsm_charge_target)
        {
            charge_target =
                g_energy.optimizer_decision.charge_target;
        }
        else
        {
            charge_target = fsm_charge_target;
        }
        if (g_energy.optimizer_decision.force_save_mode)
        {
            low_energy_mode = 1U;
        }
        if (charge_target == 1U)
        {
            charge_target_voltage_v = input->home_battery_voltage_v;
        }
        else if (charge_target == 2U)
        {
            charge_target_voltage_v = input->car_battery_voltage_v;
        }
        else
        {
            charge_target_voltage_v = 0.0f;
        }
    }
#endif
#else
    (void)current_output;
    (void)optimizer_input;
    (void)optimizer_energy;
    (void)optimizer_active;
#endif

    if (charge_target != g_energy.pv_probe_target)
    {
        g_energy.pv_charge_probed = 0U;
        g_energy.pv_probe_target = charge_target;
        g_energy.pv_probe_threshold = 0.0f;
        g_energy.pv_probe_start_tick = 0U;
    }

    can_charge_from_pv =
        pv_available &&
        (pv_surplus_w >
         (g_energy.pv_charging_active ?
          0.20f : (PV_CHARGE_MIN_POWER_W + PV_CHARGE_MARGIN_W)));
    pv_load_priority_ok =
        pv_available &&
        (input->pv_voltage_v > PV_LOADED_MIN_VOLTAGE_V) &&
        (input->pv_power_w >=
         (input->home_load_power_w +
          (g_energy.pv_charging_active ? 0.0f : PV_LOAD_MARGIN_W)));

    if (charge_target != 0U && g_energy.pv_probe_threshold <= 0.0f)
    {
        g_energy.pv_probe_threshold =
            charge_target_voltage_v + PV_CHARGE_VOLT_MARGIN_V;
    }

    if (charge_target != 0U &&
        pv_available && g_energy.pv_charge_probed == 0U &&
        (input->pv_voltage_v > g_energy.pv_probe_threshold) &&
        (now_tick - g_energy.pv_state_tick) >= PV_ACTIVE_STABILIZE_MS)
    {
        g_energy.pv_charge_probed = 1U;
        g_energy.pv_probe_start_tick = now_tick;
    }

    if (g_energy.pv_charge_probed == 1U)
    {
        uint32_t probe_elapsed_ms =
            now_tick - g_energy.pv_probe_start_tick;
        uint8_t probe_settled =
            probe_elapsed_ms >= PV_PROBE_SETTLE_MS;

        pv_probe_charge = 1U;
        if (charge_target == 0U ||
            (probe_settled &&
             (!pv_load_priority_ok ||
              input->pv_voltage_v <=
                  (charge_target_voltage_v + PV_CHARGE_HOLD_MARGIN_V))))
        {
            g_energy.pv_probe_threshold =
                input->pv_voltage_v + PV_CHARGE_VOLT_MARGIN_V;
            g_energy.pv_charge_probed = 0U;
        }
        else if (probe_elapsed_ms >= PV_PROBE_CHARGE_MS)
        {
            g_energy.pv_charge_probed = 2U;
        }
    }

    pv_can_charge_home =
        (charge_target == 1U) &&
        (pv_probe_charge ||
         (can_charge_from_pv &&
          pv_load_priority_ok &&
          (input->pv_voltage_v >
           (input->home_battery_voltage_v +
            (g_energy.pv_charging_active ?
             PV_CHARGE_HOLD_MARGIN_V : PV_CHARGE_VOLT_MARGIN_V)))));
    pv_can_charge_car =
        (charge_target == 2U) &&
        (pv_probe_charge ||
         (can_charge_from_pv &&
          pv_load_priority_ok &&
          (input->pv_voltage_v >
           (input->car_battery_voltage_v +
            (g_energy.pv_charging_active ?
             PV_CHARGE_HOLD_MARGIN_V : PV_CHARGE_VOLT_MARGIN_V)))));
    v2h_should_start =
        (!enough_for_pv_load &&
         input->home_soc <= HOME_SOC_V2H_START_PCT &&
         car_soc_control >= CAR_SOC_V2H_START_PCT);
    v2h_should_stop =
        (enough_for_pv_load ||
         input->home_soc >= HOME_SOC_V2H_STOP_PCT ||
         car_soc_control <= CAR_SOC_V2H_STOP_PCT);

    EnergyIo_ReadKeys(&keys);

    if (g_energy.pending_command_valid)
    {
        if (g_energy.pending_command.led >= 0)
        {
            g_energy.led_manual_override = 1U;
            g_energy.led_manual_on =
                (uint8_t)g_energy.pending_command.led;
            g_energy.led_manual_year = input->clock_year;
            g_energy.led_manual_month = input->clock_month;
            g_energy.led_manual_day = input->clock_day;
            g_energy.led_manual_period = led_period;
        }
        if (g_energy.pending_command.fan >= 0 &&
            (HAL_GetTick() - g_energy.fan_local_override_tick) > 8000U)
        {
            g_energy.fan_user_req =
                (uint8_t)g_energy.pending_command.fan;
        }
        if (g_energy.pending_command.load >= 0)
        {
            g_energy.rigid_user_req =
                (uint8_t)g_energy.pending_command.load;
        }
        if (g_energy.pending_command.qi >= 0)
        {
            g_energy.qi_user_req =
                (uint8_t)g_energy.pending_command.qi;
            g_energy.qi_active =
                (uint8_t)g_energy.pending_command.qi;
        }

        g_energy.pending_command.fan = -1;
        g_energy.pending_command.led = -1;
        g_energy.pending_command.load = -1;
        g_energy.pending_command.qi = -1;
        g_energy.pending_command_valid = 0U;
    }

    if (input->clock_valid && g_energy.led_manual_override &&
        (g_energy.led_manual_year != input->clock_year ||
         g_energy.led_manual_month != input->clock_month ||
         g_energy.led_manual_day != input->clock_day ||
         g_energy.led_manual_period != led_period))
    {
        g_energy.led_manual_override = 0U;
    }

    if (Energy_GetPressedEvent(&g_energy.led_key_fsm, keys.led, now_tick))
    {
        uint8_t led_now = EnergyIo_ReadOutput(ENERGY_OUTPUT_LED);
        g_energy.led_manual_override = 1U;
        g_energy.led_manual_on = led_now ? 0U : 1U;
        g_energy.led_manual_year = input->clock_year;
        g_energy.led_manual_month = input->clock_month;
        g_energy.led_manual_day = input->clock_day;
        g_energy.led_manual_period = led_period;
    }
    if (Energy_GetPressedEvent(&g_energy.fan_key_fsm, keys.fan, now_tick))
    {
        g_energy.fan_user_req = !g_energy.fan_user_req;
        g_energy.fan_local_override_tick = now_tick;
    }
    if (Energy_GetPressedEvent(&g_energy.qi_key_fsm, keys.qi, now_tick))
    {
        g_energy.qi_user_req = g_energy.qi_user_req ? 0U : 1U;
        g_energy.qi_active = g_energy.qi_user_req;
    }

    if (!g_energy.v2h_active && v2h_should_start)
    {
        EnergyIo_SendCarV2HCommand(1U);
        g_energy.v2h_active = 1U;
    }
    else if (g_energy.v2h_active && v2h_should_stop)
    {
        EnergyIo_SendCarV2HCommand(0U);
        g_energy.v2h_active = 0U;
    }

    use_pv_source = enough_for_pv_load;
    use_home_source =
        (!use_pv_source && home_bat_allowed && !g_energy.v2h_active);
    home_source_available =
        use_pv_source || use_home_source || g_energy.v2h_active;

    EnergyIo_WriteOutput(ENERGY_OUTPUT_PV_SOURCE, use_pv_source);
    EnergyIo_WriteOutput(ENERGY_OUTPUT_HOME_SOURCE, use_home_source);
    EnergyIo_WriteOutput(ENERGY_OUTPUT_RIGID,
                         home_source_available &&
                         g_energy.rigid_user_req);

    if (pv_can_charge_home && charge_target == 1U &&
        input->home_soc < HOME_SOC_CAR_CHARGE_START_PCT)
    {
        requested_charge_target = 1U;
    }
    else if (pv_can_charge_car && charge_target == 2U &&
             car_soc_control < CAR_SOC_CHARGE_STOP_PCT)
    {
        requested_charge_target = 2U;
    }

    Energy_ApplyChargeOutput(requested_charge_target,
                             charge_target,
                             fsm_charge_target,
                             input,
                             car_soc_control,
                             pv_available,
                             now_tick);
    requested_charge_target = Energy_GetChargeOutputTarget();
    g_energy.pv_charging_active =
        requested_charge_target == 1U ||
        requested_charge_target == 2U ? 1U : 0U;

    car_charge_on = EnergyIo_ReadOutput(ENERGY_OUTPUT_CAR_CHARGE);
    if (car_charge_on)
    {
        if (!g_energy.car_charge_reported ||
            (now_tick - g_energy.car_charge_report_tick) >=
                CAN_CAR_CHARGE_REPORT_MS)
        {
            EnergyIo_SendCarChargeCommand(1U, input->pv_power_w);
            g_energy.car_charge_report_tick = now_tick;
            g_energy.car_charge_reported = 1U;
        }
    }
    else if (g_energy.car_charge_reported)
    {
        EnergyIo_SendCarChargeCommand(0U, 0.0f);
        g_energy.car_charge_report_tick = now_tick;
        g_energy.car_charge_reported = 0U;
    }

    if (input->clock_valid)
    {
        if (input->clock_hour < 6U)
        {
            led_auto_request = 0U;
        }
        else if (input->clock_hour >= 18U && input->clock_hour < 22U)
        {
            led_auto_request = 1U;
        }
        else if (input->clock_hour >= 22U)
        {
            led_auto_request = 0U;
        }
        else
        {
            led_auto_request =
                input->lux < NIGHT_LUX_THRESHOLD ? 1U : 0U;
        }
    }
    else
    {
        led_auto_request =
            input->lux < NIGHT_LUX_THRESHOLD ? 1U : 0U;
    }

    led_request = g_energy.led_manual_override ?
                  g_energy.led_manual_on : led_auto_request;
    led_out = home_source_available &&
              led_request &&
              !low_energy_mode &&
              home_bat_allowed;
    EnergyIo_WriteOutput(ENERGY_OUTPUT_LED, led_out);
    if (led_out != g_energy.prev_led_out)
    {
        g_energy.prev_led_out = led_out;
    }

    fan_out = g_energy.fan_user_req &&
              home_source_available &&
              !low_energy_mode;
    EnergyIo_WriteOutput(ENERGY_OUTPUT_FAN, fan_out);
    if (fan_out != g_energy.prev_fan_out)
    {
        g_energy.prev_fan_out = fan_out;
    }

    g_energy.qi_active =
        (g_energy.qi_user_req &&
         home_source_available &&
         !low_energy_mode) ? 1U : 0U;
    EnergyIo_WriteOutput(ENERGY_OUTPUT_QI, g_energy.qi_active);

    if (!g_energy.light_user_req)
    {
        g_energy.charging_active = 0U;
    }
    else if (input->lux > DAY_LUX_THRESHOLD &&
             (input->home_soc < 95.0f ||
              input->home_load_power_w > 0.10f ||
              (input->human_soc >= 0.0f &&
               input->human_soc < HUMAN_SOC_QI_REQ_PCT)))
    {
        g_energy.charging_active = 1U;
    }
    else if (!pv_available && input->lux < NIGHT_LUX_THRESHOLD)
    {
        g_energy.charging_active = 0U;
    }
}

void EnergyService_GetOutputState(EnergyOutputState_t *state)
{
    if (state == NULL)
    {
        return;
    }

    EnergyIo_GetOutputState(state);
    state->v2h = g_energy.v2h_active ? 1U : 0U;
}

uint8_t EnergyService_GetOptimizerDecision(
    EnergyOptimizerDecision_t *decision)
{
    if (decision == NULL)
    {
        return 1U;
    }

    *decision = g_energy.optimizer_decision;
    return decision->valid ? 0U : 1U;
}

uint8_t EnergyService_IsChargingActive(void)
{
    return g_energy.charging_active ? 1U : 0U;
}

void EnergyService_BuildLstmInput(EnergyLstmInput_t *output,
                                  const EnergyServiceInput_t *input)
{
    EnergyOutputState_t state;
    float real_hour;

    if (output == NULL || input == NULL)
    {
        return;
    }

    memset(output, 0, sizeof(*output));
    if (input->clock_valid)
    {
        uint32_t elapsed_s =
            (HAL_GetTick() - input->clock_tick_ms) / 1000U;
        uint32_t seconds_of_day =
            ((uint32_t)input->clock_hour * 3600U) +
            ((uint32_t)input->clock_minute * 60U) +
            (uint32_t)input->clock_second + elapsed_s;
        seconds_of_day %= 86400U;
        real_hour = (float)seconds_of_day / 3600.0f;
    }
    else
    {
        real_hour =
            (float)((HAL_GetTick() / 1000U) % 86400U) / 3600.0f;
    }

    output->real_hour_sin =
        sinf(2.0f * ENERGY_PI_F * real_hour / 24.0f);
    output->real_hour_cos =
        cosf(2.0f * ENERGY_PI_F * real_hour / 24.0f);
    output->lux = input->lux;
    output->pv_v = input->pv_ok ? input->pv_voltage_v : 0.0f;
    output->pv_p = input->pv_ok ? input->pv_power_w : 0.0f;
    output->home_v = input->sensor_ok ?
                     input->home_battery_voltage_v : 0.0f;
    output->home_soc = input->sensor_ok ? input->home_soc : 0.0f;
    output->load_p = input->home_load_power_w;
    output->car_soc = g_energy.car_soc_control_valid ?
                      g_energy.car_soc_control : input->car_soc;
    output->human_soc = input->human_soc;

    EnergyService_GetOutputState(&state);
    output->pvsrc = state.pv_source;
    output->hsrc = state.home_source;
    output->rigid = state.rigid;
    output->led = state.led;
    output->fan = state.fan;
    output->qi = state.qi;
    output->hchg = state.home_charge;
    output->cchg = state.car_charge;
    output->v2h = state.v2h;
}

void EnergyService_ClampPrediction(EnergyLstmPrediction_t *prediction,
                                   const EnergyServiceInput_t *input)
{
    float soc_min;
    float soc_max;
    uint8_t home_charging;

    if (prediction == NULL || input == NULL ||
        !prediction->valid || !input->sensor_ok)
    {
        return;
    }

    prediction->future_pv_p =
        Energy_ClampFloat(prediction->future_pv_p, 0.0f, 20.0f);
    prediction->future_load_p =
        Energy_ClampFloat(prediction->future_load_p, 0.0f, 20.0f);

    home_charging = EnergyIo_ReadOutput(ENERGY_OUTPUT_HOME_CHARGE);
    soc_min = Energy_ClampFloat(
        input->home_soc - ENERGY_LSTM_SOC_MAX_DROP_PCT,
        0.0f,
        100.0f);
    soc_max = home_charging ?
              Energy_ClampFloat(
                  input->home_soc + ENERGY_LSTM_SOC_MAX_RISE_PCT,
                  0.0f,
                  100.0f) :
              input->home_soc;
    prediction->future_home_soc =
        Energy_ClampFloat(prediction->future_home_soc, soc_min, soc_max);
}
