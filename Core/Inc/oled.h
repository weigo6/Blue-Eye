#ifndef __OLED_H__
#define __OLED_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"

#define OLED_WIDTH 128U
#define OLED_HEIGHT 64U
#define OLED_I2C_ADDR (0x3CU << 1)

typedef enum
{
  OLED_COLOR_BLACK = 0,
  OLED_COLOR_WHITE = 1
} OLED_Color_t;

HAL_StatusTypeDef OLED_Init(I2C_HandleTypeDef *hi2c);
void OLED_Fill(OLED_Color_t color);
void OLED_UpdateScreen(void);
void OLED_DrawPixel(uint8_t x, uint8_t y, OLED_Color_t color);
void OLED_DrawChar(uint8_t x, uint8_t y, char ch);
void OLED_DrawString(uint8_t x, uint8_t y, const char *str);
void OLED_DrawHLine(uint8_t x, uint8_t y, uint8_t length, OLED_Color_t color);
void OLED_DrawVLine(uint8_t x, uint8_t y, uint8_t length, OLED_Color_t color);
void OLED_DrawRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, OLED_Color_t color);

#ifdef __cplusplus
}
#endif

#endif /* __OLED_H__ */
