/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    sd_diskio.c
  * @brief   SD Disk I/O driver
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Note: adapted to DMA transfer flow while keeping CubeMX-compatible structure. */

/* USER CODE BEGIN firstSection */
/* can be used to modify / undefine following code or add new definitions */
/* USER CODE END firstSection*/

/* Includes ------------------------------------------------------------------*/
#include <string.h>

#include "ff_gen_drv.h"
#include "sd_diskio.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
#define SD_DEFAULT_BLOCK_SIZE 512U
#define SD_DMA_ALIGNMENT 4U
#define SD_DMA_TRANSFER_TIMEOUT_MS 1000U
#define SD_CARD_READY_TIMEOUT_MS 500U

/*
 * Depending on the use case, the SD card initialization could be done at the
 * application level: if it is the case define the flag below to disable
 * the BSP_SD_Init() call in the SD_Initialize() and add a call to
 * BSP_SD_Init() elsewhere in the application.
 */
/* USER CODE BEGIN disableSDInit */
/* #define DISABLE_SD_INIT */
/* USER CODE END disableSDInit */

/* Private variables ---------------------------------------------------------*/
/* Disk status */
static volatile DSTATUS Stat = STA_NOINIT;
static volatile uint8_t ReadStatus = 0U;
#if _USE_WRITE == 1
static volatile uint8_t WriteStatus = 0U;
#endif
static volatile uint8_t TransferErrorStatus = 0U;
static uint32_t scratch[SD_DEFAULT_BLOCK_SIZE / sizeof(uint32_t)] = {0};
static SD_DiskioDiagnostics_t s_diagnostics = {0};
static volatile uint8_t s_error_latched = 0U;
static volatile uint8_t s_last_card_state = 0U;

extern SD_HandleTypeDef hsd;

/* Private function prototypes -----------------------------------------------*/
static DSTATUS SD_CheckStatus(BYTE lun);
static uint8_t SD_WaitForCardReady(uint32_t timeout_ms);
static DRESULT SD_WaitForTransfer(volatile uint8_t *completion_status);
static void SD_BeginDiagnostics(SD_DiskioStage_t stage);
static void SD_UpdateDiagnostics(SD_DiskioStage_t stage, DRESULT result);
static uint8_t SD_IsPrimaryFailure(SD_DiskioStage_t stage);
DSTATUS SD_initialize (BYTE);
DSTATUS SD_status (BYTE);
DRESULT SD_read (BYTE, BYTE*, DWORD, UINT);
#if _USE_WRITE == 1
DRESULT SD_write (BYTE, const BYTE*, DWORD, UINT);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
DRESULT SD_ioctl (BYTE, BYTE, void*);
#endif  /* _USE_IOCTL == 1 */

const Diskio_drvTypeDef  SD_Driver =
{
  SD_initialize,
  SD_status,
  SD_read,
#if  _USE_WRITE == 1
  SD_write,
#endif /* _USE_WRITE == 1 */

#if  _USE_IOCTL == 1
  SD_ioctl,
#endif /* _USE_IOCTL == 1 */
};

/* USER CODE BEGIN beforeFunctionSection */
/* can be used to modify / undefine following code or add new code */
/* USER CODE END beforeFunctionSection */

/* Private functions ---------------------------------------------------------*/

static DSTATUS SD_CheckStatus(BYTE lun)
{
  (void)lun;

  if (BSP_SD_IsDetected() != SD_PRESENT)
  {
    s_last_card_state = (uint8_t)HAL_SD_CARD_DISCONNECTED;
    Stat |= STA_NOINIT;
  }
  else if ((hsd.Instance == NULL) ||
           (hsd.State == HAL_SD_STATE_RESET) ||
           (hsd.State == HAL_SD_STATE_TIMEOUT) ||
           (hsd.State == HAL_SD_STATE_ERROR) ||
           (HAL_SD_GetError(&hsd) != HAL_SD_ERROR_NONE))
  {
    Stat |= STA_NOINIT;
  }
  else
  {
    Stat &= (DSTATUS)~STA_NOINIT;
  }

  return Stat;
}

