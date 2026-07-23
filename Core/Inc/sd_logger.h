#ifndef __SD_LOGGER_H__
#define __SD_LOGGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "sensor_record.h"

typedef enum
{
  SD_LOGGER_STATE_NO_CARD = 0,
  SD_LOGGER_STATE_MOUNTING,
  SD_LOGGER_STATE_ACTIVE,
  SD_LOGGER_STATE_EJECTING,
  SD_LOGGER_STATE_SAFE_TO_REMOVE,
  SD_LOGGER_STATE_ERROR
} SDLoggerState_t;

typedef struct
{
  SDLoggerState_t state;
  uint8_t card_present;
  uint8_t mounted;
  uint8_t file_opened;
  uint8_t accepting_records;
  uint8_t format_required;
  uint16_t queue_depth;
  char current_file[13];
  uint32_t current_file_index;
  uint32_t current_file_bytes;
  uint32_t last_error;
  uint32_t last_write_tick;
  uint32_t last_sync_tick;
  uint32_t records_written;
  uint32_t write_error_count;
  uint32_t dropped_record_count;
  uint32_t skipped_record_count;
} SDLoggerStatus_t;

void SDLogger_Init(void);
uint8_t SDLogger_Submit(const SensorRecord_t *record);
uint8_t SDLogger_Task(uint8_t io_allowed);
void SDLogger_RequestSafeEject(void);
void SDLogger_RequestResume(void);
uint8_t SDLogger_HasPendingIo(void);
const SDLoggerStatus_t *SDLogger_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* __SD_LOGGER_H__ */
