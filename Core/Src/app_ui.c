#include "app_ui.h"

#include "data_logger.h"
#include "oled.h"
#include "telemetry_uart.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define APP_UI_TEXT_STEP 6U
#define APP_UI_LEFT_PADDING 4U
#define APP_UI_RIGHT_EDGE 124U
#define APP_UI_TITLE_Y 2U
#define APP_UI_TITLE_DIVIDER_Y 11U
#define APP_UI_FOOTER_DIVIDER_Y 54U
#define APP_UI_FOOTER_Y 56U

typedef enum
{
  APP_PAGE_WELCOME = 0,
  APP_PAGE_XDA_DATA,
  APP_PAGE_PRESSURE_DATA,
  APP_PAGE_SD_CONFIG,
  APP_PAGE_UART_CONFIG
} AppPage_t;

typedef enum
{
  APP_PRESSURE_VIEW_RAW = 0,
  APP_PRESSURE_VIEW_FLOAT
} AppPressureView_t;

static AppPage_t s_current_page = APP_PAGE_WELCOME;
static AppPressureView_t s_pressure_view = APP_PRESSURE_VIEW_RAW;
static PressureSensorData_t s_pressure_sensor_data = {0};
static XDA_SensorData_t s_xda_sensor_data = {0};

static void APP_UI_SetLed(uint8_t on);
static void APP_UI_DrawPageHeader(const char *title);
static void APP_UI_DrawCenteredString(uint8_t y, const char *str);
static void APP_UI_DrawRightAlignedString(uint8_t right_x, uint8_t y, const char *str);
static void APP_UI_DrawLabeledValueRow(uint8_t y, const char *label, const char *value);
static void APP_UI_DrawFooterStatus(uint8_t address, const char *status);
static void APP_UI_ShowWelcomePage(void);
static void APP_UI_ShowXDADataPage(void);
static void APP_UI_ShowPressureDataPage(void);
static void APP_UI_ShowSdConfigPage(void);
static void APP_UI_ShowUartConfigPage(void);
static const char *APP_UI_GetPressureSensorStatusString(PressureSensorStatus_t status);
static const char *APP_UI_GetXDASensorStatusString(XDA_SensorStatus_t status);
static const char *APP_UI_GetPressureUnitString(uint16_t unit_code);
static const char *APP_UI_GetPressureViewString(AppPressureView_t view);
static const char *APP_UI_GetSdLoggerStateString(const DataLoggerStatus_t *logger_status);
static const char *APP_UI_GetSdCardStateString(const DataLoggerStatus_t *logger_status);
static const char *APP_UI_GetTelemetryStateString(const TelemetryUartStatus_t *telemetry_status);
static void APP_UI_FormatPressureValue(char *buffer, size_t buffer_size, int16_t raw_value, uint16_t decimal_point);
static void APP_UI_FormatPressureFloatValue(char *buffer, size_t buffer_size, float value, uint16_t decimal_point);
static void APP_UI_FormatSignedX10(char *buffer, size_t buffer_size, int32_t value_x10);

