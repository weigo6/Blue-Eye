#include "periodic_trigger.h"

#include <string.h>

#include "stm32f4xx_hal.h"

static volatile uint8_t s_deadline_check_requested = 0U;
static uint32_t s_next_deadline_tick = 0U;
static PeriodicTriggerStatus_t s_status;

void PeriodicTrigger_Init(void)
{
  uint32_t now = HAL_GetTick();

  (void)memset(&s_status, 0, sizeof(s_status));
  s_deadline_check_requested = 0U;
  s_next_deadline_tick = now + PERIODIC_TRIGGER_PERIOD_MS;
  s_status.period_ms = PERIODIC_TRIGGER_PERIOD_MS;
  s_status.next_deadline_tick = s_next_deadline_tick;
}

void PeriodicTrigger_OnTimerInterrupt(void)
{
  s_deadline_check_requested = 1U;
}

uint8_t PeriodicTrigger_Consume5sEvent(PeriodicTriggerEvent_t *event)
{
  uint32_t now;
  uint32_t periods_due;
  uint32_t scheduled_tick;
  uint32_t missed_periods;
  uint32_t primask;

  if ((event == NULL) || (s_deadline_check_requested == 0U))
  {
    return 0U;
  }

  now = HAL_GetTick();
  if ((int32_t)(now - s_next_deadline_tick) < 0)
  {
    return 0U;
  }

  periods_due = ((now - s_next_deadline_tick) / PERIODIC_TRIGGER_PERIOD_MS) + 1U;
  missed_periods = periods_due - 1U;
  scheduled_tick = s_next_deadline_tick +
                   (missed_periods * PERIODIC_TRIGGER_PERIOD_MS);
  s_next_deadline_tick += periods_due * PERIODIC_TRIGGER_PERIOD_MS;

  primask = __get_PRIMASK();
  __disable_irq();
  s_deadline_check_requested = 0U;
  if (primask == 0U)
  {
    __enable_irq();
  }

  event->scheduled_tick = scheduled_tick;
  event->dispatch_tick = now;
  event->missed_periods = missed_periods;

  s_status.next_deadline_tick = s_next_deadline_tick;
  s_status.last_scheduled_tick = scheduled_tick;
  s_status.last_interval_ms = (s_status.dispatched_event_count != 0U)
                                ? (now - s_status.last_dispatch_tick)
                                : 0U;
  s_status.last_dispatch_tick = now;
  s_status.last_lateness_ms = now - scheduled_tick;
  s_status.dispatched_event_count++;
  s_status.missed_event_count += missed_periods;
  return 1U;
}

const PeriodicTriggerStatus_t *PeriodicTrigger_GetStatus(void)
{
  return &s_status;
}
