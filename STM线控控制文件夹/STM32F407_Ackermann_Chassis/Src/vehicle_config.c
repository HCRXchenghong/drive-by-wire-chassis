#include "vehicle_config.h"
#include "main.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#define VEHICLE_CONFIG_VERSION 0x00070000UL
#define VEHICLE_CONFIG_VERSION_V6 0x00060000UL
#define VEHICLE_CONFIG_VERSION_V5 0x00050000UL
#define VEHICLE_CONFIG_VERSION_V4 0x00040000UL
#define VEHICLE_CONFIG_VERSION_V3 0x00030000UL
#define VEHICLE_CONFIG_STORAGE_MAGIC 0x56434647UL
#define VEHICLE_CONFIG_STORAGE_VERSION 0x00070000UL
#define VEHICLE_CONFIG_STORAGE_VERSION_V6 0x00060000UL
#define VEHICLE_CONFIG_STORAGE_VERSION_V5 0x00050000UL
#define VEHICLE_CONFIG_STORAGE_VERSION_V4 0x00040000UL
#define VEHICLE_CONFIG_STORAGE_VERSION_V3 0x00030000UL
#define VEHICLE_CONFIG_FLASH_SECTOR FLASH_SECTOR_11
#define VEHICLE_CONFIG_FLASH_ADDRESS 0x080E0000UL
#define VEHICLE_CONFIG_DEFAULT_DRIVE_MAX_RPM 500U
#define VEHICLE_CONFIG_MIN_DRIVE_MAX_RPM 50U
#define VEHICLE_CONFIG_MAX_DRIVE_MAX_RPM 5000U
#define VEHICLE_CONFIG_DEFAULT_STEERING_MAX_RPM 2000U
#define VEHICLE_CONFIG_MIN_STEERING_MAX_RPM 200U
#define VEHICLE_CONFIG_MAX_STEERING_MAX_RPM 5000U

typedef struct
{
  uint32_t magic;
  uint32_t storage_version;
  vehicle_config_t config;
  uint32_t checksum;
} vehicle_config_flash_image_t;

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
  uint8_t left_drive_inverted;
  uint8_t right_drive_inverted;
  uint16_t steer_can_node_id;
  uint16_t handwheel_can_node_id;
  uint16_t throttle_raw_min;
  uint16_t throttle_raw_max;
  uint16_t brake_raw_min;
  uint16_t brake_raw_max;
  int32_t steering_center;
  int32_t steering_left_10;
  int32_t steering_right_10;
  int32_t steering_left_limit;
  int32_t steering_right_limit;
  int32_t handwheel_center;
  int32_t handwheel_left_10;
  int32_t handwheel_right_10;
  int32_t handwheel_left_limit;
  int32_t handwheel_right_limit;
  uint16_t drive_max_rpm;
  uint32_t version;
} vehicle_config_v6_t;

typedef struct
{
  uint32_t magic;
  uint32_t storage_version;
  vehicle_config_v6_t config;
  uint32_t checksum;
} vehicle_config_flash_image_v6_t;

typedef struct
{
  uint32_t magic;
  uint32_t storage_version;
  vehicle_config_v6_t config;
  uint32_t checksum;
} vehicle_config_flash_image_v5_t;

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
  uint8_t left_drive_inverted;
  uint8_t right_drive_inverted;
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
  uint16_t drive_max_rpm;
  uint32_t version;
} vehicle_config_v4_t;

typedef struct
{
  uint32_t magic;
  uint32_t storage_version;
  vehicle_config_v4_t config;
  uint32_t checksum;
} vehicle_config_flash_image_v4_t;

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
  uint8_t left_drive_inverted;
  uint8_t right_drive_inverted;
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
} vehicle_config_v3_t;

typedef struct
{
  uint32_t magic;
  uint32_t storage_version;
  vehicle_config_v3_t config;
  uint32_t checksum;
} vehicle_config_flash_image_v3_t;

static vehicle_config_t g_vehicle_config;

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

