#include "drive_controller_mssd.h"

#include "can_transport.h"

#define MSSD_REG_RIGHT_SPEED_HIGH 0x0009U
#define MSSD_REG_RIGHT_SPEED_LOW 0x000AU
/* 2026-04-27 USB-CAN live capture confirmed left runtime speed at 0x14/0x15. */
#define MSSD_REG_LEFT_SPEED_HIGH 0x0014U
#define MSSD_REG_LEFT_SPEED_LOW 0x0015U
#define MSSD_FEEDBACK_QUERY_PERIOD_MS 20U

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

static void mssd_build_read16(uint8_t *frame, uint16_t reg_addr)
{
  uint16_t index = (uint16_t)(0x4000U + reg_addr);

  frame[0] = 0x40U;
  frame[1] = (uint8_t)(index & 0xFFU);
  frame[2] = (uint8_t)((index >> 8) & 0xFFU);
  frame[3] = 0x00U;
  frame[4] = 0x00U;
  frame[5] = 0x00U;
  frame[6] = 0x00U;
  frame[7] = 0x00U;
}

static bool mssd_send_read16(const mssd_drive_controller_t *controller, uint16_t reg_addr)
{
  uint8_t frame[8];

  if ((controller == NULL) || (controller->hcan == NULL))
  {
    return false;
  }

  mssd_build_read16(frame, reg_addr);
  return can_transport_send_std(controller->hcan, controller->node_id, frame, sizeof(frame));
}

static void mssd_consume_feedback_frame(mssd_drive_controller_t *controller,
                                        const can_transport_rx_frame_t *frame)
{
  uint16_t index;
  uint16_t word_value;

  if ((controller == NULL) || (frame == NULL))
  {
    return;
  }

  if ((frame->std_id != (uint32_t)(controller->node_id & 0x7FFU)) || (frame->dlc < 6U))
  {
    return;
  }

  if (frame->data[0] != 0x42U)
  {
    return;
  }

  if (frame->data[3] != 0x00U)
  {
    return;
  }

  index = (uint16_t)(((uint16_t)frame->data[2] << 8) | frame->data[1]);
  word_value = (uint16_t)(((uint16_t)frame->data[5] << 8) | frame->data[4]);

  if (index == (uint16_t)(0x4000U + MSSD_REG_RIGHT_SPEED_HIGH))
  {
    controller->right_speed_high_word = word_value;
    controller->right_speed_words_valid_mask |= 0x01U;
  }
  else if (index == (uint16_t)(0x4000U + MSSD_REG_RIGHT_SPEED_LOW))
  {
    controller->right_speed_low_word = word_value;
    controller->right_speed_words_valid_mask |= 0x02U;
  }
  else if (index == (uint16_t)(0x4000U + MSSD_REG_LEFT_SPEED_HIGH))
  {
    controller->left_speed_high_word = word_value;
    controller->left_speed_words_valid_mask |= 0x01U;
  }
  else if (index == (uint16_t)(0x4000U + MSSD_REG_LEFT_SPEED_LOW))
  {
    controller->left_speed_low_word = word_value;
    controller->left_speed_words_valid_mask |= 0x02U;
  }
  else
  {
    return;
  }

  if ((controller->right_speed_words_valid_mask & 0x03U) == 0x03U)
  {
    controller->right_actual_rpm =
        (int32_t)(((uint32_t)controller->right_speed_high_word << 16) | (uint32_t)controller->right_speed_low_word);
    controller->right_actual_tick_ms = frame->tick_ms;
  }

  if ((controller->left_speed_words_valid_mask & 0x03U) == 0x03U)
  {
    controller->left_actual_rpm =
        (int32_t)(((uint32_t)controller->left_speed_high_word << 16) | (uint32_t)controller->left_speed_low_word);
    controller->left_actual_tick_ms = frame->tick_ms;
  }
}

void mssd_drive_controller_bind(mssd_drive_controller_t *controller, CAN_HandleTypeDef *hcan, uint16_t node_id)
{
  if (controller == NULL)
  {
    return;
  }

  controller->hcan = hcan;
  controller->node_id = node_id;
  controller->last_feedback_rx_count = 0U;
  controller->last_feedback_query_tick_ms = 0U;
  controller->right_speed_high_word = 0U;
  controller->right_speed_low_word = 0U;
  controller->left_speed_high_word = 0U;
  controller->left_speed_low_word = 0U;
  controller->right_speed_words_valid_mask = 0U;
  controller->left_speed_words_valid_mask = 0U;
  controller->next_feedback_reg_index = 0U;
  controller->right_actual_rpm = 0;
  controller->left_actual_rpm = 0;
  controller->right_actual_tick_ms = 0U;
  controller->left_actual_tick_ms = 0U;
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

void mssd_drive_controller_feedback_process(mssd_drive_controller_t *controller)
{
  can_transport_bus_state_t bus_state;
  can_transport_rx_frame_t frame;
  uint32_t now;
  uint16_t next_reg;

  if ((controller == NULL) || (controller->hcan == NULL))
  {
    return;
  }

  if (can_transport_get_bus_state(controller->hcan, &bus_state))
  {
    while (controller->last_feedback_rx_count < bus_state.rx_count)
    {
      controller->last_feedback_rx_count++;

      if (can_transport_get_frame_by_rx_count(controller->hcan, controller->last_feedback_rx_count, &frame))
      {
        mssd_consume_feedback_frame(controller, &frame);
      }
    }
  }

  now = HAL_GetTick();
  if ((controller->last_feedback_query_tick_ms != 0U) &&
      ((now - controller->last_feedback_query_tick_ms) < MSSD_FEEDBACK_QUERY_PERIOD_MS))
  {
    return;
  }

  switch (controller->next_feedback_reg_index)
  {
    case 0U:
      next_reg = MSSD_REG_RIGHT_SPEED_HIGH;
      break;
    case 1U:
      next_reg = MSSD_REG_RIGHT_SPEED_LOW;
      break;
    case 2U:
      next_reg = MSSD_REG_LEFT_SPEED_HIGH;
      break;
    default:
      next_reg = MSSD_REG_LEFT_SPEED_LOW;
      break;
  }

  if (mssd_send_read16(controller, next_reg))
  {
    controller->last_feedback_query_tick_ms = now;
    controller->next_feedback_reg_index = (uint8_t)((controller->next_feedback_reg_index + 1U) & 0x03U);
  }
}

bool mssd_drive_controller_get_feedback(const mssd_drive_controller_t *controller, mssd_drive_feedback_t *feedback)
{
  if ((controller == NULL) || (feedback == NULL))
  {
    return false;
  }

  feedback->right_valid = (controller->right_actual_tick_ms != 0U);
  feedback->left_valid = (controller->left_actual_tick_ms != 0U);
  feedback->right_rpm = controller->right_actual_rpm;
  feedback->left_rpm = controller->left_actual_rpm;
  feedback->right_age_ms = feedback->right_valid ? (HAL_GetTick() - controller->right_actual_tick_ms) : 0U;
  feedback->left_age_ms = feedback->left_valid ? (HAL_GetTick() - controller->left_actual_tick_ms) : 0U;

  return feedback->right_valid || feedback->left_valid;
}
