#include "xda_sensor.h"

#include "modbus_bus.h"

#define XDA_SENSOR_REG_DATA_START 0x0000U
#define XDA_SENSOR_REG_COUNT 4U
#define XDA_SENSOR_POLL_MS 1000U
#define XDA_SENSOR_UART_TIMEOUT_MS 200U
#define XDA_SENSOR_OFFLINE_FAILURE_THRESHOLD 3U

static uint8_t s_sensor_address = 0x02U;
static uint32_t s_last_poll_tick = 0U;
static XDA_SensorData_t s_sensor_data = {0};

static XDA_SensorStatus_t XDA_MapBusResult(const ModbusBusResult_t *result);
static SensorTaskEvent_t XDA_ApplyResult(const ModbusBusResult_t *result);
static void XDA_SetFailure(XDA_SensorStatus_t status, uint8_t exception_code);

void XDA_Sensor_Init(uint8_t slave_address)
{
  s_sensor_address = slave_address;
  s_last_poll_tick = 0U;
  s_sensor_data.ec_x100 = 0U;
  s_sensor_data.temperature_x10 = 0;
  s_sensor_data.tds_ppm = 0U;
  s_sensor_data.salinity_ppm = 0U;
  s_sensor_data.status = XDA_SENSOR_STATUS_IDLE;
  s_sensor_data.online = 0U;
  s_sensor_data.slave_address = s_sensor_address;
  s_sensor_data.baud_rate = ModbusBus_GetBaudRate();
  s_sensor_data.last_update_tick = 0U;
  s_sensor_data.last_attempt_tick = 0U;
  s_sensor_data.sample_sequence = 0U;
  s_sensor_data.success_count = 0U;
  s_sensor_data.error_count = 0U;
  s_sensor_data.consecutive_failure_count = 0U;
  s_sensor_data.last_exception_code = 0U;
}

SensorTaskEvent_t XDA_Sensor_Task(void)
{
  ModbusBusResult_t result;
  uint32_t now = HAL_GetTick();

  if (ModbusBus_TakeResult(MODBUS_BUS_CLIENT_XDA, &result) != 0U)
  {
    return XDA_ApplyResult(&result);
  }

  if (ModbusBus_IsClientPending(MODBUS_BUS_CLIENT_XDA) != 0U)
  {
    return SENSOR_TASK_EVENT_NONE;
  }

  if ((now - s_last_poll_tick) < XDA_SENSOR_POLL_MS)
  {
    return SENSOR_TASK_EVENT_NONE;
  }

  if (ModbusBus_StartReadHoldingRegisters(MODBUS_BUS_CLIENT_XDA,
                                           s_sensor_address,
                                           XDA_SENSOR_REG_DATA_START,
                                           XDA_SENSOR_REG_COUNT,
                                           XDA_SENSOR_UART_TIMEOUT_MS) == 0U)
  {
    return SENSOR_TASK_EVENT_NONE;
  }

  s_last_poll_tick = now;
  s_sensor_data.last_attempt_tick = now;
  return SENSOR_TASK_EVENT_REQUEST_STARTED;
}

uint8_t XDA_Sensor_IsBusy(void)
{
  return ModbusBus_IsClientPending(MODBUS_BUS_CLIENT_XDA);
}

const XDA_SensorData_t *XDA_Sensor_GetData(void)
{
  return &s_sensor_data;
}

static XDA_SensorStatus_t XDA_MapBusResult(const ModbusBusResult_t *result)
{
  if (result == NULL)
  {
    return XDA_SENSOR_STATUS_FRAME_ERROR;
  }

  switch (result->code)
  {
    case MODBUS_BUS_RESULT_TIMEOUT:
      return XDA_SENSOR_STATUS_TIMEOUT;
    case MODBUS_BUS_RESULT_UART_ERROR:
      return XDA_SENSOR_STATUS_UART_ERROR;
    case MODBUS_BUS_RESULT_CRC_ERROR:
      return XDA_SENSOR_STATUS_CRC_ERROR;
    case MODBUS_BUS_RESULT_EXCEPTION:
      return XDA_SENSOR_STATUS_MODBUS_EXCEPTION;
    case MODBUS_BUS_RESULT_FRAME_ERROR:
    case MODBUS_BUS_RESULT_NONE:
    default:
      return XDA_SENSOR_STATUS_FRAME_ERROR;
  }
}

static SensorTaskEvent_t XDA_ApplyResult(const ModbusBusResult_t *result)
{
  uint16_t ec_x100;
  int16_t temperature_x10;
  uint16_t tds_ppm;
  uint16_t salinity_ppm;

  if ((result == NULL) || (result->code != MODBUS_BUS_RESULT_OK))
  {
    XDA_SetFailure(XDA_MapBusResult(result), (result != NULL) ? result->exception_code : 0U);
    return SENSOR_TASK_EVENT_STATUS_CHANGED;
  }

  if (result->data_length != 8U)
  {
    XDA_SetFailure(XDA_SENSOR_STATUS_FRAME_ERROR, 0U);
    return SENSOR_TASK_EVENT_STATUS_CHANGED;
  }

  ec_x100 = (uint16_t)(((uint16_t)result->data[0] << 8U) | result->data[1]);
  temperature_x10 = (int16_t)(((uint16_t)result->data[2] << 8U) | result->data[3]);
  tds_ppm = (uint16_t)(((uint16_t)result->data[4] << 8U) | result->data[5]);
  salinity_ppm = (uint16_t)(((uint16_t)result->data[6] << 8U) | result->data[7]);

  s_sensor_data.ec_x100 = ec_x100;
  s_sensor_data.temperature_x10 = temperature_x10;
  s_sensor_data.tds_ppm = tds_ppm;
  s_sensor_data.salinity_ppm = salinity_ppm;
  s_sensor_data.slave_address = s_sensor_address;
  s_sensor_data.baud_rate = ModbusBus_GetBaudRate();
  s_sensor_data.online = 1U;
  s_sensor_data.status = XDA_SENSOR_STATUS_OK;
  s_sensor_data.last_update_tick = HAL_GetTick();
  s_sensor_data.sample_sequence++;
  s_sensor_data.success_count++;
  s_sensor_data.consecutive_failure_count = 0U;
  s_sensor_data.last_exception_code = 0U;
  return SENSOR_TASK_EVENT_DATA_UPDATED;
}

static void XDA_SetFailure(XDA_SensorStatus_t status, uint8_t exception_code)
{
  s_sensor_data.status = status;
  s_sensor_data.error_count++;
  s_sensor_data.last_exception_code = exception_code;
  if (s_sensor_data.consecutive_failure_count < UINT16_MAX)
  {
    s_sensor_data.consecutive_failure_count++;
  }

  if ((s_sensor_data.success_count == 0U) ||
      (s_sensor_data.consecutive_failure_count >= XDA_SENSOR_OFFLINE_FAILURE_THRESHOLD))
  {
    s_sensor_data.online = 0U;
  }
}
