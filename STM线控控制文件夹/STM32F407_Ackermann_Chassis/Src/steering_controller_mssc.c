#include "steering_controller_mssc.h"

#define MSSC_REG_POSITION_HIGH 0x0013U
#define MSSC_REG_POSITION_LOW 0x0014U
#define MSSC_FEEDBACK_QUERY_PERIOD_MS 5U
#define MSSC_FEEDBACK_TIMEOUT_MS 40U

static void mssc_build_write16(uint8_t *frame, uint16_t reg_addr, int16_t value)
{
  uint16_t index = (uint16_t)(0x4000U + reg_addr);
  uint16_t raw = (uint16_t)value;

  frame[0] = 0x22U;
  frame[1] = (uint8_t)(index & 0xFFU);
  frame[2] = (uint8_t)((index >> 8) & 0xFFU);
  frame[3] = 0x00U;
  frame[4] = (uint8_t)(raw & 0xFFU);
  frame[5] = (uint8_t)((raw >> 8) & 0xFFU);
  frame[6] = 0x00U;
  frame[7] = 0x00U;
}

static void mssc_build_read16(uint8_t *frame, uint16_t reg_addr)
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

static uint32_t mssc_feedback_cookie(const mssc_steering_controller_t *controller, uint16_t reg_addr)
{
  if (controller == NULL)
  {
    return 0U;
  }

  return (((uint32_t)controller->node_id & 0x7FFU) << 16) | (uint32_t)reg_addr;
}

static void mssc_invalidate_feedback_cache(mssc_steering_controller_t *controller)
{
  if (controller == NULL)
  {
    return;
  }

  controller->position_high_word = 0U;
  controller->position_low_word = 0U;
  controller->position_words_valid_mask = 0U;
  controller->last_position_tick_ms = 0U;
}

static bool mssc_queue_write16(const mssc_steering_controller_t *controller,
                               uint16_t reg_addr,
                               int16_t value,
                               can_transport_priority_t priority)
{
  uint8_t frame[8];

  if ((controller == NULL) || (controller->hcan == NULL))
  {
    return false;
  }

  mssc_build_write16(frame, reg_addr, value);
  return can_transport_queue_write_std(controller->hcan, controller->node_id, frame, sizeof(frame), priority);
}

static bool mssc_queue_read16(const mssc_steering_controller_t *controller, uint16_t reg_addr)
{
  uint8_t frame[8];
  uint32_t sequence = 0U;

  if ((controller == NULL) || (controller->hcan == NULL))
  {
    return false;
  }

  mssc_build_read16(frame, reg_addr);
  return can_transport_queue_read_std(controller->hcan,
                                      controller->node_id,
                                      frame,
                                      sizeof(frame),
                                      MSSC_FEEDBACK_TIMEOUT_MS,
                                      mssc_feedback_cookie(controller, reg_addr),
                                      0x42U,
                                      (uint16_t)(0x4000U + reg_addr),
                                      CAN_TRANSPORT_PRIORITY_LOW,
                                      &sequence);
}

static void mssc_consume_feedback_result(mssc_steering_controller_t *controller,
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
    mssc_invalidate_feedback_cache(controller);
    return;
  }

  if (result->dlc < 6U)
  {
    return;
  }

  word_value = (uint16_t)(((uint16_t)result->data[5] << 8) | result->data[4]);
  if (result->index == (uint16_t)(0x4000U + MSSC_REG_POSITION_HIGH))
  {
    controller->position_high_word = word_value;
    controller->position_words_valid_mask |= 0x01U;
  }
  else if (result->index == (uint16_t)(0x4000U + MSSC_REG_POSITION_LOW))
  {
    controller->position_low_word = word_value;
    controller->position_words_valid_mask |= 0x02U;
  }
  else
  {
    return;
  }

  controller->feedback_request_pending = false;
  controller->last_feedback_ok_tick_ms = result->tick_ms;

  if ((controller->position_words_valid_mask & 0x03U) == 0x03U)
  {
    controller->last_position_raw =
        (int32_t)(((uint32_t)controller->position_high_word << 16) | (uint32_t)controller->position_low_word);
    controller->last_position_tick_ms = result->tick_ms;
    controller->feedback_fault_active = false;
  }
}

static void mssc_drain_feedback_results(mssc_steering_controller_t *controller)
{
  static const uint16_t regs[] = {
      MSSC_REG_POSITION_HIGH,
      MSSC_REG_POSITION_LOW};
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
      uint32_t cookie = mssc_feedback_cookie(controller, regs[index]);

      if (!can_transport_take_read_result(controller->hcan, cookie, &result))
      {
        continue;
      }

      mssc_consume_feedback_result(controller, &result);
      consumed = true;
    }
  } while (consumed);
}

static void mssc_sync_recovery_state(mssc_steering_controller_t *controller)
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
    controller->can_control_prepared = false;
    controller->feedback_request_pending = false;
    controller->feedback_fault_active = true;
    controller->next_feedback_reg = 0U;
    mssc_invalidate_feedback_cache(controller);
  }

  if ((bus_state.recovery_active || bus_state.fault_active) && !bus_state.read_pending)
  {
    controller->feedback_request_pending = false;
  }
}

