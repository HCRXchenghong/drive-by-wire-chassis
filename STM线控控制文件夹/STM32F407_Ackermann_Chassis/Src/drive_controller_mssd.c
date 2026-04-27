#include "drive_controller_mssd.h"

#define MSSD_REG_RIGHT_SPEED_HIGH 0x0009U
#define MSSD_REG_RIGHT_SPEED_LOW 0x000AU
/* 2026-04-27 USB-CAN live capture confirmed left runtime speed at 0x14/0x15. */
#define MSSD_REG_LEFT_SPEED_HIGH 0x0014U
#define MSSD_REG_LEFT_SPEED_LOW 0x0015U
#define MSSD_FEEDBACK_QUERY_PERIOD_MS 5U
#define MSSD_FEEDBACK_TIMEOUT_MS 40U

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

static uint16_t mssd_feedback_reg_by_index(uint8_t reg_index)
{
  switch (reg_index)
  {
    case 0U:
      return MSSD_REG_RIGHT_SPEED_HIGH;
    case 1U:
      return MSSD_REG_RIGHT_SPEED_LOW;
    case 2U:
      return MSSD_REG_LEFT_SPEED_HIGH;
    default:
      return MSSD_REG_LEFT_SPEED_LOW;
  }
}

static uint32_t mssd_feedback_cookie(const mssd_drive_controller_t *controller, uint16_t reg_addr)
{
  if (controller == NULL)
  {
    return 0U;
  }

  return (((uint32_t)controller->node_id & 0x7FFU) << 16) | (uint32_t)reg_addr;
}

static void mssd_invalidate_feedback_cache(mssd_drive_controller_t *controller)
{
  if (controller == NULL)
  {
    return;
  }

  controller->right_speed_high_word = 0U;
  controller->right_speed_low_word = 0U;
  controller->left_speed_high_word = 0U;
  controller->left_speed_low_word = 0U;
  controller->right_speed_words_valid_mask = 0U;
  controller->left_speed_words_valid_mask = 0U;
  controller->right_actual_rpm = 0;
  controller->left_actual_rpm = 0;
  controller->right_actual_tick_ms = 0U;
  controller->left_actual_tick_ms = 0U;
}

static void mssd_consume_feedback_result(mssd_drive_controller_t *controller,
                                         const can_transport_read_result_t *result)
{
  uint16_t word_value;

  if ((controller == NULL) || (result == NULL))
  {
    return;
  }

  if (result->type == CAN_TRANSPORT_READ_RESULT_TIMEOUT)
  {
    controller->feedback_request_pending = false;
    controller->feedback_fault_active = true;
    controller->feedback_timeout_count++;
    controller->last_feedback_timeout_tick_ms = result->tick_ms;
    mssd_invalidate_feedback_cache(controller);
    return;
  }

  if (result->dlc < 6U)
  {
    return;
  }

  word_value = (uint16_t)(((uint16_t)result->data[5] << 8) | result->data[4]);
  if (result->index == (uint16_t)(0x4000U + MSSD_REG_RIGHT_SPEED_HIGH))
  {
    controller->right_speed_high_word = word_value;
    controller->right_speed_words_valid_mask |= 0x01U;
  }
  else if (result->index == (uint16_t)(0x4000U + MSSD_REG_RIGHT_SPEED_LOW))
  {
    controller->right_speed_low_word = word_value;
    controller->right_speed_words_valid_mask |= 0x02U;
  }
  else if (result->index == (uint16_t)(0x4000U + MSSD_REG_LEFT_SPEED_HIGH))
  {
    controller->left_speed_high_word = word_value;
    controller->left_speed_words_valid_mask |= 0x01U;
  }
  else if (result->index == (uint16_t)(0x4000U + MSSD_REG_LEFT_SPEED_LOW))
  {
    controller->left_speed_low_word = word_value;
    controller->left_speed_words_valid_mask |= 0x02U;
  }
  else
  {
    return;
  }

  controller->feedback_request_pending = false;
  controller->last_feedback_ok_tick_ms = result->tick_ms;

  if ((controller->right_speed_words_valid_mask & 0x03U) == 0x03U)
  {
    controller->right_actual_rpm =
        (int32_t)(((uint32_t)controller->right_speed_high_word << 16) | (uint32_t)controller->right_speed_low_word);
    controller->right_actual_tick_ms = result->tick_ms;
  }

  if ((controller->left_speed_words_valid_mask & 0x03U) == 0x03U)
  {
    controller->left_actual_rpm =
        (int32_t)(((uint32_t)controller->left_speed_high_word << 16) | (uint32_t)controller->left_speed_low_word);
    controller->left_actual_tick_ms = result->tick_ms;
  }

  if (((controller->right_speed_words_valid_mask & 0x03U) == 0x03U) &&
      ((controller->left_speed_words_valid_mask & 0x03U) == 0x03U))
  {
    controller->feedback_fault_active = false;
  }
}

