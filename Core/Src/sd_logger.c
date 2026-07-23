#include "sd_logger.h"

#include <stdio.h>
#include <string.h>

#include "fatfs.h"
#include "fatfs_platform.h"
#include "periodic_trigger.h"
#include "sd_diskio.h"

#define SD_LOGGER_DIRECTORY "0:/LOG"
#define SD_LOGGER_FILE_HEADER_SIZE 512U
#define SD_LOGGER_FILE_FORMAT_VERSION 1U
#define SD_LOGGER_QUEUE_CAPACITY 128U
#define SD_LOGGER_RECORDS_PER_SECTOR (512U / SENSOR_RECORD_WIRE_SIZE)
#define SD_LOGGER_CARD_DEBOUNCE_MS 500U
#define SD_LOGGER_CARD_SETTLE_MS 500U
#define SD_LOGGER_SYNC_INTERVAL_MS 120000U
#define SD_LOGGER_RETRY_INTERVAL_MS 5000U
#define SD_LOGGER_MAX_FILE_BYTES (32UL * 1024UL * 1024UL)
#define SD_LOGGER_MAX_FILE_INDEX 99999UL

#define SD_LOGGER_ERROR_WRONG_FILESYSTEM 0x10000UL
#define SD_LOGGER_ERROR_FILE_INDEX 0x10001UL
#define SD_LOGGER_ERROR_SHORT_WRITE 0x10002UL
#define SD_LOGGER_ERROR_SERIALIZE 0x10003UL

_Static_assert((512U % SENSOR_RECORD_WIRE_SIZE) == 0U,
               "Sensor log records must divide a 512-byte sector");

static FIL s_log_file;
static SDLoggerStatus_t s_status;
static _Alignas(4) uint8_t s_record_queue[SD_LOGGER_QUEUE_CAPACITY][SENSOR_RECORD_WIRE_SIZE];
static _Alignas(4) uint8_t s_io_buffer[SD_LOGGER_FILE_HEADER_SIZE];
static uint16_t s_queue_head = 0U;
static uint16_t s_queue_tail = 0U;
static uint16_t s_queue_count = 0U;
static uint8_t s_card_raw_present = 0U;
static uint8_t s_card_stable_present = 0U;
static uint8_t s_file_dirty = 0U;
static uint8_t s_retry_allowed = 0U;
static uint32_t s_card_change_tick = 0U;
static uint32_t s_mount_not_before_tick = 0U;
static uint32_t s_next_retry_tick = 0U;
static uint32_t s_revision = 0U;

static void SDLogger_MarkChanged(void);
static uint8_t SDLogger_ReadCardPresent(void);
static void SDLogger_UpdateCardDetect(uint32_t now);
static void SDLogger_HandleCardInserted(void);
static void SDLogger_HandleCardRemoved(void);
static FRESULT SDLogger_StartSession(void);
static FRESULT SDLogger_OpenNextFile(void);
static FRESULT SDLogger_FindNextFileIndex(uint32_t *next_index);
static uint8_t SDLogger_ParseFileIndex(const char *name, uint32_t *index);
static void SDLogger_SerializeFileHeader(uint32_t file_index, uint8_t *buffer);
static FRESULT SDLogger_WriteQueuedRecords(uint8_t flush_partial);
static FRESULT SDLogger_RotateFile(void);
static FRESULT SDLogger_FinishSafeEject(void);
static void SDLogger_HandleIoError(FRESULT result, uint8_t allow_retry);
static void SDLogger_ReleaseFileSystem(uint8_t close_file);
static void SDLogger_ClearQueue(uint8_t count_as_dropped);
static void SDLogger_PutU16(uint8_t *buffer, uint16_t value);
static void SDLogger_PutU32(uint8_t *buffer, uint32_t value);
static uint32_t SDLogger_Crc32(const uint8_t *buffer, size_t length);

