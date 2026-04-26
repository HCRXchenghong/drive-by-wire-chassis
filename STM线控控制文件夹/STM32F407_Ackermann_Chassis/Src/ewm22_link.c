#include "ewm22_link.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define EWM22_LINE_BUFFER_LEN 192U

static UART_HandleTypeDef *s_huart = NULL;
static uint8_t s_rx_byte = 0U;
static char s_line_buffer[EWM22_LINE_BUFFER_LEN];
static uint16_t s_line_length = 0U;
static char s_pending_app_command_line[EWM22_LINE_BUFFER_LEN];
static bool s_pending_app_command_ready = false;
static ewm22_link_state_t s_state;

static bool ewm22_link_start_rx(void)
{
  if ((s_huart == NULL) || (s_huart->Instance == NULL))
  {
    return false;
  }

  return (HAL_UART_Receive_IT(s_huart, &s_rx_byte, 1U) == HAL_OK);
}

static int ascii_stricmp(const char *lhs, const char *rhs)
{
  while ((*lhs != '\0') && (*rhs != '\0'))
  {
    int lhs_ch = toupper((unsigned char)*lhs);
    int rhs_ch = toupper((unsigned char)*rhs);

    if (lhs_ch != rhs_ch)
    {
      return lhs_ch - rhs_ch;
    }

    ++lhs;
    ++rhs;
  }

  return (int)(unsigned char)*lhs - (int)(unsigned char)*rhs;
}

static int32_t clamp_int32(int32_t value, int32_t min_value, int32_t max_value)
{
  if (value < min_value)
  {
    return min_value;
  }

  if (value > max_value)
  {
    return max_value;
  }

  return value;
}

static uint8_t hex_nibble(char ch)
{
  if ((ch >= '0') && (ch <= '9'))
  {
    return (uint8_t)(ch - '0');
  }

  if ((ch >= 'A') && (ch <= 'F'))
  {
    return (uint8_t)(10 + (ch - 'A'));
  }

  if ((ch >= 'a') && (ch <= 'f'))
  {
    return (uint8_t)(10 + (ch - 'a'));
  }

  return 0xFFU;
}

static uint8_t checksum_xor(const char *text)
{
  uint8_t value = 0U;

  while (*text != '\0')
  {
    value ^= (uint8_t)*text;
    ++text;
  }

  return value;
}

static void ewm22_link_store_text_line(const char *line)
{
  size_t copy_len = strlen(line);

  if (copy_len >= sizeof(s_state.last_text_line))
  {
    copy_len = sizeof(s_state.last_text_line) - 1U;
  }

  memcpy(s_state.last_text_line, line, copy_len);
  s_state.last_text_line[copy_len] = '\0';
  s_state.last_text_tick_ms = HAL_GetTick();
}

static ewm22_remote_gear_t ewm22_link_parse_gear_string(const char *text)
{
  if (text == NULL)
  {
    return EWM22_REMOTE_GEAR_UNKNOWN;
  }

  if ((ascii_stricmp(text, "D") == 0) || (ascii_stricmp(text, "DRIVE") == 0))
  {
    return EWM22_REMOTE_GEAR_D;
  }

  if ((ascii_stricmp(text, "S") == 0) || (ascii_stricmp(text, "SPORT") == 0) || (ascii_stricmp(text, "SLIDE") == 0))
  {
    return EWM22_REMOTE_GEAR_S;
  }

  if ((ascii_stricmp(text, "N") == 0) || (ascii_stricmp(text, "NEUTRAL") == 0))
  {
    return EWM22_REMOTE_GEAR_N;
  }

  if ((ascii_stricmp(text, "R") == 0) || (ascii_stricmp(text, "REVERSE") == 0))
  {
    return EWM22_REMOTE_GEAR_R;
  }

  return EWM22_REMOTE_GEAR_UNKNOWN;
}

static ewm22_remote_gear_t ewm22_link_default_gear_from_throttle(int32_t throttle)
{
  if (throttle < 0)
  {
    return EWM22_REMOTE_GEAR_R;
  }

  if (throttle == 0)
  {
    return EWM22_REMOTE_GEAR_N;
  }

  return EWM22_REMOTE_GEAR_D;
}