static uint8_t SD_WaitForCardReady(uint32_t timeout_ms)
{
  uint32_t start_tick = HAL_GetTick();
  HAL_SD_CardStateTypeDef card_state;

  SD_UpdateDiagnostics(SD_DISKIO_STAGE_INIT_WAIT_READY, RES_OK);

  while ((HAL_GetTick() - start_tick) < timeout_ms)
  {
    if (BSP_SD_IsDetected() != SD_PRESENT)
    {
      s_last_card_state = (uint8_t)HAL_SD_CARD_DISCONNECTED;
      return 0U;
    }

    card_state = HAL_SD_GetCardState(&hsd);
    s_last_card_state = (uint8_t)card_state;
    if ((card_state == HAL_SD_CARD_TRANSFER) &&
        (HAL_SD_GetError(&hsd) == HAL_SD_ERROR_NONE))
    {
      return 1U;
    }

    if (HAL_SD_GetError(&hsd) != HAL_SD_ERROR_NONE)
    {
      return 0U;
    }
  }

  SD_UpdateDiagnostics(SD_DISKIO_STAGE_INIT_READY_TIMEOUT, RES_NOTRDY);
  return 0U;
}

static DRESULT SD_WaitForTransfer(volatile uint8_t *completion_status)
{
  uint32_t start_tick = HAL_GetTick();
  uint8_t is_read = (completion_status == &ReadStatus) ? 1U : 0U;
  HAL_SD_CardStateTypeDef card_state;

  SD_UpdateDiagnostics((is_read != 0U) ? SD_DISKIO_STAGE_READ_WAIT
                                      : SD_DISKIO_STAGE_WRITE_WAIT,
                       RES_OK);

  while (1)
  {
    if (BSP_SD_IsDetected() != SD_PRESENT)
    {
      SD_UpdateDiagnostics(SD_DISKIO_STAGE_CARD_REMOVED, RES_NOTRDY);
      (void)HAL_SD_Abort(&hsd);
      Stat |= STA_NOINIT;
      return RES_NOTRDY;
    }

    if ((TransferErrorStatus != 0U) || (HAL_SD_GetError(&hsd) != HAL_SD_ERROR_NONE))
    {
      SD_UpdateDiagnostics((is_read != 0U) ? SD_DISKIO_STAGE_READ_HAL_ERROR
                                          : SD_DISKIO_STAGE_WRITE_HAL_ERROR,
                           RES_ERROR);
      return RES_ERROR;
    }

    if (*completion_status != 0U)
    {
      card_state = HAL_SD_GetCardState(&hsd);
      s_last_card_state = (uint8_t)card_state;
      if ((card_state == HAL_SD_CARD_TRANSFER) &&
          (HAL_SD_GetError(&hsd) == HAL_SD_ERROR_NONE))
      {
        SD_UpdateDiagnostics((is_read != 0U) ? SD_DISKIO_STAGE_READ_COMPLETE
                                            : SD_DISKIO_STAGE_WRITE_COMPLETE,
                             RES_OK);
        return RES_OK;
      }
    }

    if ((HAL_GetTick() - start_tick) >= SD_DMA_TRANSFER_TIMEOUT_MS)
    {
      SD_UpdateDiagnostics((is_read != 0U) ? SD_DISKIO_STAGE_READ_TIMEOUT
                                          : SD_DISKIO_STAGE_WRITE_TIMEOUT,
                           RES_ERROR);
      (void)HAL_SD_Abort(&hsd);
      Stat |= STA_NOINIT;
      return RES_ERROR;
    }
  }
}

/**
  * @brief  Initializes a Drive
  * @param  lun : not used
  * @retval DSTATUS: Operation status
  */
DSTATUS SD_initialize(BYTE lun)
{
  (void)lun;
  if (hsd.State != HAL_SD_STATE_RESET)
  {
    SD_Diskio_Invalidate();
  }
  Stat = STA_NOINIT;
  ReadStatus = 0U;
#if _USE_WRITE == 1
  WriteStatus = 0U;
#endif
  TransferErrorStatus = 0U;
  s_last_card_state = 0U;
  s_error_latched = 0U;
  s_diagnostics.init_attempt_count++;
  SD_BeginDiagnostics(SD_DISKIO_STAGE_INITIALIZING);

#if !defined(DISABLE_SD_INIT)

  if ((BSP_SD_IsDetected() == SD_PRESENT) &&
      (BSP_SD_Init() == MSD_OK) &&
      (SD_WaitForCardReady(SD_CARD_READY_TIMEOUT_MS) != 0U))
  {
    Stat &= (DSTATUS)~STA_NOINIT;
  }

#else
  Stat = SD_CheckStatus(0U);
#endif

  SD_UpdateDiagnostics(((Stat & STA_NOINIT) == 0U) ? SD_DISKIO_STAGE_READY
                                                   : SD_DISKIO_STAGE_INIT_FAILED,
                       ((Stat & STA_NOINIT) == 0U) ? RES_OK : RES_NOTRDY);

  return Stat;
}