void SDLogger_Init(void)
{
  uint8_t card_present = SDLogger_ReadCardPresent();

  (void)memset(&s_status, 0, sizeof(s_status));
  (void)memset(s_record_queue, 0, sizeof(s_record_queue));
  (void)memset(s_io_buffer, 0, sizeof(s_io_buffer));
  s_queue_head = 0U;
  s_queue_tail = 0U;
  s_queue_count = 0U;
  s_card_raw_present = card_present;
  s_card_stable_present = card_present;
  s_card_change_tick = HAL_GetTick();
  s_mount_not_before_tick = s_card_change_tick +
                            ((card_present != 0U) ? SD_LOGGER_CARD_SETTLE_MS : 0U);
  s_file_dirty = 0U;
  s_retry_allowed = 0U;
  s_next_retry_tick = 0U;
  s_revision = 0U;

  s_status.card_present = card_present;
  s_status.state = (card_present != 0U)
                     ? SD_LOGGER_STATE_MOUNTING
                     : SD_LOGGER_STATE_NO_CARD;
  SDLogger_MarkChanged();
}

uint8_t SDLogger_Submit(const SensorRecord_t *record)
{
  uint8_t *target;

  if (record == NULL)
  {
    return 0U;
  }

  if ((s_status.state != SD_LOGGER_STATE_ACTIVE) ||
      (s_status.accepting_records == 0U))
  {
    s_status.skipped_record_count++;
    SDLogger_MarkChanged();
    return 1U;
  }

  if (s_queue_count >= SD_LOGGER_QUEUE_CAPACITY)
  {
    s_queue_tail = (uint16_t)((s_queue_tail + 1U) % SD_LOGGER_QUEUE_CAPACITY);
    s_queue_count--;
    s_status.dropped_record_count++;
  }

  target = s_record_queue[s_queue_head];
  if (SensorRecord_Serialize(record, target, SENSOR_RECORD_WIRE_SIZE) == 0U)
  {
    s_status.last_error = SD_LOGGER_ERROR_SERIALIZE;
    s_status.dropped_record_count++;
    SDLogger_MarkChanged();
    return 1U;
  }

  s_queue_head = (uint16_t)((s_queue_head + 1U) % SD_LOGGER_QUEUE_CAPACITY);
  s_queue_count++;
  s_status.queue_depth = s_queue_count;
  SDLogger_MarkChanged();
  return 1U;
}

uint8_t SDLogger_Task(uint8_t io_allowed)
{
  uint32_t now = HAL_GetTick();
  uint32_t start_revision = s_revision;
  FRESULT result;

  SDLogger_UpdateCardDetect(now);

  switch (s_status.state)
  {
    case SD_LOGGER_STATE_MOUNTING:
      if ((s_card_stable_present != 0U) &&
          (io_allowed != 0U) &&
          ((int32_t)(now - s_mount_not_before_tick) >= 0))
      {
        result = SDLogger_StartSession();
        if (result == FR_OK)
        {
          s_status.state = SD_LOGGER_STATE_ACTIVE;
          s_status.accepting_records = 1U;
          s_status.last_error = (uint32_t)FR_OK;
          s_retry_allowed = 0U;
          SDLogger_MarkChanged();
        }
        else
        {
          SDLogger_HandleIoError(result,
                                 (uint8_t)(result != FR_NO_FILESYSTEM));
        }
      }
      break;

    case SD_LOGGER_STATE_ACTIVE:
      if ((s_card_raw_present != 0U) &&
          (io_allowed != 0U) &&
          (s_queue_count >= SD_LOGGER_RECORDS_PER_SECTOR))
      {
        result = SDLogger_WriteQueuedRecords(0U);
        if (result != FR_OK)
        {
          SDLogger_HandleIoError(result, 1U);
          break;
        }
      }

      if ((s_card_raw_present != 0U) &&
          (io_allowed != 0U) &&
          (s_file_dirty != 0U) &&
          ((now - s_status.last_sync_tick) >= SD_LOGGER_SYNC_INTERVAL_MS))
      {
        result = f_sync(&s_log_file);
        if (result == FR_OK)
        {
          s_file_dirty = 0U;
          s_status.last_sync_tick = now;
          SDLogger_MarkChanged();
        }
        else
        {
          SDLogger_HandleIoError(result, 1U);
        }
      }
      break;

    case SD_LOGGER_STATE_EJECTING:
      if ((s_card_raw_present == 0U) || (io_allowed == 0U))
      {
        break;
      }

      if (s_queue_count != 0U)
      {
        result = SDLogger_WriteQueuedRecords(1U);
        if (result != FR_OK)
        {
          SDLogger_HandleIoError(result, 0U);
        }
        break;
      }

      result = SDLogger_FinishSafeEject();
      if (result == FR_OK)
      {
        s_status.state = SD_LOGGER_STATE_SAFE_TO_REMOVE;
        s_status.last_error = (uint32_t)FR_OK;
        SDLogger_MarkChanged();
      }
      else
      {
        SDLogger_HandleIoError(result, 0U);
      }
      break;

    case SD_LOGGER_STATE_ERROR:
      if ((s_retry_allowed != 0U) &&
          (s_card_stable_present != 0U) &&
          (io_allowed != 0U) &&
          ((int32_t)(now - s_next_retry_tick) >= 0))
      {
        s_status.state = SD_LOGGER_STATE_MOUNTING;
        s_mount_not_before_tick = now + SD_LOGGER_CARD_SETTLE_MS;
        SDLogger_MarkChanged();
      }
      break;

    case SD_LOGGER_STATE_NO_CARD:
    case SD_LOGGER_STATE_SAFE_TO_REMOVE:
    default:
      break;
  }

  return (s_revision != start_revision) ? 1U : 0U;
}

