#ifndef __XDA_SENSOR_H__
#define __XDA_SENSOR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

typedef enum
{
  XDA_SENSOR_STATUS_IDLE = 0,
  XDA_SENSOR_STATUS_OK,
  XDA_SENSOR_STATUS_TIMEOUT,
  XDA_SENSOR_STATUS_CRC_ERROR,
  XDA_SENSOR_STATUS_FRAME_ERROR,
  XDA_SENSOR_STATUS_UART_ERROR
} XDA_SensorStatus_t;

typedef struct
{
  uint16_t ec_x100;
  int16_t temperature_x10;
  uint16_t tds_ppm;
  uint16_t salinity_ppm;
  uint8_t slave_address;
  uint32_t baud_rate;
  uint8_t online;
  XDA_SensorStatus_t status;
  uint32_t last_update_tick;
  uint32_t success_count;
  uint32_t error_count;
} XDA_SensorData_t;

void XDA_Sensor_Init(UART_HandleTypeDef *huart, uint8_t slave_address);
uint8_t XDA_Sensor_Task(void);
const XDA_SensorData_t *XDA_Sensor_GetData(void);

#ifdef __cplusplus
}
#endif

#endif /* __XDA_SENSOR_H__ */