/**
  * @brief  Gets Disk Status
  * @param  lun : not used
  * @retval DSTATUS: Operation status
  */
DSTATUS SD_status(BYTE lun)
{
  return SD_CheckStatus(lun);
}

/* USER CODE BEGIN beforeReadSection */
/* can be used to modify previous code / undefine following code / add new code */
/* USER CODE END beforeReadSection */
/**
  * @brief  Reads Sector(s)
  * @param  lun : not used
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */

DRESULT SD_read(BYTE lun, BYTE *buff, DWORD sector, UINT count)
{
  DRESULT res = RES_ERROR;
  uintptr_t buffer_address;

  (void)lun;

  if (((Stat & STA_NOINIT) != 0U) || (BSP_SD_IsDetected() != SD_PRESENT))
  {
    SD_BeginDiagnostics(SD_DISKIO_STAGE_READ_NOT_READY);
    SD_UpdateDiagnostics(SD_DISKIO_STAGE_READ_NOT_READY, RES_NOTRDY);
    Stat |= STA_NOINIT;
    return RES_NOTRDY;
  }

  buffer_address = (uintptr_t)buff;
  if ((buffer_address & (SD_DMA_ALIGNMENT - 1U)) != 0U)
  {
    while (count-- != 0U)
    {
      SD_BeginDiagnostics(SD_DISKIO_STAGE_READ_START);
      ReadStatus = 0U;
      if (BSP_SD_ReadBlocks_DMA(scratch, (uint32_t)sector, 1U) != MSD_OK)
      {
        SD_UpdateDiagnostics(SD_DISKIO_STAGE_READ_START_FAILED, RES_ERROR);
        return RES_ERROR;
      }

      res = SD_WaitForTransfer(&ReadStatus);
      if (res != RES_OK)
      {
        return res;
      }

      memcpy(buff, scratch, SD_DEFAULT_BLOCK_SIZE);
      buff += SD_DEFAULT_BLOCK_SIZE;
      sector++;
    }
    return RES_OK;
  }

  SD_BeginDiagnostics(SD_DISKIO_STAGE_READ_START);
  ReadStatus = 0U;
  if (BSP_SD_ReadBlocks_DMA((uint32_t *)buff, (uint32_t)sector, count) == MSD_OK)
  {
    res = SD_WaitForTransfer(&ReadStatus);
  }
  else
  {
    SD_UpdateDiagnostics(SD_DISKIO_STAGE_READ_START_FAILED, RES_ERROR);
  }

  return res;
}

/* USER CODE BEGIN beforeWriteSection */
/* can be used to modify previous code / undefine following code / add new code */
/* USER CODE END beforeWriteSection */
/**
  * @brief  Writes Sector(s)
  * @param  lun : not used
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1

DRESULT SD_write(BYTE lun, const BYTE *buff, DWORD sector, UINT count)
{
  DRESULT res = RES_ERROR;
  uintptr_t buffer_address;

  (void)lun;

  if (((Stat & STA_NOINIT) != 0U) || (BSP_SD_IsDetected() != SD_PRESENT))
  {
    SD_BeginDiagnostics(SD_DISKIO_STAGE_WRITE_NOT_READY);
    SD_UpdateDiagnostics(SD_DISKIO_STAGE_WRITE_NOT_READY, RES_NOTRDY);
    Stat |= STA_NOINIT;
    return RES_NOTRDY;
  }

  buffer_address = (uintptr_t)buff;
  if ((buffer_address & (SD_DMA_ALIGNMENT - 1U)) != 0U)
  {
    while (count-- != 0U)
    {
      memcpy(scratch, buff, SD_DEFAULT_BLOCK_SIZE);
      SD_BeginDiagnostics(SD_DISKIO_STAGE_WRITE_START);
      WriteStatus = 0U;
      if (BSP_SD_WriteBlocks_DMA(scratch, (uint32_t)sector, 1U) != MSD_OK)
      {
        SD_UpdateDiagnostics(SD_DISKIO_STAGE_WRITE_START_FAILED, RES_ERROR);
        return RES_ERROR;
      }

      res = SD_WaitForTransfer(&WriteStatus);
      if (res != RES_OK)
      {
        return res;
      }

      buff += SD_DEFAULT_BLOCK_SIZE;
      sector++;
    }
    return RES_OK;
  }

  SD_BeginDiagnostics(SD_DISKIO_STAGE_WRITE_START);
  WriteStatus = 0U;
  if (BSP_SD_WriteBlocks_DMA((uint32_t *)buff, (uint32_t)sector, count) == MSD_OK)
  {
    res = SD_WaitForTransfer(&WriteStatus);
  }
  else
  {
    SD_UpdateDiagnostics(SD_DISKIO_STAGE_WRITE_START_FAILED, RES_ERROR);
  }

  return res;
}
#endif /* _USE_WRITE == 1 */

