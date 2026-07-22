#include "key.h"

#define KEY_DEBOUNCE_MS 30U
#define KEY_DOUBLE_CLICK_MS 300U

static volatile uint32_t s_last_irq_tick = 0U;
static volatile uint32_t s_first_click_tick = 0U;
static volatile uint8_t s_click_count = 0U;
static volatile KeyEvent_t s_pending_event = KEY_EVENT_NONE;

void KEY_NotifyExti(uint16_t gpio_pin)
{
  uint32_t now;

  if (gpio_pin != KEY_Pin)
  {
    return;
  }

  now = HAL_GetTick();
  if ((now - s_last_irq_tick) < KEY_DEBOUNCE_MS)
  {
    return;
  }

  s_last_irq_tick = now;

  if ((s_click_count == 1U) && ((now - s_first_click_tick) <= KEY_DOUBLE_CLICK_MS))
  {
    s_click_count = 0U;
    s_pending_event = KEY_EVENT_DOUBLE_CLICK;
    return;
  }

  s_click_count = 1U;
  s_first_click_tick = now;
}

void KEY_Task(void)
{
  uint32_t now = HAL_GetTick();

  if ((s_click_count == 1U) && ((now - s_first_click_tick) > KEY_DOUBLE_CLICK_MS))
  {
    s_click_count = 0U;
    if (s_pending_event == KEY_EVENT_NONE)
    {
      s_pending_event = KEY_EVENT_SINGLE_CLICK;
    }
  }
}

KeyEvent_t KEY_GetEvent(void)
{
  KeyEvent_t event = s_pending_event;

  s_pending_event = KEY_EVENT_NONE;
  return event;
}
