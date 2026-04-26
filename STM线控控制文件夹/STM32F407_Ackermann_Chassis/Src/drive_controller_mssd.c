#include "drive_controller_mssd.h"

#include "can_transport.h"

static void mssd_build_write32(uint8_t *frame, uint16_t reg_addr, int32_t value)
{
  uint16_t index = (uint16_t)(0x4000U + reg_addr);
  uint32_t raw = (uint32_t)value;
  uint16_t high_word = (uint16_t)((raw >> 16) & 0xFFFFU);
  uint16_t low_word = (uint16_t)(raw & 0xFFFFU);

  frame[0] = 0x24U;
  frame[1] = (uint8_t)(index & 0xFFU);
  frame[2] = (uint8_t)((index >> 8) & 0xFFU);
  frame[3] = 0x00U;
  frame[4] = (uint8_t)(high_word & 0xFFU);
  frame[5] = (uint8_t)((high_word >> 8) & 0xFFU);
  frame[6] = (uint8_t)(low_word & 0xFFU);
  frame[7] = (uint8_t)((low_word >> 8) & 0xFFU);
}

static void mssd_build_write16(uint8_t *frame, uint16_t reg_addr, uint16_t value)
{
  uint16_t index = (uint16_t)(0x4000U + reg_addr);

  frame[0] = 0x22U;
  frame[1] = (uint8_t)(index & 0xFFU);
  frame[2] = (uint8_t)((index >> 8) & 0xFFU);
  frame[3] = 0x00U;
  frame[4] = (uint8_t)(value & 0xFFU);
  frame[5] = (uint8_t)((value >> 8) & 0xFFU);
  frame[6] = 0x00U;
  frame[7] = 0x00U;
}

void mssd_drive_controller_bind(mssd_drive_controller_t *controller, CAN_HandleTypeDef *hcan, uint16_t node_id)
{
  controller->hcan = hcan;
  controller->node_id = node_id;
}

bool mssd_drive_controller_set_wheel_rpm(const mssd_drive_controller_t *controller, int32_t right_rpm, int32_t left_rpm)
{
  uint8_t right_frame[8];
  uint8_t left_frame[8];

  if ((controller == NULL) || (controller->hcan == NULL))
  {
    return false;
  }

  mssd_build_write32(right_frame, 0x0039U, right_rpm);
  mssd_build_write32(left_frame, 0x003DU, left_rpm);

  if (!can_transport_send_std(controller->hcan, controller->node_id, right_frame, sizeof(right_frame)))
  {
    return false;
  }

  return can_transport_send_std(controller->hcan, controller->node_id, left_frame, sizeof(left_frame));
}

bool mssd_drive_controller_set_stop_mode(const mssd_drive_controller_t *controller,
                                         mssd_stop_mode_t right_mode,
                                         mssd_stop_mode_t left_mode)
{
  uint8_t right_frame[8];
  uint8_t left_frame[8];

  if ((controller == NULL) || (controller->hcan == NULL))
  {
    return false;
  }

  mssd_build_write16(right_frame, 0x0038U, (uint16_t)right_mode);
  mssd_build_write16(left_frame, 0x003CU, (uint16_t)left_mode);

  if (!can_transport_send_std(controller->hcan, controller->node_id, right_frame, sizeof(right_frame)))
  {
    return false;
  }

  return can_transport_send_std(controller->hcan, controller->node_id, left_frame, sizeof(left_frame));
}

bool mssd_drive_controller_apply_stop_mode(const mssd_drive_controller_t *controller, mssd_stop_mode_t mode)
{
  return mssd_drive_controller_set_stop_mode(controller, mode, mode);
}

bool mssd_drive_controller_stop(const mssd_drive_controller_t *controller)
{
  if (!mssd_drive_controller_set_wheel_rpm(controller, 0, 0))
  {
    return false;
  }

  return mssd_drive_controller_apply_stop_mode(controller, MSSD_STOP_MODE_NORMAL);
}