static void ewm22_link_accept_remote_command(ewm22_remote_source_t source,
                                             uint32_t seq,
                                             ewm22_remote_gear_t gear,
                                             int32_t throttle,
                                             int32_t steer,
                                             int32_t aux_x,
                                             int32_t aux_y,
                                             uint32_t buttons)
{
  s_state.remote_valid = true;
  s_state.last_rx_tick_ms = HAL_GetTick();
  s_state.last_seq = seq;
  s_state.remote_source = source;
  s_state.gear = gear;
  s_state.throttle = (int16_t)clamp_int32(throttle, -1000, 1000);
  s_state.steer = (int16_t)clamp_int32(steer, -1000, 1000);
  s_state.aux_x = (int16_t)clamp_int32(aux_x, -1000, 1000);
  s_state.aux_y = (int16_t)clamp_int32(aux_y, -1000, 1000);
  s_state.buttons = (uint16_t)buttons;
  s_state.frame_count++;
}

static bool parse_int32_token(char **cursor, int32_t *value)
{
  char *token_end;
  char *next_comma;
  char saved;

  if ((*cursor == NULL) || (**cursor == '\0'))
  {
    return false;
  }

  next_comma = strchr(*cursor, ',');
  if (next_comma != NULL)
  {
    saved = *next_comma;
    *next_comma = '\0';
  }
  else
  {
    saved = '\0';
  }

  *value = (int32_t)strtol(*cursor, &token_end, 10);
  if (*cursor == token_end)
  {
    if (next_comma != NULL)
    {
      *next_comma = saved;
    }
    return false;
  }

  if (next_comma != NULL)
  {
    *next_comma = saved;
    *cursor = next_comma + 1;
  }
  else
  {
    *cursor = token_end;
  }

  return true;
}

static bool parse_uint32_token(char **cursor, uint32_t *value)
{
  char *token_end;
  char *next_comma;
  char saved;

  if ((*cursor == NULL) || (**cursor == '\0'))
  {
    return false;
  }

  next_comma = strchr(*cursor, ',');
  if (next_comma != NULL)
  {
    saved = *next_comma;
    *next_comma = '\0';
  }
  else
  {
    saved = '\0';
  }

  *value = (uint32_t)strtoul(*cursor, &token_end, 10);
  if (*cursor == token_end)
  {
    if (next_comma != NULL)
    {
      *next_comma = saved;
    }
    return false;
  }

  if (next_comma != NULL)
  {
    *next_comma = saved;
    *cursor = next_comma + 1;
  }
  else
  {
    *cursor = token_end;
  }

  return true;
}

static const char *json_find_value(const char *line, const char *key)
{
  const char *cursor = strstr(line, key);

  if (cursor == NULL)
  {
    return NULL;
  }

  cursor = strchr(cursor, ':');
  if (cursor == NULL)
  {
    return NULL;
  }

  ++cursor;
  while ((*cursor == ' ') || (*cursor == '\t'))
  {
    ++cursor;
  }

  return cursor;
}

static bool json_get_int32(const char *line, const char *key, int32_t *value)
{
  char *end_ptr;
  const char *cursor;

  if ((line == NULL) || (key == NULL) || (value == NULL))
  {
    return false;
  }

  cursor = json_find_value(line, key);
  if (cursor == NULL)
  {
    return false;
  }

  if (*cursor == '"')
  {
    ++cursor;
  }

  *value = (int32_t)strtol(cursor, &end_ptr, 10);
  return (cursor != end_ptr);
}

static bool json_get_uint32(const char *line, const char *key, uint32_t *value)
{
  char *end_ptr;
  const char *cursor;

  if ((line == NULL) || (key == NULL) || (value == NULL))
  {
    return false;
  }

  cursor = json_find_value(line, key);
  if (cursor == NULL)
  {
    return false;
  }

  if (*cursor == '"')
  {
    ++cursor;
  }

  *value = (uint32_t)strtoul(cursor, &end_ptr, 10);
  return (cursor != end_ptr);
}