void SDLogger_RequestSafeEject(void)
{
  if (s_status.state != SD_LOGGER_STATE_ACTIVE)
  {
    return;
  }

  s_status.accepting_records = 0U;
  s_status.state = SD_LOGGER_STATE_EJECTING;
  SDLogger_MarkChanged();
}

void SDLogger_RequestResume(void)
{
  if (((s_status.state != SD_LOGGER_STATE_SAFE_TO_REMOVE) &&
       (s_status.state != SD_LOGGER_STATE_ERROR)) ||
      (s_card_stable_present == 0U))
  {
    return;
  }

  s_status.state = SD_LOGGER_STATE_MOUNTING;
  s_status.last_error = (uint32_t)FR_OK;
  s_status.format_required = 0U;
  s_retry_allowed = 0U;
  s_next_retry_tick = 0U;
  s_mount_not_before_tick = HAL_GetTick() + SD_LOGGER_CARD_SETTLE_MS;
  SDLogger_MarkChanged();
}

uint8_t SDLogger_HasPendingIo(void)
{
  if (s_status.state == SD_LOGGER_STATE_MOUNTING)
  {
    return ((int32_t)(HAL_GetTick() - s_mount_not_before_tick) >= 0) ? 1U : 0U;
  }

  if (s_status.state == SD_LOGGER_STATE_EJECTING)
  {
    return 1U;
  }

  if ((s_status.state == SD_LOGGER_STATE_ACTIVE) &&
      (s_queue_count >= SD_LOGGER_RECORDS_PER_SECTOR))
  {
    return 1U;
  }

  return 0U;
}

const SDLoggerStatus_t *SDLogger_GetStatus(void)
{
  return &s_status;
}

static void SDLogger_MarkChanged(void)
{
  s_revision++;
}

static uint8_t SDLogger_ReadCardPresent(void)
{
  return (BSP_PlatformIsDetected() == SD_PRESENT) ? 1U : 0U;
}

static void SDLogger_UpdateCardDetect(uint32_t now)
{
  uint8_t card_present = SDLogger_ReadCardPresent();

  if (card_present != s_card_raw_present)
  {
    s_card_raw_present = card_present;
    s_card_change_tick = now;
  }

  if ((s_card_raw_present != s_card_stable_present) &&
      ((now - s_card_change_tick) >= SD_LOGGER_CARD_DEBOUNCE_MS))
  {
    s_card_stable_present = s_card_raw_present;
    if (s_card_stable_present != 0U)
    {
      SDLogger_HandleCardInserted();
    }
    else
    {
      SDLogger_HandleCardRemoved();
    }
  }
}

static void SDLogger_HandleCardInserted(void)
{
  s_status.card_present = 1U;
  s_status.state = SD_LOGGER_STATE_MOUNTING;
  s_status.accepting_records = 0U;
  s_status.format_required = 0U;
  s_status.last_error = (uint32_t)FR_OK;
  s_retry_allowed = 0U;
  s_next_retry_tick = 0U;
  s_mount_not_before_tick = HAL_GetTick() + SD_LOGGER_CARD_SETTLE_MS;
  SDLogger_MarkChanged();
}

