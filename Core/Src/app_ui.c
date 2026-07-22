#include "app_ui.h"

#include "oled.h"
#include <stdio.h>

typedef enum
{
  APP_PAGE_WELCOME = 0,
  APP_PAGE_XDA_DATA,
  APP_PAGE_PRESSURE_DATA
} AppPage_t;

static AppPage_t s_current_page = APP_PAGE_WELCOME;
static PressureSensorData_t s_pressure_sensor_data = {0};
static XDA_SensorData_t s_xda_sensor_data = {0};

static void APP_UI_SetLed(uint8_t on);
static void APP_UI_ShowWelcomePage(void);
static void APP_UI_ShowXDADataPage(void);
static void APP_UI_ShowPressureDataPage(void);
static const char *APP_UI_GetPressureSensorStatusString(PressureSensorStatus_t status);
static const char *APP_UI_GetXDASensorStatusString(XDA_SensorStatus_t status);
static const char *APP_UI_GetPressureUnitString(uint16_t unit_code);
static void APP_UI_FormatPressureValue(char *buffer, size_t buffer_size, int16_t raw_value, uint16_t decimal_point);
static void APP_UI_FormatSignedX10(char *buffer, size_t buffer_size, int16_t value_x10);

void APP_UI_Init(void)
{
  APP_UI_ShowWelcomePage();
}

void APP_UI_HandleKeyEvent(KeyEvent_t event)
{
  switch (event)
  {
    case KEY_EVENT_SINGLE_CLICK:
      if (s_current_page == APP_PAGE_WELCOME)
      {
        APP_UI_ShowXDADataPage();
      }
      else if (s_current_page == APP_PAGE_XDA_DATA)
      {
        APP_UI_ShowPressureDataPage();
      }
      else
      {
        APP_UI_ShowXDADataPage();
      }
      break;

    case KEY_EVENT_DOUBLE_CLICK:
      APP_UI_ShowWelcomePage();
      break;

    case KEY_EVENT_NONE:
    default:
      break;
  }
}

void APP_UI_UpdatePressureSensorData(const PressureSensorData_t *sensor_data)
{
  if (sensor_data == NULL)
  {
    return;
  }

  s_pressure_sensor_data = *sensor_data;

  if (s_current_page == APP_PAGE_PRESSURE_DATA)
  {
    APP_UI_ShowPressureDataPage();
  }
}

void APP_UI_UpdateXDASensorData(const XDA_SensorData_t *sensor_data)
{
  if (sensor_data == NULL)
  {
    return;
  }

  s_xda_sensor_data = *sensor_data;

  if (s_current_page == APP_PAGE_XDA_DATA)
  {
    APP_UI_ShowXDADataPage();
  }
}

