#ifndef __PERIODIC_TRIGGER_H__
#define __PERIODIC_TRIGGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void PeriodicTrigger_Init(void);
void PeriodicTrigger_OnTimerInterrupt(void);
uint8_t PeriodicTrigger_Consume5sEvent(void);

#ifdef __cplusplus
}
#endif

#endif /* __PERIODIC_TRIGGER_H__ */
