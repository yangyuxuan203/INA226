#include "app_state.h"

#include "cmsis_os.h"
#include <string.h>

typedef struct
{
    SensorData_t sensor;
    ESP32S3_Data_t wearable;
    EnergyLstmPrediction_t prediction;
    float prediction_raw_home_soc;
    BeijingClock_t beijing_clock;
    char beijing_time[APP_STATE_BEIJING_TIME_LENGTH];
    uint8_t onenet_online;
} AppStateStorage_t;

static osMutexId s_state_mutex = NULL;

#if (configSUPPORT_STATIC_ALLOCATION == 1)
static osStaticMutexDef_t s_state_mutex_control;
static const osMutexDef_t s_state_mutex_definition = {
    0U,
    &s_state_mutex_control
};
#else
static const osMutexDef_t s_state_mutex_definition = {0U};
#endif

static AppStateStorage_t s_state = {
    .prediction_raw_home_soc = -1.0f,
    .beijing_time = "--:--:--"
};

static uint8_t AppState_Lock(void)
{
    if (s_state_mutex == NULL)
    {
        return 1U;
    }

    return osMutexWait(s_state_mutex, osWaitForever) == osOK ? 0U : 1U;
}

static void AppState_Unlock(void)
{
    (void)osMutexRelease(s_state_mutex);
}

uint8_t AppState_Init(void)
{
    if (s_state_mutex != NULL)
    {
        return 0U;
    }

    s_state_mutex = osMutexCreate(&s_state_mutex_definition);
    return s_state_mutex == NULL ? 1U : 0U;
}

uint8_t AppState_UpdateHome(float bus_voltage, float shunt_voltage,
                            float current, float power, float soc_pct,
                            uint8_t ok)
{
    if (AppState_Lock() != 0U)
    {
        return 1U;
    }

    s_state.sensor.bus_voltage = bus_voltage;
    s_state.sensor.shunt_voltage = shunt_voltage;
    s_state.sensor.current = current;
    s_state.sensor.power = power;
    s_state.sensor.soc_pct = soc_pct;
    s_state.sensor.ina226_ok = ok ? 1U : 0U;
    AppState_Unlock();
    return 0U;
}

uint8_t AppState_SetHomeOk(uint8_t ok)
{
    if (AppState_Lock() != 0U)
    {
        return 1U;
    }

    s_state.sensor.ina226_ok = ok ? 1U : 0U;
    AppState_Unlock();
    return 0U;
}

uint8_t AppState_UpdateMq9(uint16_t adc, float voltage)
{
    if (AppState_Lock() != 0U)
    {
        return 1U;
    }

    s_state.sensor.mq9_adc = adc;
    s_state.sensor.mq9_voltage = voltage;
    AppState_Unlock();
    return 0U;
}

uint8_t AppState_UpdateLight(float lux, uint8_t ok)
{
    if (AppState_Lock() != 0U)
    {
        return 1U;
    }

    s_state.sensor.lux = lux;
    s_state.sensor.gy30_ok = ok ? 1U : 0U;
    AppState_Unlock();
    return 0U;
}

uint8_t AppState_SetLightOk(uint8_t ok)
{
    if (AppState_Lock() != 0U)
    {
        return 1U;
    }

    s_state.sensor.gy30_ok = ok ? 1U : 0U;
    AppState_Unlock();
    return 0U;
}

uint8_t AppState_UpdatePv(float voltage, float current, float power,
                          uint8_t ok)
{
    if (AppState_Lock() != 0U)
    {
        return 1U;
    }

    s_state.sensor.pv_voltage = voltage;
    s_state.sensor.pv_current = current;
    s_state.sensor.pv_power = power;
    s_state.sensor.pv_ok = ok ? 1U : 0U;
    AppState_Unlock();
    return 0U;
}

uint8_t AppState_SetPvOk(uint8_t ok)
{
    if (AppState_Lock() != 0U)
    {
        return 1U;
    }

    s_state.sensor.pv_ok = ok ? 1U : 0U;
    AppState_Unlock();
    return 0U;
}

uint8_t AppState_UpdateLoad(float voltage, float current, float power,
                            uint8_t ok)
{
    if (AppState_Lock() != 0U)
    {
        return 1U;
    }

    s_state.sensor.ina3_voltage = voltage;
    s_state.sensor.ina3_current = current;
    s_state.sensor.ina3_power = power;
    s_state.sensor.ina3_ok = ok ? 1U : 0U;
    AppState_Unlock();
    return 0U;
}

