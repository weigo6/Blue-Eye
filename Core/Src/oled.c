#include "oled.h"

#include <string.h>

#define OLED_PAGE_COUNT (OLED_HEIGHT / 8U)
#define OLED_CHAR_WIDTH 6U

static I2C_HandleTypeDef *s_oled_i2c = NULL;
static uint8_t s_oled_buffer[OLED_WIDTH * OLED_PAGE_COUNT];

static HAL_StatusTypeDef OLED_WriteCommand(uint8_t command);
static const uint8_t *OLED_GetFontPattern(char ch);

HAL_StatusTypeDef OLED_Init(I2C_HandleTypeDef *hi2c)
{
  static const uint8_t init_commands[] = {
    0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
    0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
    0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6,
    0xAF
  };
  uint32_t index;

  s_oled_i2c = hi2c;
  HAL_Delay(100);

  for (index = 0; index < (sizeof(init_commands) / sizeof(init_commands[0])); index++)
  {
    if (OLED_WriteCommand(init_commands[index]) != HAL_OK)
    {
      return HAL_ERROR;
    }
  }

  OLED_Fill(OLED_COLOR_BLACK);
  OLED_UpdateScreen();

  return HAL_OK;
}

void OLED_Fill(OLED_Color_t color)
{
  memset(s_oled_buffer, (color == OLED_COLOR_WHITE) ? 0xFF : 0x00, sizeof(s_oled_buffer));
}

void OLED_UpdateScreen(void)
{
  uint8_t page;

  for (page = 0; page < OLED_PAGE_COUNT; page++)
  {
    uint8_t tx_buffer[OLED_WIDTH + 1U];

    (void)OLED_WriteCommand((uint8_t)(0xB0U + page));
    (void)OLED_WriteCommand(0x00);
    (void)OLED_WriteCommand(0x10);

    tx_buffer[0] = 0x40;
    memcpy(&tx_buffer[1], &s_oled_buffer[OLED_WIDTH * page], OLED_WIDTH);
    (void)HAL_I2C_Master_Transmit(s_oled_i2c, OLED_I2C_ADDR, tx_buffer, sizeof(tx_buffer), HAL_MAX_DELAY);
  }
}

void OLED_DrawPixel(uint8_t x, uint8_t y, OLED_Color_t color)
{
  if ((x >= OLED_WIDTH) || (y >= OLED_HEIGHT))
  {
    return;
  }

  if (color == OLED_COLOR_WHITE)
  {
    s_oled_buffer[x + (y / 8U) * OLED_WIDTH] |= (uint8_t)(1U << (y % 8U));
  }
  else
  {
    s_oled_buffer[x + (y / 8U) * OLED_WIDTH] &= (uint8_t)~(1U << (y % 8U));
  }
}

void OLED_DrawChar(uint8_t x, uint8_t y, char ch)
{
  const uint8_t *pattern = OLED_GetFontPattern(ch);
  uint8_t column;
  uint8_t row;

  for (column = 0; column < 5U; column++)
  {
    for (row = 0; row < 7U; row++)
    {
      OLED_DrawPixel(x + column, y + row,
                     ((pattern[column] >> row) & 0x01U) ? OLED_COLOR_WHITE : OLED_COLOR_BLACK);
    }
  }

  for (row = 0; row < 7U; row++)
  {
    OLED_DrawPixel(x + 5U, y + row, OLED_COLOR_BLACK);
  }
}

void OLED_DrawString(uint8_t x, uint8_t y, const char *str)
{
  while ((*str != '\0') && ((x + 5U) < OLED_WIDTH))
  {
    OLED_DrawChar(x, y, *str);
    x = (uint8_t)(x + OLED_CHAR_WIDTH);
    str++;
  }
}

void OLED_DrawHLine(uint8_t x, uint8_t y, uint8_t length, OLED_Color_t color)
{
  uint8_t offset;

  for (offset = 0; offset < length; offset++)
  {
    OLED_DrawPixel((uint8_t)(x + offset), y, color);
  }
}

void OLED_DrawVLine(uint8_t x, uint8_t y, uint8_t length, OLED_Color_t color)
{
  uint8_t offset;

  for (offset = 0; offset < length; offset++)
  {
    OLED_DrawPixel(x, (uint8_t)(y + offset), color);
  }
}

void OLED_DrawRect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, OLED_Color_t color)
{
  if ((width == 0U) || (height == 0U))
  {
    return;
  }

  OLED_DrawHLine(x, y, width, color);
  OLED_DrawHLine(x, (uint8_t)(y + height - 1U), width, color);
  OLED_DrawVLine(x, y, height, color);
  OLED_DrawVLine((uint8_t)(x + width - 1U), y, height, color);
}

static HAL_StatusTypeDef OLED_WriteCommand(uint8_t command)
{
  uint8_t buffer[2];

  if (s_oled_i2c == NULL)
  {
    return HAL_ERROR;
  }

  buffer[0] = 0x00;
  buffer[1] = command;
  return HAL_I2C_Master_Transmit(s_oled_i2c, OLED_I2C_ADDR, buffer, sizeof(buffer), HAL_MAX_DELAY);
}

