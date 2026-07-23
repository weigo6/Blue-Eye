#include "telemetry_uart.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "sensor_record.h"
#include "uart_dma_support.h"

#define TELEMETRY_UART_TIMEOUT_MS 500U

static UART_HandleTypeDef *s_telemetry_uart = NULL;
static TelemetryUartStatus_t s_status;
static SensorRecord_t s_pending_record;
static char s_frame[208];
static uint16_t s_frame_length = 0U;
static uint32_t s_tx_start_tick = 0U;

static uint8_t TelemetryUart_CalculateXor(const char *payload, size_t length);
static uint8_t TelemetryUart_BuildFrame(void);

void TelemetryUart_Init(UART_HandleTypeDef *huart)
{
  (void)memset(&s_status, 0, sizeof(s_status));
  (void)memset(&s_pending_record, 0, sizeof(s_pending_record));
  s_telemetry_uart = huart;
  s_status.enabled = (huart != NULL) ? 1U : 0U;
}

void TelemetryUart_SetEnabled(uint8_t enabled)
{
  if (enabled == 0U)
  {
    if ((s_telemetry_uart != NULL) && (s_status.busy != 0U))
    {
      UartDmaSupport_Abort(s_telemetry_uart);
    }
    s_status.enabled = 0U;
    s_status.busy = 0U;
    s_status.pending = 0U;
    return;
  }

  s_status.enabled = (s_telemetry_uart != NULL) ? 1U : 0U;
}

void TelemetryUart_RequestSend(const SensorRecord_t *record)
{
  if ((s_status.enabled == 0U) || (s_telemetry_uart == NULL) || (record == NULL))
  {
    return;
  }

  if ((s_status.pending != 0U) || (s_status.busy != 0U))
  {
    s_status.coalesced_count++;
  }

  s_pending_record = *record;
  s_status.pending = 1U;
}

uint8_t TelemetryUart_Task(void)
{
  if ((s_status.enabled == 0U) || (s_telemetry_uart == NULL))
  {
    return 0U;
  }

  if (s_status.busy != 0U)
  {
    if (UartDmaSupport_HasError(s_telemetry_uart) != 0U)
    {
      UartDmaSupport_Abort(s_telemetry_uart);
      s_status.busy = 0U;
      s_status.tx_error_count++;
      return 1U;
    }

    if (UartDmaSupport_IsTxDone(s_telemetry_uart) != 0U)
    {
      s_status.busy = 0U;
      s_status.last_tx_tick = HAL_GetTick();
      s_status.tx_success_count++;
      return 1U;
    }

    if ((HAL_GetTick() - s_tx_start_tick) >= TELEMETRY_UART_TIMEOUT_MS)
    {
      UartDmaSupport_Abort(s_telemetry_uart);
      s_status.busy = 0U;
      s_status.tx_error_count++;
      return 1U;
    }

    return 0U;
  }

  if (s_status.pending == 0U)
  {
    return 0U;
  }

  if (TelemetryUart_BuildFrame() == 0U)
  {
    s_status.pending = 0U;
    s_status.tx_error_count++;
    return 1U;
  }

  UartDmaSupport_ClearFlags(s_telemetry_uart);
  if (UartDmaSupport_StartTransmitDMA(s_telemetry_uart,
                                       (uint8_t *)s_frame,
                                       s_frame_length) != HAL_OK)
  {
    s_status.pending = 0U;
    s_status.tx_error_count++;
    return 1U;
  }

  s_status.last_frame_length = s_frame_length;
  s_status.pending = 0U;
  s_status.busy = 1U;
  s_tx_start_tick = HAL_GetTick();
  return 0U;
}

const TelemetryUartStatus_t *TelemetryUart_GetStatus(void)
{
  return &s_status;
}

static uint8_t TelemetryUart_CalculateXor(const char *payload, size_t length)
{
  uint8_t checksum = 0U;
  size_t index;

  for (index = 0U; index < length; index++)
  {
    checksum ^= (uint8_t)payload[index];
  }

  return checksum;
}

static uint8_t TelemetryUart_BuildFrame(void)
{
  char payload[192];
  size_t payload_length;
  int payload_chars;
  int frame_chars;
  int32_t pressure_value_x1000 = INT32_MIN;
  uint8_t checksum;
  double scaled_pressure;

  if ((s_pending_record.pressure_float_valid != 0U) &&
      isfinite(s_pending_record.pressure_value))
  {
    scaled_pressure = (double)s_pending_record.pressure_value * 1000.0;
    if ((scaled_pressure <= (double)INT32_MAX) && (scaled_pressure >= (double)INT32_MIN))
    {
      pressure_value_x1000 = (int32_t)scaled_pressure;
    }
  }

  payload_chars = snprintf(payload,
                            sizeof(payload),
                            "BE,%lu,%u,%u,%u,%u,%d,%ld,%u,%u,%u,%u,%u,%d,%u,%u",
                            (unsigned long)s_pending_record.tick_ms,
                            (unsigned int)s_pending_record.pressure_online,
                            (unsigned int)s_pending_record.pressure_status,
                            (unsigned int)s_pending_record.pressure_read_mode,
                            (unsigned int)s_pending_record.pressure_float_valid,
                            (int)s_pending_record.pressure_raw,
                            (long)pressure_value_x1000,
                            (unsigned int)s_pending_record.pressure_unit_code,
                            (unsigned int)s_pending_record.pressure_decimal_point,
                            (unsigned int)s_pending_record.xda_online,
                            (unsigned int)s_pending_record.xda_status,
                            (unsigned int)s_pending_record.ec_x100,
                            (int)s_pending_record.temperature_x10,
                            (unsigned int)s_pending_record.tds_ppm,
                            (unsigned int)s_pending_record.salinity_ppm);
  if ((payload_chars <= 0) || ((size_t)payload_chars >= sizeof(payload)))
  {
    return 0U;
  }

  payload_length = (size_t)payload_chars;
  checksum = TelemetryUart_CalculateXor(payload, payload_length);
  frame_chars = snprintf(s_frame, sizeof(s_frame), "$%s*%02X\r\n", payload, (unsigned int)checksum);
  if ((frame_chars <= 0) || ((size_t)frame_chars >= sizeof(s_frame)))
  {
    return 0U;
  }

  s_frame_length = (uint16_t)frame_chars;
  return 1U;
}
