#include "key.h"

#define KEY_DEBOUNCE_MS 30U
#define KEY_LONG_PRESS_MS 700U
#define KEY_SAFE_EJECT_HOLD_MS 3000U

static volatile uint32_t s_last_irq_tick = 0U;
static volatile uint32_t s_press_tick = 0U;
static volatile uint8_t s_key_pressed = 0U;
static volatile uint8_t s_long_press_reported = 0U;
static volatile uint8_t s_safe_eject_reported = 0U;
static volatile KeyEvent_t s_pending_event = KEY_EVENT_NONE;

void KEY_NotifyExti(uint16_t gpio_pin)
{
  uint32_t now;
  uint32_t hold_duration;
  GPIO_PinState pin_state;

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
  pin_state = HAL_GPIO_ReadPin(KEY_GPIO_Port, KEY_Pin);
  if (pin_state == GPIO_PIN_SET)
  {
    s_key_pressed = 1U;
    s_press_tick = now;
    s_long_press_reported = 0U;
    s_safe_eject_reported = 0U;
    return;
  }

  if (s_key_pressed != 0U)
  {
    s_key_pressed = 0U;
    hold_duration = now - s_press_tick;
    if ((hold_duration >= KEY_SAFE_EJECT_HOLD_MS) &&
        (s_safe_eject_reported == 0U))
    {
      s_safe_eject_reported = 1U;
      s_long_press_reported = 1U;
      s_pending_event = KEY_EVENT_SAFE_EJECT_HOLD;
    }
    else if ((hold_duration >= KEY_LONG_PRESS_MS) &&
             (s_long_press_reported == 0U) &&
             (s_pending_event == KEY_EVENT_NONE))
    {
      s_long_press_reported = 1U;
      s_pending_event = KEY_EVENT_LONG_PRESS;
    }
    else if ((s_long_press_reported == 0U) &&
             (s_pending_event == KEY_EVENT_NONE))
    {
      s_pending_event = KEY_EVENT_SINGLE_CLICK;
    }
  }
}

void KEY_Task(void)
{
  uint32_t now = HAL_GetTick();
  uint32_t hold_duration;

  if (s_key_pressed == 0U)
  {
    return;
  }

  hold_duration = now - s_press_tick;

  if ((s_safe_eject_reported == 0U) &&
      (hold_duration >= KEY_SAFE_EJECT_HOLD_MS) &&
      (s_pending_event == KEY_EVENT_NONE))
  {
    s_safe_eject_reported = 1U;
    s_long_press_reported = 1U;
    s_pending_event = KEY_EVENT_SAFE_EJECT_HOLD;
    return;
  }

  if ((s_long_press_reported == 0U) &&
      (hold_duration >= KEY_LONG_PRESS_MS) &&
      (s_pending_event == KEY_EVENT_NONE))
  {
    s_long_press_reported = 1U;
    s_pending_event = KEY_EVENT_LONG_PRESS;
  }
}

KeyEvent_t KEY_GetEvent(void)
{
  uint32_t primask;
  KeyEvent_t event;

  primask = __get_PRIMASK();
  __disable_irq();
  event = s_pending_event;
  s_pending_event = KEY_EVENT_NONE;
  if (primask == 0U)
  {
    __enable_irq();
  }
  return event;
}
