#include "pressure_sensor.h"

#include <math.h>

#include "modbus_bus.h"

#define PRESSURE_SENSOR_REG_UNIT_START 0x0002U
#define PRESSURE_SENSOR_REG_UNIT_COUNT 0x0001U
#define PRESSURE_SENSOR_REG_DATA_START 0x0002U
#define PRESSURE_SENSOR_REG_COUNT 0x0003U
#define PRESSURE_SENSOR_REG_FLOAT_START 0x0016U
#define PRESSURE_SENSOR_REG_FLOAT_COUNT 0x0002U
#define PRESSURE_SENSOR_POLL_MS 1000U
#define PRESSURE_SENSOR_UART_TIMEOUT_MS 200U
#define PRESSURE_SENSOR_MAX_DECIMAL_POINT 4U
#define PRESSURE_SENSOR_MAX_UNIT_CODE 23U
#define PRESSURE_SENSOR_OFFLINE_FAILURE_THRESHOLD 3U

typedef enum
{
  PRESSURE_SENSOR_PHASE_IDLE = 0,
  PRESSURE_SENSOR_PHASE_RAW_WAITING,
  PRESSURE_SENSOR_PHASE_FLOAT_UNIT_WAITING,
  PRESSURE_SENSOR_PHASE_FLOAT_VALUE_READY,
  PRESSURE_SENSOR_PHASE_FLOAT_VALUE_WAITING
} PressureSensorPhase_t;

static uint8_t s_sensor_address = 0x01U;
static uint32_t s_last_poll_tick = 0U;
static PressureSensorData_t s_sensor_data = {0};
static PressureSensorPhase_t s_phase = PRESSURE_SENSOR_PHASE_IDLE;
static uint16_t s_float_unit_code = 0U;

static PressureSensorStatus_t PressureSensor_MapBusResult(const ModbusBusResult_t *result);
static SensorTaskEvent_t PressureSensor_HandleResult(const ModbusBusResult_t *result);
static void PressureSensor_SetFailure(PressureSensorStatus_t status, uint8_t exception_code);
static uint8_t PressureSensor_StartRead(uint16_t start_register, uint16_t register_count);
static float PressureSensor_ParseFloat32BigEndian(const uint8_t *buffer);
static SensorTaskEvent_t PressureSensor_ApplyRawData(const ModbusBusResult_t *result);
static SensorTaskEvent_t PressureSensor_ApplyFloatData(const ModbusBusResult_t *result);

void PressureSensor_Init(uint8_t slave_address)
{
  s_sensor_address = slave_address;
  s_last_poll_tick = 0U;
  s_phase = PRESSURE_SENSOR_PHASE_IDLE;
  s_float_unit_code = 0U;

  s_sensor_data.pressure_raw = 0;
  s_sensor_data.pressure_value = 0.0f;
  s_sensor_data.unit_code = 0U;
  s_sensor_data.decimal_point = 0U;
  s_sensor_data.slave_address = s_sensor_address;
  s_sensor_data.baud_rate = ModbusBus_GetBaudRate();
  s_sensor_data.online = 0U;
  s_sensor_data.float_valid = 0U;
  s_sensor_data.read_mode = PRESSURE_SENSOR_READ_MODE_RAW;
  s_sensor_data.status = PRESSURE_SENSOR_STATUS_IDLE;
  s_sensor_data.last_update_tick = 0U;
  s_sensor_data.last_attempt_tick = 0U;
  s_sensor_data.sample_sequence = 0U;
  s_sensor_data.success_count = 0U;
  s_sensor_data.error_count = 0U;
  s_sensor_data.consecutive_failure_count = 0U;
  s_sensor_data.last_exception_code = 0U;
}