static uint32_t vehicle_config_checksum_bytes(const void *data, size_t len)
{
  const uint8_t *bytes = (const uint8_t *)data;
  uint32_t hash = 2166136261UL;
  size_t index;

  for (index = 0U; index < len; ++index)
  {
    hash ^= bytes[index];
    hash *= 16777619UL;
  }

  return hash;
}

static void vehicle_config_set_defaults(vehicle_config_t *config)
{
  memset(config, 0, sizeof(*config));
  config->front_track_mm = 1280U;
  config->wheelbase_mm = 1720U;
  config->rear_track_mm = 1260U;
  config->chassis_type = (uint8_t)CHASSIS_TYPE_ACKERMANN;
  config->drive_axle = (uint8_t)DRIVE_AXLE_RWD;
  config->drive_controller_type = (uint8_t)DRIVE_CONTROLLER_MSSD_60EHB_2D;
  config->steering_controller_type = (uint8_t)STEERING_CONTROLLER_MSSC;
  config->linear_steering_enabled = 0U;
  config->pedal_config = (uint8_t)PEDAL_CONFIG_BRAKE_THROTTLE;
  config->left_drive_inverted = 0U;
  config->right_drive_inverted = 0U;
  config->steer_can_node_id = 1U;
  config->handwheel_can_node_id = 2U;
  config->throttle_raw_min = 240U;
  config->throttle_raw_max = 3840U;
  config->brake_raw_min = 240U;
  config->brake_raw_max = 3840U;
  config->steering_center = 0;
  config->steering_left_10 = 100;
  config->steering_right_10 = -100;
  config->steering_left_limit = 600;
  config->steering_right_limit = -600;
  config->handwheel_center = 0;
  config->handwheel_left_10 = -100;
  config->handwheel_right_10 = 100;
  config->handwheel_left_limit = -600;
  config->handwheel_right_limit = 600;
  config->drive_max_rpm = VEHICLE_CONFIG_DEFAULT_DRIVE_MAX_RPM;
  config->steering_max_rpm = VEHICLE_CONFIG_DEFAULT_STEERING_MAX_RPM;
  config->version = VEHICLE_CONFIG_VERSION;
}

static void vehicle_config_fix_u16_range(uint16_t *min_value, uint16_t *max_value)
{
  uint16_t safe_min;
  uint16_t safe_max;

  if ((min_value == NULL) || (max_value == NULL))
  {
    return;
  }

  safe_min = *min_value;
  safe_max = *max_value;

  if (safe_min > 4095U)
  {
    safe_min = 4095U;
  }

  if (safe_max > 4095U)
  {
    safe_max = 4095U;
  }

  if (safe_max <= safe_min)
  {
    if (safe_min >= 4090U)
    {
      safe_min = 0U;
      safe_max = 4095U;
    }
    else
    {
      safe_max = safe_min + 1U;
    }
  }

  *min_value = safe_min;
  *max_value = safe_max;
}

static void vehicle_config_fix_can_node_id(uint16_t *node_id, uint16_t fallback)
{
  if (node_id == NULL)
  {
    return;
  }

  if ((*node_id == 0U) || (*node_id > 0x007FU))
  {
    *node_id = fallback;
  }
}

static void vehicle_config_fix_drive_max_rpm(uint16_t *drive_max_rpm)
{
  if (drive_max_rpm == NULL)
  {
    return;
  }

  if (*drive_max_rpm == 0U)
  {
    *drive_max_rpm = VEHICLE_CONFIG_DEFAULT_DRIVE_MAX_RPM;
    return;
  }

  if (*drive_max_rpm < VEHICLE_CONFIG_MIN_DRIVE_MAX_RPM)
  {
    *drive_max_rpm = VEHICLE_CONFIG_MIN_DRIVE_MAX_RPM;
  }

  if (*drive_max_rpm > VEHICLE_CONFIG_MAX_DRIVE_MAX_RPM)
  {
    *drive_max_rpm = VEHICLE_CONFIG_MAX_DRIVE_MAX_RPM;
  }
}

