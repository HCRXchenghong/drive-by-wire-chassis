#include "vehicle_config.h"
#include "main.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#define VEHICLE_CONFIG_VERSION 0x00030000UL
#define VEHICLE_CONFIG_STORAGE_MAGIC 0x56434647UL
#define VEHICLE_CONFIG_STORAGE_VERSION 0x00030000UL
#define VEHICLE_CONFIG_FLASH_SECTOR FLASH_SECTOR_11
#define VEHICLE_CONFIG_FLASH_ADDRESS 0x080E0000UL

typedef struct
{
  uint32_t magic;
  uint32_t storage_version;
  vehicle_config_t config;
  uint32_t checksum;
} vehicle_config_flash_image_t;

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
  config->steer_can_node_id = 1U;
  config->handwheel_can_node_id = 2U;
  config->throttle_raw_min = 240U;
  config->throttle_raw_max = 3840U;
  config->brake_raw_min = 240U;
  config->brake_raw_max = 3840U;
  config->steering_center = 0;
  config->steering_left_10 = -100;
  config->steering_right_10 = 100;
  config->steering_left_limit = -600;
  config->steering_right_limit = 600;
  config->handwheel_center = 0;
  config->handwheel_left_10 = -100;
  config->handwheel_right_10 = 100;
  config->handwheel_left_limit = -600;
  config->handwheel_right_limit = 600;
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

  if ((*node_id == 0U) || (*node_id > 0x07FFU))
  {
    *node_id = fallback;
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

  vehicle_config_fix_can_node_id(&config->steer_can_node_id, 1U);
  vehicle_config_fix_can_node_id(&config->handwheel_can_node_id, 2U);
  vehicle_config_fix_u16_range(&config->throttle_raw_min, &config->throttle_raw_max);
  vehicle_config_fix_u16_range(&config->brake_raw_min, &config->brake_raw_max);

  memset(config->reserved0, 0, sizeof(config->reserved0));
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

  if (!vehicle_config_validate_image(image))
  {
    return false;
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

  if ((ascii_stricmp(text, "AWD") == 0) || (ascii_stricmp(text, "4WD") == 0))
  {
    *mode = DRIVE_MODE_AWD;
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
