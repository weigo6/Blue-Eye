/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    sd_diskio.h
  * @brief   Header for sd_diskio.c module
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

/* Note: code generation based on sd_diskio_template.h */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SD_DISKIO_H
#define __SD_DISKIO_H

/* USER CODE BEGIN firstSection */
/* can be used to modify / undefine following code or add new definitions */
/* USER CODE END firstSection */

/* Includes ------------------------------------------------------------------*/
#include "ff_gen_drv.h"
#include "bsp_driver_sd.h"
/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ExportedTypes */
typedef enum
{
  SD_DISKIO_STAGE_IDLE = 0x00U,
  SD_DISKIO_STAGE_INITIALIZING = 0x10U,
  SD_DISKIO_STAGE_READY = 0x11U,
  SD_DISKIO_STAGE_INIT_FAILED = 0x12U,
  SD_DISKIO_STAGE_INIT_WAIT_READY = 0x13U,
  SD_DISKIO_STAGE_INIT_READY_TIMEOUT = 0x14U,
  SD_DISKIO_STAGE_READ_START = 0x20U,
  SD_DISKIO_STAGE_READ_WAIT = 0x21U,
  SD_DISKIO_STAGE_READ_COMPLETE = 0x22U,
  SD_DISKIO_STAGE_READ_START_FAILED = 0x23U,
  SD_DISKIO_STAGE_READ_HAL_ERROR = 0x24U,
  SD_DISKIO_STAGE_READ_TIMEOUT = 0x25U,
  SD_DISKIO_STAGE_READ_NOT_READY = 0x26U,
  SD_DISKIO_STAGE_WRITE_START = 0x30U,
  SD_DISKIO_STAGE_WRITE_WAIT = 0x31U,
  SD_DISKIO_STAGE_WRITE_COMPLETE = 0x32U,
  SD_DISKIO_STAGE_WRITE_START_FAILED = 0x33U,
  SD_DISKIO_STAGE_WRITE_HAL_ERROR = 0x34U,
  SD_DISKIO_STAGE_WRITE_TIMEOUT = 0x35U,
  SD_DISKIO_STAGE_WRITE_NOT_READY = 0x36U,
  SD_DISKIO_STAGE_CARD_REMOVED = 0x40U
} SD_DiskioStage_t;

typedef struct
{
  uint8_t stage;
  uint8_t last_result;
  uint8_t callback_flags;
  uint8_t disk_status;
  uint8_t hal_state;
  uint8_t card_state;
  uint8_t init_attempt_count;
  uint8_t reserved;
  uint32_t hal_error;
  uint32_t sdio_status;
  uint32_t sdio_irq_status;
  uint32_t dma_rx_error;
  uint32_t dma_tx_error;
} SD_DiskioDiagnostics_t;
/* USER CODE END ExportedTypes */
/* Exported constants --------------------------------------------------------*/
/* Exported functions ------------------------------------------------------- */
extern const Diskio_drvTypeDef  SD_Driver;

/* USER CODE BEGIN lastSection */
/* can be used to modify / undefine previous code or add new definitions */
const SD_DiskioDiagnostics_t *SD_Diskio_GetDiagnostics(void);
void SD_Diskio_CaptureIrqStatus(uint32_t status);
void SD_Diskio_Invalidate(void);
/* USER CODE END lastSection */

#endif /* __SD_DISKIO_H */