static void vehicle_config_fix_steering_max_rpm(uint16_t *steering_max_rpm)
{
  if (steering_max_rpm == NULL)
  {
    return;
  }

  if (*steering_max_rpm == 0U)
  {
    *steering_max_rpm = VEHICLE_CONFIG_DEFAULT_STEERING_MAX_RPM;
    return;
  }

  if (*steering_max_rpm < VEHICLE_CONFIG_MIN_STEERING_MAX_RPM)
  {
    *steering_max_rpm = VEHICLE_CONFIG_MIN_STEERING_MAX_RPM;
  }

  if (*steering_max_rpm > VEHICLE_CONFIG_MAX_STEERING_MAX_RPM)
  {
    *steering_max_rpm = VEHICLE_CONFIG_MAX_STEERING_MAX_RPM;
  }
}

static void vehicle_config_swap_i32(int32_t *lhs, int32_t *rhs)
{
  int32_t tmp;

  if ((lhs == NULL) || (rhs == NULL))
  {
    return;
  }

  tmp = *lhs;
  *lhs = *rhs;
  *rhs = tmp;
}

static bool vehicle_config_steering_uses_legacy_polarity(const vehicle_config_t *config)
{
  int32_t center_value;

  if (config == NULL)
  {
    return false;
  }

  center_value = config->steering_center;
  return (config->steering_left_10 < center_value) &&
         (config->steering_left_limit < center_value) &&
         (config->steering_right_10 > center_value) &&
         (config->steering_right_limit > center_value);
}

static void vehicle_config_migrate_steering_polarity(vehicle_config_t *config)
{
  if (!vehicle_config_steering_uses_legacy_polarity(config))
  {
    return;
  }

  vehicle_config_swap_i32(&config->steering_left_10, &config->steering_right_10);
  vehicle_config_swap_i32(&config->steering_left_limit, &config->steering_right_limit);
}

static bool vehicle_config_same_center_side(int32_t center_value, int32_t lhs, int32_t rhs)
{
  int32_t lhs_delta = (int32_t)lhs - (int32_t)center_value;
  int32_t rhs_delta = (int32_t)rhs - (int32_t)center_value;

  if ((lhs_delta == 0) || (rhs_delta == 0))
  {
    return false;
  }

  return ((lhs_delta < 0) && (rhs_delta < 0)) ||
         ((lhs_delta > 0) && (rhs_delta > 0));
}

static void vehicle_config_fix_axis_limits(int32_t center_value,
                                           int32_t left_10_value,
                                           int32_t right_10_value,
                                           int32_t *left_limit,
                                           int32_t *right_limit)
{
  int32_t tmp;

  if ((left_limit == NULL) || (right_limit == NULL))
  {
    return;
  }

  if (!vehicle_config_same_center_side(center_value, left_10_value, *left_limit) &&
      vehicle_config_same_center_side(center_value, left_10_value, *right_limit) &&
      !vehicle_config_same_center_side(center_value, right_10_value, *right_limit) &&
      vehicle_config_same_center_side(center_value, right_10_value, *left_limit))
  {
    tmp = *left_limit;
    *left_limit = *right_limit;
    *right_limit = tmp;
  }
}

