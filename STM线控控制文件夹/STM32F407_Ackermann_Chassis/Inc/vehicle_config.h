#ifndef VEHICLE_CONFIG_H
#define VEHICLE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  DRIVE_MODE_FWD = 0,
  DRIVE_MODE_RWD = 1,
  DRIVE_MODE_AWD = 2,
  DRIVE_MODE_DIFF = 3,
} drive_mode_t;

typedef enum
{
  CHASSIS_TYPE_ACKERMANN = 0,
  CHASSIS_TYPE_DIFF = 1,
} chassis_type_t;

typedef enum
{
  DRIVE_AXLE_FWD = 0,
  DRIVE_AXLE_RWD = 1,
} drive_axle_t;

typedef enum
{
  PEDAL_CONFIG_BRAKE_THROTTLE = 0,
  PEDAL_CONFIG_ESTOP_THROTTLE = 1,
} pedal_config_t;

typedef enum
{
  DRIVE_CONTROLLER_NONE = 0,
  DRIVE_CONTROLLER_MSSD_60EHB_2D = 1,
} drive_controller_type_t;

typedef enum
{
  STEERING_CONTROLLER_NONE = 0,
  STEERING_CONTROLLER_MSSC = 1,
} steering_controller_type_t;

typedef struct
{
  uint16_t front_track_mm;
  uint16_t wheelbase_mm;
  uint16_t rear_track_mm;
  uint8_t chassis_type;
  uint8_t drive_axle;
  uint8_t drive_controller_type;
  uint8_t steering_controller_type;
  uint8_t linear_steering_enabled;
  uint8_t pedal_config;
  uint8_t reserved0[2];
  uint16_t steer_can_node_id;
  uint16_t handwheel_can_node_id;
  uint16_t throttle_raw_min;
  uint16_t throttle_raw_max;
  uint16_t brake_raw_min;
  uint16_t brake_raw_max;
  int16_t steering_center;
  int16_t steering_left_10;
  int16_t steering_right_10;
  int16_t steering_left_limit;
  int16_t steering_right_limit;
  int16_t handwheel_center;
  int16_t handwheel_left_10;
  int16_t handwheel_right_10;
  int16_t handwheel_left_limit;
  int16_t handwheel_right_limit;
  uint32_t version;
} vehicle_config_t;

void vehicle_config_init(void);
const vehicle_config_t *vehicle_config_get(void);
void vehicle_config_set(const vehicle_config_t *config);
bool vehicle_config_save(void);
bool vehicle_config_load(void);

drive_mode_t vehicle_config_runtime_drive_mode(const vehicle_config_t *config);

const char *vehicle_config_drive_mode_name(drive_mode_t mode);
bool vehicle_config_parse_drive_mode(const char *text, drive_mode_t *mode);

const char *vehicle_config_chassis_type_name(chassis_type_t type);
bool vehicle_config_parse_chassis_type(const char *text, chassis_type_t *type);

const char *vehicle_config_drive_axle_name(drive_axle_t axle);
bool vehicle_config_parse_drive_axle(const char *text, drive_axle_t *axle);

const char *vehicle_config_pedal_config_name(pedal_config_t config);
bool vehicle_config_parse_pedal_config(const char *text, pedal_config_t *config);

const char *vehicle_config_drive_controller_name(uint8_t type);
const char *vehicle_config_steering_controller_name(uint8_t type);

#endif
