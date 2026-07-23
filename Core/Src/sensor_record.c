#include "sensor_record.h"

#include <string.h>

#include "pressure_sensor.h"
#include "xda_sensor.h"

#define SENSOR_RECORD_MAGIC 0x52434542UL
#define SENSOR_RECORD_VERSION SENSOR_RECORD_FORMAT_VERSION

static uint32_t s_record_sequence = 0U;

static void SensorRecord_PutU16(uint8_t *buffer, uint16_t value);
static void SensorRecord_PutU32(uint8_t *buffer, uint32_t value);
static uint32_t SensorRecord_Crc32(const uint8_t *buffer, size_t length);

void SensorRecord_Build(SensorRecord_t *record, uint32_t missed_periods)
{
  const PressureSensorData_t *pressure_data;
  const XDA_SensorData_t *xda_data;

  if (record == NULL)
  {
    return;
  }

  (void)memset(record, 0, sizeof(*record));
  pressure_data = PressureSensor_GetData();
  xda_data = XDA_Sensor_GetData();

  s_record_sequence += missed_periods + 1U;
  record->sequence = s_record_sequence;
  record->tick_ms = HAL_GetTick();

  if (pressure_data != NULL)
  {
    record->pressure_sample_tick = pressure_data->last_update_tick;
    record->pressure_sample_sequence = pressure_data->sample_sequence;
    record->pressure_online = pressure_data->online;
    record->pressure_status = (uint8_t)pressure_data->status;
    record->pressure_float_valid = pressure_data->float_valid;
    record->pressure_read_mode = (uint8_t)pressure_data->read_mode;
    record->pressure_raw = pressure_data->pressure_raw;
    record->pressure_value = pressure_data->pressure_value;
    record->pressure_unit_code = pressure_data->unit_code;
    record->pressure_decimal_point = pressure_data->decimal_point;
    record->pressure_exception_code = pressure_data->last_exception_code;
  }

  if (xda_data != NULL)
  {
    record->xda_sample_tick = xda_data->last_update_tick;
    record->xda_sample_sequence = xda_data->sample_sequence;
    record->xda_online = xda_data->online;
    record->xda_status = (uint8_t)xda_data->status;
    record->ec_x100 = xda_data->ec_x100;
    record->temperature_x10 = xda_data->temperature_x10;
    record->tds_ppm = xda_data->tds_ppm;
    record->salinity_ppm = xda_data->salinity_ppm;
    record->xda_exception_code = xda_data->last_exception_code;
  }
}

uint8_t SensorRecord_Serialize(const SensorRecord_t *record,
                               uint8_t *buffer,
                               size_t buffer_size)
{
  union
  {
    float f32;
    uint32_t u32;
  } pressure_value;
  uint32_t crc;

  if ((record == NULL) ||
      (buffer == NULL) ||
      (buffer_size < SENSOR_RECORD_WIRE_SIZE))
  {
    return 0U;
  }

  (void)memset(buffer, 0, SENSOR_RECORD_WIRE_SIZE);
  pressure_value.f32 = record->pressure_value;

  SensorRecord_PutU32(&buffer[0], SENSOR_RECORD_MAGIC);
  SensorRecord_PutU16(&buffer[4], SENSOR_RECORD_VERSION);
  SensorRecord_PutU16(&buffer[6], SENSOR_RECORD_WIRE_SIZE);
  SensorRecord_PutU32(&buffer[8], record->sequence);
  SensorRecord_PutU32(&buffer[12], record->tick_ms);
  SensorRecord_PutU32(&buffer[16], record->pressure_sample_tick);
  SensorRecord_PutU32(&buffer[20], record->pressure_sample_sequence);
  SensorRecord_PutU32(&buffer[24], record->xda_sample_tick);
  SensorRecord_PutU32(&buffer[28], record->xda_sample_sequence);
  SensorRecord_PutU32(&buffer[32], pressure_value.u32);
  SensorRecord_PutU16(&buffer[36], (uint16_t)record->pressure_raw);
  SensorRecord_PutU16(&buffer[38], record->pressure_unit_code);
  SensorRecord_PutU16(&buffer[40], record->pressure_decimal_point);
  SensorRecord_PutU16(&buffer[42], record->ec_x100);
  SensorRecord_PutU16(&buffer[44], (uint16_t)record->temperature_x10);
  SensorRecord_PutU16(&buffer[46], record->tds_ppm);
  SensorRecord_PutU16(&buffer[48], record->salinity_ppm);
  buffer[50] = record->pressure_online;
  buffer[51] = record->pressure_status;
  buffer[52] = record->pressure_float_valid;
  buffer[53] = record->pressure_read_mode;
  buffer[54] = record->xda_online;
  buffer[55] = record->xda_status;
  buffer[56] = record->pressure_exception_code;
  buffer[57] = record->xda_exception_code;

  crc = SensorRecord_Crc32(buffer, SENSOR_RECORD_WIRE_SIZE - 4U);
  SensorRecord_PutU32(&buffer[SENSOR_RECORD_WIRE_SIZE - 4U], crc);
  return 1U;
}

static void SensorRecord_PutU16(uint8_t *buffer, uint16_t value)
{
  buffer[0] = (uint8_t)(value & 0xFFU);
  buffer[1] = (uint8_t)(value >> 8U);
}

static void SensorRecord_PutU32(uint8_t *buffer, uint32_t value)
{
  buffer[0] = (uint8_t)(value & 0xFFU);
  buffer[1] = (uint8_t)((value >> 8U) & 0xFFU);
  buffer[2] = (uint8_t)((value >> 16U) & 0xFFU);
  buffer[3] = (uint8_t)(value >> 24U);
}

static uint32_t SensorRecord_Crc32(const uint8_t *buffer, size_t length)
{
  uint32_t crc = 0xFFFFFFFFUL;
  size_t index;
  uint8_t bit;

  for (index = 0U; index < length; index++)
  {
    crc ^= buffer[index];
    for (bit = 0U; bit < 8U; bit++)
    {
      crc = ((crc & 1U) != 0U)
              ? ((crc >> 1U) ^ 0xEDB88320UL)
              : (crc >> 1U);
    }
  }

  return ~crc;
}