SensorTaskEvent_t PressureSensor_Task(void)
{
  ModbusBusResult_t result;
  uint32_t now = HAL_GetTick();

  if (ModbusBus_TakeResult(MODBUS_BUS_CLIENT_PRESSURE, &result) != 0U)
  {
    return PressureSensor_HandleResult(&result);
  }

  if (ModbusBus_IsClientPending(MODBUS_BUS_CLIENT_PRESSURE) != 0U)
  {
    return SENSOR_TASK_EVENT_NONE;
  }

  if (s_phase == PRESSURE_SENSOR_PHASE_FLOAT_VALUE_READY)
  {
    if (PressureSensor_StartRead(PRESSURE_SENSOR_REG_FLOAT_START,
                                 PRESSURE_SENSOR_REG_FLOAT_COUNT) == 0U)
    {
      return SENSOR_TASK_EVENT_NONE;
    }

    s_phase = PRESSURE_SENSOR_PHASE_FLOAT_VALUE_WAITING;
    s_sensor_data.last_attempt_tick = now;
    return SENSOR_TASK_EVENT_REQUEST_STARTED;
  }

  if (s_phase != PRESSURE_SENSOR_PHASE_IDLE)
  {
    return SENSOR_TASK_EVENT_NONE;
  }

  if ((now - s_last_poll_tick) < PRESSURE_SENSOR_POLL_MS)
  {
    return SENSOR_TASK_EVENT_NONE;
  }

  if (s_sensor_data.read_mode == PRESSURE_SENSOR_READ_MODE_FLOAT)
  {
    if (PressureSensor_StartRead(PRESSURE_SENSOR_REG_UNIT_START,
                                 PRESSURE_SENSOR_REG_UNIT_COUNT) == 0U)
    {
      return SENSOR_TASK_EVENT_NONE;
    }
    s_phase = PRESSURE_SENSOR_PHASE_FLOAT_UNIT_WAITING;
  }
  else
  {
    if (PressureSensor_StartRead(PRESSURE_SENSOR_REG_DATA_START,
                                 PRESSURE_SENSOR_REG_COUNT) == 0U)
    {
      return SENSOR_TASK_EVENT_NONE;
    }
    s_phase = PRESSURE_SENSOR_PHASE_RAW_WAITING;
  }

  s_last_poll_tick = now;
  s_sensor_data.last_attempt_tick = now;
  return SENSOR_TASK_EVENT_REQUEST_STARTED;
}

uint8_t PressureSensor_IsBusy(void)
{
  return ((s_phase != PRESSURE_SENSOR_PHASE_IDLE) ||
          (ModbusBus_IsClientPending(MODBUS_BUS_CLIENT_PRESSURE) != 0U)) ? 1U : 0U;
}

const PressureSensorData_t *PressureSensor_GetData(void)
{
  return &s_sensor_data;
}

void PressureSensor_SetReadMode(PressureSensorReadMode_t read_mode)
{
  if ((read_mode != PRESSURE_SENSOR_READ_MODE_RAW) &&
      (read_mode != PRESSURE_SENSOR_READ_MODE_FLOAT))
  {
    return;
  }

  s_sensor_data.read_mode = read_mode;
  if (read_mode == PRESSURE_SENSOR_READ_MODE_RAW)
  {
    s_sensor_data.float_valid = 0U;
  }
  s_last_poll_tick = 0U;
}

PressureSensorReadMode_t PressureSensor_GetReadMode(void)
{
  return s_sensor_data.read_mode;
}

static PressureSensorStatus_t PressureSensor_MapBusResult(const ModbusBusResult_t *result)
{
  if (result == NULL)
  {
    return PRESSURE_SENSOR_STATUS_FRAME_ERROR;
  }

  switch (result->code)
  {
    case MODBUS_BUS_RESULT_TIMEOUT:
      return PRESSURE_SENSOR_STATUS_TIMEOUT;
    case MODBUS_BUS_RESULT_UART_ERROR:
      return PRESSURE_SENSOR_STATUS_UART_ERROR;
    case MODBUS_BUS_RESULT_CRC_ERROR:
      return PRESSURE_SENSOR_STATUS_CRC_ERROR;
    case MODBUS_BUS_RESULT_EXCEPTION:
      return PRESSURE_SENSOR_STATUS_MODBUS_EXCEPTION;
    case MODBUS_BUS_RESULT_FRAME_ERROR:
    case MODBUS_BUS_RESULT_NONE:
    default:
      return PRESSURE_SENSOR_STATUS_FRAME_ERROR;
  }
}

