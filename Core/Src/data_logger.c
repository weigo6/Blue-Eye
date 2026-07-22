#include "data_logger.h"

#include <string.h>

#include "fatfs.h"
#include "fatfs_platform.h"
#include "sensor_record.h"

#define DATA_LOGGER_FILE_PATH "0:/SENSOR.BIN"
#define DATA_LOGGER_MAGIC 0x32455945UL
#define DATA_LOGGER_VERSION 0x0002U
#define DATA_LOGGER_HEADER_WIRE_SIZE 512U
#define DATA_LOGGER_HEADER_COPIES 2U
#define DATA_LOGGER_DATA_OFFSET (DATA_LOGGER_HEADER_WIRE_SIZE * DATA_LOGGER_HEADER_COPIES)
#define DATA_LOGGER_MIN_CAPACITY 64U
#define DATA_LOGGER_MAX_CAPACITY 262144U
#define DATA_LOGGER_RESERVED_BYTES (64UL * 1024UL)
#define DATA_LOGGER_QUEUE_CAPACITY 8U
#define DATA_LOGGER_RETRY_MS 1000U
#define DATA_LOGGER_ERROR_NO_CARD 0x10000UL
#define DATA_LOGGER_ERROR_CAPACITY 0x10001UL
#define DATA_LOGGER_ERROR_HEADER 0x10002UL

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t header_size;
  uint32_t record_size;
  uint32_t capacity_records;
  uint32_t next_write_index;
  uint32_t record_count;
  uint32_t overwrite_count;
  uint32_t generation;
} DataLoggerHeader_t;

static FIL s_log_file;
static DataLoggerHeader_t s_header;
static DataLoggerStatus_t s_status;
static SensorRecord_t s_record_queue[DATA_LOGGER_QUEUE_CAPACITY];
static uint8_t s_queue_head = 0U;
static uint8_t s_queue_tail = 0U;
static uint8_t s_queue_count = 0U;
static uint8_t s_file_opened = 0U;
static uint8_t s_active_header_copy = 1U;
static _Alignas(4) uint8_t s_io_buffer[DATA_LOGGER_HEADER_WIRE_SIZE];
static uint32_t s_next_retry_tick = 0U;

static FRESULT DataLogger_Mount(void);
static FRESULT DataLogger_WriteHeader(void);
static uint8_t DataLogger_ParseHeader(const uint8_t *buffer, DataLoggerHeader_t *header);
static void DataLogger_SerializeHeader(const DataLoggerHeader_t *header, uint8_t *buffer);
static uint8_t DataLogger_IsHeaderValid(const DataLoggerHeader_t *header);
static uint32_t DataLogger_QueryCapacity(void);
static FRESULT DataLogger_CreateFile(void);
static FRESULT DataLogger_OpenFile(void);
static FRESULT DataLogger_AppendRecord(const SensorRecord_t *record);
static void DataLogger_UpdateStatusFromHeader(void);
static void DataLogger_CloseFile(void);
static void DataLogger_StopSession(void);
static FRESULT DataLogger_StartSession(void);
static void DataLogger_DropQueuedRecords(void);
static uint16_t DataLogger_GetU16(const uint8_t *buffer);
static uint32_t DataLogger_GetU32(const uint8_t *buffer);
static void DataLogger_PutU16(uint8_t *buffer, uint16_t value);
static void DataLogger_PutU32(uint8_t *buffer, uint32_t value);
static uint32_t DataLogger_Crc32(const uint8_t *buffer, size_t length);

static FRESULT DataLogger_Mount(void)
{
  FRESULT result;

  if (BSP_PlatformIsDetected() != SD_PRESENT)
  {
    s_status.card_present = 0U;
    s_status.last_error = DATA_LOGGER_ERROR_NO_CARD;
    return FR_NOT_READY;
  }

  s_status.card_present = 1U;
  result = f_mount(&SDFatFS, SDPath, 1U);
  if (result == FR_NO_FILESYSTEM)
  {
    s_status.format_required = 1U;
  }

  s_status.mounted = (result == FR_OK) ? 1U : 0U;
  s_status.last_error = (uint32_t)result;
  return result;
}