void APP_UI_Init(void)
{
  s_pressure_view = (PressureSensor_GetReadMode() == PRESSURE_SENSOR_READ_MODE_FLOAT)
                      ? APP_PRESSURE_VIEW_FLOAT
                      : APP_PRESSURE_VIEW_RAW;
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
      else if (s_current_page == APP_PAGE_PRESSURE_DATA)
      {
        APP_UI_ShowSdConfigPage();
      }
      else if (s_current_page == APP_PAGE_SD_CONFIG)
      {
        APP_UI_ShowUartConfigPage();
      }
      else
      {
        APP_UI_ShowWelcomePage();
      }
      break;

    case KEY_EVENT_LONG_PRESS:
      if (s_current_page == APP_PAGE_PRESSURE_DATA)
      {
        s_pressure_view = (s_pressure_view == APP_PRESSURE_VIEW_RAW)
                            ? APP_PRESSURE_VIEW_FLOAT
                            : APP_PRESSURE_VIEW_RAW;
        PressureSensor_SetReadMode((s_pressure_view == APP_PRESSURE_VIEW_FLOAT)
                                     ? PRESSURE_SENSOR_READ_MODE_FLOAT
                                     : PRESSURE_SENSOR_READ_MODE_RAW);
        APP_UI_ShowPressureDataPage();
      }
      else if (s_current_page == APP_PAGE_SD_CONFIG)
      {
        const DataLoggerStatus_t *logger_status = DataLogger_GetStatus();
        uint8_t logger_enabled = (logger_status != NULL) ? logger_status->enabled : 0U;

        DataLogger_SetEnabled((uint8_t)(logger_enabled == 0U));
        APP_UI_ShowSdConfigPage();
      }
      else if (s_current_page == APP_PAGE_UART_CONFIG)
      {
        const TelemetryUartStatus_t *telemetry_status = TelemetryUart_GetStatus();
        uint8_t telemetry_enabled = (telemetry_status != NULL) ? telemetry_status->enabled : 0U;

        TelemetryUart_SetEnabled((uint8_t)(telemetry_enabled == 0U));
        APP_UI_ShowUartConfigPage();
      }
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
  s_pressure_view = (sensor_data->read_mode == PRESSURE_SENSOR_READ_MODE_FLOAT)
                      ? APP_PRESSURE_VIEW_FLOAT
                      : APP_PRESSURE_VIEW_RAW;

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

void APP_UI_RefreshServiceStatus(void)
{
  if (s_current_page == APP_PAGE_SD_CONFIG)
  {
    APP_UI_ShowSdConfigPage();
  }
  else if (s_current_page == APP_PAGE_UART_CONFIG)
  {
    APP_UI_ShowUartConfigPage();
  }
}

static void APP_UI_SetLed(uint8_t on)
{
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void APP_UI_DrawPageHeader(const char *title)
{
  OLED_DrawHLine(0, APP_UI_TITLE_DIVIDER_Y, 128U, OLED_COLOR_WHITE);
  APP_UI_DrawCenteredString(APP_UI_TITLE_Y, title);
}

static void APP_UI_DrawCenteredString(uint8_t y, const char *str)
{
  size_t length;
  uint8_t text_width;
  uint8_t start_x;

  if (str == NULL)
  {
    return;
  }

  length = strlen(str);
  if (length == 0U)
  {
    return;
  }

  text_width = (uint8_t)(length * APP_UI_TEXT_STEP);
  start_x = (text_width >= 128U) ? 0U : (uint8_t)((128U - text_width) / 2U);
  OLED_DrawString(start_x, y, str);
}

static void APP_UI_DrawRightAlignedString(uint8_t right_x, uint8_t y, const char *str)
{
  size_t length;
  uint8_t text_width;
  uint8_t start_x;

  if (str == NULL)
  {
    return;
  }

  length = strlen(str);
  if (length == 0U)
  {
    return;
  }

  text_width = (uint8_t)(length * APP_UI_TEXT_STEP);
  start_x = (text_width > (right_x + 1U)) ? 0U : (uint8_t)(right_x + 1U - text_width);
  OLED_DrawString(start_x, y, str);
}

static void APP_UI_DrawLabeledValueRow(uint8_t y, const char *label, const char *value)
{
  OLED_DrawString(APP_UI_LEFT_PADDING, y, label);
  APP_UI_DrawRightAlignedString(APP_UI_RIGHT_EDGE, y, value);
}

static void APP_UI_DrawFooterStatus(uint8_t address, const char *status)
{
  char address_buffer[12];

  OLED_DrawHLine(0, APP_UI_FOOTER_DIVIDER_Y, 128U, OLED_COLOR_WHITE);
  (void)snprintf(address_buffer, sizeof(address_buffer), "ADDR:%u", (unsigned int)address);
  OLED_DrawString(APP_UI_LEFT_PADDING, APP_UI_FOOTER_Y, address_buffer);
  APP_UI_DrawRightAlignedString(APP_UI_RIGHT_EDGE, APP_UI_FOOTER_Y, status);
}

static void APP_UI_ShowWelcomePage(void)
{
  s_current_page = APP_PAGE_WELCOME;
  APP_UI_SetLed(0U);

  OLED_Fill(OLED_COLOR_BLACK);
  APP_UI_DrawPageHeader("BLUE EYE");
  APP_UI_DrawCenteredString(18U, "DUAL SENSOR");
  APP_UI_DrawCenteredString(30U, "MONITOR");
  APP_UI_DrawCenteredString(44U, "CLICK: NEXT");
  APP_UI_DrawCenteredString(APP_UI_FOOTER_Y, "HOLD: MODE");
  OLED_UpdateScreen();
}

static void APP_UI_ShowXDADataPage(void)
{
  char value_buffer[24];
  char temp_buffer[8];

  s_current_page = APP_PAGE_XDA_DATA;
  APP_UI_SetLed(1U);

  OLED_Fill(OLED_COLOR_BLACK);
  APP_UI_DrawPageHeader("XDA SENSOR");

  (void)snprintf(value_buffer, sizeof(value_buffer), "%u.%02uMS",
                 (unsigned int)(s_xda_sensor_data.ec_x100 / 100U),
                 (unsigned int)(s_xda_sensor_data.ec_x100 % 100U));
  APP_UI_DrawLabeledValueRow(16U, "EC", value_buffer);

  APP_UI_FormatSignedX10(temp_buffer, sizeof(temp_buffer), s_xda_sensor_data.temperature_x10);
  (void)snprintf(value_buffer, sizeof(value_buffer), "%sC", temp_buffer);
  APP_UI_DrawLabeledValueRow(27U, "TEMP", value_buffer);

  (void)snprintf(value_buffer, sizeof(value_buffer), "%uPPM",
                 (unsigned int)s_xda_sensor_data.tds_ppm);
  APP_UI_DrawLabeledValueRow(38U, "TDS", value_buffer);

  (void)snprintf(value_buffer, sizeof(value_buffer), "%uPPM",
                 (unsigned int)s_xda_sensor_data.salinity_ppm);
  APP_UI_DrawLabeledValueRow(46U, "SAL", value_buffer);
  APP_UI_DrawFooterStatus(s_xda_sensor_data.slave_address,
                          APP_UI_GetXDASensorStatusString(s_xda_sensor_data.status));

  OLED_UpdateScreen();
}

static void APP_UI_ShowPressureDataPage(void)
{
  char value_buffer[24];
  char pressure_buffer[16];
  char detail_buffer[24];

  s_current_page = APP_PAGE_PRESSURE_DATA;
  APP_UI_SetLed(1U);

  OLED_Fill(OLED_COLOR_BLACK);
  APP_UI_DrawPageHeader("PRESS SENSOR");
  APP_UI_DrawLabeledValueRow(16U, "MODE", APP_UI_GetPressureViewString(s_pressure_view));

  if (s_pressure_view == APP_PRESSURE_VIEW_FLOAT)
  {
    if (s_pressure_sensor_data.float_valid != 0U)
    {
      APP_UI_FormatPressureFloatValue(pressure_buffer,
                                      sizeof(pressure_buffer),
                                      s_pressure_sensor_data.pressure_value,
                                      4U);
      (void)snprintf(value_buffer,
                     sizeof(value_buffer),
                     "%s%s",
                     pressure_buffer,
                     APP_UI_GetPressureUnitString(s_pressure_sensor_data.unit_code));
    }
    else
    {
      (void)snprintf(value_buffer, sizeof(value_buffer), "--");
    }
    APP_UI_DrawLabeledValueRow(28U, "PRESS", value_buffer);
    (void)snprintf(detail_buffer, sizeof(detail_buffer), "0016/0017");
    APP_UI_DrawLabeledValueRow(40U, "REG", detail_buffer);
  }
  else
  {
    APP_UI_FormatPressureValue(pressure_buffer,
                               sizeof(pressure_buffer),
                               s_pressure_sensor_data.pressure_raw,
                               s_pressure_sensor_data.decimal_point);
    (void)snprintf(value_buffer,
                   sizeof(value_buffer),
                   "%s%s",
                   pressure_buffer,
                   APP_UI_GetPressureUnitString(s_pressure_sensor_data.unit_code));
    APP_UI_DrawLabeledValueRow(28U, "PRESS", value_buffer);
    (void)snprintf(detail_buffer, sizeof(detail_buffer), "%d DP:%u",
                   (int)s_pressure_sensor_data.pressure_raw,
                   (unsigned int)s_pressure_sensor_data.decimal_point);
    APP_UI_DrawLabeledValueRow(40U, "RAW", detail_buffer);
  }

  APP_UI_DrawFooterStatus(s_pressure_sensor_data.slave_address,
                          APP_UI_GetPressureSensorStatusString(s_pressure_sensor_data.status));

  OLED_UpdateScreen();
}

static void APP_UI_ShowSdConfigPage(void)
{
  const DataLoggerStatus_t *logger_status = DataLogger_GetStatus();
  char value_buffer[24];

  s_current_page = APP_PAGE_SD_CONFIG;
  APP_UI_SetLed(1U);

  OLED_Fill(OLED_COLOR_BLACK);
  APP_UI_DrawPageHeader("SD CONFIG");
  APP_UI_DrawLabeledValueRow(16U, "LOG", APP_UI_GetSdLoggerStateString(logger_status));
  APP_UI_DrawLabeledValueRow(28U, "CARD", APP_UI_GetSdCardStateString(logger_status));

  if ((logger_status != NULL) && (logger_status->enabled != 0U))
  {
    (void)snprintf(value_buffer, sizeof(value_buffer), "%lu",
                   (unsigned long)logger_status->record_count);
    APP_UI_DrawLabeledValueRow(40U, "REC", value_buffer);
  }
  else
  {
    APP_UI_DrawLabeledValueRow(40U,
                               "SAFE",
                               ((logger_status != NULL) && (logger_status->mounted != 0U)) ? "WAIT" : "REMOVE");
  }

  APP_UI_DrawCenteredString(APP_UI_FOOTER_Y, "HOLD: LOG ON/OFF");
  OLED_UpdateScreen();
}

static void APP_UI_ShowUartConfigPage(void)
{
  const TelemetryUartStatus_t *telemetry_status = TelemetryUart_GetStatus();
  char value_buffer[24];

  s_current_page = APP_PAGE_UART_CONFIG;
  APP_UI_SetLed(1U);

  OLED_Fill(OLED_COLOR_BLACK);
  APP_UI_DrawPageHeader("UART CONFIG");
  APP_UI_DrawLabeledValueRow(16U, "PORT", "USART1");
  APP_UI_DrawLabeledValueRow(28U, "TX5S", APP_UI_GetTelemetryStateString(telemetry_status));

  if ((telemetry_status != NULL) && (telemetry_status->enabled != 0U))
  {
    (void)snprintf(value_buffer, sizeof(value_buffer), "%lu",
                   (unsigned long)telemetry_status->tx_success_count);
    APP_UI_DrawLabeledValueRow(40U, "SENT", value_buffer);
  }
  else
  {
    APP_UI_DrawLabeledValueRow(40U, "SAFE", "STOPPED");
  }

  APP_UI_DrawCenteredString(APP_UI_FOOTER_Y, "HOLD: UART ON/OFF");
  OLED_UpdateScreen();
}

static const char *APP_UI_GetPressureSensorStatusString(PressureSensorStatus_t status)
{
  switch (status)
  {
    case PRESSURE_SENSOR_STATUS_OK:
      return "OK";

    case PRESSURE_SENSOR_STATUS_TIMEOUT:
      return "TMO";

    case PRESSURE_SENSOR_STATUS_CRC_ERROR:
      return "CRC";

    case PRESSURE_SENSOR_STATUS_FRAME_ERROR:
      return "FRM";

    case PRESSURE_SENSOR_STATUS_UART_ERROR:
      return "UART";

    case PRESSURE_SENSOR_STATUS_MODBUS_EXCEPTION:
      return "EXC";

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
      return "TMO";

    case XDA_SENSOR_STATUS_CRC_ERROR:
      return "CRC";

    case XDA_SENSOR_STATUS_FRAME_ERROR:
      return "FRM";

    case XDA_SENSOR_STATUS_UART_ERROR:
      return "UART";

    case XDA_SENSOR_STATUS_MODBUS_EXCEPTION:
      return "EXC";

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

static const char *APP_UI_GetPressureViewString(AppPressureView_t view)
{
  return (view == APP_PRESSURE_VIEW_FLOAT) ? "FLOAT" : "RAW";
}

static const char *APP_UI_GetSdLoggerStateString(const DataLoggerStatus_t *logger_status)
{
  if (logger_status == NULL)
  {
    return "UNKNOWN";
  }

  if (logger_status->enabled != 0U)
  {
    return "ENABLED";
  }

  return (logger_status->mounted != 0U) ? "CLOSING" : "DISABLED";
}

static const char *APP_UI_GetSdCardStateString(const DataLoggerStatus_t *logger_status)
{
  if (logger_status == NULL)
  {
    return "UNKNOWN";
  }

  if (logger_status->card_present == 0U)
  {
    return "NO CARD";
  }

  if (logger_status->format_required != 0U)
  {
    return "FORMAT";
  }

  return (logger_status->mounted != 0U) ? "MOUNTED" : "IDLE";
}

static const char *APP_UI_GetTelemetryStateString(const TelemetryUartStatus_t *telemetry_status)
{
  if (telemetry_status == NULL)
  {
    return "UNKNOWN";
  }

  if (telemetry_status->enabled == 0U)
  {
    return "DISABLED";
  }
  if (telemetry_status->busy != 0U)
  {
    return "BUSY";
  }
  if (telemetry_status->pending != 0U)
  {
    return "QUEUED";
  }
  return "ENABLED";
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

static void APP_UI_FormatPressureFloatValue(char *buffer, size_t buffer_size, float value, uint16_t decimal_point)
{
  int32_t scaled_value;
  double scaled_value_f64;
  uint32_t absolute_value;
  uint32_t divisor = 1U;
  uint32_t index;

  if (decimal_point > 4U)
  {
    decimal_point = 4U;
  }

  for (index = 0U; index < decimal_point; index++)
  {
    divisor *= 10U;
  }

  if (!isfinite(value))
  {
    (void)snprintf(buffer, buffer_size, "--");
    return;
  }

  scaled_value_f64 = ((double)value * (double)divisor) + ((value >= 0.0f) ? 0.5 : -0.5);
  if ((scaled_value_f64 > (double)INT32_MAX) || (scaled_value_f64 < (double)INT32_MIN))
  {
    (void)snprintf(buffer, buffer_size, "OVR");
    return;
  }

  scaled_value = (int32_t)scaled_value_f64;
  absolute_value = (scaled_value < 0)
                     ? (uint32_t)(-(int64_t)scaled_value)
                     : (uint32_t)scaled_value;

  if (decimal_point == 0U)
  {
    (void)snprintf(buffer,
                   buffer_size,
                   "%s%lu",
                   (scaled_value < 0) ? "-" : "",
                   (unsigned long)absolute_value);
    return;
  }

  (void)snprintf(buffer,
                 buffer_size,
                 "%s%lu.%0*lu",
                 (scaled_value < 0) ? "-" : "",
                 (unsigned long)(absolute_value / divisor),
                 (int)decimal_point,
                 (unsigned long)(absolute_value % divisor));
}

static void APP_UI_FormatSignedX10(char *buffer, size_t buffer_size, int32_t value_x10)
{
  uint32_t absolute_value;

  if (value_x10 < 0)
  {
    absolute_value = (uint32_t)(-value_x10);
    (void)snprintf(buffer, buffer_size, "-%lu.%lu",
                   (unsigned long)(absolute_value / 10U),
                   (unsigned long)(absolute_value % 10U));
  }
  else
  {
    absolute_value = (uint32_t)value_x10;
    (void)snprintf(buffer, buffer_size, "%lu.%lu",
                   (unsigned long)(absolute_value / 10U),
                   (unsigned long)(absolute_value % 10U));
  }
}
