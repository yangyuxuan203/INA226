#ifndef ENERGY_IO_H
#define ENERGY_IO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "energy_types.h"

typedef enum
{
    ENERGY_OUTPUT_PV_SOURCE = 0,
    ENERGY_OUTPUT_HOME_SOURCE,
    ENERGY_OUTPUT_RIGID,
    ENERGY_OUTPUT_LED,
    ENERGY_OUTPUT_FAN,
    ENERGY_OUTPUT_QI,
    ENERGY_OUTPUT_HOME_CHARGE,
    ENERGY_OUTPUT_CAR_CHARGE
} EnergyOutputId_t;

typedef struct
{
    uint8_t led;
    uint8_t fan;
    uint8_t qi;
} EnergyKeyState_t;

void EnergyIo_ReadKeys(EnergyKeyState_t *keys);
void EnergyIo_WriteOutput(EnergyOutputId_t output, uint8_t on);
uint8_t EnergyIo_ReadOutput(EnergyOutputId_t output);
void EnergyIo_DisableChargeOutputs(void);
void EnergyIo_DisableAllOutputs(void);
void EnergyIo_GetOutputState(EnergyOutputState_t *state);
void EnergyIo_SendCarV2HCommand(uint8_t enable);
void EnergyIo_SendCarChargeCommand(uint8_t enable, float charge_power_w);

#ifdef __cplusplus
}
#endif

#endif /* ENERGY_IO_H */
