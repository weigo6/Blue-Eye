#ifndef __UART_DMA_SUPPORT_H__
#define __UART_DMA_SUPPORT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "stm32f4xx_hal.h"

HAL_StatusTypeDef UartDmaSupport_StartTransmitDMA(UART_HandleTypeDef *huart,
                                                  uint8_t *buffer,
                                                  uint16_t length);
HAL_StatusTypeDef UartDmaSupport_StartTransactionITToIdleDMA(UART_HandleTypeDef *huart,
                                                             uint8_t *tx_buffer,
                                                             uint16_t tx_length,
                                                             uint8_t *rx_buffer,
                                                             uint16_t rx_capacity);
void UartDmaSupport_Abort(UART_HandleTypeDef *huart);
void UartDmaSupport_ClearFlags(UART_HandleTypeDef *huart);
uint8_t UartDmaSupport_IsTxDone(UART_HandleTypeDef *huart);
uint8_t UartDmaSupport_IsRxDone(UART_HandleTypeDef *huart);
uint16_t UartDmaSupport_GetRxLength(UART_HandleTypeDef *huart);
uint8_t UartDmaSupport_HasError(UART_HandleTypeDef *huart);

#ifdef __cplusplus
}
#endif

#endif /* __UART_DMA_SUPPORT_H__ */