static SensorTaskEvent_t PressureSensor_HandleResult(const ModbusBusResult_t *result)
{
  uint16_t unit_code;

  if ((result == NULL) || (result->code != MODBUS_BUS_RESULT_OK))
  {
    PressureSensor_SetFailure(PressureSensor_MapBusResult(result),
                              (result != NULL) ? result->exception_code : 0U);
    s_phase = PRESSURE_SENSOR_PHASE_IDLE;
    return SENSOR_TASK_EVENT_STATUS_CHANGED;
  }

  switch (s_phase)
  {
    case PRESSURE_SENSOR_PHASE_RAW_WAITING:
      s_phase = PRESSURE_SENSOR_PHASE_IDLE;
      return PressureSensor_ApplyRawData(result);

    case PRESSURE_SENSOR_PHASE_FLOAT_UNIT_WAITING:
      if (result->data_length != 2U)
      {
        PressureSensor_SetFailure(PRESSURE_SENSOR_STATUS_FRAME_ERROR, 0U);
        s_phase = PRESSURE_SENSOR_PHASE_IDLE;
        return SENSOR_TASK_EVENT_STATUS_CHANGED;
      }

      unit_code = (uint16_t)(((uint16_t)result->data[0] << 8U) | result->data[1]);
      if (unit_code > PRESSURE_SENSOR_MAX_UNIT_CODE)
      {
        PressureSensor_SetFailure(PRESSURE_SENSOR_STATUS_FRAME_ERROR, 0U);
        s_phase = PRESSURE_SENSOR_PHASE_IDLE;
        return SENSOR_TASK_EVENT_STATUS_CHANGED;
      }

      s_float_unit_code = unit_code;
      if (s_sensor_data.read_mode != PRESSURE_SENSOR_READ_MODE_FLOAT)
      {
        s_phase = PRESSURE_SENSOR_PHASE_IDLE;
        s_last_poll_tick = 0U;
        return SENSOR_TASK_EVENT_NONE;
      }
      s_phase = PRESSURE_SENSOR_PHASE_FLOAT_VALUE_READY;
      return SENSOR_TASK_EVENT_NONE;

    case PRESSURE_SENSOR_PHASE_FLOAT_VALUE_WAITING:
      s_phase = PRESSURE_SENSOR_PHASE_IDLE;
      return PressureSensor_ApplyFloatData(result);

    case PRESSURE_SENSOR_PHASE_FLOAT_VALUE_READY:
    case PRESSURE_SENSOR_PHASE_IDLE:
    default:
      PressureSensor_SetFailure(PRESSURE_SENSOR_STATUS_FRAME_ERROR, 0U);
      s_phase = PRESSURE_SENSOR_PHASE_IDLE;
      return SENSOR_TASK_EVENT_STATUS_CHANGED;
  }
}

static void PressureSensor_SetFailure(PressureSensorStatus_t status, uint8_t exception_code)
{
  s_sensor_data.status = status;
  s_sensor_data.error_count++;
  s_sensor_data.last_exception_code = exception_code;
  if (s_sensor_data.consecutive_failure_count < UINT16_MAX)
  {
    s_sensor_data.consecutive_failure_count++;
  }

  if ((s_sensor_data.success_count == 0U) ||
      (s_sensor_data.consecutive_failure_count >= PRESSURE_SENSOR_OFFLINE_FAILURE_THRESHOLD))
  {
    s_sensor_data.online = 0U;
  }

  if (s_sensor_data.read_mode == PRESSURE_SENSOR_READ_MODE_FLOAT)
  {
    s_sensor_data.float_valid = 0U;
  }
}

static uint8_t PressureSensor_StartRead(uint16_t start_register, uint16_t register_count)
{
  return ModbusBus_StartReadHoldingRegisters(MODBUS_BUS_CLIENT_PRESSURE,
                                              s_sensor_address,
                                              start_register,
                                              register_count,
                                              PRESSURE_SENSOR_UART_TIMEOUT_MS);
}

