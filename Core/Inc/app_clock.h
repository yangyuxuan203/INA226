#ifndef APP_CLOCK_H
#define APP_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void AppClock_Service(void);
uint8_t AppClock_IsValid(void);
uint8_t AppClock_SetFromSntp(const char *raw_time);

#ifdef __cplusplus
}
#endif

#endif /* APP_CLOCK_H */