uint8_t AppState_SetLoadOk(uint8_t ok)
{
    if (AppState_Lock() != 0U)
    {
        return 1U;
    }

    s_state.sensor.ina3_ok = ok ? 1U : 0U;
    AppState_Unlock();
    return 0U;
}

uint8_t AppState_GetSensor(SensorData_t *sensor)
{
    if (sensor == NULL || AppState_Lock() != 0U)
    {
        return 1U;
    }

    *sensor = s_state.sensor;
    AppState_Unlock();
    return 0U;
}

uint8_t AppState_SetWearable(const ESP32S3_Data_t *wearable)
{
    if (wearable == NULL || AppState_Lock() != 0U)
    {
        return 1U;
    }

    s_state.wearable = *wearable;
    AppState_Unlock();
    return 0U;
}

uint8_t AppState_GetWearable(ESP32S3_Data_t *wearable)
{
    if (wearable == NULL || AppState_Lock() != 0U)
    {
        return 1U;
    }

    *wearable = s_state.wearable;
    AppState_Unlock();
    return 0U;
}

uint8_t AppState_SetPrediction(const EnergyLstmPrediction_t *prediction,
                               float raw_home_soc)
{
    if (prediction == NULL || AppState_Lock() != 0U)
    {
        return 1U;
    }

    s_state.prediction = *prediction;
    s_state.prediction_raw_home_soc = raw_home_soc;
    AppState_Unlock();
    return 0U;
}

uint8_t AppState_GetPrediction(EnergyLstmPrediction_t *prediction,
                               float *raw_home_soc)
{
    if ((prediction == NULL && raw_home_soc == NULL) ||
        AppState_Lock() != 0U)
    {
        return 1U;
    }

    if (prediction != NULL)
    {
        *prediction = s_state.prediction;
    }
    if (raw_home_soc != NULL)
    {
        *raw_home_soc = s_state.prediction_raw_home_soc;
    }
    AppState_Unlock();
    return 0U;
}

uint8_t AppState_SetBeijingTime(const BeijingClock_t *clock,
                                const char *display_text)
{
    if (clock == NULL || display_text == NULL || AppState_Lock() != 0U)
    {
        return 1U;
    }

    s_state.beijing_clock = *clock;
    strncpy(s_state.beijing_time, display_text,
            sizeof(s_state.beijing_time) - 1U);
    s_state.beijing_time[sizeof(s_state.beijing_time) - 1U] = '\0';
    AppState_Unlock();
    return 0U;
}

uint8_t AppState_GetBeijingTime(BeijingClock_t *clock,
                                char *display_text,
                                uint32_t display_text_size)
{
    if ((clock == NULL && display_text == NULL) ||
        (display_text != NULL && display_text_size == 0U) ||
        AppState_Lock() != 0U)
    {
        return 1U;
    }

    if (clock != NULL)
    {
        *clock = s_state.beijing_clock;
    }
    if (display_text != NULL)
    {
        strncpy(display_text, s_state.beijing_time,
                display_text_size - 1U);
        display_text[display_text_size - 1U] = '\0';
    }
    AppState_Unlock();
    return 0U;
}

uint8_t AppState_SetOneNETOnline(uint8_t online)
{
    if (AppState_Lock() != 0U)
    {
        return 1U;
    }

    s_state.onenet_online = online ? 1U : 0U;
    AppState_Unlock();
    return 0U;
}

uint8_t AppState_GetOneNETOnline(uint8_t *online)
{
    if (online == NULL || AppState_Lock() != 0U)
    {
        return 1U;
    }

    *online = s_state.onenet_online;
    AppState_Unlock();
    return 0U;
}

uint8_t AppState_GetSnapshot(AppStateSnapshot_t *snapshot)
{
    if (snapshot == NULL || AppState_Lock() != 0U)
    {
        return 1U;
    }

    snapshot->sensor = s_state.sensor;
    snapshot->wearable = s_state.wearable;
    snapshot->prediction = s_state.prediction;
    snapshot->prediction_raw_home_soc = s_state.prediction_raw_home_soc;
    snapshot->beijing_clock = s_state.beijing_clock;
    memcpy(snapshot->beijing_time, s_state.beijing_time,
           sizeof(snapshot->beijing_time));
    snapshot->onenet_online = s_state.onenet_online;
    AppState_Unlock();
    return 0U;
}
