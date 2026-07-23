#ifndef __PERIODIC_TRIGGER_H__
#define __PERIODIC_TRIGGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define PERIODIC_TRIGGER_PERIOD_MS 5000U

typedef struct
{
  uint32_t scheduled_tick;
  uint32_t dispatch_tick;
  uint32_t missed_periods;
} PeriodicTriggerEvent_t;

typedef struct
{
  uint32_t period_ms;
  uint32_t next_deadline_tick;
  uint32_t last_scheduled_tick;
  uint32_t last_dispatch_tick;
  uint32_t last_interval_ms;
  uint32_t last_lateness_ms;
  uint32_t dispatched_event_count;
  uint32_t missed_event_count;
} PeriodicTriggerStatus_t;

void PeriodicTrigger_Init(void);
void PeriodicTrigger_OnTimerInterrupt(void);
uint8_t PeriodicTrigger_Consume5sEvent(PeriodicTriggerEvent_t *event);
const PeriodicTriggerStatus_t *PeriodicTrigger_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* __PERIODIC_TRIGGER_H__ */
