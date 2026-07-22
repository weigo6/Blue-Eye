#include "modbus_bus.h"

#include <string.h>

#include "uart_dma_support.h"

#define MODBUS_BUS_FUNCTION_READ_HOLDING_REGISTERS 0x03U
#define MODBUS_BUS_REQUEST_LENGTH 8U
#define MODBUS_BUS_MAX_RESPONSE_LENGTH (MODBUS_BUS_MAX_DATA_LENGTH + 5U)
#define MODBUS_BUS_INTERFRAME_GAP_MS 4U

static UART_HandleTypeDef *s_uart = NULL;
static uint8_t s_request[MODBUS_BUS_REQUEST_LENGTH] = {0};
static uint8_t s_response[MODBUS_BUS_MAX_RESPONSE_LENGTH] = {0};
static uint8_t s_active = 0U;
static uint8_t s_result_pending = 0U;
static ModbusBusClient_t s_client = MODBUS_BUS_CLIENT_NONE;
static uint8_t s_slave_address = 0U;
static uint8_t s_function_code = 0U;
static uint8_t s_expected_data_length = 0U;
static uint16_t s_expected_response_length = 0U;
static uint32_t s_start_tick = 0U;
static uint32_t s_timeout_ms = 0U;
static uint32_t s_next_tx_tick = 0U;
static ModbusBusResult_t s_result = {0};

static uint16_t ModbusBus_Crc16(const uint8_t *buffer, uint16_t length);
static void ModbusBus_Finalize(ModbusBusResultCode_t code);
static void ModbusBus_ProcessResponse(uint16_t response_length);
static uint8_t ModbusBus_TimeReached(uint32_t now, uint32_t deadline);

void ModbusBus_Init(UART_HandleTypeDef *huart)
{
  s_uart = huart;
  s_active = 0U;
  s_result_pending = 0U;
  s_client = MODBUS_BUS_CLIENT_NONE;
  s_next_tx_tick = HAL_GetTick();
  (void)memset(&s_result, 0, sizeof(s_result));
  UartDmaSupport_ClearFlags(s_uart);
}

void ModbusBus_Task(void)
{
  uint32_t now;
  uint16_t response_length;

  if ((s_uart == NULL) || (s_active == 0U))
  {
    return;
  }

  now = HAL_GetTick();

  if (UartDmaSupport_HasError(s_uart) != 0U)
  {
    UartDmaSupport_Abort(s_uart);
    ModbusBus_Finalize(MODBUS_BUS_RESULT_UART_ERROR);
    return;
  }

  if (UartDmaSupport_IsRxDone(s_uart) != 0U)
  {
    response_length = UartDmaSupport_GetRxLength(s_uart);
    ModbusBus_ProcessResponse(response_length);
    return;
  }

  if ((now - s_start_tick) >= s_timeout_ms)
  {
    UartDmaSupport_Abort(s_uart);
    ModbusBus_Finalize(MODBUS_BUS_RESULT_TIMEOUT);
  }
}

uint8_t ModbusBus_StartReadHoldingRegisters(ModbusBusClient_t client,
                                             uint8_t slave_address,
                                             uint16_t start_register,
                                             uint16_t register_count,
                                             uint32_t timeout_ms)
{
  uint16_t crc;
  uint16_t response_length;
  uint32_t now;

  if ((s_uart == NULL) || (client == MODBUS_BUS_CLIENT_NONE) ||
      (register_count == 0U) || (register_count > (MODBUS_BUS_MAX_DATA_LENGTH / 2U)) ||
      (timeout_ms == 0U) || (s_active != 0U) || (s_result_pending != 0U))
  {
    return 0U;
  }

  now = HAL_GetTick();
  if (ModbusBus_TimeReached(now, s_next_tx_tick) == 0U)
  {
    return 0U;
  }

  response_length = (uint16_t)(5U + (register_count * 2U));
  s_request[0] = slave_address;
  s_request[1] = MODBUS_BUS_FUNCTION_READ_HOLDING_REGISTERS;
  s_request[2] = (uint8_t)(start_register >> 8U);
  s_request[3] = (uint8_t)(start_register & 0xFFU);
  s_request[4] = (uint8_t)(register_count >> 8U);
  s_request[5] = (uint8_t)(register_count & 0xFFU);
  crc = ModbusBus_Crc16(s_request, 6U);
  s_request[6] = (uint8_t)(crc & 0xFFU);
  s_request[7] = (uint8_t)(crc >> 8U);

  (void)memset(s_response, 0, sizeof(s_response));
  (void)memset(&s_result, 0, sizeof(s_result));
  __HAL_UART_CLEAR_OREFLAG(s_uart);
  __HAL_UART_CLEAR_NEFLAG(s_uart);
  __HAL_UART_CLEAR_FEFLAG(s_uart);
  (void)__HAL_UART_FLUSH_DRREGISTER(s_uart);
  UartDmaSupport_ClearFlags(s_uart);

  s_client = client;
  s_slave_address = slave_address;
  s_function_code = MODBUS_BUS_FUNCTION_READ_HOLDING_REGISTERS;
  s_expected_data_length = (uint8_t)(register_count * 2U);
  s_expected_response_length = response_length;
  s_timeout_ms = timeout_ms;
  s_start_tick = now;
  s_active = 1U;

  if (UartDmaSupport_StartTransactionITToIdleDMA(s_uart,
                                                  s_request,
                                                  sizeof(s_request),
                                                  s_response,
                                                  sizeof(s_response)) != HAL_OK)
  {
    ModbusBus_Finalize(MODBUS_BUS_RESULT_UART_ERROR);
  }

  return 1U;
}

