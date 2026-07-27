#ifndef SOC_ESTIMATOR_H
#define SOC_ESTIMATOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct
{
    float coulomb_mah;
    float last_cal_voltage;
    uint8_t initialized;
} SocEstimator_t;

void SocEstimator_Init(SocEstimator_t *estimator);
float SocEstimator_Update(SocEstimator_t *estimator,
                          float voltage_v,
                          float current_a,
                          uint32_t interval_ms);

#ifdef __cplusplus
}
#endif

#endif /* SOC_ESTIMATOR_H */
