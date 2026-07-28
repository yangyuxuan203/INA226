#ifndef APP_TASKS_H
#define APP_TASKS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t AppTasks_GetDataElapsedMs(void);
void HomeSensorTask(void const *argument);
void GasSensorTask(void const *argument);
void LightSensorTask(void const *argument);
void EnergyControlTask(void const *argument);
void UiTask(void const *argument);

#ifdef __cplusplus
}
#endif

#endif /* APP_TASKS_H */