static void vehicle_config_sanitize(vehicle_config_t *config)
{
  if (config->front_track_mm < 100U)
  {
    config->front_track_mm = 100U;
  }

  if (config->wheelbase_mm < 100U)
  {
    config->wheelbase_mm = 100U;
  }

  if (config->rear_track_mm < 100U)
  {
    config->rear_track_mm = 100U;
  }

  if (config->chassis_type > (uint8_t)CHASSIS_TYPE_DIFF)
  {
    config->chassis_type = (uint8_t)CHASSIS_TYPE_ACKERMANN;
  }

  if (config->drive_axle > (uint8_t)DRIVE_AXLE_RWD)
  {
    config->drive_axle = (uint8_t)DRIVE_AXLE_RWD;
  }

  if (config->drive_controller_type != (uint8_t)DRIVE_CONTROLLER_MSSD_60EHB_2D)
  {
    config->drive_controller_type = (uint8_t)DRIVE_CONTROLLER_MSSD_60EHB_2D;
  }

  if (config->steering_controller_type != (uint8_t)STEERING_CONTROLLER_MSSC)
  {
    config->steering_controller_type = (uint8_t)STEERING_CONTROLLER_MSSC;
  }

  config->linear_steering_enabled = config->linear_steering_enabled ? 1U : 0U;

  if (config->pedal_config > (uint8_t)PEDAL_CONFIG_ESTOP_THROTTLE)
  {
    config->pedal_config = (uint8_t)PEDAL_CONFIG_BRAKE_THROTTLE;
  }

  config->left_drive_inverted = config->left_drive_inverted ? 1U : 0U;
  config->right_drive_inverted = config->right_drive_inverted ? 1U : 0U;

  vehicle_config_fix_can_node_id(&config->steer_can_node_id, 1U);
  vehicle_config_fix_can_node_id(&config->handwheel_can_node_id, 2U);
  vehicle_config_fix_u16_range(&config->throttle_raw_min, &config->throttle_raw_max);
  vehicle_config_fix_u16_range(&config->brake_raw_min, &config->brake_raw_max);
  vehicle_config_fix_drive_max_rpm(&config->drive_max_rpm);
  vehicle_config_fix_steering_max_rpm(&config->steering_max_rpm);
  vehicle_config_migrate_steering_polarity(config);
  vehicle_config_fix_axis_limits(config->steering_center,
                                 config->steering_left_10,
                                 config->steering_right_10,
                                 &config->steering_left_limit,
                                 &config->steering_right_limit);
  vehicle_config_fix_axis_limits(config->handwheel_center,
                                 config->handwheel_left_10,
                                 config->handwheel_right_10,
                                 &config->handwheel_left_limit,
                                 &config->handwheel_right_limit);

  config->version = VEHICLE_CONFIG_VERSION;
}

static bool vehicle_config_validate_image(const vehicle_config_flash_image_t *image)
{
  uint32_t expected_checksum;

  if (image == NULL)
  {
    return false;
  }

  if ((image->magic != VEHICLE_CONFIG_STORAGE_MAGIC) ||
      (image->storage_version != VEHICLE_CONFIG_STORAGE_VERSION) ||
      (image->config.version != VEHICLE_CONFIG_VERSION))
  {
    return false;
  }

  expected_checksum =
      vehicle_config_checksum_bytes(image, offsetof(vehicle_config_flash_image_t, checksum));

  return (expected_checksum == image->checksum);
}

static bool vehicle_config_validate_image_v6(const vehicle_config_flash_image_v6_t *image)
{
  uint32_t expected_checksum;

  if (image == NULL)
  {
    return false;
  }

  if ((image->magic != VEHICLE_CONFIG_STORAGE_MAGIC) ||
      (image->storage_version != VEHICLE_CONFIG_STORAGE_VERSION_V6) ||
      (image->config.version != VEHICLE_CONFIG_VERSION_V6))
  {
    return false;
  }

  expected_checksum =
      vehicle_config_checksum_bytes(image, offsetof(vehicle_config_flash_image_v6_t, checksum));

  return (expected_checksum == image->checksum);
}

static bool vehicle_config_validate_image_v5(const vehicle_config_flash_image_v5_t *image)
{
  uint32_t expected_checksum;

  if (image == NULL)
  {
    return false;
  }

  if ((image->magic != VEHICLE_CONFIG_STORAGE_MAGIC) ||
      (image->storage_version != VEHICLE_CONFIG_STORAGE_VERSION_V5) ||
      (image->config.version != VEHICLE_CONFIG_VERSION_V5))
  {
    return false;
  }

  expected_checksum =
      vehicle_config_checksum_bytes(image, offsetof(vehicle_config_flash_image_v5_t, checksum));

  return (expected_checksum == image->checksum);
}

static bool vehicle_config_validate_image_v4(const vehicle_config_flash_image_v4_t *image)
{
  uint32_t expected_checksum;

  if (image == NULL)
  {
    return false;
  }

  if ((image->magic != VEHICLE_CONFIG_STORAGE_MAGIC) ||
      (image->storage_version != VEHICLE_CONFIG_STORAGE_VERSION_V4) ||
      (image->config.version != VEHICLE_CONFIG_VERSION_V4))
  {
    return false;
  }

  expected_checksum =
      vehicle_config_checksum_bytes(image, offsetof(vehicle_config_flash_image_v4_t, checksum));

  return (expected_checksum == image->checksum);
}

