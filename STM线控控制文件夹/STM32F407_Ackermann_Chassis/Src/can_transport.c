#include "can_transport.h"

#include <string.h>

#define CAN_TRANSPORT_RX_HISTORY_DEPTH 16U

typedef struct
{
  CAN_HandleTypeDef *hcan;
  can_transport_bus_state_t state;
  can_transport_rx_frame_t history[CAN_TRANSPORT_RX_HISTORY_DEPTH];
  uint8_t history_write_index;
} can_transport_slot_t;

static can_transport_slot_t s_slots[2];

static can_transport_slot_t *find_slot(CAN_HandleTypeDef *hcan)
{
  size_t index;

  if (hcan == NULL)
  {
    return NULL;
  }

  for (index = 0U; index < (sizeof(s_slots) / sizeof(s_slots[0])); ++index)
  {
    if (s_slots[index].hcan == hcan)
    {
      return &s_slots[index];
    }
  }

  for (index = 0U; index < (sizeof(s_slots) / sizeof(s_slots[0])); ++index)
  {
    if (s_slots[index].hcan == NULL)
    {
      s_slots[index].hcan = hcan;
      memset(&s_slots[index].state, 0, sizeof(s_slots[index].state));
      return &s_slots[index];
    }
  }

  return NULL;
}

static void can_transport_record_rx(CAN_HandleTypeDef *hcan)
{
  CAN_RxHeaderTypeDef rx_header;
  uint8_t rx_data[8];
  can_transport_slot_t *slot = find_slot(hcan);
  can_transport_rx_frame_t *frame;

  if (slot == NULL)
  {
    return;
  }

  while (HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) > 0U)
  {
    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data) != HAL_OK)
    {
      return;
    }

    slot->state.rx_count++;
    slot->state.last_rx_tick_ms = HAL_GetTick();
    slot->state.last_std_id = (rx_header.IDE == CAN_ID_STD) ? rx_header.StdId : rx_header.ExtId;
    slot->state.last_dlc = (uint8_t)((rx_header.DLC > 8U) ? 8U : rx_header.DLC);
    memset(slot->state.last_data, 0, sizeof(slot->state.last_data));
    memcpy(slot->state.last_data, rx_data, slot->state.last_dlc);

    frame = &slot->history[slot->history_write_index];
    frame->rx_count = slot->state.rx_count;
    frame->tick_ms = slot->state.last_rx_tick_ms;
    frame->std_id = slot->state.last_std_id;
    frame->dlc = slot->state.last_dlc;
    memset(frame->data, 0, sizeof(frame->data));
    memcpy(frame->data, rx_data, frame->dlc);

    slot->history_write_index = (uint8_t)((slot->history_write_index + 1U) % CAN_TRANSPORT_RX_HISTORY_DEPTH);
  }
}

bool can_transport_start(CAN_HandleTypeDef *hcan, uint32_t filter_bank, uint32_t slave_start_filter_bank)
{
  CAN_FilterTypeDef filter = {0};
  can_transport_slot_t *slot = find_slot(hcan);

  if (slot == NULL)
  {
    return false;
  }

  memset(&slot->state, 0, sizeof(slot->state));
  memset(slot->history, 0, sizeof(slot->history));
  slot->history_write_index = 0U;

  filter.FilterBank = filter_bank;
  filter.FilterMode = CAN_FILTERMODE_IDMASK;
  filter.FilterScale = CAN_FILTERSCALE_32BIT;
  filter.FilterIdHigh = 0x0000U;
  filter.FilterIdLow = 0x0000U;
  filter.FilterMaskIdHigh = 0x0000U;
  filter.FilterMaskIdLow = 0x0000U;
  filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
  filter.FilterActivation = ENABLE;
  filter.SlaveStartFilterBank = slave_start_filter_bank;

  if (HAL_CAN_ConfigFilter(hcan, &filter) != HAL_OK)
  {
    return false;
  }

  if (HAL_CAN_Start(hcan) != HAL_OK)
  {
    return false;
  }

  return (HAL_CAN_ActivateNotification(hcan, CAN_IT_RX_FIFO0_MSG_PENDING) == HAL_OK);
}

bool can_transport_send_std(CAN_HandleTypeDef *hcan, uint32_t std_id, const uint8_t *data, uint8_t len)
{
  CAN_TxHeaderTypeDef tx_header = {0};
  uint32_t mailbox = 0U;

  if ((hcan == NULL) || (data == NULL) || (len > 8U))
  {
    return false;
  }

  tx_header.StdId = std_id & 0x7FFU;
  tx_header.ExtId = 0U;
  tx_header.IDE = CAN_ID_STD;
  tx_header.RTR = CAN_RTR_DATA;
  tx_header.DLC = len;
  tx_header.TransmitGlobalTime = DISABLE;

  return (HAL_CAN_AddTxMessage(hcan, &tx_header, (uint8_t *)data, &mailbox) == HAL_OK);
}

bool can_transport_get_bus_state(CAN_HandleTypeDef *hcan, can_transport_bus_state_t *state)
{
  can_transport_slot_t *slot = find_slot(hcan);

  if ((slot == NULL) || (state == NULL))
  {
    return false;
  }

  *state = slot->state;
  return true;
}

bool can_transport_bus_seen_recently(CAN_HandleTypeDef *hcan, uint32_t age_ms)
{
  can_transport_bus_state_t state;

  if (!can_transport_get_bus_state(hcan, &state))
  {
    return false;
  }

  if (state.last_rx_tick_ms == 0U)
  {
    return false;
  }

  return ((HAL_GetTick() - state.last_rx_tick_ms) <= age_ms);
}

bool can_transport_get_frame_by_rx_count(CAN_HandleTypeDef *hcan,
                                         uint32_t rx_count,
                                         can_transport_rx_frame_t *frame)
{
  can_transport_slot_t *slot = find_slot(hcan);
  uint32_t newest_rx_count;
  uint32_t age;
  uint8_t history_index;

  if ((slot == NULL) || (frame == NULL) || (rx_count == 0U))
  {
    return false;
  }

  newest_rx_count = slot->state.rx_count;
  if ((rx_count > newest_rx_count) || ((newest_rx_count - rx_count) >= CAN_TRANSPORT_RX_HISTORY_DEPTH))
  {
    return false;
  }

  age = newest_rx_count - rx_count;
  history_index =
      (uint8_t)((slot->history_write_index + CAN_TRANSPORT_RX_HISTORY_DEPTH - 1U - age) %
                CAN_TRANSPORT_RX_HISTORY_DEPTH);

  if (slot->history[history_index].rx_count != rx_count)
  {
    return false;
  }

  *frame = slot->history[history_index];
  return true;
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
  can_transport_record_rx(hcan);
}

void HAL_CAN_RxFifo0FullCallback(CAN_HandleTypeDef *hcan)
{
  can_transport_record_rx(hcan);
}
