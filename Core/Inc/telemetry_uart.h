#ifndef __TELEMETRY_UART_H__
#define __TELEMETRY_UART_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "sensor_record.h"
#include "stm32f4xx_hal.h"

typedef struct
{
  uint8_t enabled;
  uint8_t busy;
  uint8_t pending;
  uint32_t last_tx_tick;
  uint32_t tx_success_count;
  uint32_t tx_error_count;
  uint32_t coalesced_count;
  uint32_t last_frame_length;
} TelemetryUartStatus_t;

void TelemetryUart_Init(UART_HandleTypeDef *huart);
void TelemetryUart_SetEnabled(uint8_t enabled);
void TelemetryUart_RequestSend(const SensorRecord_t *record);
uint8_t TelemetryUart_Task(void);
const TelemetryUartStatus_t *TelemetryUart_GetStatus(void);

#ifdef __cplusplus
}
#endif

#endif /* __TELEMETRY_UART_H__ */