static bool vehicle_config_validate_image_v3(const vehicle_config_flash_image_v3_t *image)
{
  uint32_t expected_checksum;

  if (image == NULL)
  {
    return false;
  }

  if ((image->magic != VEHICLE_CONFIG_STORAGE_MAGIC) ||
      (image->storage_version != VEHICLE_CONFIG_STORAGE_VERSION_V3) ||
      (image->config.version != VEHICLE_CONFIG_VERSION_V3))
  {
    return false;
  }

  expected_checksum =
      vehicle_config_checksum_bytes(image, offsetof(vehicle_config_flash_image_v3_t, checksum));

  return (expected_checksum == image->checksum);
}

static void vehicle_config_import_v6(vehicle_config_t *dest, const vehicle_config_v6_t *src)
{
  if ((dest == NULL) || (src == NULL))
  {
    return;
  }

  vehicle_config_set_defaults(dest);
  dest->front_track_mm = src->front_track_mm;
  dest->wheelbase_mm = src->wheelbase_mm;
  dest->rear_track_mm = src->rear_track_mm;
  dest->chassis_type = src->chassis_type;
  dest->drive_axle = src->drive_axle;
  dest->drive_controller_type = src->drive_controller_type;
  dest->steering_controller_type = src->steering_controller_type;
  dest->linear_steering_enabled = src->linear_steering_enabled;
  dest->pedal_config = src->pedal_config;
  dest->left_drive_inverted = src->left_drive_inverted;
  dest->right_drive_inverted = src->right_drive_inverted;
  dest->steer_can_node_id = src->steer_can_node_id;
  dest->handwheel_can_node_id = src->handwheel_can_node_id;
  dest->throttle_raw_min = src->throttle_raw_min;
  dest->throttle_raw_max = src->throttle_raw_max;
  dest->brake_raw_min = src->brake_raw_min;
  dest->brake_raw_max = src->brake_raw_max;
  dest->steering_center = src->steering_center;
  dest->steering_left_10 = src->steering_left_10;
  dest->steering_right_10 = src->steering_right_10;
  dest->steering_left_limit = src->steering_left_limit;
  dest->steering_right_limit = src->steering_right_limit;
  dest->handwheel_center = src->handwheel_center;
  dest->handwheel_left_10 = src->handwheel_left_10;
  dest->handwheel_right_10 = src->handwheel_right_10;
  dest->handwheel_left_limit = src->handwheel_left_limit;
  dest->handwheel_right_limit = src->handwheel_right_limit;
  dest->drive_max_rpm = src->drive_max_rpm;
  dest->version = VEHICLE_CONFIG_VERSION;
}

static void vehicle_config_import_v5(vehicle_config_t *dest, const vehicle_config_v6_t *src)
{
  vehicle_config_import_v6(dest, src);
}

static void vehicle_config_import_v4(vehicle_config_t *dest, const vehicle_config_v4_t *src)
{
  if ((dest == NULL) || (src == NULL))
  {
    return;
  }

  vehicle_config_set_defaults(dest);
  dest->front_track_mm = src->front_track_mm;
  dest->wheelbase_mm = src->wheelbase_mm;
  dest->rear_track_mm = src->rear_track_mm;
  dest->chassis_type = src->chassis_type;
  dest->drive_axle = src->drive_axle;
  dest->drive_controller_type = src->drive_controller_type;
  dest->steering_controller_type = src->steering_controller_type;
  dest->linear_steering_enabled = src->linear_steering_enabled;
  dest->pedal_config = src->pedal_config;
  dest->left_drive_inverted = src->left_drive_inverted;
  dest->right_drive_inverted = src->right_drive_inverted;
  dest->steer_can_node_id = src->steer_can_node_id;
  dest->handwheel_can_node_id = src->handwheel_can_node_id;
  dest->throttle_raw_min = src->throttle_raw_min;
  dest->throttle_raw_max = src->throttle_raw_max;
  dest->brake_raw_min = src->brake_raw_min;
  dest->brake_raw_max = src->brake_raw_max;
  dest->steering_center = src->steering_center;
  dest->steering_left_10 = src->steering_left_10;
  dest->steering_right_10 = src->steering_right_10;
  dest->steering_left_limit = src->steering_left_limit;
  dest->steering_right_limit = src->steering_right_limit;
  dest->handwheel_center = src->handwheel_center;
  dest->handwheel_left_10 = src->handwheel_left_10;
  dest->handwheel_right_10 = src->handwheel_right_10;
  dest->handwheel_left_limit = src->handwheel_left_limit;
  dest->handwheel_right_limit = src->handwheel_right_limit;
  dest->drive_max_rpm = src->drive_max_rpm;
  dest->version = VEHICLE_CONFIG_VERSION;
}

