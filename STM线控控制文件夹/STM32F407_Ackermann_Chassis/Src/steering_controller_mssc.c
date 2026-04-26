#include "steering_controller_mssc.h"

#include "can_transport.h"

#define MSSC_REG_POSITION_HIGH 0x0013U
#define MSSC_REG_POSITION_LOW 0x0014U
#define MSSC_FEEDBACK_QUERY_PERIOD_MS 25U

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

static bool mssc_send_write16(const mssc_steering_controller_t *controller, uint16_t reg_addr, int16_t value)
{
  uint8_t frame[8];

  mssc_build_write16(frame, reg_addr, value);
  return can_transport_send_std(controller->hcan, controller->node_id, frame, sizeof(frame));
}

static bool mssc_send_read16(const mssc_steering_controller_t *controller, uint16_t reg_addr)
{
  uint8_t frame[8];

  mssc_build_read16(frame, reg_addr);
  return can_transport_send_std(controller->hcan, controller->node_id, frame, sizeof(frame));
}

static void mssc_consume_feedback_frame(mssc_steering_controller_t *controller,
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

  index = (uint16_t)(((uint16_t)frame->data[2] << 8) | frame->data[1]);
  if (frame->data[3] != 0x00U)
  {
    return;
  }

  word_value = (uint16_t)(((uint16_t)frame->data[5] << 8) | frame->data[4]);
  if (index == (uint16_t)(0x4000U + MSSC_REG_POSITION_HIGH))
  {
    controller->position_high_word = word_value;
    controller->position_words_valid_mask |= 0x01U;
  }
  else if (index == (uint16_t)(0x4000U + MSSC_REG_POSITION_LOW))
  {
    controller->position_low_word = word_value;
    controller->position_words_valid_mask |= 0x02U;
  }
  else
  {
    return;
  }

  if ((controller->position_words_valid_mask & 0x03U) == 0x03U)
  {
    controller->last_position_raw =
        (int32_t)(((uint32_t)controller->position_high_word << 16) | (uint32_t)controller->position_low_word);
    controller->last_position_tick_ms = frame->tick_ms;
  }
}

void mssc_steering_controller_bind(mssc_steering_controller_t *controller,
                                   CAN_HandleTypeDef *hcan,
                                   uint16_t node_id,
                                   uint16_t control_mode)
{
  controller->hcan = hcan;
  controller->node_id = node_id;
  controller->control_mode = control_mode;
  controller->last_feedback_rx_count = 0U;
  controller->last_feedback_query_tick_ms = 0U;
  controller->position_high_word = 0U;
  controller->position_low_word = 0U;
  controller->position_words_valid_mask = 0U;
  controller->next_feedback_reg = 0U;
  controller->last_position_raw = 0;
  controller->last_position_tick_ms = 0U;
}

bool mssc_steering_controller_prepare_for_can_control(const mssc_steering_controller_t *controller)
{
  if ((controller == NULL) || (controller->hcan == NULL))
  {
    return false;
  }

  if (!mssc_send_write16(controller, 0x006BU, 0))
  {
    return false;
  }

  return mssc_send_write16(controller, 0x006CU, (int16_t)controller->control_mode);
}

bool mssc_steering_controller_set_speed_rpm(const mssc_steering_controller_t *controller, int16_t speed_rpm)
{
  if (!mssc_steering_controller_prepare_for_can_control(controller))
  {
    return false;
  }

  if (!mssc_send_write16(controller, 0x0030U, 0))
  {
    return false;
  }

  return mssc_send_write16(controller, 0x0031U, speed_rpm);
}

bool mssc_steering_controller_stop(const mssc_steering_controller_t *controller)
{
  if ((controller == NULL) || (controller->hcan == NULL))
  {
    return false;
  }

  if (!mssc_send_write16(controller, 0x0031U, 0))
  {
    return false;
  }

  return mssc_send_write16(controller, 0x0030U, 0);
}

void mssc_steering_controller_feedback_process(mssc_steering_controller_t *controller)
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
        mssc_consume_feedback_frame(controller, &frame);
      }
    }
  }

  now = HAL_GetTick();
  if ((controller->last_feedback_query_tick_ms != 0U) &&
      ((now - controller->last_feedback_query_tick_ms) < MSSC_FEEDBACK_QUERY_PERIOD_MS))
  {
    return;
  }

  next_reg = (controller->next_feedback_reg == 0U) ? MSSC_REG_POSITION_HIGH : MSSC_REG_POSITION_LOW;
  if (mssc_send_read16(controller, next_reg))
  {
    controller->last_feedback_query_tick_ms = now;
    controller->next_feedback_reg ^= 0x01U;
  }
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
