#include "chassis_app.h"

#include "board_io.h"
#include "can_transport.h"
#include "drive_controller_mssd.h"
#include "ewm22_link.h"
#include "steering_controller_mssc.h"
#include "usb_cdc_shell.h"
#include "vehicle_config.h"

#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DRIVE_CAN_NODE_ID 1U
#define CONTROL_LOOP_PERIOD_MS 20U
#define CONTROL_STEER_INPUT_LIMIT 1000
#define CONTROL_DEFAULT_DRIVE_MAX_RPM 500U
#define CONTROL_MIN_DRIVE_MAX_RPM 50U
#define CONTROL_MAX_DRIVE_MAX_RPM 5000U
#define CONTROL_MAX_STEER_RPM 80.0f
#define CONTROL_MIN_TURN_RADIUS_MM 1500.0f
#define CONTROL_TURN_RADIUS_WHEELBASE_SCALE 2.0f
#define CONTROL_MAX_ACKERMANN_SPLIT 0.35f
#define CONTROL_MAX_DIFF_SPLIT 0.85f
#define REMOTE_CONTROL_TIMEOUT_MS 250U
#define REMOTE_BUTTON_ESTOP_MASK 0x0001U
#define REMOTE_BUTTON_SOFT_STOP_MASK 0x0002U
#define STATUS_PUBLISH_PERIOD_MS 200U
#define LINEAR_STEERING_ACTIVITY_WINDOW_MS 5000U
#define PEDAL_SAMPLE_TARGET_COUNT 5U
#define STEERING_FEEDBACK_STALE_MS 250U
#define DRIVE_FEEDBACK_STALE_MS 250U
#define PEDAL_ACTIVE_THRESHOLD 0.05f
#define VEHICLE_MOTION_HOLD_MS 1500U
#define VEHICLE_ACTUAL_MOTION_HOLD_MS 250U
#define VEHICLE_TARGET_RPM_MOVING_THRESHOLD 15
#define STEERING_LIMIT_CLAMP_RATIO 0.95f
#define STEERING_POSITION_DEADBAND_DEG 0.5f
#define STEERING_POSITION_MIN_RPM 12.0f
#define STEERING_POSITION_KP 4.0f
#define STEERING_FEEDBACK_BOOT_GRACE_MS 1200U

static mssd_drive_controller_t s_drive_controller;
static mssc_steering_controller_t s_steer_controller;
static mssc_steering_controller_t s_handwheel_controller;
static CAN_HandleTypeDef *s_drive_can_handle = NULL;
static CAN_HandleTypeDef *s_steer_can_handle = NULL;
static bool s_drive_can_ready = false;
static bool s_steer_can_ready = false;
static bool s_reply_to_remote = false;

static bool calibration_override_active(void);
static bool preferred_vehicle_motion_state(void);
static bool steering_feedback_get(int32_t *position_raw, uint32_t *age_ms);
static bool steering_feedback_is_recent(void);
static float steering_raw_to_angle_deg(const vehicle_config_t *config, int32_t raw_value);
static int32_t steering_target_raw_from_norm(const vehicle_config_t *config, float steer_norm);
static int16_t steering_target_rpm_from_feedback(const vehicle_config_t *config,
                                                 int32_t target_raw,
                                                 int32_t current_raw,
                                                 float *target_angle_deg,
                                                 float *actual_angle_deg);
static void fault_state_refresh(void);

typedef enum
{
  REMOTE_CONTROL_MODE_MONITOR = 0,
  REMOTE_CONTROL_MODE_TAKEOVER = 1
} remote_control_mode_t;

typedef enum
{
  DRIVE_COMMAND_SPEED = 0,
  DRIVE_COMMAND_NORMAL_STOP = 1,
  DRIVE_COMMAND_FREE_STOP = 2,
  DRIVE_COMMAND_ESTOP = 3
} drive_command_mode_t;

typedef struct
{
  bool control_enabled;
  bool outputs_locked_by_fault;
  remote_control_mode_t remote_control_mode;
  bool remote_takeover_enabled;
  bool remote_soft_stop_latched;
  bool remote_estop_latched;
  int32_t steer_input;
  int32_t drive_base_rpm;
  int32_t right_target_rpm;
  int32_t left_target_rpm;
  int32_t right_actual_rpm;
  int32_t left_actual_rpm;
  int16_t steer_target_rpm;
  int32_t steer_target_raw;
  float steer_target_angle_deg;
  float steer_actual_angle_deg;
  drive_command_mode_t drive_command_mode;
  bool remote_active;
  bool local_control_active;
  bool vehicle_moving;
  bool vehicle_moving_command;
  bool vehicle_moving_actual;
  bool right_actual_rpm_valid;
  bool left_actual_rpm_valid;
  bool drive_feedback_valid;
  bool soft_stop_active;
  bool emergency_stop_active;
  bool hardware_estop_active;
  uint32_t last_control_tick_ms;
  uint32_t last_motion_tick_ms;
  uint32_t last_actual_motion_tick_ms;
} chassis_control_state_t;

typedef struct
{
  uint32_t last_status_publish_tick_ms;
  uint32_t status_publish_seq;
} chassis_telemetry_state_t;

typedef enum
{
  CHASSIS_FAULT_CODE_NONE = 0,
  CHASSIS_FAULT_CODE_DRIVE_REPLY_TIMEOUT,
  CHASSIS_FAULT_CODE_STEER_REPLY_TIMEOUT,
  CHASSIS_FAULT_CODE_HANDWHEEL_REPLY_TIMEOUT,
  CHASSIS_FAULT_CODE_DRIVE_BUS_ERROR,
  CHASSIS_FAULT_CODE_STEER_BUS_ERROR,
  CHASSIS_FAULT_CODE_STEERING_FEEDBACK_LOST
} chassis_fault_code_t;

typedef enum
{
  CHASSIS_FAULT_DOMAIN_NONE = 0,
  CHASSIS_FAULT_DOMAIN_DRIVE_CAN,
  CHASSIS_FAULT_DOMAIN_STEER_CAN,
  CHASSIS_FAULT_DOMAIN_HANDWHEEL_CAN
} chassis_fault_domain_t;

typedef struct
{
  bool drive_can_fault;
  bool steer_can_fault;
  bool handwheel_can_fault;
  bool can_recovery_active;
  bool outputs_locked;
  chassis_fault_code_t code;
  chassis_fault_domain_t domain;
} chassis_fault_state_t;

typedef struct
{
  bool steering_override_active;
  bool handwheel_override_active;
  int16_t steering_jog_rpm;
  int32_t steering_estimate;
  int16_t handwheel_jog_rpm;
  int32_t handwheel_estimate;
  uint32_t last_update_tick_ms;
} chassis_calibration_runtime_t;

typedef struct
{
  uint32_t pressed_sum;
  uint32_t pressed_count;
  uint32_t released_sum;
  uint32_t released_count;
} pedal_sample_accumulator_t;

static chassis_control_state_t s_control_state;
static chassis_telemetry_state_t s_telemetry_state;
static chassis_fault_state_t s_fault_state;
static chassis_calibration_runtime_t s_calibration_runtime;
static pedal_sample_accumulator_t s_left_pedal_samples;
static pedal_sample_accumulator_t s_right_pedal_samples;

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

static bool json_get_uint32(const char *line, const char *key, uint32_t *value)
{
  char *end_ptr;
  const char *cursor = strstr(line, key);

  if ((cursor == NULL) || (value == NULL))
  {
    return false;
  }

  cursor = strchr(cursor, ':');
  if (cursor == NULL)
  {
    return false;
  }

  ++cursor;
  while ((*cursor == ' ') || (*cursor == '\t') || (*cursor == '"'))
  {
    ++cursor;
  }

  *value = (uint32_t)strtoul(cursor, &end_ptr, 10);
  return (cursor != end_ptr);
}

static bool json_get_int32(const char *line, const char *key, int32_t *value)
{
  char *end_ptr;
  const char *cursor = strstr(line, key);

  if ((cursor == NULL) || (value == NULL))
  {
    return false;
  }

  cursor = strchr(cursor, ':');
  if (cursor == NULL)
  {
    return false;
  }

  ++cursor;
  while ((*cursor == ' ') || (*cursor == '\t') || (*cursor == '"'))
  {
    ++cursor;
  }

  *value = (int32_t)strtol(cursor, &end_ptr, 10);
  return (cursor != end_ptr);
}

static bool json_get_bool(const char *line, const char *key, bool *value)
{
  const char *cursor = strstr(line, key);

  if ((cursor == NULL) || (value == NULL))
  {
    return false;
  }

  cursor = strchr(cursor, ':');
  if (cursor == NULL)
  {
    return false;
  }

  ++cursor;
  while ((*cursor == ' ') || (*cursor == '\t'))
  {
    ++cursor;
  }

  if (*cursor == '"')
  {
    ++cursor;
  }

  if ((toupper((unsigned char)cursor[0]) == 'T') &&
      (toupper((unsigned char)cursor[1]) == 'R') &&
      (toupper((unsigned char)cursor[2]) == 'U') &&
      (toupper((unsigned char)cursor[3]) == 'E'))
  {
    *value = true;
    return true;
  }

  if ((toupper((unsigned char)cursor[0]) == 'F') &&
      (toupper((unsigned char)cursor[1]) == 'A') &&
      (toupper((unsigned char)cursor[2]) == 'L') &&
      (toupper((unsigned char)cursor[3]) == 'S') &&
      (toupper((unsigned char)cursor[4]) == 'E'))
  {
    *value = false;
    return true;
  }

  if (*cursor == '1')
  {
    *value = true;
    return true;
  }

  if (*cursor == '0')
  {
    *value = false;
    return true;
  }

  return false;
}

static bool json_get_string(const char *line, const char *key, char *value, size_t value_len)
{
  const char *cursor = strstr(line, key);
  const char *end;
  size_t length;

  if ((cursor == NULL) || (value == NULL) || (value_len == 0U))
  {
    return false;
  }

  cursor = strchr(cursor, ':');
  if (cursor == NULL)
  {
    return false;
  }

  ++cursor;
  while ((*cursor == ' ') || (*cursor == '\t'))
  {
    ++cursor;
  }

  if (*cursor != '"')
  {
    return false;
  }

  ++cursor;
  end = strchr(cursor, '"');
  if (end == NULL)
  {
    return false;
  }

  length = (size_t)(end - cursor);
  if (length >= value_len)
  {
    length = value_len - 1U;
  }

  memcpy(value, cursor, length);
  value[length] = '\0';
  return true;
}

static void json_escape_string(const char *input, char *output, size_t output_len)
{
  size_t out_index = 0U;

  if ((output == NULL) || (output_len == 0U))
  {
    return;
  }

  if (input == NULL)
  {
    output[0] = '\0';
    return;
  }

  while ((*input != '\0') && ((out_index + 1U) < output_len))
  {
    char ch = *input++;

    if ((ch == '\\') || (ch == '"'))
    {
      if ((out_index + 2U) >= output_len)
      {
        break;
      }

      output[out_index++] = '\\';
      output[out_index++] = ch;
      continue;
    }

    if ((ch == '\r') || (ch == '\n'))
    {
      if ((out_index + 2U) >= output_len)
      {
        break;
      }

      output[out_index++] = '\\';
      output[out_index++] = (ch == '\r') ? 'r' : 'n';
      continue;
    }

    if ((unsigned char)ch < 0x20U)
    {
      continue;
    }

    output[out_index++] = ch;
  }

  output[out_index] = '\0';
}

