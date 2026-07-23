#ifndef __KEY_H__
#define __KEY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef enum
{
  KEY_EVENT_NONE = 0,
  KEY_EVENT_SINGLE_CLICK,
  KEY_EVENT_LONG_PRESS,
  KEY_EVENT_SAFE_EJECT_HOLD
} KeyEvent_t;

void KEY_NotifyExti(uint16_t gpio_pin);
void KEY_Task(void);
KeyEvent_t KEY_GetEvent(void);

#ifdef __cplusplus
}
#endif

#endif /* __KEY_H__ */