static void vehicle_config_import_v3(vehicle_config_t *dest, const vehicle_config_v3_t *src)
{
  if ((dest == NULL) || (src == NULL))
  {
    return;
  }

  vehicle_config_set_defaults(dest);
  dest->front_track_mm = src->front_track_mm;
  dest->wheelbase_mm = src->wheelbase_mm;
  dest->rear_track_mm = src->rear_track_mm;
  dest->chassis_type = src->chassis_type;
  dest->drive_axle = src->drive_axle;
  dest->drive_controller_type = src->drive_controller_type;
  dest->steering_controller_type = src->steering_controller_type;
  dest->linear_steering_enabled = src->linear_steering_enabled;
  dest->pedal_config = src->pedal_config;
  dest->left_drive_inverted = src->left_drive_inverted;
  dest->right_drive_inverted = src->right_drive_inverted;
  dest->steer_can_node_id = src->steer_can_node_id;
  dest->handwheel_can_node_id = src->handwheel_can_node_id;
  dest->throttle_raw_min = src->throttle_raw_min;
  dest->throttle_raw_max = src->throttle_raw_max;
  dest->brake_raw_min = src->brake_raw_min;
  dest->brake_raw_max = src->brake_raw_max;
  dest->steering_center = src->steering_center;
  dest->steering_left_10 = src->steering_left_10;
  dest->steering_right_10 = src->steering_right_10;
  dest->steering_left_limit = src->steering_left_limit;
  dest->steering_right_limit = src->steering_right_limit;
  dest->handwheel_center = src->handwheel_center;
  dest->handwheel_left_10 = src->handwheel_left_10;
  dest->handwheel_right_10 = src->handwheel_right_10;
  dest->handwheel_left_limit = src->handwheel_left_limit;
  dest->handwheel_right_limit = src->handwheel_right_limit;
  dest->drive_max_rpm = VEHICLE_CONFIG_DEFAULT_DRIVE_MAX_RPM;
  dest->version = VEHICLE_CONFIG_VERSION;
}

void vehicle_config_init(void)
{
  if (!vehicle_config_load())
  {
    vehicle_config_set_defaults(&g_vehicle_config);
  }
}

const vehicle_config_t *vehicle_config_get(void)
{
  return &g_vehicle_config;
}

void vehicle_config_set(const vehicle_config_t *config)
{
  g_vehicle_config = *config;
  vehicle_config_sanitize(&g_vehicle_config);
}

bool vehicle_config_save(void)
{
  vehicle_config_flash_image_t image;
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t sector_error = 0U;
  uint32_t address = VEHICLE_CONFIG_FLASH_ADDRESS;
  const uint32_t *words = (const uint32_t *)(const void *)&image;
  size_t word_index;
  bool success = false;

  memset(&image, 0, sizeof(image));
  image.magic = VEHICLE_CONFIG_STORAGE_MAGIC;
  image.storage_version = VEHICLE_CONFIG_STORAGE_VERSION;
  image.config = g_vehicle_config;
  vehicle_config_sanitize(&image.config);
  image.checksum =
      vehicle_config_checksum_bytes(&image, offsetof(vehicle_config_flash_image_t, checksum));

  if (HAL_FLASH_Unlock() != HAL_OK)
  {
    return false;
  }

  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP |
                         FLASH_FLAG_OPERR |
                         FLASH_FLAG_WRPERR |
                         FLASH_FLAG_PGAERR |
                         FLASH_FLAG_PGPERR |
                         FLASH_FLAG_PGSERR);

  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.Sector = VEHICLE_CONFIG_FLASH_SECTOR;
  erase.NbSectors = 1U;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;

  if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
  {
    goto exit;
  }

  for (word_index = 0U; word_index < (sizeof(image) / sizeof(uint32_t)); ++word_index)
  {
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, words[word_index]) != HAL_OK)
    {
      goto exit;
    }

    address += sizeof(uint32_t);
  }

  success = true;

