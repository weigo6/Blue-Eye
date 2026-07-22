#ifndef __PRESSURE_SENSOR_H__
#define __PRESSURE_SENSOR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

typedef enum
{
  PRESSURE_SENSOR_STATUS_IDLE = 0,
  PRESSURE_SENSOR_STATUS_OK,
  PRESSURE_SENSOR_STATUS_TIMEOUT,
  PRESSURE_SENSOR_STATUS_CRC_ERROR,
  PRESSURE_SENSOR_STATUS_FRAME_ERROR,
  PRESSURE_SENSOR_STATUS_UART_ERROR
} PressureSensorStatus_t;

typedef struct
{
  int16_t pressure_raw;
  uint16_t unit_code;
  uint16_t decimal_point;
  uint8_t slave_address;
  uint32_t baud_rate;
  uint8_t online;
  PressureSensorStatus_t status;
  uint32_t last_update_tick;
  uint32_t success_count;
  uint32_t error_count;
} PressureSensorData_t;

void PressureSensor_Init(UART_HandleTypeDef *huart, uint8_t slave_address);
uint8_t PressureSensor_Task(void);
const PressureSensorData_t *PressureSensor_GetData(void);

#ifdef __cplusplus
}
#endif

#endif /* __PRESSURE_SENSOR_H__ */
