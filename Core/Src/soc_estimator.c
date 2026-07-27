#include "soc_estimator.h"

#include <stddef.h>

#define SOC_CAPACITY_MAH       2200.0f
#define SOC_CUTOFF_VOLTAGE_V      6.5f

static uint8_t SocEstimator_VoltageToPercent(float voltage)
{
    static const uint16_t voltage_mv[] = {
        6500U, 6800U, 7200U, 7600U, 8000U, 8400U
    };
    static const uint8_t percent[] = {0U, 20U, 40U, 65U, 90U, 100U};
    const uint32_t point_count =
        (uint32_t)(sizeof(voltage_mv) / sizeof(voltage_mv[0]));
    int32_t measured_mv = (int32_t)(voltage * 1000.0f);
    uint32_t index;

    if (measured_mv <= (int32_t)voltage_mv[0])
    {
        return 0U;
    }
    if (measured_mv >= (int32_t)voltage_mv[point_count - 1U])
    {
        return 100U;
    }

    for (index = 0U; index + 1U < point_count; index++)
    {
        if (measured_mv >= (int32_t)voltage_mv[index] &&
            measured_mv <= (int32_t)voltage_mv[index + 1U])
        {
            float ratio =
                (float)(measured_mv - (int32_t)voltage_mv[index]) /
                (float)(voltage_mv[index + 1U] - voltage_mv[index]);
            return (uint8_t)(percent[index] +
                ratio * (float)(percent[index + 1U] - percent[index]));
        }
    }

    return 0U;
}

void SocEstimator_Init(SocEstimator_t *estimator)
{
    if (estimator == NULL)
    {
        return;
    }

    estimator->coulomb_mah = SOC_CAPACITY_MAH;
    estimator->last_cal_voltage = 0.0f;
    estimator->initialized = 0U;
}

float SocEstimator_Update(SocEstimator_t *estimator,
                          float voltage_v,
                          float current_a,
                          uint32_t interval_ms)
{
    float current_ma;
    float voltage_soc_mah;
    float soc;

    if (estimator == NULL)
    {
        return 0.0f;
    }

    if (voltage_v < SOC_CUTOFF_VOLTAGE_V)
    {
        estimator->coulomb_mah = 0.0f;
        estimator->initialized = 0U;
        return 0.0f;
    }

    if (estimator->initialized == 0U)
    {
        estimator->coulomb_mah =
            (float)SocEstimator_VoltageToPercent(voltage_v) /
            100.0f * SOC_CAPACITY_MAH;
        estimator->initialized = 1U;
    }

    current_ma = current_a * 1000.0f;
    estimator->coulomb_mah -=
        current_ma * ((float)interval_ms / 3600000.0f);

    voltage_soc_mah =
        (float)SocEstimator_VoltageToPercent(voltage_v) /
        100.0f * SOC_CAPACITY_MAH;

    if (current_ma > -5.0f && current_ma < 5.0f)
    {
        estimator->coulomb_mah =
            estimator->coulomb_mah * 0.9f + voltage_soc_mah * 0.1f;
    }
    else if (estimator->last_cal_voltage > 0.0f &&
             (voltage_v - estimator->last_cal_voltage > 0.05f ||
              estimator->last_cal_voltage - voltage_v > 0.05f))
    {
        estimator->coulomb_mah = voltage_soc_mah;
    }
    estimator->last_cal_voltage = voltage_v;

    if (estimator->coulomb_mah < 0.0f)
    {
        estimator->coulomb_mah = 0.0f;
    }
    if (estimator->coulomb_mah > SOC_CAPACITY_MAH)
    {
        estimator->coulomb_mah = SOC_CAPACITY_MAH;
    }

    soc = estimator->coulomb_mah / SOC_CAPACITY_MAH * 100.0f;
    if (soc > 100.0f)
    {
        return 100.0f;
    }
    if (soc < 0.0f)
    {
        return 0.0f;
    }
    return soc;
}