exit:
  (void)HAL_FLASH_Lock();

  if (!success)
  {
    return false;
  }

  g_vehicle_config = image.config;
  return vehicle_config_load();
}

bool vehicle_config_load(void)
{
  const vehicle_config_flash_image_t *image =
      (const vehicle_config_flash_image_t *)(uintptr_t)VEHICLE_CONFIG_FLASH_ADDRESS;
  const vehicle_config_flash_image_v6_t *image_v6 =
      (const vehicle_config_flash_image_v6_t *)(uintptr_t)VEHICLE_CONFIG_FLASH_ADDRESS;
  const vehicle_config_flash_image_v5_t *image_v5 =
      (const vehicle_config_flash_image_v5_t *)(uintptr_t)VEHICLE_CONFIG_FLASH_ADDRESS;
  const vehicle_config_flash_image_v4_t *image_v4 =
      (const vehicle_config_flash_image_v4_t *)(uintptr_t)VEHICLE_CONFIG_FLASH_ADDRESS;
  const vehicle_config_flash_image_v3_t *image_v3 =
      (const vehicle_config_flash_image_v3_t *)(uintptr_t)VEHICLE_CONFIG_FLASH_ADDRESS;

  if (!vehicle_config_validate_image(image))
  {
    if (vehicle_config_validate_image_v6(image_v6))
    {
      vehicle_config_import_v6(&g_vehicle_config, &image_v6->config);
      vehicle_config_sanitize(&g_vehicle_config);
      return true;
    }

    if (vehicle_config_validate_image_v5(image_v5))
    {
      vehicle_config_import_v5(&g_vehicle_config, &image_v5->config);
      vehicle_config_sanitize(&g_vehicle_config);
      return true;
    }

    if (vehicle_config_validate_image_v4(image_v4))
    {
      vehicle_config_import_v4(&g_vehicle_config, &image_v4->config);
      vehicle_config_sanitize(&g_vehicle_config);
      return true;
    }

    if (!vehicle_config_validate_image_v3(image_v3))
    {
      return false;
    }

    vehicle_config_import_v3(&g_vehicle_config, &image_v3->config);
    vehicle_config_sanitize(&g_vehicle_config);
    return true;
  }

  g_vehicle_config = image->config;
  vehicle_config_sanitize(&g_vehicle_config);
  return true;
}

drive_mode_t vehicle_config_runtime_drive_mode(const vehicle_config_t *config)
{
  if (config == NULL)
  {
    return DRIVE_MODE_RWD;
  }

  if ((chassis_type_t)config->chassis_type == CHASSIS_TYPE_DIFF)
  {
    return DRIVE_MODE_DIFF;
  }

  return ((drive_axle_t)config->drive_axle == DRIVE_AXLE_FWD) ? DRIVE_MODE_FWD : DRIVE_MODE_RWD;
}

const char *vehicle_config_drive_mode_name(drive_mode_t mode)
{
  switch (mode)
  {
    case DRIVE_MODE_FWD:
      return "FWD";
    case DRIVE_MODE_RWD:
      return "RWD";
    case DRIVE_MODE_AWD:
      return "AWD";
    case DRIVE_MODE_DIFF:
      return "DIFF";
    default:
      return "UNKNOWN";
  }
}

