#include "uart_dma_support.h"

typedef struct
{
  UART_HandleTypeDef *huart;
  volatile uint8_t tx_done;
  volatile uint8_t rx_done;
  volatile uint8_t error;
  volatile uint8_t chained_rx_pending;
  volatile uint16_t rx_length;
  uint8_t *chained_rx_buffer;
  uint16_t chained_rx_capacity;
} UartDmaContext_t;

static UartDmaContext_t s_uart_dma_contexts[3] = {0};

static UartDmaContext_t *UartDmaSupport_GetContext(UART_HandleTypeDef *huart)
{
  uint32_t index;

  if (huart == NULL)
  {
    return NULL;
  }

  for (index = 0U; index < (sizeof(s_uart_dma_contexts) / sizeof(s_uart_dma_contexts[0])); index++)
  {
    if ((s_uart_dma_contexts[index].huart == huart) || (s_uart_dma_contexts[index].huart == NULL))
    {
      s_uart_dma_contexts[index].huart = huart;
      return &s_uart_dma_contexts[index];
    }
  }

  return NULL;
}

HAL_StatusTypeDef UartDmaSupport_StartTransmitDMA(UART_HandleTypeDef *huart,
                                                  uint8_t *buffer,
                                                  uint16_t length)
{
  UartDmaContext_t *context = UartDmaSupport_GetContext(huart);

  if ((context == NULL) || (buffer == NULL) || (length == 0U))
  {
    return HAL_ERROR;
  }

  context->tx_done = 0U;
  context->error = 0U;
  context->chained_rx_pending = 0U;
  return HAL_UART_Transmit_DMA(huart, buffer, length);
}

HAL_StatusTypeDef UartDmaSupport_StartTransactionITToIdleDMA(UART_HandleTypeDef *huart,
                                                             uint8_t *tx_buffer,
                                                             uint16_t tx_length,
                                                             uint8_t *rx_buffer,
                                                             uint16_t rx_capacity)
{
  HAL_StatusTypeDef status;
  UartDmaContext_t *context = UartDmaSupport_GetContext(huart);

  if ((context == NULL) || (tx_buffer == NULL) || (tx_length == 0U) ||
      (rx_buffer == NULL) || (rx_capacity == 0U) || (huart->hdmarx == NULL))
  {
    return HAL_ERROR;
  }

  context->tx_done = 0U;
  context->rx_done = 0U;
  context->rx_length = 0U;
  context->error = 0U;
  context->chained_rx_buffer = rx_buffer;
  context->chained_rx_capacity = rx_capacity;
  context->chained_rx_pending = 1U;

  status = HAL_UART_Transmit_IT(huart, tx_buffer, tx_length);
  if (status != HAL_OK)
  {
    context->chained_rx_pending = 0U;
  }

  return status;
}

void UartDmaSupport_Abort(UART_HandleTypeDef *huart)
{
  UartDmaContext_t *context = UartDmaSupport_GetContext(huart);

  if (huart == NULL)
  {
    return;
  }

  if (context != NULL)
  {
    context->chained_rx_pending = 0U;
  }

  (void)HAL_UART_AbortTransmit(huart);
  (void)HAL_UART_AbortReceive(huart);
}

void UartDmaSupport_ClearFlags(UART_HandleTypeDef *huart)
{
  UartDmaContext_t *context = UartDmaSupport_GetContext(huart);

  if (context == NULL)
  {
    return;
  }

  context->tx_done = 0U;
  context->rx_done = 0U;
  context->rx_length = 0U;
  context->error = 0U;
  context->chained_rx_pending = 0U;
}

uint8_t UartDmaSupport_IsTxDone(UART_HandleTypeDef *huart)
{
  UartDmaContext_t *context = UartDmaSupport_GetContext(huart);
  return (context != NULL) ? context->tx_done : 0U;
}

uint8_t UartDmaSupport_IsRxDone(UART_HandleTypeDef *huart)
{
  UartDmaContext_t *context = UartDmaSupport_GetContext(huart);
  return (context != NULL) ? context->rx_done : 0U;
}

uint16_t UartDmaSupport_GetRxLength(UART_HandleTypeDef *huart)
{
  UartDmaContext_t *context = UartDmaSupport_GetContext(huart);
  return (context != NULL) ? context->rx_length : 0U;
}

uint8_t UartDmaSupport_HasError(UART_HandleTypeDef *huart)
{
  UartDmaContext_t *context = UartDmaSupport_GetContext(huart);
  return (context != NULL) ? context->error : 1U;
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  HAL_StatusTypeDef status;
  UartDmaContext_t *context = UartDmaSupport_GetContext(huart);

  if (context == NULL)
  {
    return;
  }

  context->tx_done = 1U;
  if (context->chained_rx_pending == 0U)
  {
    return;
  }

  context->chained_rx_pending = 0U;
  status = HAL_UARTEx_ReceiveToIdle_DMA(huart,
                                        context->chained_rx_buffer,
                                        context->chained_rx_capacity);
  if (status != HAL_OK)
  {
    context->error = 1U;
    return;
  }

  __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  UartDmaContext_t *context = UartDmaSupport_GetContext(huart);

  if (context != NULL)
  {
    context->rx_length = huart->RxXferSize;
    context->rx_done = 1U;
  }
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
  UartDmaContext_t *context = UartDmaSupport_GetContext(huart);

  if (context != NULL)
  {
    context->rx_length = size;
    context->rx_done = 1U;
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  UartDmaContext_t *context = UartDmaSupport_GetContext(huart);

  if (context != NULL)
  {
    context->chained_rx_pending = 0U;
    context->error = 1U;
  }
}