static FRESULT DataLogger_WriteHeader(void)
{
  UINT bytes_written;
  FRESULT result;
  uint8_t target_copy;

  target_copy = (uint8_t)(s_active_header_copy ^ 1U);
  s_header.generation++;
  DataLogger_SerializeHeader(&s_header, s_io_buffer);

  result = f_lseek(&s_log_file, (FSIZE_t)target_copy * DATA_LOGGER_HEADER_WIRE_SIZE);
  if (result != FR_OK)
  {
    return result;
  }

  result = f_write(&s_log_file, s_io_buffer, sizeof(s_io_buffer), &bytes_written);
  if ((result != FR_OK) || (bytes_written != sizeof(s_io_buffer)))
  {
    return (result != FR_OK) ? result : FR_DISK_ERR;
  }

  result = f_sync(&s_log_file);
  if (result == FR_OK)
  {
    s_active_header_copy = target_copy;
  }
  return result;
}

static uint8_t DataLogger_ParseHeader(const uint8_t *buffer, DataLoggerHeader_t *header)
{
  uint32_t stored_crc;
  uint32_t calculated_crc;

  if ((buffer == NULL) || (header == NULL))
  {
    return 0U;
  }

  stored_crc = DataLogger_GetU32(&buffer[DATA_LOGGER_HEADER_WIRE_SIZE - 4U]);
  calculated_crc = DataLogger_Crc32(buffer, DATA_LOGGER_HEADER_WIRE_SIZE - 4U);
  if (stored_crc != calculated_crc)
  {
    return 0U;
  }

  header->magic = DataLogger_GetU32(&buffer[0]);
  header->version = DataLogger_GetU16(&buffer[4]);
  header->header_size = DataLogger_GetU16(&buffer[6]);
  header->record_size = DataLogger_GetU32(&buffer[8]);
  header->capacity_records = DataLogger_GetU32(&buffer[12]);
  header->next_write_index = DataLogger_GetU32(&buffer[16]);
  header->record_count = DataLogger_GetU32(&buffer[20]);
  header->overwrite_count = DataLogger_GetU32(&buffer[24]);
  header->generation = DataLogger_GetU32(&buffer[28]);
  return DataLogger_IsHeaderValid(header);
}

static void DataLogger_SerializeHeader(const DataLoggerHeader_t *header, uint8_t *buffer)
{
  uint32_t crc;

  (void)memset(buffer, 0, DATA_LOGGER_HEADER_WIRE_SIZE);
  DataLogger_PutU32(&buffer[0], header->magic);
  DataLogger_PutU16(&buffer[4], header->version);
  DataLogger_PutU16(&buffer[6], header->header_size);
  DataLogger_PutU32(&buffer[8], header->record_size);
  DataLogger_PutU32(&buffer[12], header->capacity_records);
  DataLogger_PutU32(&buffer[16], header->next_write_index);
  DataLogger_PutU32(&buffer[20], header->record_count);
  DataLogger_PutU32(&buffer[24], header->overwrite_count);
  DataLogger_PutU32(&buffer[28], header->generation);
  crc = DataLogger_Crc32(buffer, DATA_LOGGER_HEADER_WIRE_SIZE - 4U);
  DataLogger_PutU32(&buffer[DATA_LOGGER_HEADER_WIRE_SIZE - 4U], crc);
}

static uint8_t DataLogger_IsHeaderValid(const DataLoggerHeader_t *header)
{
  if ((header == NULL) ||
      (header->magic != DATA_LOGGER_MAGIC) ||
      (header->version != DATA_LOGGER_VERSION) ||
      (header->header_size != DATA_LOGGER_HEADER_WIRE_SIZE) ||
      (header->record_size != SENSOR_RECORD_WIRE_SIZE) ||
      (header->capacity_records == 0U) ||
      (header->capacity_records > DATA_LOGGER_MAX_CAPACITY) ||
      (header->next_write_index >= header->capacity_records) ||
      (header->record_count > header->capacity_records))
  {
    return 0U;
  }

  return 1U;
}