void mssc_steering_controller_bind(mssc_steering_controller_t *controller,
                                   CAN_HandleTypeDef *hcan,
                                   uint16_t node_id,
                                   uint16_t control_mode)
{
  if (controller == NULL)
  {
    return;
  }

  controller->hcan = hcan;
  controller->node_id = node_id;
  controller->control_mode = control_mode;
  controller->last_feedback_query_tick_ms = 0U;
  controller->last_feedback_ok_tick_ms = 0U;
  controller->feedback_timeout_count = 0U;
  controller->last_feedback_timeout_tick_ms = 0U;
  controller->last_seen_recovery_count = 0U;
  controller->can_control_prepared = false;
  controller->feedback_request_pending = false;
  controller->feedback_fault_active = false;
  controller->position_high_word = 0U;
  controller->position_low_word = 0U;
  controller->position_words_valid_mask = 0U;
  controller->next_feedback_reg = 0U;
  controller->last_position_raw = 0;
  controller->last_position_tick_ms = 0U;
}

bool mssc_steering_controller_prepare_for_can_control(const mssc_steering_controller_t *controller)
{
  mssc_steering_controller_t *mutable_controller = (mssc_steering_controller_t *)controller;

  if ((controller == NULL) || (controller->hcan == NULL))
  {
    return false;
  }

  if (!mssc_queue_write16(controller, 0x006BU, 0, CAN_TRANSPORT_PRIORITY_HIGH))
  {
    return false;
  }

  if (!mssc_queue_write16(controller, 0x006CU, (int16_t)controller->control_mode, CAN_TRANSPORT_PRIORITY_HIGH))
  {
    return false;
  }

  if (mutable_controller != NULL)
  {
    mutable_controller->can_control_prepared = true;
  }

  return true;
}

bool mssc_steering_controller_set_speed_rpm_priority(const mssc_steering_controller_t *controller,
                                                     int16_t speed_rpm,
                                                     can_transport_priority_t priority)
{
  if ((controller == NULL) || (controller->hcan == NULL))
  {
    return false;
  }

  if (!((mssc_steering_controller_t *)controller)->can_control_prepared)
  {
    if (!mssc_steering_controller_prepare_for_can_control(controller))
    {
      return false;
    }
  }

  if (!mssc_queue_write16(controller, 0x0030U, 0, priority))
  {
    return false;
  }

  return mssc_queue_write16(controller, 0x0031U, speed_rpm, priority);
}

bool mssc_steering_controller_set_speed_rpm(const mssc_steering_controller_t *controller, int16_t speed_rpm)
{
  return mssc_steering_controller_set_speed_rpm_priority(controller, speed_rpm, CAN_TRANSPORT_PRIORITY_NORMAL);
}

bool mssc_steering_controller_stop_priority(const mssc_steering_controller_t *controller,
                                            can_transport_priority_t priority)
{
  if ((controller == NULL) || (controller->hcan == NULL))
  {
    return false;
  }

  if (!mssc_queue_write16(controller, 0x0031U, 0, priority))
  {
    return false;
  }

  return mssc_queue_write16(controller, 0x0030U, 0, priority);
}

bool mssc_steering_controller_stop(const mssc_steering_controller_t *controller)
{
  return mssc_steering_controller_stop_priority(controller, CAN_TRANSPORT_PRIORITY_HIGH);
}

void mssc_steering_controller_feedback_process(mssc_steering_controller_t *controller)
{
  uint32_t now;
  uint16_t next_reg;

  if ((controller == NULL) || (controller->hcan == NULL))
  {
    return;
  }

  mssc_sync_recovery_state(controller);
  mssc_drain_feedback_results(controller);

  if (controller->feedback_request_pending)
  {
    return;
  }

  now = HAL_GetTick();
  if ((controller->last_feedback_query_tick_ms != 0U) &&
      ((now - controller->last_feedback_query_tick_ms) < MSSC_FEEDBACK_QUERY_PERIOD_MS))
  {
    return;
  }

  next_reg = (controller->next_feedback_reg == 0U) ? MSSC_REG_POSITION_HIGH : MSSC_REG_POSITION_LOW;
  if (!mssc_queue_read16(controller, next_reg))
  {
    return;
  }

  controller->feedback_request_pending = true;
  controller->last_feedback_query_tick_ms = now;
  controller->next_feedback_reg ^= 0x01U;
}

bool mssc_steering_controller_get_position_raw(const mssc_steering_controller_t *controller,
                                               int32_t *position_raw,
                                               uint32_t *age_ms)
{
  if ((controller == NULL) || (position_raw == NULL) || (controller->last_position_tick_ms == 0U))
  {
    return false;
  }

  *position_raw = controller->last_position_raw;
  if (age_ms != NULL)
  {
    *age_ms = HAL_GetTick() - controller->last_position_tick_ms;
  }

  return true;
}

bool mssc_steering_controller_feedback_fault_active(const mssc_steering_controller_t *controller)
{
  return (controller != NULL) && controller->feedback_fault_active;
}

uint32_t mssc_steering_controller_feedback_timeout_count(const mssc_steering_controller_t *controller)
{
  return (controller != NULL) ? controller->feedback_timeout_count : 0U;
}

uint32_t mssc_steering_controller_last_feedback_ok_age_ms(const mssc_steering_controller_t *controller)
{
  if ((controller == NULL) || (controller->last_feedback_ok_tick_ms == 0U))
  {
    return 0xFFFFFFFFUL;
  }

  return HAL_GetTick() - controller->last_feedback_ok_tick_ms;
}

void mssc_steering_controller_invalidate_can_control_mode(mssc_steering_controller_t *controller)
{
  if (controller == NULL)
  {
    return;
  }

  controller->can_control_prepared = false;
}
