#include "periodic_trigger.h"

#include "stm32f4xx_hal.h"

static volatile uint8_t s_tick_1s_count = 0U;
static volatile uint8_t s_event_5s_pending_count = 0U;

void PeriodicTrigger_Init(void)
{
  s_tick_1s_count = 0U;
  s_event_5s_pending_count = 0U;
}

void PeriodicTrigger_OnTimerInterrupt(void)
{
  s_tick_1s_count++;
  if (s_tick_1s_count >= 5U)
  {
    s_tick_1s_count = 0U;
    if (s_event_5s_pending_count < UINT8_MAX)
    {
      s_event_5s_pending_count++;
    }
  }
}

uint8_t PeriodicTrigger_Consume5sEvent(void)
{
  uint32_t primask;
  uint8_t pending = 0U;

  primask = __get_PRIMASK();
  __disable_irq();
  if (s_event_5s_pending_count != 0U)
  {
    s_event_5s_pending_count--;
    pending = 1U;
  }
  if (primask == 0U)
  {
    __enable_irq();
  }
  return pending;
}