static float PressureSensor_ParseFloat32BigEndian(const uint8_t *buffer)
{
  union
  {
    uint32_t u32;
    float f32;
  } value;

  value.u32 = ((uint32_t)buffer[0] << 24U) |
              ((uint32_t)buffer[1] << 16U) |
              ((uint32_t)buffer[2] << 8U) |
              (uint32_t)buffer[3];
  return value.f32;
}

static SensorTaskEvent_t PressureSensor_ApplyRawData(const ModbusBusResult_t *result)
{
  uint16_t unit_code;
  uint16_t decimal_point;
  int16_t pressure_raw;

  if ((result == NULL) || (result->data_length != 6U))
  {
    PressureSensor_SetFailure(PRESSURE_SENSOR_STATUS_FRAME_ERROR, 0U);
    return SENSOR_TASK_EVENT_STATUS_CHANGED;
  }

  unit_code = (uint16_t)(((uint16_t)result->data[0] << 8U) | result->data[1]);
  decimal_point = (uint16_t)(((uint16_t)result->data[2] << 8U) | result->data[3]);
  pressure_raw = (int16_t)(((uint16_t)result->data[4] << 8U) | result->data[5]);
  if ((unit_code > PRESSURE_SENSOR_MAX_UNIT_CODE) ||
      (decimal_point > PRESSURE_SENSOR_MAX_DECIMAL_POINT))
  {
    PressureSensor_SetFailure(PRESSURE_SENSOR_STATUS_FRAME_ERROR, 0U);
    return SENSOR_TASK_EVENT_STATUS_CHANGED;
  }

  s_sensor_data.unit_code = unit_code;
  s_sensor_data.decimal_point = decimal_point;
  s_sensor_data.pressure_raw = pressure_raw;
  s_sensor_data.slave_address = s_sensor_address;
  s_sensor_data.baud_rate = ModbusBus_GetBaudRate();
  s_sensor_data.online = 1U;
  s_sensor_data.float_valid = 0U;
  s_sensor_data.status = PRESSURE_SENSOR_STATUS_OK;
  s_sensor_data.last_update_tick = HAL_GetTick();
  s_sensor_data.sample_sequence++;
  s_sensor_data.success_count++;
  s_sensor_data.consecutive_failure_count = 0U;
  s_sensor_data.last_exception_code = 0U;
  return SENSOR_TASK_EVENT_DATA_UPDATED;
}

static SensorTaskEvent_t PressureSensor_ApplyFloatData(const ModbusBusResult_t *result)
{
  float pressure_value;

  if ((result == NULL) || (result->data_length != 4U))
  {
    PressureSensor_SetFailure(PRESSURE_SENSOR_STATUS_FRAME_ERROR, 0U);
    return SENSOR_TASK_EVENT_STATUS_CHANGED;
  }

  pressure_value = PressureSensor_ParseFloat32BigEndian(result->data);
  if (!isfinite(pressure_value))
  {
    PressureSensor_SetFailure(PRESSURE_SENSOR_STATUS_FRAME_ERROR, 0U);
    return SENSOR_TASK_EVENT_STATUS_CHANGED;
  }

  s_sensor_data.unit_code = s_float_unit_code;
  s_sensor_data.pressure_value = pressure_value;
  s_sensor_data.slave_address = s_sensor_address;
  s_sensor_data.baud_rate = ModbusBus_GetBaudRate();
  s_sensor_data.online = 1U;
  s_sensor_data.float_valid = (s_sensor_data.read_mode == PRESSURE_SENSOR_READ_MODE_FLOAT) ? 1U : 0U;
  s_sensor_data.status = PRESSURE_SENSOR_STATUS_OK;
  s_sensor_data.last_update_tick = HAL_GetTick();
  s_sensor_data.sample_sequence++;
  s_sensor_data.success_count++;
  s_sensor_data.consecutive_failure_count = 0U;
  s_sensor_data.last_exception_code = 0U;
  return SENSOR_TASK_EVENT_DATA_UPDATED;
}
