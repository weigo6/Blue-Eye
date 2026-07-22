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
static uint32_t scratch[SD_DEFAULT_BLOCK_SIZE / sizeof(uint32_t)] = {0};

extern SD_HandleTypeDef hsd;

/* Private function prototypes -----------------------------------------------*/
static DSTATUS SD_CheckStatus(BYTE lun);
static DRESULT SD_WaitForTransfer(volatile uint8_t *completion_status);
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
  Stat = STA_NOINIT;

  if(BSP_SD_GetCardState() == MSD_OK)
  {
    Stat &= ~STA_NOINIT;
  }

  return Stat;
}

static DRESULT SD_WaitForTransfer(volatile uint8_t *completion_status)
{
  uint32_t start_tick = HAL_GetTick();

  while (1)
  {
    if (BSP_SD_IsDetected() != SD_PRESENT)
    {
      (void)HAL_SD_Abort(&hsd);
      Stat |= STA_NOINIT;
      return RES_NOTRDY;
    }

    if ((*completion_status != 0U) && (BSP_SD_GetCardState() == MSD_OK))
    {
      return RES_OK;
    }

    if ((HAL_GetTick() - start_tick) >= SD_DMA_TRANSFER_TIMEOUT_MS)
    {
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
Stat = STA_NOINIT;

#if !defined(DISABLE_SD_INIT)

  if(BSP_SD_Init() == MSD_OK)
  {
    Stat = SD_CheckStatus(lun);
  }

#else
  Stat = SD_CheckStatus(lun);
#endif

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

  if (((Stat & STA_NOINIT) != 0U) || (BSP_SD_IsDetected() != SD_PRESENT))
  {
    Stat |= STA_NOINIT;
    return RES_NOTRDY;
  }

  buffer_address = (uintptr_t)buff;
  if ((buffer_address & (SD_DMA_ALIGNMENT - 1U)) != 0U)
  {
    while (count-- != 0U)
    {
      ReadStatus = 0U;
      if (BSP_SD_ReadBlocks_DMA(scratch, (uint32_t)sector, 1U) != MSD_OK)
      {
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

  ReadStatus = 0U;
  if (BSP_SD_ReadBlocks_DMA((uint32_t *)buff, (uint32_t)sector, count) == MSD_OK)
  {
    res = SD_WaitForTransfer(&ReadStatus);
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

  if (((Stat & STA_NOINIT) != 0U) || (BSP_SD_IsDetected() != SD_PRESENT))
  {
    Stat |= STA_NOINIT;
    return RES_NOTRDY;
  }

  buffer_address = (uintptr_t)buff;
  if ((buffer_address & (SD_DMA_ALIGNMENT - 1U)) != 0U)
  {
    while (count-- != 0U)
    {
      memcpy(scratch, buff, SD_DEFAULT_BLOCK_SIZE);
      WriteStatus = 0U;
      if (BSP_SD_WriteBlocks_DMA(scratch, (uint32_t)sector, 1U) != MSD_OK)
      {
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

  WriteStatus = 0U;
  if (BSP_SD_WriteBlocks_DMA((uint32_t *)buff, (uint32_t)sector, count) == MSD_OK)
  {
    res = SD_WaitForTransfer(&WriteStatus);
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
void BSP_SD_ReadCpltCallback(void)
{
  ReadStatus = 1U;
}

#if _USE_WRITE == 1
void BSP_SD_WriteCpltCallback(void)
{
  WriteStatus = 1U;
}
#endif
/* USER CODE END lastSection */
