#ifndef __SENSOR_RECORD_H__
#define __SENSOR_RECORD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#define SENSOR_RECORD_WIRE_SIZE 64U
#define SENSOR_RECORD_FORMAT_VERSION 3U

typedef struct
{
  uint32_t sequence;
  uint32_t tick_ms;
  uint32_t pressure_sample_tick;
  uint32_t pressure_sample_sequence;
  uint32_t xda_sample_tick;
  uint32_t xda_sample_sequence;
  uint8_t pressure_online;
  uint8_t pressure_status;
  uint8_t pressure_float_valid;
  uint8_t pressure_read_mode;
  int16_t pressure_raw;
  float pressure_value;
  uint16_t pressure_unit_code;
  uint16_t pressure_decimal_point;
  uint8_t xda_online;
  uint8_t xda_status;
  uint16_t ec_x100;
  int16_t temperature_x10;
  uint16_t tds_ppm;
  uint16_t salinity_ppm;
  uint8_t pressure_exception_code;
  uint8_t xda_exception_code;
} SensorRecord_t;

void SensorRecord_Build(SensorRecord_t *record, uint32_t missed_periods);
uint8_t SensorRecord_Serialize(const SensorRecord_t *record,
                               uint8_t *buffer,
                               size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* __SENSOR_RECORD_H__ */
