#include "pressure_sensor.h"

#define PRESSURE_SENSOR_REG_DATA_START 0x0002U
#define PRESSURE_SENSOR_REG_COUNT 0x0003U
#define PRESSURE_SENSOR_POLL_MS 1000U
#define PRESSURE_SENSOR_REQUEST_LEN 8U
#define PRESSURE_SENSOR_RESPONSE_LEN 11U
#define PRESSURE_SENSOR_UART_TIMEOUT_MS 200U
#define PRESSURE_SENSOR_MAX_DECIMAL_POINT 4U

static UART_HandleTypeDef *s_sensor_uart = NULL;
static uint8_t s_sensor_address = 0x01U;
static uint32_t s_last_poll_tick = 0U;
static PressureSensorData_t s_sensor_data = {0};

static uint16_t PressureSensor_ModbusCrc16(const uint8_t *buffer, uint16_t length);
static HAL_StatusTypeDef PressureSensor_TransmitReceiveFrame(const uint8_t *request,
                                                            uint16_t request_length,
                                                            uint8_t *response,
                                                            uint16_t response_length);
static uint8_t PressureSensor_ReadRealtimeData(void);

void PressureSensor_Init(UART_HandleTypeDef *huart, uint8_t slave_address)
{
  s_sensor_uart = huart;
  s_sensor_address = slave_address;
  s_last_poll_tick = 0U;

  s_sensor_data.pressure_raw = 0;
  s_sensor_data.unit_code = 0U;
  s_sensor_data.decimal_point = 0U;
  s_sensor_data.slave_address = s_sensor_address;
  s_sensor_data.baud_rate = (huart != NULL) ? huart->Init.BaudRate : 9600U;
  s_sensor_data.online = 0U;
  s_sensor_data.status = PRESSURE_SENSOR_STATUS_IDLE;
  s_sensor_data.last_update_tick = 0U;
  s_sensor_data.success_count = 0U;
  s_sensor_data.error_count = 0U;
}

uint8_t PressureSensor_Task(void)
{
  uint32_t now = HAL_GetTick();

  if ((s_sensor_uart == NULL) || ((now - s_last_poll_tick) < PRESSURE_SENSOR_POLL_MS))
  {
    return 0U;
  }

  s_last_poll_tick = now;
  return PressureSensor_ReadRealtimeData();
}

const PressureSensorData_t *PressureSensor_GetData(void)
{
  return &s_sensor_data;
}

static uint16_t PressureSensor_ModbusCrc16(const uint8_t *buffer, uint16_t length)
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

static HAL_StatusTypeDef PressureSensor_TransmitReceiveFrame(const uint8_t *request,
                                                            uint16_t request_length,
                                                            uint8_t *response,
                                                            uint16_t response_length)
{
  HAL_StatusTypeDef hal_status;

  __HAL_UART_CLEAR_OREFLAG(s_sensor_uart);
  __HAL_UART_CLEAR_NEFLAG(s_sensor_uart);
  __HAL_UART_CLEAR_FEFLAG(s_sensor_uart);
  (void)__HAL_UART_FLUSH_DRREGISTER(s_sensor_uart);

  hal_status = HAL_UART_Transmit(s_sensor_uart,
                                 (uint8_t *)request,
                                 request_length,
                                 PRESSURE_SENSOR_UART_TIMEOUT_MS);
  if (hal_status != HAL_OK)
  {
    return hal_status;
  }

  return HAL_UART_Receive(s_sensor_uart,
                          response,
                          response_length,
                          PRESSURE_SENSOR_UART_TIMEOUT_MS);
}

static uint8_t PressureSensor_ReadRealtimeData(void)
{
  uint8_t request[PRESSURE_SENSOR_REQUEST_LEN] = {0};
  uint8_t response[PRESSURE_SENSOR_RESPONSE_LEN] = {0};
  uint16_t crc;
  HAL_StatusTypeDef hal_status;

  request[0] = s_sensor_address;
  request[1] = 0x03U;
  request[2] = (uint8_t)(PRESSURE_SENSOR_REG_DATA_START >> 8U);
  request[3] = (uint8_t)(PRESSURE_SENSOR_REG_DATA_START & 0xFFU);
  request[4] = 0x00U;
  request[5] = PRESSURE_SENSOR_REG_COUNT;

  crc = PressureSensor_ModbusCrc16(request, 6U);
  request[6] = (uint8_t)(crc & 0xFFU);
  request[7] = (uint8_t)(crc >> 8U);

  hal_status = PressureSensor_TransmitReceiveFrame(request,
                                                   sizeof(request),
                                                   response,
                                                   sizeof(response));
  if (hal_status != HAL_OK)
  {
    s_sensor_data.status = (hal_status == HAL_TIMEOUT) ? PRESSURE_SENSOR_STATUS_TIMEOUT
                                                       : PRESSURE_SENSOR_STATUS_UART_ERROR;
    s_sensor_data.online = 0U;
    s_sensor_data.error_count++;
    return 1U;
  }

  if ((response[0] != s_sensor_address) || (response[1] != 0x03U) || (response[2] != 0x06U))
  {
    s_sensor_data.status = PRESSURE_SENSOR_STATUS_FRAME_ERROR;
    s_sensor_data.online = 0U;
    s_sensor_data.error_count++;
    return 1U;
  }

  crc = PressureSensor_ModbusCrc16(response, 9U);
  if ((response[9] != (uint8_t)(crc & 0xFFU)) || (response[10] != (uint8_t)(crc >> 8U)))
  {
    s_sensor_data.status = PRESSURE_SENSOR_STATUS_CRC_ERROR;
    s_sensor_data.online = 0U;
    s_sensor_data.error_count++;
    return 1U;
  }

  s_sensor_data.unit_code = (uint16_t)(((uint16_t)response[3] << 8U) | response[4]);
  s_sensor_data.decimal_point = (uint16_t)(((uint16_t)response[5] << 8U) | response[6]);
  s_sensor_data.pressure_raw = (int16_t)(((uint16_t)response[7] << 8U) | response[8]);
  s_sensor_data.slave_address = s_sensor_address;
  s_sensor_data.baud_rate = s_sensor_uart->Init.BaudRate;
  s_sensor_data.online = 1U;
  s_sensor_data.status = PRESSURE_SENSOR_STATUS_OK;
  s_sensor_data.last_update_tick = HAL_GetTick();
  s_sensor_data.success_count++;

  if (s_sensor_data.decimal_point > PRESSURE_SENSOR_MAX_DECIMAL_POINT)
  {
    s_sensor_data.status = PRESSURE_SENSOR_STATUS_FRAME_ERROR;
    s_sensor_data.online = 0U;
    s_sensor_data.error_count++;
  }

  return 1U;
}