static bool json_get_string(const char *line, const char *key, char *value, size_t value_len)
{
  const char *cursor;
  const char *end;
  size_t copy_len;

  if ((line == NULL) || (key == NULL) || (value == NULL) || (value_len == 0U))
  {
    return false;
  }

  cursor = json_find_value(line, key);
  if ((cursor == NULL) || (*cursor != '"'))
  {
    return false;
  }

  ++cursor;
  end = strchr(cursor, '"');
  if (end == NULL)
  {
    return false;
  }

  copy_len = (size_t)(end - cursor);
  if (copy_len >= value_len)
  {
    copy_len = value_len - 1U;
  }

  memcpy(value, cursor, copy_len);
  value[copy_len] = '\0';
  return true;
}

static bool ewm22_link_store_app_command_line(const char *line)
{
  char cmd[32];
  size_t copy_len;

  if ((line == NULL) || (line[0] != '{'))
  {
    return false;
  }

  if (!json_get_string(line, "\"cmd\"", cmd, sizeof(cmd)))
  {
    return false;
  }

  if ((ascii_stricmp(cmd, "app_control") == 0) ||
      (ascii_stricmp(cmd, "control") == 0) ||
      (ascii_stricmp(cmd, "joystick") == 0))
  {
    return false;
  }

  copy_len = strlen(line);
  if (copy_len >= sizeof(s_pending_app_command_line))
  {
    copy_len = sizeof(s_pending_app_command_line) - 1U;
  }

  memcpy(s_pending_app_command_line, line, copy_len);
  s_pending_app_command_line[copy_len] = '\0';
  s_pending_app_command_ready = true;
  return true;
}

static bool ewm22_link_parse_remote_frame(char *line)
{
  char *payload = NULL;
  char *asterisk = strchr(line, '*');
  char *cursor;
  uint32_t seq = 0U;
  uint32_t buttons = 0U;
  int32_t throttle = 0;
  int32_t steer = 0;
  int32_t aux_x = 0;
  int32_t aux_y = 0;
  uint8_t expected_crc;
  uint8_t received_crc;
  uint8_t high_nibble;
  uint8_t low_nibble;

  if ((line[0] != '$') || (strncmp(line, "$RC,", 4) != 0) || (asterisk == NULL))
  {
    return false;
  }

  if ((asterisk[1] == '\0') || (asterisk[2] == '\0'))
  {
    s_state.parse_error_count++;
    return false;
  }

  high_nibble = hex_nibble(asterisk[1]);
  low_nibble = hex_nibble(asterisk[2]);
  if ((high_nibble == 0xFFU) || (low_nibble == 0xFFU))
  {
    s_state.parse_error_count++;
    return false;
  }

  *asterisk = '\0';
  expected_crc = checksum_xor(&line[1]);
  received_crc = (uint8_t)((high_nibble << 4) | low_nibble);
  if (expected_crc != received_crc)
  {
    s_state.crc_error_count++;
    return false;
  }

  payload = &line[4];
  cursor = payload;

  if (!parse_uint32_token(&cursor, &seq) ||
      !parse_int32_token(&cursor, &throttle) ||
      !parse_int32_token(&cursor, &steer) ||
      !parse_int32_token(&cursor, &aux_x) ||
      !parse_int32_token(&cursor, &aux_y) ||
      !parse_uint32_token(&cursor, &buttons))
  {
    s_state.parse_error_count++;
    return false;
  }

  ewm22_link_accept_remote_command(EWM22_REMOTE_SOURCE_RC_FRAME,
                                   seq,
                                   ewm22_link_default_gear_from_throttle(throttle),
                                   throttle,
                                   steer,
                                   aux_x,
                                   aux_y,
                                   buttons);
  return true;
}