static void APP_UI_SetLed(uint8_t on)
{
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void APP_UI_ShowWelcomePage(void)
{
  s_current_page = APP_PAGE_WELCOME;
  APP_UI_SetLed(0U);

  OLED_Fill(OLED_COLOR_BLACK);
  OLED_DrawRect(0, 0, 128, 64, OLED_COLOR_WHITE);
  OLED_DrawHLine(0, 12, 128, OLED_COLOR_WHITE);
  OLED_DrawString(22, 2, "BLUE EYE");
  OLED_DrawString(22, 18, "DUAL SENSOR");
  OLED_DrawString(8, 34, "SINGLE: NEXT");
  OLED_DrawString(8, 46, "DOUBLE: HOME");
  OLED_UpdateScreen();
}

static void APP_UI_ShowXDADataPage(void)
{
  char line_buffer[24];
  char temp_buffer[8];

  s_current_page = APP_PAGE_XDA_DATA;
  APP_UI_SetLed(1U);

  OLED_Fill(OLED_COLOR_BLACK);
  OLED_DrawRect(0, 0, 128, 64, OLED_COLOR_WHITE);
  OLED_DrawHLine(0, 12, 128, OLED_COLOR_WHITE);
  OLED_DrawString(24, 2, "XDA SENSOR");
  (void)snprintf(line_buffer, sizeof(line_buffer), "EC:%u.%02uMS",
                 (unsigned int)(s_xda_sensor_data.ec_x100 / 100U),
                 (unsigned int)(s_xda_sensor_data.ec_x100 % 100U));
  OLED_DrawString(0, 16, line_buffer);

  APP_UI_FormatSignedX10(temp_buffer, sizeof(temp_buffer), s_xda_sensor_data.temperature_x10);
  (void)snprintf(line_buffer, sizeof(line_buffer), "TMP:%sC", temp_buffer);
  OLED_DrawString(0, 28, line_buffer);

  (void)snprintf(line_buffer, sizeof(line_buffer), "TDS:%uPPM",
                 (unsigned int)s_xda_sensor_data.tds_ppm);
  OLED_DrawString(0, 40, line_buffer);

  (void)snprintf(line_buffer, sizeof(line_buffer), "S:%u %s",
                 (unsigned int)s_xda_sensor_data.slave_address,
                 APP_UI_GetXDASensorStatusString(s_xda_sensor_data.status));
  OLED_DrawString(0, 52, line_buffer);

  (void)snprintf(line_buffer, sizeof(line_buffer), "SAL:%u", (unsigned int)s_xda_sensor_data.salinity_ppm);
  OLED_DrawString(72, 52, line_buffer);

  OLED_UpdateScreen();
}

static void APP_UI_ShowPressureDataPage(void)
{
  char line_buffer[24];
  char pressure_buffer[16];

  s_current_page = APP_PAGE_PRESSURE_DATA;
  APP_UI_SetLed(1U);

  OLED_Fill(OLED_COLOR_BLACK);
  OLED_DrawRect(0, 0, 128, 64, OLED_COLOR_WHITE);
  OLED_DrawHLine(0, 12, 128, OLED_COLOR_WHITE);
  OLED_DrawString(12, 2, "PRESS SENSOR");

  APP_UI_FormatPressureValue(pressure_buffer,
                             sizeof(pressure_buffer),
                             s_pressure_sensor_data.pressure_raw,
                             s_pressure_sensor_data.decimal_point);
  (void)snprintf(line_buffer, sizeof(line_buffer), "P:%s", pressure_buffer);
  OLED_DrawString(0, 16, line_buffer);

  (void)snprintf(line_buffer, sizeof(line_buffer), "UNIT:%s", APP_UI_GetPressureUnitString(s_pressure_sensor_data.unit_code));
  OLED_DrawString(0, 28, line_buffer);

  (void)snprintf(line_buffer, sizeof(line_buffer), "RAW:%d D:%u",
                 (int)s_pressure_sensor_data.pressure_raw,
                 (unsigned int)s_pressure_sensor_data.decimal_point);
  OLED_DrawString(0, 40, line_buffer);

  (void)snprintf(line_buffer, sizeof(line_buffer), "S:%u %s",
                 (unsigned int)s_pressure_sensor_data.slave_address,
                 APP_UI_GetPressureSensorStatusString(s_pressure_sensor_data.status));
  OLED_DrawString(0, 52, line_buffer);

  OLED_UpdateScreen();
}

static const char *APP_UI_GetPressureSensorStatusString(PressureSensorStatus_t status)
{
  switch (status)
  {
    case PRESSURE_SENSOR_STATUS_OK:
      return "OK";

    case PRESSURE_SENSOR_STATUS_TIMEOUT:
      return "TIMEOUT";

    case PRESSURE_SENSOR_STATUS_CRC_ERROR:
      return "CRCERR";

    case PRESSURE_SENSOR_STATUS_FRAME_ERROR:
      return "FRAME";

    case PRESSURE_SENSOR_STATUS_UART_ERROR:
      return "UARTERR";

    case PRESSURE_SENSOR_STATUS_IDLE:
    default:
      return "WAIT";
  }
}

static const char *APP_UI_GetXDASensorStatusString(XDA_SensorStatus_t status)
{
  switch (status)
  {
    case XDA_SENSOR_STATUS_OK:
      return "OK";

    case XDA_SENSOR_STATUS_TIMEOUT:
      return "TIMEOUT";

    case XDA_SENSOR_STATUS_CRC_ERROR:
      return "CRCERR";

    case XDA_SENSOR_STATUS_FRAME_ERROR:
      return "FRAME";

    case XDA_SENSOR_STATUS_UART_ERROR:
      return "UARTERR";

    case XDA_SENSOR_STATUS_IDLE:
    default:
      return "WAIT";
  }
}

static const char *APP_UI_GetPressureUnitString(uint16_t unit_code)
{
  switch (unit_code)
  {
    case 0U:
      return "MPa";
    case 1U:
      return "KPa";
    case 2U:
      return "Pa";
    case 3U:
      return "Bar";
    case 4U:
      return "MBar";
    case 5U:
      return "kg/cm2";
    case 6U:
      return "psi";
    case 7U:
      return "mh2o";
    case 8U:
      return "mmh2o";
    case 9U:
      return "inH2O";
    case 10U:
      return "H2O";
    case 11U:
      return "mHg";
    case 12U:
      return "mmHg";
    case 13U:
      return "inHg";
    case 14U:
      return "atm";
    case 15U:
      return "Torr";
    case 16U:
      return "m";
    case 17U:
      return "cm";
    case 18U:
      return "mm";
    case 19U:
      return "Kg";
    case 20U:
      return "degC";
    case 21U:
      return "PH";
    case 22U:
      return "degF";
    case 23U:
      return "--";
    default:
      return "UNK";
  }
}

static void APP_UI_FormatPressureValue(char *buffer, size_t buffer_size, int16_t raw_value, uint16_t decimal_point)
{
  uint32_t divisor = 1U;
  uint32_t index;
  uint32_t absolute_value;

  if (decimal_point > 4U)
  {
    decimal_point = 4U;
  }

  for (index = 0U; index < decimal_point; index++)
  {
    divisor *= 10U;
  }

  absolute_value = (raw_value < 0) ? (uint32_t)(-raw_value) : (uint32_t)raw_value;

  if (decimal_point == 0U)
  {
    (void)snprintf(buffer,
                   buffer_size,
                   "%s%lu",
                   (raw_value < 0) ? "-" : "",
                   (unsigned long)absolute_value);
    return;
  }

  (void)snprintf(buffer,
                 buffer_size,
                 "%s%lu.%0*lu",
                 (raw_value < 0) ? "-" : "",
                 (unsigned long)(absolute_value / divisor),
                 (int)decimal_point,
                 (unsigned long)(absolute_value % divisor));
}

static void APP_UI_FormatSignedX10(char *buffer, size_t buffer_size, int16_t value_x10)
{
  uint16_t absolute_value;

  if (value_x10 < 0)
  {
    absolute_value = (uint16_t)(-value_x10);
    (void)snprintf(buffer, buffer_size, "-%u.%u",
                   (unsigned int)(absolute_value / 10U),
                   (unsigned int)(absolute_value % 10U));
  }
  else
  {
    absolute_value = (uint16_t)value_x10;
    (void)snprintf(buffer, buffer_size, "%u.%u",
                   (unsigned int)(absolute_value / 10U),
                   (unsigned int)(absolute_value % 10U));
  }
}