/* USER CODE BEGIN beforeIoctlSection */
/* can be used to modify previous code / undefine following code / add new code */
/* USER CODE END beforeIoctlSection */
/**
  * @brief  I/O control operation
  * @param  lun : not used
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT SD_ioctl(BYTE lun, BYTE cmd, void *buff)
{
  DRESULT res = RES_ERROR;
  BSP_SD_CardInfo CardInfo;

  (void)lun;

  if (Stat & STA_NOINIT) return RES_NOTRDY;

  switch (cmd)
  {
  /* Make sure that no pending write process */
  case CTRL_SYNC :
    res = RES_OK;
    break;

  /* Get number of sectors on the disk (DWORD) */
  case GET_SECTOR_COUNT :
    BSP_SD_GetCardInfo(&CardInfo);
    *(DWORD*)buff = CardInfo.LogBlockNbr;
    res = RES_OK;
    break;

  /* Get R/W sector size (WORD) */
  case GET_SECTOR_SIZE :
    BSP_SD_GetCardInfo(&CardInfo);
    *(WORD*)buff = CardInfo.LogBlockSize;
    res = RES_OK;
    break;

  /* Get erase block size in unit of sector (DWORD) */
  case GET_BLOCK_SIZE :
    BSP_SD_GetCardInfo(&CardInfo);
    *(DWORD*)buff = CardInfo.LogBlockSize / SD_DEFAULT_BLOCK_SIZE;
    res = RES_OK;
    break;

  default:
    res = RES_PARERR;
  }

  return res;
}
#endif /* _USE_IOCTL == 1 */

/* USER CODE BEGIN afterIoctlSection */
/* can be used to modify previous code / undefine following code / add new code */
/* USER CODE END afterIoctlSection */

/* USER CODE BEGIN lastSection */
/* can be used to modify / undefine previous code or add new code */
static void SD_BeginDiagnostics(SD_DiskioStage_t stage)
{
  TransferErrorStatus = 0U;
  if (s_error_latched == 0U)
  {
    s_diagnostics.callback_flags = 0U;
    s_diagnostics.sdio_irq_status = 0U;
  }
  SD_UpdateDiagnostics(stage, RES_OK);
}

static void SD_UpdateDiagnostics(SD_DiskioStage_t stage, DRESULT result)
{
  uint8_t transfer_succeeded =
      (((stage == SD_DISKIO_STAGE_READ_COMPLETE) ||
        (stage == SD_DISKIO_STAGE_WRITE_COMPLETE)) &&
       (result == RES_OK)) ? 1U : 0U;

  if (s_error_latched != 0U)
  {
    if (transfer_succeeded == 0U)
    {
      return;
    }

    s_error_latched = 0U;
    s_diagnostics.callback_flags = 0U;
    s_diagnostics.sdio_irq_status = 0U;
  }

  s_diagnostics.stage = (uint8_t)stage;
  s_diagnostics.last_result = (uint8_t)result;
  s_diagnostics.disk_status = (uint8_t)Stat;
  s_diagnostics.hal_state = (uint8_t)hsd.State;
  s_diagnostics.card_state = s_last_card_state;
  s_diagnostics.hal_error = HAL_SD_GetError(&hsd);
  s_diagnostics.sdio_status = (hsd.Instance != NULL) ? hsd.Instance->STA : 0U;
  s_diagnostics.dma_rx_error = (hsd.hdmarx != NULL) ? hsd.hdmarx->ErrorCode : 0U;
  s_diagnostics.dma_tx_error = (hsd.hdmatx != NULL) ? hsd.hdmatx->ErrorCode : 0U;

  if ((result != RES_OK) && (SD_IsPrimaryFailure(stage) != 0U))
  {
    s_error_latched = 1U;
  }
}