static void SDLogger_HandleCardRemoved(void)
{
  SDLogger_ClearQueue(1U);
  s_status.card_present = 0U;
  s_status.mounted = 0U;
  s_status.file_opened = 0U;
  s_status.accepting_records = 0U;
  s_status.format_required = 0U;
  s_status.current_file[0] = '\0';
  s_status.current_file_bytes = 0U;
  s_status.state = SD_LOGGER_STATE_NO_CARD;
  s_file_dirty = 0U;
  s_retry_allowed = 0U;
  s_next_retry_tick = 0U;
  s_mount_not_before_tick = 0U;
  SD_Diskio_Invalidate();
  SDLogger_MarkChanged();
}

static FRESULT SDLogger_StartSession(void)
{
  FRESULT result;

  result = f_mount(&SDFatFS, SDPath, 1U);
  if (result != FR_OK)
  {
    if (result == FR_NO_FILESYSTEM)
    {
      s_status.format_required = 1U;
    }
    return result;
  }

  s_status.mounted = 1U;
  if (SDFatFS.fs_type != FS_FAT32)
  {
    s_status.format_required = 1U;
    s_status.last_error = SD_LOGGER_ERROR_WRONG_FILESYSTEM;
    return FR_NO_FILESYSTEM;
  }

  result = f_mkdir(SD_LOGGER_DIRECTORY);
  if ((result != FR_OK) && (result != FR_EXIST))
  {
    return result;
  }

  result = SDLogger_OpenNextFile();
  return result;
}

static FRESULT SDLogger_OpenNextFile(void)
{
  FRESULT result;
  UINT bytes_written = 0U;
  uint32_t file_index;
  char file_path[28];

  result = SDLogger_FindNextFileIndex(&file_index);
  if (result != FR_OK)
  {
    return result;
  }

  (void)snprintf(file_path,
                 sizeof(file_path),
                 SD_LOGGER_DIRECTORY "/LOG%05lu.BIN",
                 (unsigned long)file_index);
  result = f_open(&s_log_file, file_path, FA_CREATE_NEW | FA_WRITE);
  if (result != FR_OK)
  {
    return result;
  }

  s_status.file_opened = 1U;
  SDLogger_SerializeFileHeader(file_index, s_io_buffer);
  result = f_write(&s_log_file,
                   s_io_buffer,
                   SD_LOGGER_FILE_HEADER_SIZE,
                   &bytes_written);
  if ((result != FR_OK) || (bytes_written != SD_LOGGER_FILE_HEADER_SIZE))
  {
    return (result != FR_OK) ? result : FR_DISK_ERR;
  }

  result = f_sync(&s_log_file);
  if (result != FR_OK)
  {
    return result;
  }

  (void)snprintf(s_status.current_file,
                 sizeof(s_status.current_file),
                 "LOG%05lu.BIN",
                 (unsigned long)file_index);
  s_status.current_file_index = file_index;
  s_status.current_file_bytes = SD_LOGGER_FILE_HEADER_SIZE;
  s_status.last_sync_tick = HAL_GetTick();
  s_file_dirty = 0U;
  SDLogger_MarkChanged();
  return FR_OK;
}

static FRESULT SDLogger_FindNextFileIndex(uint32_t *next_index)
{
  DIR directory;
  FILINFO file_info;
  FRESULT result;
  uint8_t found = 0U;
  uint32_t highest_index = 0U;
  uint32_t parsed_index;

  if (next_index == NULL)
  {
    return FR_INVALID_PARAMETER;
  }

  result = f_opendir(&directory, SD_LOGGER_DIRECTORY);
  if (result != FR_OK)
  {
    return result;
  }

  while (1)
  {
    result = f_readdir(&directory, &file_info);
    if ((result != FR_OK) || (file_info.fname[0] == '\0'))
    {
      break;
    }

    if (SDLogger_ParseFileIndex(file_info.fname, &parsed_index) != 0U)
    {
      if ((found == 0U) || (parsed_index > highest_index))
      {
        highest_index = parsed_index;
      }
      found = 1U;
    }
  }

  (void)f_closedir(&directory);
  if (result != FR_OK)
  {
    return result;
  }

  if (found == 0U)
  {
    *next_index = 0U;
    return FR_OK;
  }

  if (highest_index >= SD_LOGGER_MAX_FILE_INDEX)
  {
    s_status.last_error = SD_LOGGER_ERROR_FILE_INDEX;
    return FR_DENIED;
  }

  *next_index = highest_index + 1U;
  return FR_OK;
}

