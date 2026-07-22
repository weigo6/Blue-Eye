#ifndef __APP_UI_H__
#define __APP_UI_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "key.h"
#include "pressure_sensor.h"
#include "xda_sensor.h"

void APP_UI_Init(void);
void APP_UI_HandleKeyEvent(KeyEvent_t event);
void APP_UI_UpdatePressureSensorData(const PressureSensorData_t *sensor_data);
void APP_UI_UpdateXDASensorData(const XDA_SensorData_t *sensor_data);

#ifdef __cplusplus
}
#endif

#endif /* __APP_UI_H__ */