static bool ewm22_link_parse_app_control_json(const char *line)
{
  char cmd[24];
  char gear_text[24];
  uint32_t seq = 0U;
  uint32_t buttons = 0U;
  int32_t throttle = 0;
  int32_t steer = 0;
  int32_t aux_x = 0;
  int32_t aux_y = 0;
  bool have_cmd;
  bool have_throttle;
  bool have_steer;
  ewm22_remote_gear_t gear = EWM22_REMOTE_GEAR_UNKNOWN;

  if ((line == NULL) || (line[0] != '{'))
  {
    return false;
  }

  have_throttle = json_get_int32(line, "\"throttle\"", &throttle);
  have_steer = json_get_int32(line, "\"steer\"", &steer);
  if (!have_throttle && !have_steer)
  {
    return false;
  }

  have_cmd = json_get_string(line, "\"cmd\"", cmd, sizeof(cmd));
  if (have_cmd &&
      (ascii_stricmp(cmd, "app_control") != 0) &&
      (ascii_stricmp(cmd, "control") != 0) &&
      (ascii_stricmp(cmd, "joystick") != 0))
  {
    return false;
  }

  if (!json_get_uint32(line, "\"seq\"", &seq))
  {
    seq = s_state.last_seq + 1U;
  }

  (void)json_get_int32(line, "\"aux_x\"", &aux_x);
  (void)json_get_int32(line, "\"aux_y\"", &aux_y);
  (void)json_get_uint32(line, "\"buttons\"", &buttons);

  if (json_get_string(line, "\"gear\"", gear_text, sizeof(gear_text)))
  {
    gear = ewm22_link_parse_gear_string(gear_text);
  }

  if (gear == EWM22_REMOTE_GEAR_UNKNOWN)
  {
    gear = ewm22_link_default_gear_from_throttle(throttle);
  }

  ewm22_link_accept_remote_command(EWM22_REMOTE_SOURCE_APP_JSON,
                                   seq,
                                   gear,
                                   throttle,
                                   steer,
                                   aux_x,
                                   aux_y,
                                   buttons);
  return true;
}

static bool ewm22_link_parse_status_line(const char *line)
{
  bool recognized = false;

  if ((line == NULL) || (line[0] == '\0'))
  {
    return false;
  }

  if (strstr(line, "BLE CONNECT") != NULL)
  {
    s_state.ble_connected = true;
    recognized = true;
  }
  else if (strstr(line, "BLE DISCONNECT") != NULL)
  {
    s_state.ble_connected = false;
    s_state.remote_valid = false;
    s_state.gear = EWM22_REMOTE_GEAR_UNKNOWN;
    recognized = true;
  }
  else if (strstr(line, "WIFI START CONNECT:") != NULL)
  {
    s_state.wifi_sta_connected = false;
    s_state.tcp_client_started = false;
    s_state.tcp_connected = false;
    s_state.remote_valid = false;
    s_state.gear = EWM22_REMOTE_GEAR_UNKNOWN;
    recognized = true;
  }
  else if ((strstr(line, "CONNECTED AP GOT IP") != NULL) ||
           (strstr(line, "WIFI CONNECTED") != NULL))
  {
    s_state.wifi_sta_connected = true;
    recognized = true;
  }
  else if (strstr(line, "TCP CLIENT START") != NULL)
  {
    s_state.tcp_client_started = true;
    s_state.tcp_connected = false;
    recognized = true;
  }
  else if (strstr(line, "TCP CONNECTED") != NULL)
  {
    s_state.wifi_sta_connected = true;
    s_state.tcp_client_started = true;
    s_state.tcp_connected = true;
    recognized = true;
  }
  else if (strstr(line, "TCP DISCONNECTED") != NULL)
  {
    s_state.tcp_client_started = false;
    s_state.tcp_connected = false;
    s_state.remote_valid = false;
    s_state.gear = EWM22_REMOTE_GEAR_UNKNOWN;
    recognized = true;
  }
  else if ((strstr(line, "WIFI DISCONNECTED") != NULL) ||
           (strstr(line, "WIFI CONNECT FAIL") != NULL) ||
           (strstr(line, "CONNECT AP FAIL") != NULL) ||
           (strstr(line, "CONNECTED AP FAIL") != NULL))
  {
    s_state.wifi_sta_connected = false;
    s_state.tcp_client_started = false;
    s_state.tcp_connected = false;
    s_state.remote_valid = false;
    s_state.gear = EWM22_REMOTE_GEAR_UNKNOWN;
    recognized = true;
  }

  if (recognized)
  {
    ewm22_link_store_text_line(line);
  }

  return recognized;
}