static const uint8_t *OLED_GetFontPattern(char ch)
{
  static const uint8_t font_space[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
  static const uint8_t font_minus[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
  static const uint8_t font_dot[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
  static const uint8_t font_colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
  static const uint8_t font_slash[5] = {0x20, 0x10, 0x08, 0x04, 0x02};
  static const uint8_t font_0[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
  static const uint8_t font_1[5] = {0x00, 0x42, 0x7F, 0x40, 0x00};
  static const uint8_t font_2[5] = {0x42, 0x61, 0x51, 0x49, 0x46};
  static const uint8_t font_3[5] = {0x21, 0x41, 0x45, 0x4B, 0x31};
  static const uint8_t font_4[5] = {0x18, 0x14, 0x12, 0x7F, 0x10};
  static const uint8_t font_5[5] = {0x27, 0x45, 0x45, 0x45, 0x39};
  static const uint8_t font_6[5] = {0x3C, 0x4A, 0x49, 0x49, 0x30};
  static const uint8_t font_7[5] = {0x01, 0x71, 0x09, 0x05, 0x03};
  static const uint8_t font_8[5] = {0x36, 0x49, 0x49, 0x49, 0x36};
  static const uint8_t font_9[5] = {0x06, 0x49, 0x49, 0x29, 0x1E};
  static const uint8_t font_A[5] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
  static const uint8_t font_B[5] = {0x7F, 0x49, 0x49, 0x49, 0x36};
  static const uint8_t font_C[5] = {0x3E, 0x41, 0x41, 0x41, 0x22};
  static const uint8_t font_D[5] = {0x7F, 0x41, 0x41, 0x22, 0x1C};
  static const uint8_t font_E[5] = {0x7F, 0x49, 0x49, 0x49, 0x41};
  static const uint8_t font_F[5] = {0x7F, 0x09, 0x09, 0x09, 0x01};
  static const uint8_t font_G[5] = {0x3E, 0x41, 0x49, 0x49, 0x7A};
  static const uint8_t font_H[5] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
  static const uint8_t font_I[5] = {0x00, 0x41, 0x7F, 0x41, 0x00};
  static const uint8_t font_J[5] = {0x20, 0x40, 0x41, 0x3F, 0x01};
  static const uint8_t font_K[5] = {0x7F, 0x08, 0x14, 0x22, 0x41};
  static const uint8_t font_L[5] = {0x7F, 0x40, 0x40, 0x40, 0x40};
  static const uint8_t font_M[5] = {0x7F, 0x02, 0x0C, 0x02, 0x7F};
  static const uint8_t font_N[5] = {0x7F, 0x04, 0x08, 0x10, 0x7F};
  static const uint8_t font_O[5] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
  static const uint8_t font_P[5] = {0x7F, 0x09, 0x09, 0x09, 0x06};
  static const uint8_t font_Q[5] = {0x3E, 0x41, 0x51, 0x21, 0x5E};
  static const uint8_t font_R[5] = {0x7F, 0x09, 0x19, 0x29, 0x46};
  static const uint8_t font_S[5] = {0x46, 0x49, 0x49, 0x49, 0x31};
  static const uint8_t font_T[5] = {0x01, 0x01, 0x7F, 0x01, 0x01};
  static const uint8_t font_U[5] = {0x3F, 0x40, 0x40, 0x40, 0x3F};
  static const uint8_t font_V[5] = {0x1F, 0x20, 0x40, 0x20, 0x1F};
  static const uint8_t font_W[5] = {0x3F, 0x40, 0x38, 0x40, 0x3F};
  static const uint8_t font_X[5] = {0x63, 0x14, 0x08, 0x14, 0x63};
  static const uint8_t font_Y[5] = {0x07, 0x08, 0x70, 0x08, 0x07};
  static const uint8_t font_Z[5] = {0x61, 0x51, 0x49, 0x45, 0x43};

  if ((ch >= 'a') && (ch <= 'z'))
  {
    ch = (char)(ch - ('a' - 'A'));
  }

  switch (ch)
  {
    case '-': return font_minus;
    case '.': return font_dot;
    case ':': return font_colon;
    case '/': return font_slash;
    case '0': return font_0;
    case '1': return font_1;
    case '2': return font_2;
    case '3': return font_3;
    case '4': return font_4;
    case '5': return font_5;
    case '6': return font_6;
    case '7': return font_7;
    case '8': return font_8;
    case '9': return font_9;
    case 'A': return font_A;
    case 'B': return font_B;
    case 'C': return font_C;
    case 'D': return font_D;
    case 'E': return font_E;
    case 'F': return font_F;
    case 'G': return font_G;
    case 'H': return font_H;
    case 'I': return font_I;
    case 'J': return font_J;
    case 'K': return font_K;
    case 'L': return font_L;
    case 'M': return font_M;
    case 'N': return font_N;
    case 'O': return font_O;
    case 'P': return font_P;
    case 'Q': return font_Q;
    case 'R': return font_R;
    case 'S': return font_S;
    case 'T': return font_T;
    case 'U': return font_U;
    case 'V': return font_V;
    case 'W': return font_W;
    case 'X': return font_X;
    case 'Y': return font_Y;
    case 'Z': return font_Z;
    case ' ': return font_space;
    default:  return font_space;
  }
}