static uint8_t SDLogger_ParseFileIndex(const char *name, uint32_t *index)
{
  uint32_t value = 0U;
  uint8_t digit_index;

  if ((name == NULL) || (index == NULL) || (strlen(name) != 12U))
  {
    return 0U;
  }

  if ((name[0] != 'L') || (name[1] != 'O') || (name[2] != 'G') ||
      (name[8] != '.') || (name[9] != 'B') ||
      (name[10] != 'I') || (name[11] != 'N'))
  {
    return 0U;
  }

  for (digit_index = 3U; digit_index < 8U; digit_index++)
  {
    if ((name[digit_index] < '0') || (name[digit_index] > '9'))
    {
      return 0U;
    }
    value = (value * 10U) + (uint32_t)(name[digit_index] - '0');
  }

  *index = value;
  return 1U;
}

static void SDLogger_SerializeFileHeader(uint32_t file_index, uint8_t *buffer)
{
  static const uint8_t magic[8] = {'B', 'E', 'Y', 'E', 'L', 'O', 'G', '1'};
  uint32_t crc;

  (void)memset(buffer, 0, SD_LOGGER_FILE_HEADER_SIZE);
  (void)memcpy(&buffer[0], magic, sizeof(magic));
  SDLogger_PutU16(&buffer[8], SD_LOGGER_FILE_FORMAT_VERSION);
  SDLogger_PutU16(&buffer[10], SD_LOGGER_FILE_HEADER_SIZE);
  SDLogger_PutU16(&buffer[12], SENSOR_RECORD_WIRE_SIZE);
  SDLogger_PutU16(&buffer[14], SENSOR_RECORD_FORMAT_VERSION);
  SDLogger_PutU32(&buffer[16], PERIODIC_TRIGGER_PERIOD_MS);
  SDLogger_PutU32(&buffer[20], file_index);
  SDLogger_PutU32(&buffer[24], HAL_GetTick());
  crc = SDLogger_Crc32(buffer, SD_LOGGER_FILE_HEADER_SIZE - 4U);
  SDLogger_PutU32(&buffer[SD_LOGGER_FILE_HEADER_SIZE - 4U], crc);
}

static FRESULT SDLogger_WriteQueuedRecords(uint8_t flush_partial)
{
  UINT bytes_written = 0U;
  FRESULT result;
  uint16_t records_to_write;
  uint16_t queue_index;
  uint16_t record_index;
  uint32_t bytes_to_write;

  if ((s_status.file_opened == 0U) || (s_queue_count == 0U))
  {
    return FR_OK;
  }

  if ((flush_partial == 0U) &&
      (s_queue_count < SD_LOGGER_RECORDS_PER_SECTOR))
  {
    return FR_OK;
  }

  records_to_write = (s_queue_count >= SD_LOGGER_RECORDS_PER_SECTOR)
                       ? SD_LOGGER_RECORDS_PER_SECTOR
                       : s_queue_count;
  bytes_to_write = (uint32_t)records_to_write * SENSOR_RECORD_WIRE_SIZE;

  if ((s_status.current_file_bytes + bytes_to_write) > SD_LOGGER_MAX_FILE_BYTES)
  {
    result = SDLogger_RotateFile();
    if (result != FR_OK)
    {
      return result;
    }
  }

  queue_index = s_queue_tail;
  for (record_index = 0U; record_index < records_to_write; record_index++)
  {
    (void)memcpy(&s_io_buffer[(uint32_t)record_index * SENSOR_RECORD_WIRE_SIZE],
                 s_record_queue[queue_index],
                 SENSOR_RECORD_WIRE_SIZE);
    queue_index = (uint16_t)((queue_index + 1U) % SD_LOGGER_QUEUE_CAPACITY);
  }

  result = f_write(&s_log_file, s_io_buffer, bytes_to_write, &bytes_written);
  if ((result != FR_OK) || (bytes_written != bytes_to_write))
  {
    if ((result == FR_OK) && (bytes_written != bytes_to_write))
    {
      s_status.last_error = SD_LOGGER_ERROR_SHORT_WRITE;
    }
    return (result != FR_OK) ? result : FR_DISK_ERR;
  }

  s_queue_tail = queue_index;
  s_queue_count = (uint16_t)(s_queue_count - records_to_write);
  s_status.queue_depth = s_queue_count;
  s_status.current_file_bytes += bytes_to_write;
  s_status.records_written += records_to_write;
  s_status.last_write_tick = HAL_GetTick();
  s_file_dirty = 1U;
  SDLogger_MarkChanged();
  return FR_OK;
}