static void mssd_drain_feedback_results(mssd_drive_controller_t *controller)
{
  static const uint16_t regs[] = {
      MSSD_REG_RIGHT_SPEED_HIGH,
      MSSD_REG_RIGHT_SPEED_LOW,
      MSSD_REG_LEFT_SPEED_HIGH,
      MSSD_REG_LEFT_SPEED_LOW};
  bool consumed = false;
  size_t index;

  if ((controller == NULL) || (controller->hcan == NULL))
  {
    return;
  }

  do
  {
    consumed = false;

    for (index = 0U; index < (sizeof(regs) / sizeof(regs[0])); ++index)
    {
      can_transport_read_result_t result;
      uint32_t cookie = mssd_feedback_cookie(controller, regs[index]);

      if (!can_transport_take_read_result(controller->hcan, cookie, &result))
      {
        continue;
      }

      mssd_consume_feedback_result(controller, &result);
      consumed = true;
    }
  } while (consumed);
}

static void mssd_sync_recovery_state(mssd_drive_controller_t *controller)
{
  can_transport_bus_state_t bus_state;

  if ((controller == NULL) || (controller->hcan == NULL))
  {
    return;
  }

  if (!can_transport_get_bus_state(controller->hcan, &bus_state))
  {
    return;
  }

  if (bus_state.recovery_count != controller->last_seen_recovery_count)
  {
    controller->last_seen_recovery_count = bus_state.recovery_count;
    controller->feedback_request_pending = false;
    controller->feedback_fault_active = true;
    controller->next_feedback_reg_index = 0U;
    mssd_invalidate_feedback_cache(controller);
  }

  if ((bus_state.recovery_active || bus_state.fault_active) && !bus_state.read_pending)
  {
    controller->feedback_request_pending = false;
  }
}

static bool mssd_queue_read16(const mssd_drive_controller_t *controller, uint16_t reg_addr)
{
  uint8_t frame[8];
  uint32_t sequence = 0U;

  if ((controller == NULL) || (controller->hcan == NULL))
  {
    return false;
  }

  mssd_build_read16(frame, reg_addr);
  return can_transport_queue_read_std(controller->hcan,
                                      controller->node_id,
                                      frame,
                                      sizeof(frame),
                                      MSSD_FEEDBACK_TIMEOUT_MS,
                                      mssd_feedback_cookie(controller, reg_addr),
                                      0x42U,
                                      (uint16_t)(0x4000U + reg_addr),
                                      CAN_TRANSPORT_PRIORITY_LOW,
                                      &sequence);
}

void mssd_drive_controller_bind(mssd_drive_controller_t *controller, CAN_HandleTypeDef *hcan, uint16_t node_id)
{
  if (controller == NULL)
  {
    return;
  }

  controller->hcan = hcan;
  controller->node_id = node_id;
  controller->last_feedback_query_tick_ms = 0U;
  controller->last_feedback_ok_tick_ms = 0U;
  controller->feedback_timeout_count = 0U;
  controller->last_feedback_timeout_tick_ms = 0U;
  controller->last_seen_recovery_count = 0U;
  controller->pending_feedback_sequence = 0U;
  controller->right_speed_high_word = 0U;
  controller->right_speed_low_word = 0U;
  controller->left_speed_high_word = 0U;
  controller->left_speed_low_word = 0U;
  controller->right_speed_words_valid_mask = 0U;
  controller->left_speed_words_valid_mask = 0U;
  controller->next_feedback_reg_index = 0U;
  controller->pending_feedback_reg_index = 0U;
  controller->feedback_request_pending = false;
  controller->feedback_fault_active = false;
  controller->right_actual_rpm = 0;
  controller->left_actual_rpm = 0;
  controller->right_actual_tick_ms = 0U;
  controller->left_actual_tick_ms = 0U;
}

bool mssd_drive_controller_set_wheel_rpm_priority(const mssd_drive_controller_t *controller,
                                                  int32_t right_rpm,
                                                  int32_t left_rpm,
                                                  can_transport_priority_t priority)
{
  uint8_t right_frame[8];
  uint8_t left_frame[8];

  if ((controller == NULL) || (controller->hcan == NULL))
  {
    return false;
  }

  mssd_build_write32(right_frame, 0x0039U, right_rpm);
  mssd_build_write32(left_frame, 0x003DU, left_rpm);

  if (!can_transport_queue_write_std(controller->hcan, controller->node_id, right_frame, sizeof(right_frame), priority))
  {
    return false;
  }

  return can_transport_queue_write_std(controller->hcan, controller->node_id, left_frame, sizeof(left_frame), priority);
}