static uint32_t DataLogger_QueryCapacity(void)
{
  DWORD free_clusters;
  FATFS *file_system;
  FRESULT result;
  uint64_t free_bytes;
  uint64_t usable_bytes;
  uint64_t capacity;

  result = f_getfree(SDPath, &free_clusters, &file_system);
  if ((result != FR_OK) || (file_system == NULL))
  {
    s_status.last_error = (uint32_t)result;
    return 0U;
  }

  free_bytes = (uint64_t)free_clusters * (uint64_t)file_system->csize * (uint64_t)_MIN_SS;
  usable_bytes = (free_bytes > (DATA_LOGGER_RESERVED_BYTES + DATA_LOGGER_DATA_OFFSET))
                   ? (free_bytes - DATA_LOGGER_RESERVED_BYTES - DATA_LOGGER_DATA_OFFSET)
                   : (free_bytes / 2ULL);
  capacity = usable_bytes / SENSOR_RECORD_WIRE_SIZE;

  if (capacity > DATA_LOGGER_MAX_CAPACITY)
  {
    capacity = DATA_LOGGER_MAX_CAPACITY;
  }

  if ((capacity < DATA_LOGGER_MIN_CAPACITY) &&
      (free_bytes >= (DATA_LOGGER_DATA_OFFSET +
                      ((uint64_t)DATA_LOGGER_MIN_CAPACITY * SENSOR_RECORD_WIRE_SIZE))))
  {
    capacity = DATA_LOGGER_MIN_CAPACITY;
  }

  return (uint32_t)capacity;
}

static FRESULT DataLogger_CreateFile(void)
{
  FRESULT result;
  uint32_t capacity;
  FSIZE_t total_size;

  capacity = DataLogger_QueryCapacity();
  if (capacity == 0U)
  {
    s_status.last_error = DATA_LOGGER_ERROR_CAPACITY;
    return FR_DENIED;
  }

  (void)memset(&s_header, 0, sizeof(s_header));
  s_header.magic = DATA_LOGGER_MAGIC;
  s_header.version = DATA_LOGGER_VERSION;
  s_header.header_size = DATA_LOGGER_HEADER_WIRE_SIZE;
  s_header.record_size = SENSOR_RECORD_WIRE_SIZE;
  s_header.capacity_records = capacity;
  s_active_header_copy = 1U;

  total_size = (FSIZE_t)DATA_LOGGER_DATA_OFFSET +
               ((FSIZE_t)capacity * (FSIZE_t)SENSOR_RECORD_WIRE_SIZE);
  result = f_lseek(&s_log_file, total_size);
  if (result == FR_OK)
  {
    result = f_truncate(&s_log_file);
  }
  if (result != FR_OK)
  {
    return result;
  }

  result = DataLogger_WriteHeader();
  if (result == FR_OK)
  {
    result = DataLogger_WriteHeader();
  }
  return result;
}

static FRESULT DataLogger_OpenFile(void)
{
  FRESULT result;
  UINT bytes_read = 0U;
  DataLoggerHeader_t header_a;
  DataLoggerHeader_t header_b;
  uint8_t valid_a = 0U;
  uint8_t valid_b = 0U;
  FSIZE_t file_size;

  result = f_open(&s_log_file, DATA_LOGGER_FILE_PATH, FA_OPEN_ALWAYS | FA_READ | FA_WRITE);
  if (result != FR_OK)
  {
    s_status.last_error = (uint32_t)result;
    return result;
  }
  s_file_opened = 1U;
  file_size = f_size(&s_log_file);

  if (file_size == 0U)
  {
    result = DataLogger_CreateFile();
    s_status.last_error = (uint32_t)result;
    return result;
  }

  if (file_size < DATA_LOGGER_DATA_OFFSET)
  {
    s_status.last_error = DATA_LOGGER_ERROR_HEADER;
    return FR_INVALID_OBJECT;
  }

  result = f_lseek(&s_log_file, 0U);
  if (result == FR_OK)
  {
    result = f_read(&s_log_file, s_io_buffer, sizeof(s_io_buffer), &bytes_read);
  }
  if ((result == FR_OK) && (bytes_read == sizeof(s_io_buffer)))
  {
    valid_a = DataLogger_ParseHeader(s_io_buffer, &header_a);
  }

  bytes_read = 0U;
  if (result == FR_OK)
  {
    result = f_lseek(&s_log_file, DATA_LOGGER_HEADER_WIRE_SIZE);
  }
  if (result == FR_OK)
  {
    result = f_read(&s_log_file, s_io_buffer, sizeof(s_io_buffer), &bytes_read);
  }
  if ((result == FR_OK) && (bytes_read == sizeof(s_io_buffer)))
  {
    valid_b = DataLogger_ParseHeader(s_io_buffer, &header_b);
  }

  if ((valid_a == 0U) && (valid_b == 0U))
  {
    s_status.last_error = DATA_LOGGER_ERROR_HEADER;
    return FR_INVALID_OBJECT;
  }

  if ((valid_b != 0U) &&
      ((valid_a == 0U) || ((int32_t)(header_b.generation - header_a.generation) > 0)))
  {
    s_header = header_b;
    s_active_header_copy = 1U;
  }
  else
  {
    s_header = header_a;
    s_active_header_copy = 0U;
  }

  if (file_size < ((FSIZE_t)DATA_LOGGER_DATA_OFFSET +
                   ((FSIZE_t)s_header.capacity_records * SENSOR_RECORD_WIRE_SIZE)))
  {
    s_status.last_error = DATA_LOGGER_ERROR_HEADER;
    return FR_INVALID_OBJECT;
  }

  return FR_OK;
}

