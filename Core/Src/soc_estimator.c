#include "soc_estimator.h"

#include <stdint.h>

float SocEstimator_FromVoltage(float voltage_v)
{
    static const float voltage_points_v[] = {
        6.5f, 6.8f, 7.2f, 7.6f, 8.0f, 8.4f
    };
    static const float soc_points_pct[] = {
        0.0f, 20.0f, 40.0f, 65.0f, 90.0f, 100.0f
    };
    const uint32_t point_count =
        (uint32_t)(sizeof(voltage_points_v) /
                   sizeof(voltage_points_v[0]));
    uint32_t index;

    if (voltage_v <= voltage_points_v[0])
    {
        return soc_points_pct[0];
    }
    if (voltage_v >= voltage_points_v[point_count - 1U])
    {
        return soc_points_pct[point_count - 1U];
    }

    for (index = 0U; index + 1U < point_count; index++)
    {
        if (voltage_v >= voltage_points_v[index] &&
            voltage_v <= voltage_points_v[index + 1U])
        {
            float ratio =
                (voltage_v - voltage_points_v[index]) /
                (voltage_points_v[index + 1U] -
                 voltage_points_v[index]);

            return soc_points_pct[index] +
                   ratio * (soc_points_pct[index + 1U] -
                            soc_points_pct[index]);
        }
    }

    return 0.0f;
}