bool mssd_drive_controller_set_wheel_rpm(const mssd_drive_controller_t *controller, int32_t right_rpm, int32_t left_rpm)
{
  return mssd_drive_controller_set_wheel_rpm_priority(
      controller,
      right_rpm,
      left_rpm,
      CAN_TRANSPORT_PRIORITY_NORMAL);
}

bool mssd_drive_controller_set_stop_mode_priority(const mssd_drive_controller_t *controller,
                                                  mssd_stop_mode_t right_mode,
                                                  mssd_stop_mode_t left_mode,
                                                  can_transport_priority_t priority)
{
  uint8_t right_frame[8];
  uint8_t left_frame[8];

  if ((controller == NULL) || (controller->hcan == NULL))
  {
    return false;
  }

  mssd_build_write16(right_frame, 0x0038U, (uint16_t)right_mode);
  mssd_build_write16(left_frame, 0x003CU, (uint16_t)left_mode);

  if (!can_transport_queue_write_std(controller->hcan, controller->node_id, right_frame, sizeof(right_frame), priority))
  {
    return false;
  }

  return can_transport_queue_write_std(controller->hcan, controller->node_id, left_frame, sizeof(left_frame), priority);
}

bool mssd_drive_controller_set_stop_mode(const mssd_drive_controller_t *controller,
                                         mssd_stop_mode_t right_mode,
                                         mssd_stop_mode_t left_mode)
{
  return mssd_drive_controller_set_stop_mode_priority(
      controller,
      right_mode,
      left_mode,
      CAN_TRANSPORT_PRIORITY_HIGH);
}

bool mssd_drive_controller_apply_stop_mode_priority(const mssd_drive_controller_t *controller,
                                                    mssd_stop_mode_t mode,
                                                    can_transport_priority_t priority)
{
  return mssd_drive_controller_set_stop_mode_priority(controller, mode, mode, priority);
}

bool mssd_drive_controller_apply_stop_mode(const mssd_drive_controller_t *controller, mssd_stop_mode_t mode)
{
  return mssd_drive_controller_apply_stop_mode_priority(controller, mode, CAN_TRANSPORT_PRIORITY_HIGH);
}

bool mssd_drive_controller_stop_priority(const mssd_drive_controller_t *controller,
                                         can_transport_priority_t priority)
{
  if (!mssd_drive_controller_set_wheel_rpm_priority(controller, 0, 0, priority))
  {
    return false;
  }

  return mssd_drive_controller_apply_stop_mode_priority(controller, MSSD_STOP_MODE_NORMAL, priority);
}

bool mssd_drive_controller_stop(const mssd_drive_controller_t *controller)
{
  return mssd_drive_controller_stop_priority(controller, CAN_TRANSPORT_PRIORITY_HIGH);
}

void mssd_drive_controller_feedback_process(mssd_drive_controller_t *controller)
{
  uint32_t now;
  uint16_t next_reg;

  if ((controller == NULL) || (controller->hcan == NULL))
  {
    return;
  }

  mssd_sync_recovery_state(controller);
  mssd_drain_feedback_results(controller);

  if (controller->feedback_request_pending)
  {
    return;
  }

  now = HAL_GetTick();
  if ((controller->last_feedback_query_tick_ms != 0U) &&
      ((now - controller->last_feedback_query_tick_ms) < MSSD_FEEDBACK_QUERY_PERIOD_MS))
  {
    return;
  }

  next_reg = mssd_feedback_reg_by_index(controller->next_feedback_reg_index);
  if (!mssd_queue_read16(controller, next_reg))
  {
    return;
  }

  controller->feedback_request_pending = true;
  controller->pending_feedback_reg_index = controller->next_feedback_reg_index;
  controller->last_feedback_query_tick_ms = now;
  controller->next_feedback_reg_index = (uint8_t)((controller->next_feedback_reg_index + 1U) & 0x03U);
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

bool mssd_drive_controller_feedback_fault_active(const mssd_drive_controller_t *controller)
{
  return (controller != NULL) && controller->feedback_fault_active;
}

uint32_t mssd_drive_controller_feedback_timeout_count(const mssd_drive_controller_t *controller)
{
  return (controller != NULL) ? controller->feedback_timeout_count : 0U;
}

uint32_t mssd_drive_controller_last_feedback_ok_age_ms(const mssd_drive_controller_t *controller)
{
  if ((controller == NULL) || (controller->last_feedback_ok_tick_ms == 0U))
  {
    return 0xFFFFFFFFUL;
  }

  return HAL_GetTick() - controller->last_feedback_ok_tick_ms;
}
