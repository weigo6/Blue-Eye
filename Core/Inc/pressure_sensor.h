#ifndef __PRESSURE_SENSOR_H__
#define __PRESSURE_SENSOR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include "sensor_task.h"

typedef enum
{
  PRESSURE_SENSOR_STATUS_IDLE = 0,
  PRESSURE_SENSOR_STATUS_OK,
  PRESSURE_SENSOR_STATUS_TIMEOUT,
  PRESSURE_SENSOR_STATUS_CRC_ERROR,
  PRESSURE_SENSOR_STATUS_FRAME_ERROR,
  PRESSURE_SENSOR_STATUS_UART_ERROR,
  PRESSURE_SENSOR_STATUS_MODBUS_EXCEPTION
} PressureSensorStatus_t;

typedef enum
{
  PRESSURE_SENSOR_READ_MODE_RAW = 0,
  PRESSURE_SENSOR_READ_MODE_FLOAT
} PressureSensorReadMode_t;

typedef struct
{
  int16_t pressure_raw;
  float pressure_value;
  uint16_t unit_code;
  uint16_t decimal_point;
  uint8_t slave_address;
  uint32_t baud_rate;
  uint8_t online;
  uint8_t float_valid;
  PressureSensorReadMode_t read_mode;
  PressureSensorStatus_t status;
  uint32_t last_update_tick;
  uint32_t last_attempt_tick;
  uint32_t sample_sequence;
  uint32_t success_count;
  uint32_t error_count;
  uint16_t consecutive_failure_count;
  uint8_t last_exception_code;
} PressureSensorData_t;

void PressureSensor_Init(uint8_t slave_address);
SensorTaskEvent_t PressureSensor_Task(void);
uint8_t PressureSensor_IsBusy(void);
const PressureSensorData_t *PressureSensor_GetData(void);
void PressureSensor_SetReadMode(PressureSensorReadMode_t read_mode);
PressureSensorReadMode_t PressureSensor_GetReadMode(void);

#ifdef __cplusplus
}
#endif

#endif /* __PRESSURE_SENSOR_H__ */
