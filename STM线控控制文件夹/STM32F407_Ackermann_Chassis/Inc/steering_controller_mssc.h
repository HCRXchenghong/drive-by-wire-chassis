#ifndef STEERING_CONTROLLER_MSSC_H
#define STEERING_CONTROLLER_MSSC_H

#include "main.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  CAN_HandleTypeDef *hcan;
  uint16_t node_id;
  uint16_t control_mode;
  uint32_t last_feedback_rx_count;
  uint32_t last_feedback_query_tick_ms;
  uint16_t position_high_word;
  uint16_t position_low_word;
  uint8_t position_words_valid_mask;
  uint8_t next_feedback_reg;
  int32_t last_position_raw;
  uint32_t last_position_tick_ms;
} mssc_steering_controller_t;

void mssc_steering_controller_bind(mssc_steering_controller_t *controller,
                                   CAN_HandleTypeDef *hcan,
                                   uint16_t node_id,
                                   uint16_t control_mode);
bool mssc_steering_controller_prepare_for_can_control(const mssc_steering_controller_t *controller);
bool mssc_steering_controller_set_speed_rpm(const mssc_steering_controller_t *controller, int16_t speed_rpm);
bool mssc_steering_controller_stop(const mssc_steering_controller_t *controller);
void mssc_steering_controller_feedback_process(mssc_steering_controller_t *controller);
bool mssc_steering_controller_get_position_raw(const mssc_steering_controller_t *controller,
                                               int32_t *position_raw,
                                               uint32_t *age_ms);

#endif