static FRESULT SDLogger_RotateFile(void)
{
  FRESULT result;

  result = f_sync(&s_log_file);
  if (result != FR_OK)
  {
    return result;
  }

  result = f_close(&s_log_file);
  s_status.file_opened = 0U;
  if (result != FR_OK)
  {
    return result;
  }

  s_file_dirty = 0U;
  return SDLogger_OpenNextFile();
}

static FRESULT SDLogger_FinishSafeEject(void)
{
  FRESULT result;

  if (s_status.file_opened != 0U)
  {
    result = f_sync(&s_log_file);
    if (result != FR_OK)
    {
      return result;
    }

    result = f_close(&s_log_file);
    s_status.file_opened = 0U;
    if (result != FR_OK)
    {
      return result;
    }
  }

  if (s_status.mounted != 0U)
  {
    result = f_mount(NULL, SDPath, 1U);
    if (result != FR_OK)
    {
      return result;
    }
    s_status.mounted = 0U;
  }

  s_file_dirty = 0U;
  s_status.accepting_records = 0U;
  s_status.queue_depth = 0U;
  s_status.last_sync_tick = HAL_GetTick();
  SD_Diskio_Invalidate();
  return FR_OK;
}

static void SDLogger_HandleIoError(FRESULT result, uint8_t allow_retry)
{
  s_status.write_error_count++;
  if (s_status.last_error < SD_LOGGER_ERROR_WRONG_FILESYSTEM)
  {
    s_status.last_error = (uint32_t)result;
  }
  s_status.accepting_records = 0U;
  SDLogger_ReleaseFileSystem(1U);
  s_status.state = SD_LOGGER_STATE_ERROR;
  s_retry_allowed = ((allow_retry != 0U) &&
                     (result != FR_DENIED) &&
                     (result != FR_WRITE_PROTECTED) &&
                     (result != FR_NO_FILESYSTEM)) ? 1U : 0U;
  s_next_retry_tick = HAL_GetTick() + SD_LOGGER_RETRY_INTERVAL_MS;
  SDLogger_MarkChanged();
}

static void SDLogger_ReleaseFileSystem(uint8_t close_file)
{
  if ((close_file != 0U) &&
      (s_status.file_opened != 0U) &&
      (s_card_raw_present != 0U))
  {
    (void)f_close(&s_log_file);
  }

  s_status.file_opened = 0U;
  if ((s_status.mounted != 0U) && (s_card_raw_present != 0U))
  {
    (void)f_mount(NULL, SDPath, 1U);
  }
  s_status.mounted = 0U;
  s_file_dirty = 0U;
  SD_Diskio_Invalidate();
}

static void SDLogger_ClearQueue(uint8_t count_as_dropped)
{
  if (count_as_dropped != 0U)
  {
    s_status.dropped_record_count += s_queue_count;
  }

  s_queue_head = 0U;
  s_queue_tail = 0U;
  s_queue_count = 0U;
  s_status.queue_depth = 0U;
}

static void SDLogger_PutU16(uint8_t *buffer, uint16_t value)
{
  buffer[0] = (uint8_t)(value & 0xFFU);
  buffer[1] = (uint8_t)(value >> 8U);
}

static void SDLogger_PutU32(uint8_t *buffer, uint32_t value)
{
  buffer[0] = (uint8_t)(value & 0xFFU);
  buffer[1] = (uint8_t)((value >> 8U) & 0xFFU);
  buffer[2] = (uint8_t)((value >> 16U) & 0xFFU);
  buffer[3] = (uint8_t)(value >> 24U);
}

static uint32_t SDLogger_Crc32(const uint8_t *buffer, size_t length)
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
