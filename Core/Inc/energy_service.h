#ifndef ENERGY_SERVICE_H
#define ENERGY_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "energy_types.h"

#define ENERGY_COMMAND_QUEUE_DEPTH 4U

uint8_t EnergyService_Init(void);
uint8_t EnergyService_SubmitCommand(const EnergyActuatorCommand_t *command);
void EnergyService_Process(const EnergyServiceInput_t *input);
void EnergyService_GetOutputState(EnergyOutputState_t *state);
uint8_t EnergyService_IsChargingActive(void);
void EnergyService_BuildLstmInput(EnergyLstmInput_t *output,
                                  const EnergyServiceInput_t *input);
void EnergyService_ClampPrediction(EnergyLstmPrediction_t *prediction,
                                   const EnergyServiceInput_t *input);

#ifdef __cplusplus
}
#endif

#endif /* ENERGY_SERVICE_H */
