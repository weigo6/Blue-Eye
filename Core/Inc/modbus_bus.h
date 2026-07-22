#ifndef __MODBUS_BUS_H__
#define __MODBUS_BUS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "stm32f4xx_hal.h"

#define MODBUS_BUS_MAX_DATA_LENGTH 32U

typedef enum
{
  MODBUS_BUS_CLIENT_NONE = 0,
  MODBUS_BUS_CLIENT_XDA,
  MODBUS_BUS_CLIENT_PRESSURE
} ModbusBusClient_t;

typedef enum
{
  MODBUS_BUS_RESULT_NONE = 0,
  MODBUS_BUS_RESULT_OK,
  MODBUS_BUS_RESULT_TIMEOUT,
  MODBUS_BUS_RESULT_UART_ERROR,
  MODBUS_BUS_RESULT_CRC_ERROR,
  MODBUS_BUS_RESULT_FRAME_ERROR,
  MODBUS_BUS_RESULT_EXCEPTION
} ModbusBusResultCode_t;

typedef struct
{
  ModbusBusResultCode_t code;
  uint8_t exception_code;
  uint8_t data_length;
  uint8_t data[MODBUS_BUS_MAX_DATA_LENGTH];
} ModbusBusResult_t;

void ModbusBus_Init(UART_HandleTypeDef *huart);
void ModbusBus_Task(void);
uint8_t ModbusBus_StartReadHoldingRegisters(ModbusBusClient_t client,
                                             uint8_t slave_address,
                                             uint16_t start_register,
                                             uint16_t register_count,
                                             uint32_t timeout_ms);
uint8_t ModbusBus_TakeResult(ModbusBusClient_t client, ModbusBusResult_t *result);
uint8_t ModbusBus_IsBusy(void);
uint8_t ModbusBus_IsClientPending(ModbusBusClient_t client);
uint32_t ModbusBus_GetBaudRate(void);

#ifdef __cplusplus
}
#endif

#endif /* __MODBUS_BUS_H__ */