static FRESULT DataLogger_AppendRecord(const SensorRecord_t *record)
{
  UINT bytes_written;
  FRESULT result;
  FSIZE_t record_offset;
  DataLoggerHeader_t previous_header;
  uint8_t record_buffer[SENSOR_RECORD_WIRE_SIZE];

  if ((record == NULL) || (s_header.capacity_records == 0U) ||
      (SensorRecord_Serialize(record, record_buffer, sizeof(record_buffer)) == 0U))
  {
    return FR_INVALID_OBJECT;
  }

  previous_header = s_header;

  record_offset = (FSIZE_t)DATA_LOGGER_DATA_OFFSET +
                  ((FSIZE_t)s_header.next_write_index * SENSOR_RECORD_WIRE_SIZE);
  result = f_lseek(&s_log_file, record_offset);
  if (result != FR_OK)
  {
    return result;
  }

  result = f_write(&s_log_file, record_buffer, sizeof(record_buffer), &bytes_written);
  if ((result != FR_OK) || (bytes_written != sizeof(record_buffer)))
  {
    return (result != FR_OK) ? result : FR_DISK_ERR;
  }

  if (s_header.record_count < s_header.capacity_records)
  {
    s_header.record_count++;
  }
  else
  {
    s_header.overwrite_count++;
  }

  s_header.next_write_index++;
  if (s_header.next_write_index >= s_header.capacity_records)
  {
    s_header.next_write_index = 0U;
  }

  result = DataLogger_WriteHeader();
  if (result != FR_OK)
  {
    s_header = previous_header;
    return result;
  }

  s_status.last_write_tick = HAL_GetTick();
  s_status.write_success_count++;
  s_status.last_error = (uint32_t)FR_OK;
  DataLogger_UpdateStatusFromHeader();
  return FR_OK;
}

static void DataLogger_UpdateStatusFromHeader(void)
{
  s_status.capacity_records = s_header.capacity_records;
  s_status.record_count = s_header.record_count;
  s_status.next_write_index = s_header.next_write_index;
  s_status.overwrite_count = s_header.overwrite_count;
}

static void DataLogger_CloseFile(void)
{
  if (s_file_opened == 0U)
  {
    return;
  }

  (void)f_sync(&s_log_file);
  (void)f_close(&s_log_file);
  s_file_opened = 0U;
}

static void DataLogger_StopSession(void)
{
  DataLogger_CloseFile();
  if (s_status.mounted != 0U)
  {
    (void)f_mount(NULL, SDPath, 1U);
  }
  s_status.mounted = 0U;
  s_status.card_present = (BSP_PlatformIsDetected() == SD_PRESENT) ? 1U : 0U;
}

static FRESULT DataLogger_StartSession(void)
{
  FRESULT result;

  result = DataLogger_Mount();
  if (result != FR_OK)
  {
    return result;
  }

  result = DataLogger_OpenFile();
  if (result != FR_OK)
  {
    DataLogger_StopSession();
    return result;
  }

  s_status.format_required = 0U;
  DataLogger_UpdateStatusFromHeader();
  return FR_OK;
}

void DataLogger_Init(void)
{
  (void)memset(&s_status, 0, sizeof(s_status));
  (void)memset(&s_header, 0, sizeof(s_header));
  (void)memset(s_record_queue, 0, sizeof(s_record_queue));
  s_queue_head = 0U;
  s_queue_tail = 0U;
  s_queue_count = 0U;
  s_next_retry_tick = 0U;
  s_status.enabled = 1U;
  s_status.card_present = (BSP_PlatformIsDetected() == SD_PRESENT) ? 1U : 0U;
}

void DataLogger_RequestSnapshot(const SensorRecord_t *record)
{
  if ((s_status.enabled == 0U) || (record == NULL))
  {
    return;
  }

  if (s_queue_count >= DATA_LOGGER_QUEUE_CAPACITY)
  {
    s_status.dropped_record_count++;
    return;
  }

  s_record_queue[s_queue_head] = *record;
  s_queue_head = (uint8_t)((s_queue_head + 1U) % DATA_LOGGER_QUEUE_CAPACITY);
  s_queue_count++;
  s_status.pending_records = s_queue_count;
}

