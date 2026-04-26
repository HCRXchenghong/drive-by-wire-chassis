#ifndef DRIVE_CONTROLLER_MSSD_H
#define DRIVE_CONTROLLER_MSSD_H

#include "main.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  CAN_HandleTypeDef *hcan;
  uint16_t node_id;
} mssd_drive_controller_t;

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

#endif
