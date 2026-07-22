#ifndef __DATA_LOGGER_H__
#define __DATA_LOGGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "sensor_record.h"

typedef struct
{
  uint8_t enabled;
  uint8_t mounted;
  uint8_t card_present;
  uint8_t format_required;
  uint8_t pending_records;
  uint32_t last_error;
  uint32_t capacity_records;
  uint32_t record_count;
  uint32_t next_write_index;
  uint32_t overwrite_count;
  uint32_t last_write_tick;
  uint32_t write_success_count;
  uint32_t write_error_count;
  uint32_t dropped_record_count;
} DataLoggerStatus_t;

void DataLogger_Init(void);
void DataLogger_RequestSnapshot(const SensorRecord_t *record);
uint8_t DataLogger_Task(void);
void DataLogger_SetEnabled(uint8_t enabled);
const DataLoggerStatus_t *DataLogger_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* __DATA_LOGGER_H__ */
