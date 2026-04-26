#ifndef DRIVE_CONTROLLER_MSSD_H
#define DRIVE_CONTROLLER_MSSD_H

#include "main.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  CAN_HandleTypeDef *hcan;
  uint16_t node_id;
  uint32_t last_feedback_rx_count;
  uint32_t last_feedback_query_tick_ms;
  uint16_t right_speed_high_word;
  uint16_t right_speed_low_word;
  uint16_t left_speed_high_word;
  uint16_t left_speed_low_word;
  uint8_t right_speed_words_valid_mask;
  uint8_t left_speed_words_valid_mask;
  uint8_t next_feedback_reg_index;
  int32_t right_actual_rpm;
  int32_t left_actual_rpm;
  uint32_t right_actual_tick_ms;
  uint32_t left_actual_tick_ms;
} mssd_drive_controller_t;

typedef struct
{
  bool right_valid;
  bool left_valid;
  int32_t right_rpm;
  int32_t left_rpm;
  uint32_t right_age_ms;
  uint32_t left_age_ms;
} mssd_drive_feedback_t;

typedef enum
{
  MSSD_STOP_MODE_NORMAL = 0,
  MSSD_STOP_MODE_EMERGENCY = 1,
  MSSD_STOP_MODE_FREE = 2
} mssd_stop_mode_t;

void mssd_drive_controller_bind(mssd_drive_controller_t *controller, CAN_HandleTypeDef *hcan, uint16_t node_id);
bool mssd_drive_controller_set_wheel_rpm(const mssd_drive_controller_t *controller, int32_t right_rpm, int32_t left_rpm);
bool mssd_drive_controller_set_stop_mode(const mssd_drive_controller_t *controller,
                                         mssd_stop_mode_t right_mode,
                                         mssd_stop_mode_t left_mode);
bool mssd_drive_controller_apply_stop_mode(const mssd_drive_controller_t *controller, mssd_stop_mode_t mode);
bool mssd_drive_controller_stop(const mssd_drive_controller_t *controller);
void mssd_drive_controller_feedback_process(mssd_drive_controller_t *controller);
bool mssd_drive_controller_get_feedback(const mssd_drive_controller_t *controller, mssd_drive_feedback_t *feedback);

#endif