void DataLogger_SetEnabled(uint8_t enabled)
{
  if (enabled != 0U)
  {
    s_status.enabled = 1U;
    s_next_retry_tick = 0U;
    return;
  }

  s_status.enabled = 0U;
}

uint8_t DataLogger_Task(void)
{
  FRESULT result;
  uint32_t now = HAL_GetTick();
  uint8_t old_card_present = s_status.card_present;
  uint8_t status_changed;

  s_status.card_present = (BSP_PlatformIsDetected() == SD_PRESENT) ? 1U : 0U;
  status_changed = (old_card_present != s_status.card_present) ? 1U : 0U;

  if ((status_changed != 0U) && (s_status.card_present != 0U))
  {
    s_status.format_required = 0U;
    s_status.last_error = (uint32_t)FR_OK;
    s_next_retry_tick = 0U;
  }

  if (s_status.enabled == 0U)
  {
    uint8_t did_work = ((s_file_opened != 0U) || (s_status.mounted != 0U) || (s_queue_count != 0U)) ? 1U : 0U;
    DataLogger_StopSession();
    DataLogger_DropQueuedRecords();
    return (uint8_t)(did_work | status_changed);
  }

  if (s_status.card_present == 0U)
  {
    if ((s_file_opened != 0U) || (s_status.mounted != 0U))
    {
      DataLogger_StopSession();
      status_changed = 1U;
    }
    s_status.last_error = DATA_LOGGER_ERROR_NO_CARD;
    return status_changed;
  }

  if ((s_queue_count == 0U) || ((int32_t)(now - s_next_retry_tick) < 0))
  {
    return status_changed;
  }

  if (s_file_opened == 0U)
  {
    result = DataLogger_StartSession();
    if (result != FR_OK)
    {
      s_status.write_error_count++;
      s_next_retry_tick = now + DATA_LOGGER_RETRY_MS;
      return 1U;
    }
  }

  result = DataLogger_AppendRecord(&s_record_queue[s_queue_tail]);
  if (result == FR_OK)
  {
    s_queue_tail = (uint8_t)((s_queue_tail + 1U) % DATA_LOGGER_QUEUE_CAPACITY);
    s_queue_count--;
    s_status.pending_records = s_queue_count;
    s_next_retry_tick = 0U;
    return 1U;
  }

  s_status.write_error_count++;
  s_status.last_error = (uint32_t)result;
  s_next_retry_tick = now + DATA_LOGGER_RETRY_MS;
  DataLogger_StopSession();
  return 1U;
}

const DataLoggerStatus_t *DataLogger_GetStatus(void)
{
  return &s_status;
}

static void DataLogger_DropQueuedRecords(void)
{
  s_status.dropped_record_count += s_queue_count;
  s_queue_head = 0U;
  s_queue_tail = 0U;
  s_queue_count = 0U;
  s_status.pending_records = 0U;
}

static uint16_t DataLogger_GetU16(const uint8_t *buffer)
{
  return (uint16_t)((uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8U));
}

static uint32_t DataLogger_GetU32(const uint8_t *buffer)
{
  return (uint32_t)buffer[0] |
         ((uint32_t)buffer[1] << 8U) |
         ((uint32_t)buffer[2] << 16U) |
         ((uint32_t)buffer[3] << 24U);
}

static void DataLogger_PutU16(uint8_t *buffer, uint16_t value)
{
  buffer[0] = (uint8_t)(value & 0xFFU);
  buffer[1] = (uint8_t)(value >> 8U);
}

static void DataLogger_PutU32(uint8_t *buffer, uint32_t value)
{
  buffer[0] = (uint8_t)(value & 0xFFU);
  buffer[1] = (uint8_t)((value >> 8U) & 0xFFU);
  buffer[2] = (uint8_t)((value >> 16U) & 0xFFU);
  buffer[3] = (uint8_t)(value >> 24U);
}

static uint32_t DataLogger_Crc32(const uint8_t *buffer, size_t length)
{
  uint32_t crc = 0xFFFFFFFFUL;
  size_t index;
  uint8_t bit;

  for (index = 0U; index < length; index++)
  {
    crc ^= buffer[index];
    for (bit = 0U; bit < 8U; bit++)
    {
      crc = ((crc & 1U) != 0U) ? ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
    }
  }

  return ~crc;
}