static void ewm22_link_process_line(char *line)
{
  if (ewm22_link_parse_remote_frame(line))
  {
    return;
  }

  if (ewm22_link_parse_app_control_json(line))
  {
    return;
  }

  if (ewm22_link_parse_status_line(line))
  {
    return;
  }

  if (ewm22_link_store_app_command_line(line))
  {
    return;
  }

  ewm22_link_store_text_line(line);
}

static void ewm22_link_process_byte(uint8_t byte)
{
  if (byte == '\r')
  {
    return;
  }

  if (byte == '\n')
  {
    if (s_line_length == 0U)
    {
      return;
    }

    s_line_buffer[s_line_length] = '\0';
    ewm22_link_process_line(s_line_buffer);
    s_line_length = 0U;
    return;
  }

  if (!isprint(byte) && (byte != '$') && (byte != '*') && (byte != ','))
  {
    return;
  }

  if (s_line_length >= (EWM22_LINE_BUFFER_LEN - 1U))
  {
    s_line_length = 0U;
    s_state.overflow_error_count++;
    return;
  }

  s_line_buffer[s_line_length++] = (char)byte;
}

void ewm22_link_init(UART_HandleTypeDef *huart)
{
  memset(&s_state, 0, sizeof(s_state));
  memset(s_pending_app_command_line, 0, sizeof(s_pending_app_command_line));
  s_pending_app_command_ready = false;
  s_huart = huart;
  s_state.initialized = (huart != NULL);
  s_state.remote_source = EWM22_REMOTE_SOURCE_NONE;
  s_state.gear = EWM22_REMOTE_GEAR_UNKNOWN;
  s_line_length = 0U;

  if (s_state.initialized)
  {
    (void)ewm22_link_start_rx();
  }
}

bool ewm22_link_send_text(const char *text)
{
  size_t len;

  if ((s_huart == NULL) || (text == NULL))
  {
    return false;
  }

  len = strlen(text);
  if (len == 0U)
  {
    return false;
  }

  return (HAL_UART_Transmit(s_huart, (uint8_t *)(uintptr_t)text, (uint16_t)len, 50U) == HAL_OK);
}

bool ewm22_link_pop_app_command_line(char *line, size_t max_len)
{
  size_t copy_len;

  if ((line == NULL) || (max_len == 0U) || !s_pending_app_command_ready)
  {
    return false;
  }

  copy_len = strlen(s_pending_app_command_line);
  if (copy_len >= max_len)
  {
    copy_len = max_len - 1U;
  }

  memcpy(line, s_pending_app_command_line, copy_len);
  line[copy_len] = '\0';
  s_pending_app_command_line[0] = '\0';
  s_pending_app_command_ready = false;
  return true;
}

const ewm22_link_state_t *ewm22_link_get_state(void)
{
  return &s_state;
}

const char *ewm22_link_remote_source_name(ewm22_remote_source_t source)
{
  switch (source)
  {
    case EWM22_REMOTE_SOURCE_RC_FRAME:
      return "RC_FRAME";
    case EWM22_REMOTE_SOURCE_APP_JSON:
      return "APP_JSON";
    default:
      return "NONE";
  }
}

const char *ewm22_link_remote_gear_name(ewm22_remote_gear_t gear)
{
  switch (gear)
  {
    case EWM22_REMOTE_GEAR_D:
      return "D";
    case EWM22_REMOTE_GEAR_S:
      return "S";
    case EWM22_REMOTE_GEAR_N:
      return "N";
    case EWM22_REMOTE_GEAR_R:
      return "R";
    default:
      return "?";
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if ((s_huart == NULL) || (huart != s_huart))
  {
    return;
  }

  ewm22_link_process_byte(s_rx_byte);
  (void)ewm22_link_start_rx();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if ((s_huart == NULL) || (huart != s_huart))
  {
    return;
  }

  (void)HAL_UART_AbortReceive_IT(s_huart);
  (void)ewm22_link_start_rx();
}
