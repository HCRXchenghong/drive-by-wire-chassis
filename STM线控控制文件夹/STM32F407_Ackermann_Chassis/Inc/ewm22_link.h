#ifndef EWM22_LINK_H
#define EWM22_LINK_H

#include "main.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum
{
  EWM22_REMOTE_SOURCE_NONE = 0,
  EWM22_REMOTE_SOURCE_RC_FRAME = 1,
  EWM22_REMOTE_SOURCE_APP_JSON = 2,
  EWM22_REMOTE_SOURCE_USB_JSON = 3,
} ewm22_remote_source_t;

typedef enum
{
  EWM22_REMOTE_GEAR_UNKNOWN = 0,
  EWM22_REMOTE_GEAR_D = 1,
  EWM22_REMOTE_GEAR_S = 2,
  EWM22_REMOTE_GEAR_N = 3,
  EWM22_REMOTE_GEAR_R = 4,
} ewm22_remote_gear_t;

typedef struct
{
  bool initialized;
  bool remote_valid;
  bool ble_connected;
  bool wifi_sta_connected;
  bool tcp_client_started;
  bool tcp_connected;
  uint32_t last_rx_tick_ms;
  uint32_t last_any_rx_tick_ms;
  uint32_t last_seq;
  ewm22_remote_source_t remote_source;
  ewm22_remote_gear_t gear;
  int16_t throttle;
  int16_t steer;
  int16_t aux_x;
  int16_t aux_y;
  uint16_t buttons;
  uint32_t frame_count;
  uint32_t crc_error_count;
  uint32_t parse_error_count;
  uint32_t overflow_error_count;
  char last_text_line[64];
  uint32_t last_text_tick_ms;
} ewm22_link_state_t;

void ewm22_link_init(UART_HandleTypeDef *huart);
bool ewm22_link_send_text(const char *text);
bool ewm22_link_pop_app_command_line(char *line, size_t max_len);
const ewm22_link_state_t *ewm22_link_get_state(void);
bool ewm22_link_inject_app_control_json(const char *line);
const char *ewm22_link_remote_source_name(ewm22_remote_source_t source);
const char *ewm22_link_remote_gear_name(ewm22_remote_gear_t gear);

#endif