static bool command_is(const char *line, const char *command)
{
  char value[32];

  if (!json_get_string(line, "\"cmd\"", value, sizeof(value)))
  {
    return false;
  }

  return (ascii_stricmp(value, command) == 0);
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

static float clamp_float(float value, float min_value, float max_value)
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

static int32_t float_to_int32(float value)
{
  return (value >= 0.0f) ? (int32_t)(value + 0.5f) : (int32_t)(value - 0.5f);
}

static bool reply_write(const char *text)
{
  bool ok = true;

  if (!s_reply_to_remote)
  {
    ok = usb_cdc_shell_write(text);
  }
  else
  {
    ok = ewm22_link_send_text(text);
  }

  return ok;
}

static bool reply_printf(const char *fmt, ...)
{
  char buffer[2048];
  va_list args;

  va_start(args, fmt);
  (void)vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  return reply_write(buffer);
}

static const char *chassis_fault_code_name(chassis_fault_code_t code)
{
  switch (code)
  {
    case CHASSIS_FAULT_CODE_DRIVE_REPLY_TIMEOUT:
      return "drive_reply_timeout";
    case CHASSIS_FAULT_CODE_STEER_REPLY_TIMEOUT:
      return "steer_reply_timeout";
    case CHASSIS_FAULT_CODE_HANDWHEEL_REPLY_TIMEOUT:
      return "handwheel_reply_timeout";
    case CHASSIS_FAULT_CODE_DRIVE_BUS_ERROR:
      return "drive_bus_error";
    case CHASSIS_FAULT_CODE_STEER_BUS_ERROR:
      return "steer_bus_error";
    case CHASSIS_FAULT_CODE_STEERING_FEEDBACK_LOST:
      return "steering_feedback_lost";
    case CHASSIS_FAULT_CODE_NONE:
    default:
      return "none";
  }
}

static const char *chassis_fault_domain_name(chassis_fault_domain_t domain)
{
  switch (domain)
  {
    case CHASSIS_FAULT_DOMAIN_DRIVE_CAN:
      return "drive_can";
    case CHASSIS_FAULT_DOMAIN_STEER_CAN:
      return "steer_can";
    case CHASSIS_FAULT_DOMAIN_HANDWHEEL_CAN:
      return "handwheel_can";
    case CHASSIS_FAULT_DOMAIN_NONE:
    default:
      return "none";
  }
}

static const char *chassis_fault_message_zh(chassis_fault_code_t code)
{
  switch (code)
  {
    case CHASSIS_FAULT_CODE_DRIVE_REPLY_TIMEOUT:
      return "驱动 CAN 回复超时，已锁停并等待自动恢复";
    case CHASSIS_FAULT_CODE_STEER_REPLY_TIMEOUT:
      return "机械转向 CAN 回复超时，已禁止行驶并等待自动恢复";
    case CHASSIS_FAULT_CODE_HANDWHEEL_REPLY_TIMEOUT:
      return "线性方向盘回复超时，已禁用线性方向盘相关功能";
    case CHASSIS_FAULT_CODE_DRIVE_BUS_ERROR:
      return "驱动 CAN 总线异常，已锁停并等待自动恢复";
    case CHASSIS_FAULT_CODE_STEER_BUS_ERROR:
      return "转向 CAN 总线异常，已禁止行驶并等待自动恢复";
    case CHASSIS_FAULT_CODE_STEERING_FEEDBACK_LOST:
      return "机械转向真实反馈丢失，已禁止行驶";
    case CHASSIS_FAULT_CODE_NONE:
    default:
      return "";
  }
}

static float steering_side_raw_per_10deg(int16_t center_value, int16_t ten_degree_value, int16_t limit_value)
{
  int32_t delta = (int32_t)ten_degree_value - (int32_t)center_value;

  if (delta != 0)
  {
    return (float)delta;
  }

  delta = (int32_t)limit_value - (int32_t)center_value;
  return (float)delta;
}

static float steering_raw_to_angle_deg(const vehicle_config_t *config, int32_t raw_value)
{
  float delta_per_10deg;
  int32_t center_value;
  int32_t raw_delta;

  if (config == NULL)
  {
    return 0.0f;
  }

  center_value = (int32_t)config->steering_center;
  raw_delta = raw_value - center_value;
  if (raw_delta == 0)
  {
    return 0.0f;
  }

  if (raw_delta < 0)
  {
    delta_per_10deg = steering_side_raw_per_10deg(config->steering_center,
                                                  config->steering_left_10,
                                                  config->steering_left_limit);
  }
  else
  {
    delta_per_10deg = steering_side_raw_per_10deg(config->steering_center,
                                                  config->steering_right_10,
                                                  config->steering_right_limit);
  }

  if (delta_per_10deg == 0.0f)
  {
    return 0.0f;
  }

  return ((float)raw_delta / delta_per_10deg) * 10.0f;
}

static int32_t steering_target_raw_from_norm(const vehicle_config_t *config, float steer_norm)
{
  int32_t center_value;
  int32_t limit_value;
  float clamped_norm;
  float delta;

  if (config == NULL)
  {
    return 0;
  }

  center_value = (int32_t)config->steering_center;
  clamped_norm = clamp_float(steer_norm, -1.0f, 1.0f);
  if (clamped_norm < 0.0f)
  {
    limit_value = (int32_t)config->steering_left_limit;
  }
  else
  {
    limit_value = (int32_t)config->steering_right_limit;
  }

  if (limit_value == center_value)
  {
    return center_value;
  }

  delta = ((float)limit_value - (float)center_value) * fabsf(clamped_norm) * STEERING_LIMIT_CLAMP_RATIO;
  return center_value + float_to_int32(delta);
}

static int16_t steering_target_rpm_from_feedback(const vehicle_config_t *config,
                                                 int32_t target_raw,
                                                 int32_t current_raw,
                                                 float *target_angle_deg,
                                                 float *actual_angle_deg)
{
  float target_angle = steering_raw_to_angle_deg(config, target_raw);
  float current_angle = steering_raw_to_angle_deg(config, current_raw);
  float angle_error = target_angle - current_angle;
  float rpm_command = angle_error * STEERING_POSITION_KP;

  if (target_angle_deg != NULL)
  {
    *target_angle_deg = target_angle;
  }

  if (actual_angle_deg != NULL)
  {
    *actual_angle_deg = current_angle;
  }

  if (fabsf(angle_error) <= STEERING_POSITION_DEADBAND_DEG)
  {
    return 0;
  }

  rpm_command = clamp_float(rpm_command, -CONTROL_MAX_STEER_RPM, CONTROL_MAX_STEER_RPM);
  if ((fabsf(rpm_command) < STEERING_POSITION_MIN_RPM) && (fabsf(angle_error) > STEERING_POSITION_DEADBAND_DEG))
  {
    rpm_command = (rpm_command >= 0.0f) ? STEERING_POSITION_MIN_RPM : -STEERING_POSITION_MIN_RPM;
  }

  return (int16_t)float_to_int32(rpm_command);
}

static void control_reset_targets(void)
{
  s_control_state.drive_base_rpm = 0;
  s_control_state.right_target_rpm = 0;
  s_control_state.left_target_rpm = 0;
  s_control_state.steer_target_rpm = 0;
  s_control_state.steer_target_raw = 0;
  s_control_state.steer_target_angle_deg = 0.0f;
  s_control_state.steer_actual_angle_deg = 0.0f;
  s_control_state.drive_command_mode = DRIVE_COMMAND_NORMAL_STOP;
  s_control_state.remote_active = false;
  s_control_state.local_control_active = false;
  s_control_state.soft_stop_active = false;
  s_control_state.emergency_stop_active = false;
  s_control_state.vehicle_moving = preferred_vehicle_motion_state() || calibration_override_active();
}

static void control_stop_outputs(void)
{
  if (s_drive_can_ready)
  {
    (void)mssd_drive_controller_stop_priority(&s_drive_controller, CAN_TRANSPORT_PRIORITY_CRITICAL);
  }

  if (s_steer_can_ready)
  {
    (void)mssc_steering_controller_stop_priority(&s_steer_controller, CAN_TRANSPORT_PRIORITY_CRITICAL);
    (void)mssc_steering_controller_stop_priority(&s_handwheel_controller, CAN_TRANSPORT_PRIORITY_CRITICAL);
  }

  control_reset_targets();
}

static void control_set_enabled(bool enabled)
{
  if (s_control_state.control_enabled == enabled)
  {
    return;
  }

  s_control_state.control_enabled = enabled;
  s_control_state.last_control_tick_ms = 0U;

  if (!enabled)
  {
    control_stop_outputs();
  }
}

static bool control_outputs_allowed(void)
{
  return s_control_state.control_enabled && !s_control_state.outputs_locked_by_fault;
}

static bool int32_abs_exceeds(int32_t value, int32_t threshold)
{
  return (value > threshold) || (value < -threshold);
}

static int32_t drive_direction_apply_output_rpm(int32_t logical_rpm, bool inverted)
{
  return inverted ? -logical_rpm : logical_rpm;
}

static int32_t drive_direction_normalize_feedback_rpm(int32_t raw_rpm, bool inverted)
{
  return inverted ? -raw_rpm : raw_rpm;
}

static int32_t drive_direction_apply_right_output_rpm(const vehicle_config_t *config, int32_t logical_rpm)
{
  return drive_direction_apply_output_rpm(logical_rpm,
                                          (config != NULL) && (config->right_drive_inverted != 0U));
}

static int32_t drive_direction_apply_left_output_rpm(const vehicle_config_t *config, int32_t logical_rpm)
{
  return drive_direction_apply_output_rpm(logical_rpm,
                                          (config != NULL) && (config->left_drive_inverted != 0U));
}

static int32_t drive_direction_normalize_right_feedback_rpm(const vehicle_config_t *config, int32_t raw_rpm)
{
  return drive_direction_normalize_feedback_rpm(raw_rpm,
                                                (config != NULL) && (config->right_drive_inverted != 0U));
}

static int32_t drive_direction_normalize_left_feedback_rpm(const vehicle_config_t *config, int32_t raw_rpm)
{
  return drive_direction_normalize_feedback_rpm(raw_rpm,
                                                (config != NULL) && (config->left_drive_inverted != 0U));
}

static float control_drive_max_rpm(const vehicle_config_t *config)
{
  uint16_t drive_max_rpm = CONTROL_DEFAULT_DRIVE_MAX_RPM;

  if ((config != NULL) &&
      (config->drive_max_rpm >= CONTROL_MIN_DRIVE_MAX_RPM) &&
      (config->drive_max_rpm <= CONTROL_MAX_DRIVE_MAX_RPM))
  {
    drive_max_rpm = config->drive_max_rpm;
  }

  return (float)drive_max_rpm;
}

static float pedal_raw_to_normalized(uint16_t raw, uint16_t min_raw, uint16_t max_raw)
{
  float range;
  float value;

  if (max_raw <= min_raw)
  {
    return 0.0f;
  }

  if (raw <= min_raw)
  {
    return 0.0f;
  }

  if (raw >= max_raw)
  {
    return 1.0f;
  }

  range = (float)(max_raw - min_raw);
  value = ((float)raw - (float)min_raw) / range;
  return clamp_float(value, 0.0f, 1.0f);
}

static bool calibration_override_active(void)
{
  return s_calibration_runtime.steering_override_active ||
         s_calibration_runtime.handwheel_override_active;
}

static bool drive_feedback_available(void)
{
  return s_control_state.right_actual_rpm_valid || s_control_state.left_actual_rpm_valid;
}

static bool preferred_vehicle_motion_state(void)
{
  if (drive_feedback_available())
  {
    return s_control_state.vehicle_moving_actual;
  }

  return s_control_state.vehicle_moving_command;
}

static bool remote_command_is_fresh(const ewm22_link_state_t *remote_state, uint32_t now)
{
  if ((remote_state == NULL) || !remote_state->remote_valid)
  {
    return false;
  }

  return ((now - remote_state->last_rx_tick_ms) <= REMOTE_CONTROL_TIMEOUT_MS);
}

static bool remote_drive_authority_active(const ewm22_link_state_t *remote_state, uint32_t now)
{
  return s_control_state.remote_takeover_enabled && remote_command_is_fresh(remote_state, now);
}

static const char *remote_control_mode_name(remote_control_mode_t mode)
{
  return (mode == REMOTE_CONTROL_MODE_TAKEOVER) ? "TAKEOVER" : "MONITOR";
}

static const char *control_owner_name(const ewm22_link_state_t *remote_state, uint32_t now)
{
  return remote_drive_authority_active(remote_state, now) ? "REMOTE" : "LOCAL";
}

static bool chassis_service_actions_locked(void)
{
  return preferred_vehicle_motion_state() || calibration_override_active();
}

static void controllers_bind_from_config(void)
{
  const vehicle_config_t *config = vehicle_config_get();

  if (s_drive_can_handle != NULL)
  {
    mssd_drive_controller_bind(&s_drive_controller, s_drive_can_handle, DRIVE_CAN_NODE_ID);
  }

  if (s_steer_can_handle != NULL)
  {
    mssc_steering_controller_bind(&s_steer_controller,
                                  s_steer_can_handle,
                                  config->steer_can_node_id,
                                  10U);
    mssc_steering_controller_bind(&s_handwheel_controller,
                                  s_steer_can_handle,
                                  config->handwheel_can_node_id,
                                  10U);

    if (s_steer_can_ready)
    {
      (void)mssc_steering_controller_prepare_for_can_control(&s_steer_controller);
      (void)mssc_steering_controller_prepare_for_can_control(&s_handwheel_controller);
    }
  }
}

static bool linear_steering_detected(void)
{
  int32_t position_raw = 0;
  uint32_t age_ms = 0U;

  if (!s_steer_can_ready)
  {
    return false;
  }

  if (!mssc_steering_controller_get_position_raw(&s_handwheel_controller, &position_raw, &age_ms))
  {
    return false;
  }

  return (age_ms <= LINEAR_STEERING_ACTIVITY_WINDOW_MS);
}

static bool steering_feedback_get(int32_t *position_raw, uint32_t *age_ms)
{
  return mssc_steering_controller_get_position_raw(&s_steer_controller, position_raw, age_ms);
}

static bool steering_feedback_is_recent(void)
{
  int32_t position_raw = 0;
  uint32_t age_ms = 0U;

  if (!steering_feedback_get(&position_raw, &age_ms))
  {
    return false;
  }

  return (age_ms <= STEERING_FEEDBACK_STALE_MS);
}

static void steering_feedback_update(void)
{
  int32_t position_raw = 0;

  if (!s_steer_can_ready)
  {
    return;
  }

  mssc_steering_controller_feedback_process(&s_steer_controller);
  if (steering_feedback_get(&position_raw, NULL))
  {
    s_calibration_runtime.steering_estimate = position_raw;
  }
}

static bool handwheel_feedback_get(int32_t *position_raw, uint32_t *age_ms)
{
  return mssc_steering_controller_get_position_raw(&s_handwheel_controller, position_raw, age_ms);
}

static void handwheel_feedback_update(void)
{
  int32_t position_raw = 0;

  if (!s_steer_can_ready)
  {
    return;
  }

  mssc_steering_controller_feedback_process(&s_handwheel_controller);
  if (handwheel_feedback_get(&position_raw, NULL))
  {
    s_calibration_runtime.handwheel_estimate = position_raw;
  }
}

static void drive_feedback_update(void)
{
  const vehicle_config_t *config = vehicle_config_get();
  mssd_drive_feedback_t feedback;
  bool right_recent = false;
  bool left_recent = false;
  bool actual_motion_present = false;
  uint32_t now = HAL_GetTick();

  if (!s_drive_can_ready)
  {
    s_control_state.right_actual_rpm_valid = false;
    s_control_state.left_actual_rpm_valid = false;
    s_control_state.drive_feedback_valid = false;
    s_control_state.vehicle_moving_actual = false;
    s_control_state.vehicle_moving = preferred_vehicle_motion_state() || calibration_override_active();
    return;
  }

  mssd_drive_controller_feedback_process(&s_drive_controller);
  if (mssd_drive_controller_get_feedback(&s_drive_controller, &feedback))
  {
    s_control_state.right_actual_rpm =
        drive_direction_normalize_right_feedback_rpm(config, feedback.right_rpm);
    s_control_state.left_actual_rpm =
        drive_direction_normalize_left_feedback_rpm(config, feedback.left_rpm);

    right_recent = feedback.right_valid && (feedback.right_age_ms <= DRIVE_FEEDBACK_STALE_MS);
    left_recent = feedback.left_valid && (feedback.left_age_ms <= DRIVE_FEEDBACK_STALE_MS);
  }

  s_control_state.right_actual_rpm_valid = right_recent;
  s_control_state.left_actual_rpm_valid = left_recent;
  s_control_state.drive_feedback_valid = drive_feedback_available();

  actual_motion_present =
      (right_recent && int32_abs_exceeds(s_control_state.right_actual_rpm, VEHICLE_TARGET_RPM_MOVING_THRESHOLD)) ||
      (left_recent && int32_abs_exceeds(s_control_state.left_actual_rpm, VEHICLE_TARGET_RPM_MOVING_THRESHOLD));

  if (actual_motion_present)
  {
    s_control_state.last_actual_motion_tick_ms = now;
  }

  if (s_control_state.drive_feedback_valid)
  {
    s_control_state.vehicle_moving_actual =
        actual_motion_present ||
        ((s_control_state.last_actual_motion_tick_ms != 0U) &&
         ((now - s_control_state.last_actual_motion_tick_ms) <= VEHICLE_ACTUAL_MOTION_HOLD_MS));
  }
  else
  {
    s_control_state.vehicle_moving_actual = false;
  }

  s_control_state.vehicle_moving = preferred_vehicle_motion_state() || calibration_override_active();
}

static void fault_state_refresh(void)
{
  const vehicle_config_t *config = vehicle_config_get();
  can_transport_bus_state_t drive_bus_state = {0};
  can_transport_bus_state_t steer_bus_state = {0};
  chassis_fault_state_t next_fault_state;
  bool drive_bus_fault = false;
  bool steer_bus_fault = false;
  bool drive_reply_fault = false;
  bool steer_reply_fault = false;
  bool handwheel_reply_fault = false;
  bool steering_feedback_lost = false;
  uint32_t now = HAL_GetTick();

  memset(&next_fault_state, 0, sizeof(next_fault_state));

  if (s_drive_can_ready && can_transport_get_bus_state(s_drive_can_handle, &drive_bus_state))
  {
    drive_bus_fault = drive_bus_state.fault_active || drive_bus_state.recovery_active;
    next_fault_state.can_recovery_active =
        next_fault_state.can_recovery_active || drive_bus_state.recovery_active;
  }

  if (s_steer_can_ready && can_transport_get_bus_state(s_steer_can_handle, &steer_bus_state))
  {
    steer_bus_fault = steer_bus_state.fault_active || steer_bus_state.recovery_active;
    next_fault_state.can_recovery_active =
        next_fault_state.can_recovery_active || steer_bus_state.recovery_active;
  }

  drive_reply_fault = s_drive_can_ready && mssd_drive_controller_feedback_fault_active(&s_drive_controller);
  steer_reply_fault = s_steer_can_ready && mssc_steering_controller_feedback_fault_active(&s_steer_controller);
  handwheel_reply_fault =
      config->linear_steering_enabled &&
      s_steer_can_ready &&
      mssc_steering_controller_feedback_fault_active(&s_handwheel_controller);
  steering_feedback_lost =
      s_steer_can_ready &&
      (now >= STEERING_FEEDBACK_BOOT_GRACE_MS) &&
      !steering_feedback_is_recent() &&
      (mssc_steering_controller_last_feedback_ok_age_ms(&s_steer_controller) > STEERING_FEEDBACK_STALE_MS);

  next_fault_state.drive_can_fault = drive_bus_fault || drive_reply_fault;
  next_fault_state.steer_can_fault = steer_bus_fault || steer_reply_fault || steering_feedback_lost;
  next_fault_state.handwheel_can_fault = handwheel_reply_fault;
  next_fault_state.outputs_locked = next_fault_state.drive_can_fault || next_fault_state.steer_can_fault;

  if (next_fault_state.steer_can_fault)
  {
    next_fault_state.domain = CHASSIS_FAULT_DOMAIN_STEER_CAN;
    if (steer_bus_fault)
    {
      next_fault_state.code = CHASSIS_FAULT_CODE_STEER_BUS_ERROR;
    }
    else if (steer_reply_fault)
    {
      next_fault_state.code = CHASSIS_FAULT_CODE_STEER_REPLY_TIMEOUT;
    }
    else
    {
      next_fault_state.code = CHASSIS_FAULT_CODE_STEERING_FEEDBACK_LOST;
    }
  }
  else if (next_fault_state.drive_can_fault)
  {
    next_fault_state.domain = CHASSIS_FAULT_DOMAIN_DRIVE_CAN;
    next_fault_state.code = drive_bus_fault ? CHASSIS_FAULT_CODE_DRIVE_BUS_ERROR
                                            : CHASSIS_FAULT_CODE_DRIVE_REPLY_TIMEOUT;
  }
  else if (next_fault_state.handwheel_can_fault)
  {
    next_fault_state.domain = CHASSIS_FAULT_DOMAIN_HANDWHEEL_CAN;
    next_fault_state.code = CHASSIS_FAULT_CODE_HANDWHEEL_REPLY_TIMEOUT;
  }
  else
  {
    next_fault_state.domain = CHASSIS_FAULT_DOMAIN_NONE;
    next_fault_state.code = CHASSIS_FAULT_CODE_NONE;
  }

  if (next_fault_state.outputs_locked && !s_fault_state.outputs_locked)
  {
    s_calibration_runtime.steering_override_active = false;
    s_calibration_runtime.handwheel_override_active = false;
    s_calibration_runtime.steering_jog_rpm = 0;
    s_calibration_runtime.handwheel_jog_rpm = 0;
    mssc_steering_controller_invalidate_can_control_mode(&s_steer_controller);
    mssc_steering_controller_invalidate_can_control_mode(&s_handwheel_controller);
    control_reset_targets();
    (void)mssd_drive_controller_stop_priority(&s_drive_controller, CAN_TRANSPORT_PRIORITY_CRITICAL);
    (void)mssc_steering_controller_stop_priority(&s_steer_controller, CAN_TRANSPORT_PRIORITY_CRITICAL);
    (void)mssc_steering_controller_stop_priority(&s_handwheel_controller, CAN_TRANSPORT_PRIORITY_CRITICAL);
  }

  s_fault_state = next_fault_state;
  s_control_state.outputs_locked_by_fault = next_fault_state.outputs_locked;
}

static bool drive_controller_apply_outputs(drive_command_mode_t mode, int32_t right_rpm, int32_t left_rpm)
{
  const vehicle_config_t *config = vehicle_config_get();

  if (!s_drive_can_ready)
  {
    return false;
  }

  switch (mode)
  {
    case DRIVE_COMMAND_SPEED:
      return mssd_drive_controller_set_wheel_rpm_priority(
          &s_drive_controller,
          drive_direction_apply_right_output_rpm(config, right_rpm),
          drive_direction_apply_left_output_rpm(config, left_rpm),
          CAN_TRANSPORT_PRIORITY_NORMAL);
    case DRIVE_COMMAND_FREE_STOP:
      return mssd_drive_controller_apply_stop_mode_priority(&s_drive_controller,
                                                            MSSD_STOP_MODE_FREE,
                                                            CAN_TRANSPORT_PRIORITY_HIGH);
    case DRIVE_COMMAND_ESTOP:
      return mssd_drive_controller_apply_stop_mode_priority(&s_drive_controller,
                                                            MSSD_STOP_MODE_EMERGENCY,
                                                            CAN_TRANSPORT_PRIORITY_CRITICAL);
    case DRIVE_COMMAND_NORMAL_STOP:
    default:
      return mssd_drive_controller_stop_priority(&s_drive_controller, CAN_TRANSPORT_PRIORITY_HIGH);
  }
}

static float control_compute_mode_split(const vehicle_config_t *config,
                                        drive_mode_t mode,
                                        float actual_steer_angle_deg,
                                        float steer_norm_fallback)
{
  float wheelbase_mm;
  float rear_split;
  float split = 0.0f;
  float steer_angle_deg = actual_steer_angle_deg;
  float curvature;

  if (mode == DRIVE_MODE_DIFF)
  {
    return clamp_float(steer_norm_fallback * CONTROL_MAX_DIFF_SPLIT,
                       -CONTROL_MAX_DIFF_SPLIT,
                       CONTROL_MAX_DIFF_SPLIT);
  }

  wheelbase_mm = (config->wheelbase_mm == 0U) ? 1.0f : (float)config->wheelbase_mm;
  if (wheelbase_mm < CONTROL_MIN_TURN_RADIUS_MM)
  {
    wheelbase_mm = CONTROL_MIN_TURN_RADIUS_MM;
  }

  if ((fabsf(steer_angle_deg) < STEERING_POSITION_DEADBAND_DEG) &&
      (fabsf(steer_norm_fallback) > 0.001f))
  {
    steer_angle_deg = steer_norm_fallback * 10.0f;
  }

  curvature = tanf(steer_angle_deg * 3.14159265358979323846f / 180.0f) / wheelbase_mm;
  rear_split = 0.5f * (float)config->rear_track_mm * curvature;
  split = rear_split;

  return clamp_float(split,
                     -CONTROL_MAX_ACKERMANN_SPLIT,
                     CONTROL_MAX_ACKERMANN_SPLIT);
}

static void calibration_runtime_update(void)
{
  steering_feedback_update();
  handwheel_feedback_update();
}

static void control_update_targets(void)
{
  const vehicle_config_t *config = vehicle_config_get();
  const board_io_state_t *io_state = board_io_get();
  const ewm22_link_state_t *remote_state = ewm22_link_get_state();
  drive_mode_t mode = vehicle_config_runtime_drive_mode(config);
  uint32_t now = HAL_GetTick();
  float throttle = pedal_raw_to_normalized(io_state->throttle_raw,
                                           config->throttle_raw_min,
                                           config->throttle_raw_max);
  float brake = pedal_raw_to_normalized(io_state->brake_raw,
                                        config->brake_raw_min,
                                        config->brake_raw_max);
  float demand = 0.0f;
  float steer_norm;
  float split;
  float left_rpm;
  float right_rpm;
  int32_t steer_input = 0;
  drive_command_mode_t drive_command_mode = DRIVE_COMMAND_NORMAL_STOP;
  bool remote_fresh = remote_command_is_fresh(remote_state, now);
  bool remote_drive_active = remote_drive_authority_active(remote_state, now);
  bool throttle_active = (throttle > PEDAL_ACTIVE_THRESHOLD);
  bool brake_active = (brake > PEDAL_ACTIVE_THRESHOLD);
  bool hardware_estop_active = io_state->hardware_estop_active;
  bool local_estop_active = false;
  bool remote_estop_active = s_control_state.remote_estop_latched;
  bool remote_soft_stop_active = s_control_state.remote_soft_stop_latched;
  bool motion_command_present = false;

  if (remote_fresh && ((remote_state->buttons & REMOTE_BUTTON_ESTOP_MASK) != 0U))
  {
    remote_estop_active = true;
  }

  if (remote_fresh && ((remote_state->buttons & REMOTE_BUTTON_SOFT_STOP_MASK) != 0U))
  {
    remote_soft_stop_active = true;
  }

  if ((pedal_config_t)config->pedal_config == PEDAL_CONFIG_ESTOP_THROTTLE)
  {
    local_estop_active = brake_active;
  }

  s_control_state.hardware_estop_active = hardware_estop_active;
  s_control_state.local_control_active =
      (!remote_drive_active) && (throttle_active || brake_active || hardware_estop_active);

  if (hardware_estop_active || local_estop_active || remote_estop_active)
  {
    demand = 0.0f;
    steer_input = 0;
    drive_command_mode = DRIVE_COMMAND_ESTOP;
    s_control_state.remote_active = false;
    s_control_state.soft_stop_active = false;
    s_control_state.emergency_stop_active = true;
  }
  else if (remote_soft_stop_active)
  {
    demand = 0.0f;
    steer_input = 0;
    drive_command_mode = DRIVE_COMMAND_NORMAL_STOP;
    s_control_state.remote_active = false;
    s_control_state.soft_stop_active = true;
    s_control_state.emergency_stop_active = false;
  }
  else if (remote_drive_active)
  {
    demand = (float)remote_state->throttle / (float)CONTROL_STEER_INPUT_LIMIT;
    steer_input = remote_state->steer;
    s_control_state.remote_active = true;
    s_control_state.soft_stop_active = false;
    s_control_state.emergency_stop_active = false;

    if (remote_state->gear == EWM22_REMOTE_GEAR_N)
    {
      demand = 0.0f;
      drive_command_mode = DRIVE_COMMAND_NORMAL_STOP;
    }
    else if (remote_state->gear == EWM22_REMOTE_GEAR_D)
    {
      if (demand < 0.0f)
      {
        demand = 0.0f;
      }
      drive_command_mode = (demand > PEDAL_ACTIVE_THRESHOLD) ? DRIVE_COMMAND_SPEED : DRIVE_COMMAND_NORMAL_STOP;
    }
    else if (remote_state->gear == EWM22_REMOTE_GEAR_S)
    {
      if (demand < 0.0f)
      {
        demand = 0.0f;
      }
      drive_command_mode = (demand > PEDAL_ACTIVE_THRESHOLD) ? DRIVE_COMMAND_SPEED : DRIVE_COMMAND_FREE_STOP;
    }
    else if (remote_state->gear == EWM22_REMOTE_GEAR_R)
    {
      if (demand > 0.0f)
      {
        demand = 0.0f;
      }
      drive_command_mode = (demand < -PEDAL_ACTIVE_THRESHOLD) ? DRIVE_COMMAND_SPEED : DRIVE_COMMAND_NORMAL_STOP;
    }
    else
    {
      demand = 0.0f;
      drive_command_mode = DRIVE_COMMAND_NORMAL_STOP;
    }
  }
  else
  {
    if ((pedal_config_t)config->pedal_config == PEDAL_CONFIG_ESTOP_THROTTLE)
    {
      /* Single-pedal mode: right pedal always maps to a speed target.
         Releasing the pedal lowers the target toward zero, so the drive
         controller decelerates progressively instead of instantly free-coasting. */
      if (brake >= PEDAL_ACTIVE_THRESHOLD)
      {
        demand = 0.0f;
        steer_input = 0;
        drive_command_mode = DRIVE_COMMAND_ESTOP;
      }
      else
      {
        demand = (throttle > PEDAL_ACTIVE_THRESHOLD) ? throttle : 0.0f;
        drive_command_mode = DRIVE_COMMAND_SPEED;
      }
    }
    else
    {
      if (brake > PEDAL_ACTIVE_THRESHOLD)
      {
        demand = 0.0f;
        drive_command_mode = DRIVE_COMMAND_NORMAL_STOP;
      }
      else if (throttle > PEDAL_ACTIVE_THRESHOLD)
      {
        demand = throttle;
        drive_command_mode = DRIVE_COMMAND_SPEED;
      }
      else
      {
        demand = 0.0f;
        drive_command_mode = DRIVE_COMMAND_FREE_STOP;
      }
    }

    if ((drive_command_mode == DRIVE_COMMAND_SPEED) && (io_state->gear == GEAR_REVERSE))
    {
      demand = -demand;
    }

    if (drive_command_mode != DRIVE_COMMAND_ESTOP)
    {
      steer_input = s_control_state.steer_input;
    }
    s_control_state.remote_active = false;
    s_control_state.soft_stop_active = false;
    s_control_state.emergency_stop_active = false;
  }

  s_control_state.drive_command_mode = drive_command_mode;
  s_control_state.drive_base_rpm = float_to_int32(demand * control_drive_max_rpm(config));
  steer_input = clamp_int32(steer_input, -CONTROL_STEER_INPUT_LIMIT, CONTROL_STEER_INPUT_LIMIT);
  steer_norm = (float)steer_input / (float)CONTROL_STEER_INPUT_LIMIT;
  s_control_state.steer_target_raw = steering_target_raw_from_norm(config, steer_norm);
  s_control_state.steer_target_angle_deg = steering_raw_to_angle_deg(config, s_control_state.steer_target_raw);

  if (mode == DRIVE_MODE_DIFF)
  {
    s_control_state.steer_target_rpm = 0;
    s_control_state.steer_actual_angle_deg = 0.0f;
  }
  else
  {
    int32_t steering_feedback_raw = 0;
    uint32_t steering_feedback_age_ms = 0U;

    if (steering_feedback_get(&steering_feedback_raw, &steering_feedback_age_ms) &&
        (steering_feedback_age_ms <= STEERING_FEEDBACK_STALE_MS))
    {
      s_control_state.steer_target_rpm =
          steering_target_rpm_from_feedback(config,
                                            s_control_state.steer_target_raw,
                                            steering_feedback_raw,
                                            &s_control_state.steer_target_angle_deg,
                                            &s_control_state.steer_actual_angle_deg);
    }
    else
    {
      s_control_state.steer_target_rpm = 0;
      s_control_state.steer_actual_angle_deg = 0.0f;
    }
  }

  split = control_compute_mode_split(config, mode, s_control_state.steer_actual_angle_deg, steer_norm);
  left_rpm = (float)s_control_state.drive_base_rpm * (1.0f + split);
  right_rpm = (float)s_control_state.drive_base_rpm * (1.0f - split);
  s_control_state.left_target_rpm = float_to_int32(left_rpm);
  s_control_state.right_target_rpm = float_to_int32(right_rpm);

  motion_command_present =
      int32_abs_exceeds(s_control_state.drive_base_rpm, VEHICLE_TARGET_RPM_MOVING_THRESHOLD) ||
      int32_abs_exceeds(s_control_state.left_target_rpm, VEHICLE_TARGET_RPM_MOVING_THRESHOLD) ||
      int32_abs_exceeds(s_control_state.right_target_rpm, VEHICLE_TARGET_RPM_MOVING_THRESHOLD);

  if (motion_command_present)
  {
    s_control_state.last_motion_tick_ms = now;
  }

  s_control_state.vehicle_moving_command =
      ((s_control_state.last_motion_tick_ms != 0U) &&
       ((now - s_control_state.last_motion_tick_ms) <= VEHICLE_MOTION_HOLD_MS));
  s_control_state.vehicle_moving = preferred_vehicle_motion_state() || calibration_override_active();
}

static void control_process(void)
{
  uint32_t now = HAL_GetTick();
  drive_mode_t mode = vehicle_config_runtime_drive_mode(vehicle_config_get());

  if (!control_outputs_allowed())
  {
    return;
  }

  if (calibration_override_active())
  {
    return;
  }

  if ((now - s_control_state.last_control_tick_ms) < CONTROL_LOOP_PERIOD_MS)
  {
    return;
  }

  s_control_state.last_control_tick_ms = now;
  control_update_targets();

  if (s_drive_can_ready)
  {
    (void)drive_controller_apply_outputs(s_control_state.drive_command_mode,
                                         s_control_state.right_target_rpm,
                                         s_control_state.left_target_rpm);
  }

  if (s_steer_can_ready)
  {
    if ((mode == DRIVE_MODE_DIFF) || (s_control_state.steer_target_rpm == 0))
    {
      (void)mssc_steering_controller_stop_priority(&s_steer_controller, CAN_TRANSPORT_PRIORITY_HIGH);
    }
    else
    {
      (void)mssc_steering_controller_set_speed_rpm_priority(&s_steer_controller,
                                                            s_control_state.steer_target_rpm,
                                                            CAN_TRANSPORT_PRIORITY_NORMAL);
    }
  }
}

static void calibration_process(void)
{
  calibration_runtime_update();

  if (!s_steer_can_ready)
  {
    return;
  }

  if (s_calibration_runtime.steering_override_active)
  {
    (void)mssc_steering_controller_set_speed_rpm_priority(&s_steer_controller,
                                                          s_calibration_runtime.steering_jog_rpm,
                                                          CAN_TRANSPORT_PRIORITY_HIGH);
  }

  if (s_calibration_runtime.handwheel_override_active)
  {
    (void)mssc_steering_controller_set_speed_rpm_priority(&s_handwheel_controller,
                                                          s_calibration_runtime.handwheel_jog_rpm,
                                                          CAN_TRANSPORT_PRIORITY_HIGH);
  }
}

static void publish_remote_status_if_needed(void)
{
  const vehicle_config_t *config = vehicle_config_get();
  const board_io_state_t *io_state = board_io_get();
  const ewm22_link_state_t *remote_state = ewm22_link_get_state();
  drive_mode_t mode = vehicle_config_runtime_drive_mode(config);
  mssd_drive_feedback_t drive_feedback;
  bool have_drive_feedback = false;
  bool right_feedback_recent = false;
  bool left_feedback_recent = false;
  bool outputs_enabled = control_outputs_allowed();
  uint32_t now = HAL_GetTick();
  bool remote_fresh = remote_command_is_fresh(remote_state, now);
  uint32_t steering_feedback_age_ms = 0U;
  int32_t steering_feedback_raw = 0;
  can_transport_bus_state_t drive_bus_state = {0};
  can_transport_bus_state_t steer_bus_state = {0};
  bool steering_feedback_valid = steering_feedback_get(&steering_feedback_raw, &steering_feedback_age_ms) &&
                                 steering_feedback_is_recent();
  char fault_message_json[192];
  char buffer[1400];

  if (s_drive_can_ready && mssd_drive_controller_get_feedback(&s_drive_controller, &drive_feedback))
  {
    have_drive_feedback = true;
    right_feedback_recent = drive_feedback.right_valid && (drive_feedback.right_age_ms <= DRIVE_FEEDBACK_STALE_MS);
    left_feedback_recent = drive_feedback.left_valid && (drive_feedback.left_age_ms <= DRIVE_FEEDBACK_STALE_MS);
  }

  (void)can_transport_get_bus_state(s_drive_can_handle, &drive_bus_state);
  (void)can_transport_get_bus_state(s_steer_can_handle, &steer_bus_state);
  json_escape_string(chassis_fault_message_zh(s_fault_state.code),
                     fault_message_json,
                     sizeof(fault_message_json));

  if (!(remote_state->ble_connected || remote_state->tcp_connected || remote_fresh))
  {
    return;
  }

  if ((now - s_telemetry_state.last_status_publish_tick_ms) < STATUS_PUBLISH_PERIOD_MS)
  {
    return;
  }

  s_telemetry_state.last_status_publish_tick_ms = now;
  s_telemetry_state.status_publish_seq++;

  (void)snprintf(
      buffer,
      sizeof(buffer),
      "{\"cmd\":\"chassis_status\",\"seq\":%lu,\"dm\":\"%s\",\"ct\":\"%s\",\"da\":\"%s\","
      "\"g\":\"%s\",\"oe\":%u,\"src\":\"%s\",\"rg\":\"%s\",\"ra\":%u,\"rv\":%u,"
      "\"co\":\"%s\",\"rm\":\"%s\",\"rto\":%u,\"lca\":%u,\"mov\":%u,\"mvc\":%u,\"mva\":%u,"
      "\"ssa\":%u,\"esa\":%u,\"hea\":%u,"
      "\"fault_code\":\"%s\",\"fault_domain\":\"%s\",\"fault_message_zh\":\"%s\","
      "\"dcf\":%u,\"scf\":%u,\"hcf\":%u,\"cra\":%u,"
      "\"c1bo\":%lu,\"c2bo\":%lu,\"c1lec\":%u,\"c2lec\":%u,"
      "\"ft\":%u,\"wb\":%u,\"rt\":%u,\"dmr\":%u,\"sid\":%u,\"hid\":%u,\"ls\":%u,\"lsd\":%u,\"ldi\":%u,\"rdi\":%u,"
      "\"thr\":%u,\"brk\":%u,"
      "\"lr\":%ld,\"rr\":%ld,\"lar\":%ld,\"rar\":%ld,\"lav\":%u,\"rav\":%u,\"dfv\":%u,"
      "\"laa\":%lu,\"raa\":%lu,\"sr\":%d,\"dc\":%u,\"sc\":%u,"
      "\"sf\":%ld,\"sfv\":%u,\"sfa\":%lu}\n",
      (unsigned long)s_telemetry_state.status_publish_seq,
      vehicle_config_drive_mode_name(mode),
      vehicle_config_chassis_type_name((chassis_type_t)config->chassis_type),
      vehicle_config_drive_axle_name((drive_axle_t)config->drive_axle),
      board_io_gear_name(io_state->gear),
      outputs_enabled ? 1U : 0U,
      ewm22_link_remote_source_name(remote_state->remote_source),
      ewm22_link_remote_gear_name(remote_state->gear),
      s_control_state.remote_active ? 1U : 0U,
      remote_fresh ? 1U : 0U,
      control_owner_name(remote_state, now),
      remote_control_mode_name(s_control_state.remote_control_mode),
      s_control_state.remote_takeover_enabled ? 1U : 0U,
      s_control_state.local_control_active ? 1U : 0U,
      s_control_state.vehicle_moving ? 1U : 0U,
      s_control_state.vehicle_moving_command ? 1U : 0U,
      s_control_state.vehicle_moving_actual ? 1U : 0U,
      s_control_state.soft_stop_active ? 1U : 0U,
      s_control_state.emergency_stop_active ? 1U : 0U,
      s_control_state.hardware_estop_active ? 1U : 0U,
      chassis_fault_code_name(s_fault_state.code),
      chassis_fault_domain_name(s_fault_state.domain),
      fault_message_json,
      s_fault_state.drive_can_fault ? 1U : 0U,
      s_fault_state.steer_can_fault ? 1U : 0U,
      s_fault_state.handwheel_can_fault ? 1U : 0U,
      s_fault_state.can_recovery_active ? 1U : 0U,
      (unsigned long)drive_bus_state.bus_off_count,
      (unsigned long)steer_bus_state.bus_off_count,
      (unsigned int)drive_bus_state.last_error_code,
      (unsigned int)steer_bus_state.last_error_code,
      (unsigned int)config->front_track_mm,
      (unsigned int)config->wheelbase_mm,
      (unsigned int)config->rear_track_mm,
      (unsigned int)config->drive_max_rpm,
      (unsigned int)config->steer_can_node_id,
      (unsigned int)config->handwheel_can_node_id,
      (unsigned int)config->linear_steering_enabled,
      linear_steering_detected() ? 1U : 0U,
      (unsigned int)(config->left_drive_inverted ? 1U : 0U),
      (unsigned int)(config->right_drive_inverted ? 1U : 0U),
      (unsigned int)io_state->throttle_raw,
      (unsigned int)io_state->brake_raw,
      (long)s_control_state.left_target_rpm,
      (long)s_control_state.right_target_rpm,
      (long)s_control_state.left_actual_rpm,
      (long)s_control_state.right_actual_rpm,
      left_feedback_recent ? 1U : 0U,
      right_feedback_recent ? 1U : 0U,
      (have_drive_feedback && (right_feedback_recent || left_feedback_recent)) ? 1U : 0U,
      (unsigned long)(left_feedback_recent ? drive_feedback.left_age_ms : 0U),
      (unsigned long)(right_feedback_recent ? drive_feedback.right_age_ms : 0U),
      (int)s_control_state.steer_target_rpm,
      s_drive_can_ready ? 1U : 0U,
      s_steer_can_ready ? 1U : 0U,
      (long)steering_feedback_raw,
      steering_feedback_valid ? 1U : 0U,
      (unsigned long)steering_feedback_age_ms);

  (void)ewm22_link_send_text(buffer);
}

static void reply_capabilities(void)
{
  (void)reply_printf(
      "{\"cmd\":\"capability_status\",\"ok\":true,"
      "\"ackermann_supported\":true,\"diff_supported\":true,"
      "\"front_drive_supported\":true,\"rear_drive_supported\":true,"
      "\"linear_steering_supported\":true,"
      "\"pedal_calibration_supported\":true,"
      "\"steering_calibration_supported\":true,"
      "\"remote_takeover_supported\":true,\"remote_monitor_supported\":true,"
      "\"soft_stop_supported\":true,\"hardware_estop_supported\":true,"
      "\"drive_controller\":\"%s\",\"steering_controller\":\"%s\","
      "\"drive_can_ready\":%s,\"steer_can_ready\":%s}\r\n",
      vehicle_config_drive_controller_name((uint8_t)DRIVE_CONTROLLER_MSSD_60EHB_2D),
      vehicle_config_steering_controller_name((uint8_t)STEERING_CONTROLLER_MSSC),
      s_drive_can_ready ? "true" : "false",
      s_steer_can_ready ? "true" : "false");
}

static void reply_linear_steering_status(void)
{
  can_transport_bus_state_t bus_state;
  bool have_bus_state = can_transport_get_bus_state(s_steer_controller.hcan, &bus_state);
  uint32_t rx_count = have_bus_state ? bus_state.rx_count : 0U;
  uint32_t last_rx_ms = have_bus_state ? bus_state.last_rx_tick_ms : 0U;
  uint32_t feedback_age_ms = 0U;
  int32_t position_raw = 0;
  bool detected = handwheel_feedback_get(&position_raw, &feedback_age_ms) &&
                  (feedback_age_ms <= LINEAR_STEERING_ACTIVITY_WINDOW_MS);

  (void)reply_printf(
      "{\"cmd\":\"linear_steering_status\",\"ok\":true,"
      "\"enabled\":%s,\"detected\":%s,\"protocol_supported\":true,\"steer_can_ready\":%s,"
      "\"steer_can_rx_count\":%lu,\"steer_can_last_rx_ms\":%lu,"
      "\"node_id\":%u,\"feedback_age_ms\":%lu,\"position_raw\":%ld,"
      "\"reason\":\"%s\"}\r\n",
      vehicle_config_get()->linear_steering_enabled ? "true" : "false",
      detected ? "true" : "false",
      s_steer_can_ready ? "true" : "false",
      (unsigned long)rx_count,
      (unsigned long)last_rx_ms,
      (unsigned int)vehicle_config_get()->handwheel_can_node_id,
      (unsigned long)feedback_age_ms,
      (long)position_raw,
      detected ? "feedback_ok" : "no_handwheel_feedback");
}

static void reply_config(void)
{
  const vehicle_config_t *config = vehicle_config_get();
  drive_mode_t mode = vehicle_config_runtime_drive_mode(config);

  (void)reply_printf(
      "{\"cmd\":\"config_ack\",\"ok\":true,"
      "\"front_track_mm\":%u,\"wheelbase_mm\":%u,\"rear_track_mm\":%u,\"drive_max_rpm\":%u,"
      "\"steer_can_node_id\":%u,\"handwheel_can_node_id\":%u,"
      "\"chassis_type\":\"%s\",\"drive_axle\":\"%s\",\"drive_mode\":\"%s\","
      "\"linear_steering_enabled\":%s,\"pedal_config\":\"%s\","
      "\"left_drive_inverted\":%s,\"right_drive_inverted\":%s,"
      "\"drive_controller_type\":\"%s\",\"steering_controller_type\":\"%s\"}\r\n",
      (unsigned int)config->front_track_mm,
      (unsigned int)config->wheelbase_mm,
      (unsigned int)config->rear_track_mm,
      (unsigned int)config->drive_max_rpm,
      (unsigned int)config->steer_can_node_id,
      (unsigned int)config->handwheel_can_node_id,
      vehicle_config_chassis_type_name((chassis_type_t)config->chassis_type),
      vehicle_config_drive_axle_name((drive_axle_t)config->drive_axle),
      vehicle_config_drive_mode_name(mode),
      config->linear_steering_enabled ? "true" : "false",
      vehicle_config_pedal_config_name((pedal_config_t)config->pedal_config),
      config->left_drive_inverted ? "true" : "false",
      config->right_drive_inverted ? "true" : "false",
      vehicle_config_drive_controller_name(config->drive_controller_type),
      vehicle_config_steering_controller_name(config->steering_controller_type));
}

static void reply_calibration_status(void)
{
  const vehicle_config_t *config = vehicle_config_get();

  (void)reply_printf(
      "{\"cmd\":\"calibration_status\",\"ok\":true,"
      "\"throttle_raw_min\":%u,\"throttle_raw_max\":%u,"
      "\"brake_raw_min\":%u,\"brake_raw_max\":%u,"
      "\"steering_center\":%d,\"steering_left_10\":%d,\"steering_right_10\":%d,"
      "\"steering_left_limit\":%d,\"steering_right_limit\":%d,"
      "\"handwheel_center\":%d,\"handwheel_left_10\":%d,\"handwheel_right_10\":%d,"
      "\"handwheel_left_limit\":%d,\"handwheel_right_limit\":%d,"
      "\"steering_estimate\":%ld,\"handwheel_estimate\":%ld}\r\n",
      (unsigned int)config->throttle_raw_min,
      (unsigned int)config->throttle_raw_max,
      (unsigned int)config->brake_raw_min,
      (unsigned int)config->brake_raw_max,
      (int)config->steering_center,
      (int)config->steering_left_10,
      (int)config->steering_right_10,
      (int)config->steering_left_limit,
      (int)config->steering_right_limit,
      (int)config->handwheel_center,
      (int)config->handwheel_left_10,
      (int)config->handwheel_right_10,
      (int)config->handwheel_left_limit,
      (int)config->handwheel_right_limit,
      (long)s_calibration_runtime.steering_estimate,
      (long)s_calibration_runtime.handwheel_estimate);
}

static void reply_status(void)
{
  const vehicle_config_t *config = vehicle_config_get();
  const board_io_state_t *io_state = board_io_get();
  const ewm22_link_state_t *remote_state = ewm22_link_get_state();
  drive_mode_t mode = vehicle_config_runtime_drive_mode(config);
  mssd_drive_feedback_t drive_feedback;
  bool have_drive_feedback = false;
  bool right_feedback_recent = false;
  bool left_feedback_recent = false;
  uint32_t now = HAL_GetTick();
  bool remote_fresh = remote_command_is_fresh(remote_state, now);
  uint32_t remote_age_ms = 0U;
  uint32_t text_age_ms = 0U;
  uint32_t steering_feedback_age_ms = 0U;
  int32_t steering_feedback_raw = 0;
  can_transport_bus_state_t drive_bus_state = {0};
  can_transport_bus_state_t steer_bus_state = {0};
  bool steering_feedback_valid = steering_feedback_get(&steering_feedback_raw, &steering_feedback_age_ms) &&
                                 steering_feedback_is_recent();
  char ewm22_last_text_json[128];
  char fault_message_json[192];

  if (remote_state->last_rx_tick_ms != 0U)
  {
    remote_age_ms = now - remote_state->last_rx_tick_ms;
  }

  if (remote_state->last_text_tick_ms != 0U)
  {
    text_age_ms = HAL_GetTick() - remote_state->last_text_tick_ms;
  }

  json_escape_string(remote_state->last_text_line,
                     ewm22_last_text_json,
                     sizeof(ewm22_last_text_json));
  json_escape_string(chassis_fault_message_zh(s_fault_state.code),
                     fault_message_json,
                     sizeof(fault_message_json));

  if (s_drive_can_ready && mssd_drive_controller_get_feedback(&s_drive_controller, &drive_feedback))
  {
    have_drive_feedback = true;
    right_feedback_recent = drive_feedback.right_valid && (drive_feedback.right_age_ms <= DRIVE_FEEDBACK_STALE_MS);
    left_feedback_recent = drive_feedback.left_valid && (drive_feedback.left_age_ms <= DRIVE_FEEDBACK_STALE_MS);
  }

  (void)can_transport_get_bus_state(s_drive_can_handle, &drive_bus_state);
  (void)can_transport_get_bus_state(s_steer_can_handle, &steer_bus_state);

  (void)reply_printf(
      "{\"cmd\":\"status_ack\",\"ok\":true,"
      "\"gear\":\"%s\",\"throttle_raw\":%u,\"brake_raw\":%u,"
      "\"chassis_type\":\"%s\",\"drive_axle\":\"%s\",\"drive_mode\":\"%s\",\"drive_max_rpm\":%u,"
      "\"control_enabled\":%s,\"steer_input\":%ld,"
      "\"drive_base_rpm\":%ld,\"right_target_rpm\":%ld,\"left_target_rpm\":%ld,"
      "\"right_actual_rpm\":%ld,\"left_actual_rpm\":%ld,"
      "\"right_actual_rpm_valid\":%s,\"left_actual_rpm_valid\":%s,"
      "\"right_actual_rpm_age_ms\":%lu,\"left_actual_rpm_age_ms\":%lu,"
      "\"drive_feedback_valid\":%s,"
      "\"steer_target_rpm\":%d,\"remote_active\":%s,\"remote_valid\":%s,"
      "\"control_owner\":\"%s\",\"remote_mode\":\"%s\",\"remote_takeover_enabled\":%s,"
      "\"local_control_active\":%s,\"vehicle_moving\":%s,"
      "\"vehicle_moving_command\":%s,\"vehicle_moving_actual\":%s,"
      "\"soft_stop_active\":%s,\"emergency_stop_active\":%s,\"hardware_estop_active\":%s,"
      "\"outputs_enabled\":%s,\"outputs_locked_by_fault\":%s,"
      "\"fault_code\":\"%s\",\"fault_domain\":\"%s\",\"fault_message_zh\":\"%s\","
      "\"drive_can_fault\":%s,\"steer_can_fault\":%s,\"handwheel_can_fault\":%s,"
      "\"can_recovery_active\":%s,\"can1_bus_off_count\":%lu,\"can2_bus_off_count\":%lu,"
      "\"can1_last_error_code\":%u,\"can2_last_error_code\":%u,"
      "\"remote_source\":\"%s\",\"remote_gear\":\"%s\","
      "\"remote_seq\":%lu,\"remote_throttle\":%d,\"remote_steer\":%d,"
      "\"remote_aux_x\":%d,\"remote_aux_y\":%d,\"remote_buttons\":%u,"
      "\"remote_age_ms\":%lu,\"ble_connected\":%s,\"wifi_sta_connected\":%s,"
      "\"tcp_client_started\":%s,\"tcp_connected\":%s,"
      "\"ewm22_last_text\":\"%s\",\"ewm22_last_text_age_ms\":%lu,"
      "\"drive_can_ready\":%s,\"steer_can_ready\":%s,"
      "\"steer_can_node_id\":%u,\"handwheel_can_node_id\":%u,"
      "\"linear_steering_enabled\":%s,\"linear_steering_detected\":%s,"
      "\"left_drive_inverted\":%s,\"right_drive_inverted\":%s,"
      "\"steering_feedback\":%ld,\"steering_feedback_valid\":%s,\"steering_feedback_age_ms\":%lu}\r\n",
      board_io_gear_name(io_state->gear),
      (unsigned int)io_state->throttle_raw,
      (unsigned int)io_state->brake_raw,
      vehicle_config_chassis_type_name((chassis_type_t)config->chassis_type),
      vehicle_config_drive_axle_name((drive_axle_t)config->drive_axle),
      vehicle_config_drive_mode_name(mode),
      (unsigned int)config->drive_max_rpm,
      s_control_state.control_enabled ? "true" : "false",
      (long)s_control_state.steer_input,
      (long)s_control_state.drive_base_rpm,
      (long)s_control_state.right_target_rpm,
      (long)s_control_state.left_target_rpm,
      (long)s_control_state.right_actual_rpm,
      (long)s_control_state.left_actual_rpm,
      right_feedback_recent ? "true" : "false",
      left_feedback_recent ? "true" : "false",
      (unsigned long)(right_feedback_recent ? drive_feedback.right_age_ms : 0U),
      (unsigned long)(left_feedback_recent ? drive_feedback.left_age_ms : 0U),
      (have_drive_feedback && (right_feedback_recent || left_feedback_recent)) ? "true" : "false",
      (int)s_control_state.steer_target_rpm,
      s_control_state.remote_active ? "true" : "false",
      remote_fresh ? "true" : "false",
      control_owner_name(remote_state, now),
      remote_control_mode_name(s_control_state.remote_control_mode),
      s_control_state.remote_takeover_enabled ? "true" : "false",
      s_control_state.local_control_active ? "true" : "false",
      s_control_state.vehicle_moving ? "true" : "false",
      s_control_state.vehicle_moving_command ? "true" : "false",
      s_control_state.vehicle_moving_actual ? "true" : "false",
      s_control_state.soft_stop_active ? "true" : "false",
      s_control_state.emergency_stop_active ? "true" : "false",
      s_control_state.hardware_estop_active ? "true" : "false",
      control_outputs_allowed() ? "true" : "false",
      s_control_state.outputs_locked_by_fault ? "true" : "false",
      chassis_fault_code_name(s_fault_state.code),
      chassis_fault_domain_name(s_fault_state.domain),
      fault_message_json,
      s_fault_state.drive_can_fault ? "true" : "false",
      s_fault_state.steer_can_fault ? "true" : "false",
      s_fault_state.handwheel_can_fault ? "true" : "false",
      s_fault_state.can_recovery_active ? "true" : "false",
      (unsigned long)drive_bus_state.bus_off_count,
      (unsigned long)steer_bus_state.bus_off_count,
      (unsigned int)drive_bus_state.last_error_code,
      (unsigned int)steer_bus_state.last_error_code,
      ewm22_link_remote_source_name(remote_state->remote_source),
      ewm22_link_remote_gear_name(remote_state->gear),
      (unsigned long)remote_state->last_seq,
      (int)remote_state->throttle,
      (int)remote_state->steer,
      (int)remote_state->aux_x,
      (int)remote_state->aux_y,
      (unsigned int)remote_state->buttons,
      (unsigned long)remote_age_ms,
      remote_state->ble_connected ? "true" : "false",
      remote_state->wifi_sta_connected ? "true" : "false",
      remote_state->tcp_client_started ? "true" : "false",
      remote_state->tcp_connected ? "true" : "false",
      ewm22_last_text_json,
      (unsigned long)text_age_ms,
      s_drive_can_ready ? "true" : "false",
      s_steer_can_ready ? "true" : "false",
      (unsigned int)config->steer_can_node_id,
      (unsigned int)config->handwheel_can_node_id,
      config->linear_steering_enabled ? "true" : "false",
      linear_steering_detected() ? "true" : "false",
      config->left_drive_inverted ? "true" : "false",
      config->right_drive_inverted ? "true" : "false",
        (long)steering_feedback_raw,
        steering_feedback_valid ? "true" : "false",
        (unsigned long)steering_feedback_age_ms);
}

static bool parse_pedal_name(const char *text, bool *is_left)
{
  if ((text == NULL) || (is_left == NULL))
  {
    return false;
  }

  if ((ascii_stricmp(text, "LEFT") == 0) || (ascii_stricmp(text, "L") == 0))
  {
    *is_left = true;
    return true;
  }

  if ((ascii_stricmp(text, "RIGHT") == 0) || (ascii_stricmp(text, "R") == 0))
  {
    *is_left = false;
    return true;
  }

  return false;
}

static bool parse_remote_control_mode(const char *text, remote_control_mode_t *mode)
{
  if ((text == NULL) || (mode == NULL))
  {
    return false;
  }

  if ((ascii_stricmp(text, "MONITOR") == 0) ||
      (ascii_stricmp(text, "WATCH") == 0) ||
      (ascii_stricmp(text, "OBSERVE") == 0) ||
      (ascii_stricmp(text, "RELEASE") == 0))
  {
    *mode = REMOTE_CONTROL_MODE_MONITOR;
    return true;
  }

  if ((ascii_stricmp(text, "TAKEOVER") == 0) ||
      (ascii_stricmp(text, "REMOTE") == 0) ||
      (ascii_stricmp(text, "CONTROL") == 0))
  {
    *mode = REMOTE_CONTROL_MODE_TAKEOVER;
    return true;
  }

  return false;
}

static bool parse_calibration_axis(const char *text, bool *is_handwheel)
{
  if ((text == NULL) || (is_handwheel == NULL))
  {
    return false;
  }

  if ((ascii_stricmp(text, "STEERING") == 0) || (ascii_stricmp(text, "CHASSIS_STEERING") == 0))
  {
    *is_handwheel = false;
    return true;
  }

  if ((ascii_stricmp(text, "HANDWHEEL") == 0) || (ascii_stricmp(text, "LINEAR_STEERING") == 0))
  {
    *is_handwheel = true;
    return true;
  }

  return false;
}

static int16_t *resolve_calibration_field(bool is_handwheel, const char *point_name)
{
  vehicle_config_t *config = (vehicle_config_t *)vehicle_config_get();

  if (point_name == NULL)
  {
    return NULL;
  }

  if (!is_handwheel)
  {
    if (ascii_stricmp(point_name, "CENTER") == 0)
    {
      return &config->steering_center;
    }
    if ((ascii_stricmp(point_name, "LEFT10") == 0) || (ascii_stricmp(point_name, "LEFT_10") == 0))
    {
      return &config->steering_left_10;
    }
    if ((ascii_stricmp(point_name, "RIGHT10") == 0) || (ascii_stricmp(point_name, "RIGHT_10") == 0))
    {
      return &config->steering_right_10;
    }
    if ((ascii_stricmp(point_name, "LEFT_LIMIT") == 0) || (ascii_stricmp(point_name, "LEFT_MAX") == 0))
    {
      return &config->steering_left_limit;
    }
    if ((ascii_stricmp(point_name, "RIGHT_LIMIT") == 0) || (ascii_stricmp(point_name, "RIGHT_MAX") == 0))
    {
      return &config->steering_right_limit;
    }
  }
  else
  {
    if (ascii_stricmp(point_name, "CENTER") == 0)
    {
      return &config->handwheel_center;
    }
    if ((ascii_stricmp(point_name, "LEFT10") == 0) || (ascii_stricmp(point_name, "LEFT_10") == 0))
    {
      return &config->handwheel_left_10;
    }
    if ((ascii_stricmp(point_name, "RIGHT10") == 0) || (ascii_stricmp(point_name, "RIGHT_10") == 0))
    {
      return &config->handwheel_right_10;
    }
    if ((ascii_stricmp(point_name, "LEFT_LIMIT") == 0) || (ascii_stricmp(point_name, "LEFT_MAX") == 0))
    {
      return &config->handwheel_left_limit;
    }
    if ((ascii_stricmp(point_name, "RIGHT_LIMIT") == 0) || (ascii_stricmp(point_name, "RIGHT_MAX") == 0))
    {
      return &config->handwheel_right_limit;
    }
  }

  return NULL;
}

static void handle_set_config(const char *line)
{
  uint32_t value = 0U;
  char text[32];
  chassis_type_t chassis_type;
  drive_axle_t drive_axle;
  drive_mode_t drive_mode;
  pedal_config_t pedal_config;
  bool linear_enabled = false;
  vehicle_config_t config = *vehicle_config_get();

  if (chassis_service_actions_locked())
  {
    (void)reply_write("{\"cmd\":\"config_ack\",\"ok\":false,\"error\":\"vehicle_moving\"}\r\n");
    return;
  }

  if (json_get_uint32(line, "\"front_track_mm\"", &value))
  {
    config.front_track_mm = (uint16_t)value;
  }

  if (json_get_uint32(line, "\"wheelbase_mm\"", &value))
  {
    config.wheelbase_mm = (uint16_t)value;
  }

  if (json_get_uint32(line, "\"rear_track_mm\"", &value))
  {
    config.rear_track_mm = (uint16_t)value;
  }

  if (json_get_uint32(line, "\"drive_max_rpm\"", &value))
  {
    config.drive_max_rpm = (uint16_t)value;
  }

  if (json_get_uint32(line, "\"steer_can_node_id\"", &value))
  {
    config.steer_can_node_id = (uint16_t)value;
  }

  if (json_get_uint32(line, "\"handwheel_can_node_id\"", &value))
  {
    config.handwheel_can_node_id = (uint16_t)value;
  }

  if (json_get_bool(line, "\"left_drive_inverted\"", &linear_enabled))
  {
    config.left_drive_inverted = linear_enabled ? 1U : 0U;
  }

  if (json_get_bool(line, "\"right_drive_inverted\"", &linear_enabled))
  {
    config.right_drive_inverted = linear_enabled ? 1U : 0U;
  }

  if (json_get_string(line, "\"chassis_type\"", text, sizeof(text)) &&
      vehicle_config_parse_chassis_type(text, &chassis_type))
  {
    config.chassis_type = (uint8_t)chassis_type;
  }

  if (json_get_string(line, "\"drive_axle\"", text, sizeof(text)) &&
      vehicle_config_parse_drive_axle(text, &drive_axle))
  {
    config.drive_axle = (uint8_t)drive_axle;
  }

  if (json_get_string(line, "\"drive_mode\"", text, sizeof(text)) &&
      vehicle_config_parse_drive_mode(text, &drive_mode))
  {
    if (drive_mode == DRIVE_MODE_DIFF)
    {
      config.chassis_type = (uint8_t)CHASSIS_TYPE_DIFF;
    }
    else if (drive_mode == DRIVE_MODE_FWD)
    {
      config.chassis_type = (uint8_t)CHASSIS_TYPE_ACKERMANN;
      config.drive_axle = (uint8_t)DRIVE_AXLE_FWD;
    }
    else if ((drive_mode == DRIVE_MODE_RWD) || (drive_mode == DRIVE_MODE_AWD))
    {
      config.chassis_type = (uint8_t)CHASSIS_TYPE_ACKERMANN;
      config.drive_axle = (uint8_t)DRIVE_AXLE_RWD;
    }
  }

  if (json_get_string(line, "\"pedal_config\"", text, sizeof(text)) &&
      vehicle_config_parse_pedal_config(text, &pedal_config))
  {
    config.pedal_config = (uint8_t)pedal_config;
  }

  if (json_get_bool(line, "\"linear_steering_enabled\"", &linear_enabled))
  {
    config.linear_steering_enabled = linear_enabled ? 1U : 0U;
  }

  vehicle_config_set(&config);
  controllers_bind_from_config();
  reply_config();
}

static void handle_save_config(void)
{
  if (chassis_service_actions_locked())
  {
    (void)reply_write("{\"cmd\":\"save_result\",\"ok\":false,\"domain\":\"config\",\"error\":\"vehicle_moving\"}\r\n");
    return;
  }

  bool saved = vehicle_config_save();

  if (saved)
  {
    (void)reply_write("{\"cmd\":\"save_result\",\"ok\":true,\"domain\":\"config\"}\r\n");
  }
  else
  {
    (void)reply_write("{\"cmd\":\"save_result\",\"ok\":false,\"domain\":\"config\",\"error\":\"flash_save_failed\"}\r\n");
  }
}

static void handle_load_config(void)
{
  if (chassis_service_actions_locked())
  {
    (void)reply_write("{\"cmd\":\"save_result\",\"ok\":false,\"domain\":\"config\",\"error\":\"vehicle_moving\"}\r\n");
    return;
  }

  if (vehicle_config_load())
  {
    controllers_bind_from_config();
    reply_config();
  }
  else
  {
    (void)reply_write("{\"cmd\":\"save_result\",\"ok\":false,\"domain\":\"config\",\"error\":\"flash_config_invalid\"}\r\n");
  }
}

static void handle_set_control_enable(const char *line)
{
  bool enabled = false;

  if (!json_get_bool(line, "\"enabled\"", &enabled))
  {
    (void)reply_write("{\"cmd\":\"status_ack\",\"ok\":false,\"error\":\"set_control_enable_needs_enabled\"}\r\n");
    return;
  }

  control_set_enabled(enabled);
  reply_status();
}

static void handle_set_steer_input(const char *line)
{
  int32_t value = 0;

  if (!json_get_int32(line, "\"value\"", &value))
  {
    (void)reply_write("{\"cmd\":\"status_ack\",\"ok\":false,\"error\":\"set_steer_input_needs_value\"}\r\n");
    return;
  }

  s_control_state.steer_input =
      clamp_int32(value, -CONTROL_STEER_INPUT_LIMIT, CONTROL_STEER_INPUT_LIMIT);
  reply_status();
}

static void handle_ewm22_send_at(const char *line)
{
  char text[64];

  if (!json_get_string(line, "\"text\"", text, sizeof(text)))
  {
    (void)reply_write("{\"cmd\":\"ewm22_send_at\",\"ok\":false,\"error\":\"ewm22_send_at_needs_text\"}\r\n");
    return;
  }

  if (ewm22_link_send_text(text))
  {
    (void)reply_printf("{\"cmd\":\"ewm22_send_at\",\"ok\":true,\"text\":\"%s\"}\r\n", text);
  }
  else
  {
    (void)reply_write("{\"cmd\":\"ewm22_send_at\",\"ok\":false,\"error\":\"ewm22_send_failed\"}\r\n");
  }
}

static void handle_drive_test(const char *line)
{
  const vehicle_config_t *config = vehicle_config_get();
  int32_t right_rpm = 0;
  int32_t left_rpm = 0;
  bool have_right = json_get_int32(line, "\"right_rpm\"", &right_rpm);
  bool have_left = json_get_int32(line, "\"left_rpm\"", &left_rpm);

  if (!have_right && !have_left)
  {
    int32_t speed_rpm = 0;
    if (json_get_int32(line, "\"speed_rpm\"", &speed_rpm))
    {
      right_rpm = speed_rpm;
      left_rpm = speed_rpm;
      have_right = true;
      have_left = true;
    }
  }

  if (!have_right || !have_left)
  {
    (void)reply_write("{\"cmd\":\"drive_test\",\"ok\":false,\"error\":\"drive_test_needs_left_rpm_and_right_rpm\"}\r\n");
    return;
  }

  if (!s_drive_can_ready)
  {
    (void)reply_write("{\"cmd\":\"drive_test\",\"ok\":false,\"error\":\"drive_can_not_ready\"}\r\n");
    return;
  }

  control_set_enabled(false);

  if (mssd_drive_controller_set_wheel_rpm(&s_drive_controller,
                                          drive_direction_apply_right_output_rpm(config, right_rpm),
                                          drive_direction_apply_left_output_rpm(config, left_rpm)))
  {
    s_control_state.right_target_rpm = right_rpm;
    s_control_state.left_target_rpm = left_rpm;
    s_control_state.drive_base_rpm = float_to_int32(((float)right_rpm + (float)left_rpm) * 0.5f);
    (void)reply_printf(
        "{\"cmd\":\"drive_test\",\"ok\":true,\"right_rpm\":%ld,\"left_rpm\":%ld}\r\n",
        (long)right_rpm, (long)left_rpm);
  }
  else
  {
    (void)reply_write("{\"cmd\":\"drive_test\",\"ok\":false,\"error\":\"drive_can_send_failed\"}\r\n");
  }
}

static void handle_steer_test(const char *line)
{
  int32_t speed_rpm = 0;

  if (!json_get_int32(line, "\"speed_rpm\"", &speed_rpm))
  {
    (void)reply_write("{\"cmd\":\"steer_test\",\"ok\":false,\"error\":\"steer_test_needs_speed_rpm\"}\r\n");
    return;
  }

  if (!s_steer_can_ready)
  {
    (void)reply_write("{\"cmd\":\"steer_test\",\"ok\":false,\"error\":\"steer_can_not_ready\"}\r\n");
    return;
  }

  control_set_enabled(false);

  if (mssc_steering_controller_set_speed_rpm(&s_steer_controller, (int16_t)speed_rpm))
  {
    s_control_state.steer_target_rpm = (int16_t)speed_rpm;
    (void)reply_printf(
        "{\"cmd\":\"steer_test\",\"ok\":true,\"speed_rpm\":%ld}\r\n",
        (long)speed_rpm);
  }
  else
  {
    (void)reply_write("{\"cmd\":\"steer_test\",\"ok\":false,\"error\":\"steer_can_send_failed\"}\r\n");
  }
}

static void handle_stop_all(void)
{
  bool drive_ok = true;
  bool steer_ok = true;
  bool handwheel_ok = true;

  control_set_enabled(false);
  s_calibration_runtime.steering_override_active = false;
  s_calibration_runtime.handwheel_override_active = false;
  s_calibration_runtime.steering_jog_rpm = 0;
  s_calibration_runtime.handwheel_jog_rpm = 0;
  s_control_state.remote_soft_stop_latched = false;
  s_control_state.remote_estop_latched = false;

  if (s_drive_can_ready)
  {
    drive_ok = mssd_drive_controller_stop(&s_drive_controller);
  }

  if (s_steer_can_ready)
  {
    steer_ok = mssc_steering_controller_stop(&s_steer_controller);
    handwheel_ok = mssc_steering_controller_stop(&s_handwheel_controller);
  }

  (void)reply_printf(
      "{\"cmd\":\"stop_all\",\"ok\":%s,\"drive_ok\":%s,\"steer_ok\":%s,\"handwheel_ok\":%s}\r\n",
      (drive_ok && steer_ok && handwheel_ok) ? "true" : "false",
      drive_ok ? "true" : "false",
      steer_ok ? "true" : "false",
      handwheel_ok ? "true" : "false");
}

static void handle_query_linear_steering(void)
{
  reply_linear_steering_status();
}

static void handle_set_remote_mode(const char *line)
{
  char mode_text[24];
  remote_control_mode_t mode;

  if (!json_get_string(line, "\"mode\"", mode_text, sizeof(mode_text)) ||
      !parse_remote_control_mode(mode_text, &mode))
  {
    (void)reply_write("{\"cmd\":\"status_ack\",\"ok\":false,\"error\":\"set_remote_mode_needs_mode\"}\r\n");
    return;
  }

  s_control_state.remote_control_mode = mode;
  s_control_state.remote_takeover_enabled = (mode == REMOTE_CONTROL_MODE_TAKEOVER);
  s_control_state.last_control_tick_ms = 0U;

  if (!s_control_state.remote_takeover_enabled)
  {
    s_control_state.remote_active = false;
  }

  reply_status();
}

static void handle_set_remote_stop_state(const char *line)
{
  bool have_soft_stop = false;
  bool have_estop = false;
  bool soft_stop = false;
  bool estop = false;

  have_soft_stop = json_get_bool(line, "\"soft_stop\"", &soft_stop);
  have_estop = json_get_bool(line, "\"estop\"", &estop);

  if (!have_soft_stop && !have_estop)
  {
    (void)reply_write("{\"cmd\":\"status_ack\",\"ok\":false,\"error\":\"set_remote_stop_state_needs_state\"}\r\n");
    return;
  }

  if (have_estop && !estop && drive_feedback_available() && s_control_state.vehicle_moving_actual)
  {
    (void)reply_write("{\"cmd\":\"status_ack\",\"ok\":false,\"error\":\"vehicle_moving_actual\"}\r\n");
    return;
  }

  if (have_estop)
  {
    s_control_state.remote_estop_latched = estop;
    if (estop)
    {
      s_control_state.remote_soft_stop_latched = false;
    }
  }

  if (have_soft_stop)
  {
    s_control_state.remote_soft_stop_latched = soft_stop;
    if (soft_stop)
    {
      s_control_state.remote_estop_latched = false;
    }
  }

  s_control_state.last_control_tick_ms = 0U;

  reply_status();
}

static void handle_get_calibration(void)
{
  calibration_runtime_update();
  reply_calibration_status();
}

static void handle_get_capabilities(void)
{
  reply_capabilities();
}

static void handle_steering_jog(const char *line)
{
  char axis_text[32];
  bool is_handwheel = false;
  int32_t rpm = 0;

  if (!json_get_string(line, "\"axis\"", axis_text, sizeof(axis_text)) ||
      !parse_calibration_axis(axis_text, &is_handwheel))
  {
    (void)reply_write("{\"cmd\":\"steering_jog\",\"ok\":false,\"error\":\"steering_jog_needs_axis\"}\r\n");
    return;
  }

  if (!json_get_int32(line, "\"rpm\"", &rpm))
  {
    (void)reply_write("{\"cmd\":\"steering_jog\",\"ok\":false,\"error\":\"steering_jog_needs_rpm\"}\r\n");
    return;
  }

  if (chassis_service_actions_locked())
  {
    (void)reply_write("{\"cmd\":\"steering_jog\",\"ok\":false,\"error\":\"vehicle_moving\"}\r\n");
    return;
  }

  calibration_runtime_update();

  if (!s_steer_can_ready)
  {
    (void)reply_write("{\"cmd\":\"steering_jog\",\"ok\":false,\"error\":\"steer_can_not_ready\"}\r\n");
    return;
  }

  if (!is_handwheel)
  {
    control_set_enabled(false);
    control_stop_outputs();
    s_calibration_runtime.steering_override_active = (rpm != 0);
    s_calibration_runtime.handwheel_override_active = false;
    s_calibration_runtime.steering_jog_rpm = (int16_t)clamp_int32(rpm, -200, 200);
    s_calibration_runtime.handwheel_jog_rpm = 0;

    if (!s_calibration_runtime.steering_override_active)
    {
      (void)mssc_steering_controller_stop(&s_steer_controller);
    }

    (void)mssc_steering_controller_stop(&s_handwheel_controller);
  }
  else
  {
    control_set_enabled(false);
    control_stop_outputs();
    s_calibration_runtime.handwheel_override_active = (rpm != 0);
    s_calibration_runtime.steering_override_active = false;
    s_calibration_runtime.handwheel_jog_rpm = (int16_t)clamp_int32(rpm, -200, 200);
    s_calibration_runtime.steering_jog_rpm = 0;

    if (!s_calibration_runtime.handwheel_override_active)
    {
      (void)mssc_steering_controller_stop(&s_handwheel_controller);
    }

    (void)mssc_steering_controller_stop(&s_steer_controller);
  }

  (void)reply_printf(
      "{\"cmd\":\"steering_jog\",\"ok\":true,\"axis\":\"%s\",\"rpm\":%ld,"
      "\"steering_estimate\":%ld,\"handwheel_estimate\":%ld}\r\n",
      is_handwheel ? "HANDWHEEL" : "STEERING",
      (long)rpm,
      (long)s_calibration_runtime.steering_estimate,
      (long)s_calibration_runtime.handwheel_estimate);
}

static void handle_set_calibration_point(const char *line)
{
  char axis_text[32];
  char point_text[32];
  bool is_handwheel = false;
  int16_t *target_field;
  int32_t source_value;

  if (!json_get_string(line, "\"axis\"", axis_text, sizeof(axis_text)) ||
      !parse_calibration_axis(axis_text, &is_handwheel))
  {
    (void)reply_write("{\"cmd\":\"set_calibration_point\",\"ok\":false,\"error\":\"set_calibration_point_needs_axis\"}\r\n");
    return;
  }

  if (!json_get_string(line, "\"point\"", point_text, sizeof(point_text)))
  {
    (void)reply_write("{\"cmd\":\"set_calibration_point\",\"ok\":false,\"error\":\"set_calibration_point_needs_point\"}\r\n");
    return;
  }

  target_field = resolve_calibration_field(is_handwheel, point_text);
  if (target_field == NULL)
  {
    (void)reply_write("{\"cmd\":\"set_calibration_point\",\"ok\":false,\"error\":\"set_calibration_point_unknown_point\"}\r\n");
    return;
  }

  if (chassis_service_actions_locked())
  {
    (void)reply_write("{\"cmd\":\"set_calibration_point\",\"ok\":false,\"error\":\"vehicle_moving\"}\r\n");
    return;
  }

  calibration_runtime_update();
  source_value = is_handwheel ? s_calibration_runtime.handwheel_estimate : s_calibration_runtime.steering_estimate;
  *target_field = (int16_t)clamp_int32(source_value, -32768, 32767);
  vehicle_config_set(vehicle_config_get());

  (void)reply_printf(
      "{\"cmd\":\"set_calibration_point\",\"ok\":true,\"axis\":\"%s\",\"point\":\"%s\",\"value\":%ld}\r\n",
      is_handwheel ? "HANDWHEEL" : "STEERING",
      point_text,
      (long)source_value);
}

static void handle_capture_pedal_sample(const char *line)
{
  char pedal_text[16];
  char phase_text[16];
  bool is_left = false;
  bool pressed_phase;
  bool reset_requested = false;
  pedal_sample_accumulator_t *accumulator;
  const board_io_state_t *io_state = board_io_get();
  vehicle_config_t config = *vehicle_config_get();
  uint16_t raw_value;
  uint32_t average_value;
  uint32_t sample_count;

  if (!json_get_string(line, "\"pedal\"", pedal_text, sizeof(pedal_text)) ||
      !parse_pedal_name(pedal_text, &is_left))
  {
    (void)reply_write("{\"cmd\":\"pedal_sample_result\",\"ok\":false,\"error\":\"capture_pedal_sample_needs_pedal\"}\r\n");
    return;
  }

  if (!json_get_string(line, "\"phase\"", phase_text, sizeof(phase_text)))
  {
    (void)reply_write("{\"cmd\":\"pedal_sample_result\",\"ok\":false,\"error\":\"capture_pedal_sample_needs_phase\"}\r\n");
    return;
  }

  if (chassis_service_actions_locked())
  {
    (void)reply_write("{\"cmd\":\"pedal_sample_result\",\"ok\":false,\"error\":\"vehicle_moving\"}\r\n");
    return;
  }

  pressed_phase =
      (ascii_stricmp(phase_text, "PRESSED") == 0) || (ascii_stricmp(phase_text, "PRESS") == 0);

  accumulator = is_left ? &s_left_pedal_samples : &s_right_pedal_samples;
  if (json_get_bool(line, "\"reset\"", &reset_requested) && reset_requested)
  {
    memset(accumulator, 0, sizeof(*accumulator));
  }
  raw_value = is_left ? io_state->brake_raw : io_state->throttle_raw;

  if (pressed_phase)
  {
    accumulator->pressed_sum += raw_value;
    accumulator->pressed_count++;
    sample_count = accumulator->pressed_count;
    average_value = accumulator->pressed_sum / accumulator->pressed_count;
  }
  else
  {
    accumulator->released_sum += raw_value;
    accumulator->released_count++;
    sample_count = accumulator->released_count;
    average_value = accumulator->released_sum / accumulator->released_count;
  }

  if (is_left)
  {
    if (pressed_phase && (sample_count >= PEDAL_SAMPLE_TARGET_COUNT))
    {
      config.brake_raw_max = (uint16_t)average_value;
    }
    else if (!pressed_phase && (sample_count >= PEDAL_SAMPLE_TARGET_COUNT))
    {
      config.brake_raw_min = (uint16_t)average_value;
    }
  }
  else
  {
    if (pressed_phase && (sample_count >= PEDAL_SAMPLE_TARGET_COUNT))
    {
      config.throttle_raw_max = (uint16_t)average_value;
    }
    else if (!pressed_phase && (sample_count >= PEDAL_SAMPLE_TARGET_COUNT))
    {
      config.throttle_raw_min = (uint16_t)average_value;
    }
  }

  vehicle_config_set(&config);

  (void)reply_printf(
      "{\"cmd\":\"pedal_sample_result\",\"ok\":true,\"pedal\":\"%s\",\"phase\":\"%s\","
      "\"raw\":%u,\"count\":%lu,\"average\":%lu}\r\n",
      is_left ? "LEFT" : "RIGHT",
      pressed_phase ? "PRESSED" : "RELEASED",
      (unsigned int)raw_value,
      (unsigned long)sample_count,
      (unsigned long)average_value);
}

static void handle_save_calibration(void)
{
  if (chassis_service_actions_locked())
  {
    (void)reply_write("{\"cmd\":\"save_result\",\"ok\":false,\"domain\":\"calibration\",\"error\":\"vehicle_moving\"}\r\n");
    return;
  }

  bool saved = vehicle_config_save();

  if (saved)
  {
    (void)reply_write("{\"cmd\":\"save_result\",\"ok\":true,\"domain\":\"calibration\"}\r\n");
  }
  else
  {
    (void)reply_write("{\"cmd\":\"save_result\",\"ok\":false,\"domain\":\"calibration\",\"error\":\"flash_save_failed\"}\r\n");
  }
}

static void handle_help(void)
{
  (void)reply_write(
      "{\"cmd\":\"help\",\"ok\":true,\"commands\":["
      "\"get_config\",\"set_config\",\"save_config\",\"load_config\","
      "\"get_status\",\"set_control_enable\",\"set_steer_input\","
      "\"set_remote_mode\",\"set_remote_stop_state\","
      "\"ewm22_send_at\",\"drive_test\",\"steer_test\",\"stop_all\","
      "\"get_capabilities\",\"query_linear_steering\","
      "\"get_calibration\",\"steering_jog\",\"set_calibration_point\","
      "\"capture_pedal_sample\",\"save_calibration\",\"help\"]}\r\n");
}

static void process_line_with_reply_target(const char *line, bool reply_to_remote)
{
  bool previous_reply_target = s_reply_to_remote;

  s_reply_to_remote = reply_to_remote;

  if (command_is(line, "get_config") || command_is(line, "get_app_config"))
  {
    reply_config();
  }
  else if (command_is(line, "set_config") || command_is(line, "set_app_config"))
  {
    handle_set_config(line);
  }
  else if (command_is(line, "save_config") || command_is(line, "save_app_config"))
  {
    handle_save_config();
  }
  else if (command_is(line, "load_config") || command_is(line, "load_app_config"))
  {
    handle_load_config();
  }
  else if (command_is(line, "get_status"))
  {
    reply_status();
  }
  else if (command_is(line, "set_control_enable"))
  {
    handle_set_control_enable(line);
  }
  else if (command_is(line, "set_steer_input"))
  {
    handle_set_steer_input(line);
  }
  else if (command_is(line, "set_remote_mode"))
  {
    handle_set_remote_mode(line);
  }
  else if (command_is(line, "set_remote_stop_state"))
  {
    handle_set_remote_stop_state(line);
  }
  else if (command_is(line, "ewm22_send_at"))
  {
    handle_ewm22_send_at(line);
  }
  else if (command_is(line, "drive_test"))
  {
    handle_drive_test(line);
  }
  else if (command_is(line, "steer_test"))
  {
    handle_steer_test(line);
  }
  else if (command_is(line, "stop_all"))
  {
    handle_stop_all();
  }
  else if (command_is(line, "get_capabilities"))
  {
    handle_get_capabilities();
  }
  else if (command_is(line, "query_linear_steering"))
  {
    handle_query_linear_steering();
  }
  else if (command_is(line, "get_calibration"))
  {
    handle_get_calibration();
  }
  else if (command_is(line, "steering_jog"))
  {
    handle_steering_jog(line);
  }
  else if (command_is(line, "set_calibration_point"))
  {
    handle_set_calibration_point(line);
  }
  else if (command_is(line, "capture_pedal_sample"))
  {
    handle_capture_pedal_sample(line);
  }
  else if (command_is(line, "save_calibration"))
  {
    handle_save_calibration();
  }
  else if (command_is(line, "help"))
  {
    handle_help();
  }
  else
  {
    (void)reply_write("{\"cmd\":\"error\",\"ok\":false,\"error\":\"unknown_command\"}\r\n");
  }

  s_reply_to_remote = previous_reply_target;
}

void chassis_app_init(ADC_HandleTypeDef *hadc,
                      CAN_HandleTypeDef *drive_can,
                      CAN_HandleTypeDef *steer_can,
                      UART_HandleTypeDef *ewm22_uart)
{
  usb_cdc_shell_init();
  vehicle_config_init();
  board_io_init(hadc);
  ewm22_link_init(ewm22_uart);
  control_reset_targets();
  memset(&s_calibration_runtime, 0, sizeof(s_calibration_runtime));
  memset(&s_left_pedal_samples, 0, sizeof(s_left_pedal_samples));
  memset(&s_right_pedal_samples, 0, sizeof(s_right_pedal_samples));
  s_control_state.control_enabled = true;
  s_control_state.remote_control_mode = REMOTE_CONTROL_MODE_MONITOR;
  s_control_state.remote_takeover_enabled = false;
  s_control_state.remote_soft_stop_latched = false;
  s_control_state.remote_estop_latched = false;
  s_control_state.steer_input = 0;
  s_control_state.last_control_tick_ms = 0U;
  s_control_state.last_motion_tick_ms = 0U;
  s_control_state.last_actual_motion_tick_ms = 0U;
  s_control_state.right_actual_rpm = 0;
  s_control_state.left_actual_rpm = 0;
  s_control_state.right_actual_rpm_valid = false;
  s_control_state.left_actual_rpm_valid = false;
  s_control_state.drive_feedback_valid = false;
  s_control_state.outputs_locked_by_fault = false;
  s_control_state.vehicle_moving_command = false;
  s_control_state.vehicle_moving_actual = false;
  s_telemetry_state.last_status_publish_tick_ms = 0U;
  s_telemetry_state.status_publish_seq = 0U;
  memset(&s_fault_state, 0, sizeof(s_fault_state));
  s_reply_to_remote = false;
  s_drive_can_handle = drive_can;
  s_steer_can_handle = steer_can;
  controllers_bind_from_config();

  s_drive_can_ready = can_transport_start(drive_can, 0U, 14U);
  s_steer_can_ready = can_transport_start(steer_can, 14U, 14U);
  controllers_bind_from_config();
}

void chassis_app_process(void)
{
  char line[256];

  board_io_process();
  can_transport_process();
  drive_feedback_update();
  calibration_runtime_update();
  fault_state_refresh();

  if (usb_cdc_shell_get_line(line, sizeof(line)))
  {
    process_line_with_reply_target(line, false);
  }

  if (ewm22_link_pop_app_command_line(line, sizeof(line)))
  {
    process_line_with_reply_target(line, true);
  }

  control_process();
  calibration_process();
  can_transport_process();
  drive_feedback_update();
  calibration_runtime_update();
  fault_state_refresh();
  publish_remote_status_if_needed();
}

void chassis_app_usb_rx(const uint8_t *data, uint32_t len)
{
  usb_cdc_shell_feed_bytes(data, len);
}