uint8_t ModbusBus_TakeResult(ModbusBusClient_t client, ModbusBusResult_t *result)
{
  if ((result == NULL) || (s_result_pending == 0U) || (client != s_client))
  {
    return 0U;
  }

  *result = s_result;
  s_result_pending = 0U;
  s_client = MODBUS_BUS_CLIENT_NONE;
  return 1U;
}

uint8_t ModbusBus_IsBusy(void)
{
  return s_active;
}

uint8_t ModbusBus_IsClientPending(ModbusBusClient_t client)
{
  if (client == MODBUS_BUS_CLIENT_NONE)
  {
    return 0U;
  }

  return (((s_active != 0U) || (s_result_pending != 0U)) && (s_client == client)) ? 1U : 0U;
}

uint32_t ModbusBus_GetBaudRate(void)
{
  return (s_uart != NULL) ? s_uart->Init.BaudRate : 0U;
}

static uint16_t ModbusBus_Crc16(const uint8_t *buffer, uint16_t length)
{
  uint16_t crc = 0xFFFFU;
  uint16_t index;
  uint8_t bit;

  for (index = 0U; index < length; index++)
  {
    crc ^= buffer[index];
    for (bit = 0U; bit < 8U; bit++)
    {
      if ((crc & 0x0001U) != 0U)
      {
        crc = (crc >> 1U) ^ 0xA001U;
      }
      else
      {
        crc >>= 1U;
      }
    }
  }

  return crc;
}

static void ModbusBus_Finalize(ModbusBusResultCode_t code)
{
  s_result.code = code;
  s_active = 0U;
  s_result_pending = 1U;
  s_next_tx_tick = HAL_GetTick() + MODBUS_BUS_INTERFRAME_GAP_MS;
}

static void ModbusBus_ProcessResponse(uint16_t response_length)
{
  uint16_t crc;

  s_result.exception_code = 0U;
  s_result.data_length = 0U;
  (void)memset(s_result.data, 0, sizeof(s_result.data));

  if (response_length < 5U)
  {
    ModbusBus_Finalize(MODBUS_BUS_RESULT_FRAME_ERROR);
    return;
  }

  crc = ModbusBus_Crc16(s_response, (uint16_t)(response_length - 2U));
  if ((s_response[response_length - 2U] != (uint8_t)(crc & 0xFFU)) ||
      (s_response[response_length - 1U] != (uint8_t)(crc >> 8U)))
  {
    ModbusBus_Finalize(MODBUS_BUS_RESULT_CRC_ERROR);
    return;
  }

  if (s_response[0] != s_slave_address)
  {
    ModbusBus_Finalize(MODBUS_BUS_RESULT_FRAME_ERROR);
    return;
  }

  if ((response_length == 5U) && (s_response[1] == (uint8_t)(s_function_code | 0x80U)))
  {
    s_result.exception_code = s_response[2];
    ModbusBus_Finalize(MODBUS_BUS_RESULT_EXCEPTION);
    return;
  }

  if ((response_length != s_expected_response_length) ||
      (s_response[1] != s_function_code) ||
      (s_response[2] != s_expected_data_length))
  {
    ModbusBus_Finalize(MODBUS_BUS_RESULT_FRAME_ERROR);
    return;
  }

  s_result.data_length = s_expected_data_length;
  (void)memcpy(s_result.data, &s_response[3], s_expected_data_length);
  ModbusBus_Finalize(MODBUS_BUS_RESULT_OK);
}

static uint8_t ModbusBus_TimeReached(uint32_t now, uint32_t deadline)
{
  return (((int32_t)(now - deadline)) >= 0) ? 1U : 0U;
}