static uint8_t SD_IsPrimaryFailure(SD_DiskioStage_t stage)
{
  switch (stage)
  {
    case SD_DISKIO_STAGE_INIT_FAILED:
    case SD_DISKIO_STAGE_INIT_READY_TIMEOUT:
    case SD_DISKIO_STAGE_READ_START_FAILED:
    case SD_DISKIO_STAGE_READ_HAL_ERROR:
    case SD_DISKIO_STAGE_READ_TIMEOUT:
    case SD_DISKIO_STAGE_READ_NOT_READY:
    case SD_DISKIO_STAGE_WRITE_START_FAILED:
    case SD_DISKIO_STAGE_WRITE_HAL_ERROR:
    case SD_DISKIO_STAGE_WRITE_TIMEOUT:
    case SD_DISKIO_STAGE_WRITE_NOT_READY:
    case SD_DISKIO_STAGE_CARD_REMOVED:
      return 1U;

    default:
      return 0U;
  }
}

const SD_DiskioDiagnostics_t *SD_Diskio_GetDiagnostics(void)
{
  return &s_diagnostics;
}

void SD_Diskio_Invalidate(void)
{
  Stat |= STA_NOINIT;
  s_last_card_state = 0U;
  ReadStatus = 0U;
#if _USE_WRITE == 1
  WriteStatus = 0U;
#endif
  TransferErrorStatus = 0U;

  if (hsd.Instance != NULL)
  {
    HAL_NVIC_DisableIRQ(SDIO_IRQn);
    HAL_NVIC_DisableIRQ(DMA2_Stream3_IRQn);
    HAL_NVIC_DisableIRQ(DMA2_Stream6_IRQn);

    if ((hsd.hdmatx != NULL) && (hsd.hdmatx->State == HAL_DMA_STATE_BUSY))
    {
      (void)HAL_DMA_Abort(hsd.hdmatx);
    }
    if ((hsd.hdmarx != NULL) && (hsd.hdmarx->State == HAL_DMA_STATE_BUSY))
    {
      (void)HAL_DMA_Abort(hsd.hdmarx);
    }

    if (hsd.State != HAL_SD_STATE_RESET)
    {
      (void)HAL_SD_DeInit(&hsd);
    }

    __HAL_RCC_SDIO_FORCE_RESET();
    __HAL_RCC_SDIO_RELEASE_RESET();
    HAL_NVIC_ClearPendingIRQ(SDIO_IRQn);
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream3_IRQn);
    HAL_NVIC_ClearPendingIRQ(DMA2_Stream6_IRQn);
    HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);
    HAL_NVIC_EnableIRQ(DMA2_Stream6_IRQn);
  }
}

void SD_Diskio_CaptureIrqStatus(uint32_t status)
{
  if (s_error_latched == 0U)
  {
    s_diagnostics.sdio_irq_status = status;
  }
}

void BSP_SD_AbortCallback(void)
{
  if (s_error_latched == 0U)
  {
    s_diagnostics.callback_flags |= 0x08U;
  }
}

void BSP_SD_ErrorCallback(void)
{
  TransferErrorStatus = 1U;
  if (s_error_latched == 0U)
  {
    s_diagnostics.callback_flags |= 0x04U;
    s_diagnostics.hal_error = HAL_SD_GetError(&hsd);
    s_diagnostics.sdio_status = (hsd.Instance != NULL) ? hsd.Instance->STA : 0U;
    s_diagnostics.dma_rx_error = (hsd.hdmarx != NULL) ? hsd.hdmarx->ErrorCode : 0U;
    s_diagnostics.dma_tx_error = (hsd.hdmatx != NULL) ? hsd.hdmatx->ErrorCode : 0U;
  }
}

void BSP_SD_ReadCpltCallback(void)
{
  ReadStatus = 1U;
  if (s_error_latched == 0U)
  {
    s_diagnostics.callback_flags |= 0x01U;
  }
}

#if _USE_WRITE == 1
void BSP_SD_WriteCpltCallback(void)
{
  WriteStatus = 1U;
  if (s_error_latched == 0U)
  {
    s_diagnostics.callback_flags |= 0x02U;
  }
}
#endif
/* USER CODE END lastSection */