bool vehicle_config_parse_drive_mode(const char *text, drive_mode_t *mode)
{
  if ((text == NULL) || (mode == NULL))
  {
    return false;
  }

  if (ascii_stricmp(text, "FWD") == 0)
  {
    *mode = DRIVE_MODE_FWD;
    return true;
  }

  if (ascii_stricmp(text, "RWD") == 0)
  {
    *mode = DRIVE_MODE_RWD;
    return true;
  }

  if ((ascii_stricmp(text, "DIFF") == 0) || (ascii_stricmp(text, "DIFFERENTIAL") == 0))
  {
    *mode = DRIVE_MODE_DIFF;
    return true;
  }

  return false;
}

const char *vehicle_config_chassis_type_name(chassis_type_t type)
{
  switch (type)
  {
    case CHASSIS_TYPE_ACKERMANN:
      return "ACKERMANN";
    case CHASSIS_TYPE_DIFF:
      return "DIFF";
    default:
      return "UNKNOWN";
  }
}

bool vehicle_config_parse_chassis_type(const char *text, chassis_type_t *type)
{
  if ((text == NULL) || (type == NULL))
  {
    return false;
  }

  if ((ascii_stricmp(text, "ACKERMANN") == 0) ||
      (ascii_stricmp(text, "LINEAR_ACKERMANN") == 0))
  {
    *type = CHASSIS_TYPE_ACKERMANN;
    return true;
  }

  if ((ascii_stricmp(text, "DIFF") == 0) ||
      (ascii_stricmp(text, "DIFFERENTIAL") == 0) ||
      (ascii_stricmp(text, "LINEAR_DIFF") == 0))
  {
    *type = CHASSIS_TYPE_DIFF;
    return true;
  }

  return false;
}

const char *vehicle_config_drive_axle_name(drive_axle_t axle)
{
  switch (axle)
  {
    case DRIVE_AXLE_FWD:
      return "FWD";
    case DRIVE_AXLE_RWD:
      return "RWD";
    default:
      return "UNKNOWN";
  }
}

bool vehicle_config_parse_drive_axle(const char *text, drive_axle_t *axle)
{
  if ((text == NULL) || (axle == NULL))
  {
    return false;
  }

  if ((ascii_stricmp(text, "FWD") == 0) ||
      (ascii_stricmp(text, "FRONT") == 0))
  {
    *axle = DRIVE_AXLE_FWD;
    return true;
  }

  if ((ascii_stricmp(text, "RWD") == 0) ||
      (ascii_stricmp(text, "REAR") == 0))
  {
    *axle = DRIVE_AXLE_RWD;
    return true;
  }

  return false;
}

const char *vehicle_config_pedal_config_name(pedal_config_t config)
{
  switch (config)
  {
    case PEDAL_CONFIG_BRAKE_THROTTLE:
      return "BRAKE_THROTTLE";
    case PEDAL_CONFIG_ESTOP_THROTTLE:
      return "ESTOP_THROTTLE";
    default:
      return "UNKNOWN";
  }
}

bool vehicle_config_parse_pedal_config(const char *text, pedal_config_t *config)
{
  if ((text == NULL) || (config == NULL))
  {
    return false;
  }

  if ((ascii_stricmp(text, "BRAKE_THROTTLE") == 0) ||
      (ascii_stricmp(text, "BRAKE") == 0))
  {
    *config = PEDAL_CONFIG_BRAKE_THROTTLE;
    return true;
  }

  if ((ascii_stricmp(text, "ESTOP_THROTTLE") == 0) ||
      (ascii_stricmp(text, "ESTOP") == 0))
  {
    *config = PEDAL_CONFIG_ESTOP_THROTTLE;
    return true;
  }

  return false;
}

const char *vehicle_config_drive_controller_name(uint8_t type)
{
  switch ((drive_controller_type_t)type)
  {
    case DRIVE_CONTROLLER_MSSD_60EHB_2D:
      return "MSSD-60EHB_2D";
    default:
      return "UNKNOWN";
  }
}

const char *vehicle_config_steering_controller_name(uint8_t type)
{
  switch ((steering_controller_type_t)type)
  {
    case STEERING_CONTROLLER_MSSC:
      return "MSSC";
    default:
      return "UNKNOWN";
  }
}
