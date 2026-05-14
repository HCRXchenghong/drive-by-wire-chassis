#include "can_transport.h"

#include <string.h>

#define CAN_TRANSPORT_RX_HISTORY_DEPTH 16U
#define CAN_TRANSPORT_REQUEST_QUEUE_DEPTH 16U
#define CAN_TRANSPORT_RESULT_QUEUE_DEPTH 16U
#define CAN_TRANSPORT_SEND_GAP_MS 10U
#define CAN_TRANSPORT_RECOVERY_RETRY_MS 100U

#define CAN_TRANSPORT_ESR_LEC_MASK 0x00000070UL
#define CAN_TRANSPORT_ESR_LEC_SHIFT 4U
#define CAN_TRANSPORT_ESR_TEC_MASK 0x00FF0000UL
#define CAN_TRANSPORT_ESR_TEC_SHIFT 16U
#define CAN_TRANSPORT_ESR_REC_MASK 0xFF000000UL
#define CAN_TRANSPORT_ESR_REC_SHIFT 24U

typedef enum
{
  CAN_TRANSPORT_REQUEST_KIND_WRITE = 0,
  CAN_TRANSPORT_REQUEST_KIND_READ = 1
} can_transport_request_kind_t;

typedef struct
{
  can_transport_request_kind_t kind;
  can_transport_priority_t priority;
  uint32_t sequence;
  uint32_t std_id;
  uint32_t cookie;
  uint32_t timeout_ms;
  uint16_t expected_index;
  uint8_t len;
  uint8_t expected_cmd;
  uint8_t data[8];
} can_transport_request_t;

typedef struct
{
  bool active;
  uint32_t sent_tick_ms;
  can_transport_request_t request;
} can_transport_active_request_t;

typedef struct
{
  CAN_HandleTypeDef *hcan;
  uint32_t notification_mask;
  uint32_t next_sequence;
  uint32_t tx_hold_until_ms;
  uint32_t last_recovery_attempt_tick_ms;
  bool recovery_requested;
  can_transport_bus_state_t state;
  can_transport_rx_frame_t history[CAN_TRANSPORT_RX_HISTORY_DEPTH];
  uint8_t history_write_index;
  can_transport_request_t request_queue[CAN_TRANSPORT_REQUEST_QUEUE_DEPTH];
  uint8_t request_queue_count;
  can_transport_active_request_t active_request;
  can_transport_read_result_t result_queue[CAN_TRANSPORT_RESULT_QUEUE_DEPTH];
  uint8_t result_queue_count;
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
      memset(&s_slots[index], 0, sizeof(s_slots[index]));
      s_slots[index].hcan = hcan;
      s_slots[index].next_sequence = 1U;
      return &s_slots[index];
    }
  }

  return NULL;
}

static void slot_capture_error_state(can_transport_slot_t *slot, uint32_t hal_error)
{
  uint32_t esr;

  if ((slot == NULL) || (slot->hcan == NULL))
  {
    return;
  }

  esr = slot->hcan->Instance->ESR;
  slot->state.last_hal_error = hal_error;
  slot->state.last_error_code =
      (uint8_t)((esr & CAN_TRANSPORT_ESR_LEC_MASK) >> CAN_TRANSPORT_ESR_LEC_SHIFT);
  slot->state.tx_error_counter =
      (uint8_t)((esr & CAN_TRANSPORT_ESR_TEC_MASK) >> CAN_TRANSPORT_ESR_TEC_SHIFT);
  slot->state.rx_error_counter =
      (uint8_t)((esr & CAN_TRANSPORT_ESR_REC_MASK) >> CAN_TRANSPORT_ESR_REC_SHIFT);
}

static void slot_mark_fault(can_transport_slot_t *slot, bool request_recovery)
{
  if (slot == NULL)
  {
    return;
  }

  slot->state.fault_active = true;
  slot->state.last_fault_tick_ms = HAL_GetTick();

  if (request_recovery)
  {
    slot->state.recovery_active = true;
    slot->recovery_requested = true;
  }
}

static void slot_prune_queue_below_priority(can_transport_slot_t *slot, can_transport_priority_t min_priority)
{
  uint8_t read_index;
  uint8_t write_index = 0U;

  if (slot == NULL)
  {
    return;
  }

  for (read_index = 0U; read_index < slot->request_queue_count; ++read_index)
  {
    if (slot->request_queue[read_index].priority < min_priority)
    {
      continue;
    }

    if (write_index != read_index)
    {
      slot->request_queue[write_index] = slot->request_queue[read_index];
    }
    ++write_index;
  }

  if (write_index < CAN_TRANSPORT_REQUEST_QUEUE_DEPTH)
  {
    memset(&slot->request_queue[write_index],
           0,
           (CAN_TRANSPORT_REQUEST_QUEUE_DEPTH - write_index) * sizeof(slot->request_queue[0]));
  }

  slot->request_queue_count = write_index;
  slot->state.queue_depth = write_index;
}

static void slot_push_result(can_transport_slot_t *slot, const can_transport_read_result_t *result)
{
  if ((slot == NULL) || (result == NULL))
  {
    return;
  }

  if (slot->result_queue_count >= CAN_TRANSPORT_RESULT_QUEUE_DEPTH)
  {
    memmove(&slot->result_queue[0],
            &slot->result_queue[1],
            (CAN_TRANSPORT_RESULT_QUEUE_DEPTH - 1U) * sizeof(slot->result_queue[0]));
    slot->result_queue_count = CAN_TRANSPORT_RESULT_QUEUE_DEPTH - 1U;
  }

  slot->result_queue[slot->result_queue_count++] = *result;
}

static bool slot_enqueue_request(can_transport_slot_t *slot, const can_transport_request_t *request)
{
  uint8_t insert_index;

  if ((slot == NULL) || (request == NULL))
  {
    return false;
  }

  if (slot->request_queue_count >= CAN_TRANSPORT_REQUEST_QUEUE_DEPTH)
  {
    if (request->priority != CAN_TRANSPORT_PRIORITY_CRITICAL)
    {
      return false;
    }

    memset(&slot->request_queue[CAN_TRANSPORT_REQUEST_QUEUE_DEPTH - 1U],
           0,
           sizeof(slot->request_queue[0]));
    slot->request_queue_count = CAN_TRANSPORT_REQUEST_QUEUE_DEPTH - 1U;
  }

  insert_index = slot->request_queue_count;
  while ((insert_index > 0U) &&
         (slot->request_queue[insert_index - 1U].priority < request->priority))
  {
    slot->request_queue[insert_index] = slot->request_queue[insert_index - 1U];
    --insert_index;
  }

  slot->request_queue[insert_index] = *request;
  slot->request_queue_count++;
  slot->state.queue_depth = slot->request_queue_count;
  return true;
}

static void slot_pop_request_at(can_transport_slot_t *slot, uint8_t index)
{
  if ((slot == NULL) || (slot->request_queue_count == 0U) || (index >= slot->request_queue_count))
  {
    return;
  }

  if (index < (uint8_t)(slot->request_queue_count - 1U))
  {
    memmove(&slot->request_queue[index],
            &slot->request_queue[index + 1U],
            (slot->request_queue_count - index - 1U) * sizeof(slot->request_queue[0]));
  }

  slot->request_queue_count--;
  memset(&slot->request_queue[slot->request_queue_count], 0, sizeof(slot->request_queue[0]));
  slot->state.queue_depth = slot->request_queue_count;
}

static bool slot_read_matches(const can_transport_active_request_t *active_request,
                              const CAN_RxHeaderTypeDef *rx_header,
                              const uint8_t *rx_data)
{
  uint32_t rx_std_id;
  uint16_t rx_index;

  if ((active_request == NULL) || !active_request->active || (rx_header == NULL) || (rx_data == NULL))
  {
    return false;
  }

  rx_std_id = (rx_header->IDE == CAN_ID_STD) ? rx_header->StdId : rx_header->ExtId;
  if (rx_std_id != (active_request->request.std_id & 0x7FFU))
  {
    return false;
  }

  if (rx_header->DLC < 6U)
  {
    return false;
  }

  if (rx_data[0] != active_request->request.expected_cmd)
  {
    return false;
  }

  rx_index = (uint16_t)(((uint16_t)rx_data[2] << 8) | rx_data[1]);
  return (rx_index == active_request->request.expected_index);
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
    can_transport_read_result_t result;

    if (HAL_CAN_GetRxMessage(hcan, CAN_RX_FIFO0, &rx_header, rx_data) != HAL_OK)
    {
      slot_capture_error_state(slot, HAL_CAN_GetError(hcan));
      slot_mark_fault(slot, true);
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

    if (slot_read_matches(&slot->active_request, &rx_header, rx_data))
    {
      memset(&result, 0, sizeof(result));
      result.sequence = slot->active_request.request.sequence;
      result.cookie = slot->active_request.request.cookie;
      result.tick_ms = slot->state.last_rx_tick_ms;
      result.std_id = slot->state.last_std_id;
      result.index = (uint16_t)(((uint16_t)rx_data[2] << 8) | rx_data[1]);
      result.dlc = slot->state.last_dlc;
      result.type = CAN_TRANSPORT_READ_RESULT_REPLY;
      memcpy(result.data, rx_data, result.dlc);
      slot_push_result(slot, &result);
      memset(&slot->active_request, 0, sizeof(slot->active_request));
      slot->state.read_pending = false;
      slot->state.fault_active = false;
      slot->state.recovery_active = false;
      slot->recovery_requested = false;
    }
  }
}

static void slot_request_recovery(can_transport_slot_t *slot)
{
  if (slot == NULL)
  {
    return;
  }

  slot_mark_fault(slot, true);
}

static void slot_handle_timeout(can_transport_slot_t *slot, uint32_t now)
{
  can_transport_read_result_t result;

  if ((slot == NULL) || !slot->active_request.active)
  {
    return;
  }

  memset(&result, 0, sizeof(result));
  result.sequence = slot->active_request.request.sequence;
  result.cookie = slot->active_request.request.cookie;
  result.tick_ms = now;
  result.std_id = slot->active_request.request.std_id;
  result.index = slot->active_request.request.expected_index;
  result.type = CAN_TRANSPORT_READ_RESULT_TIMEOUT;
  slot_push_result(slot, &result);

  slot->state.timeout_count++;
  memset(&slot->active_request, 0, sizeof(slot->active_request));
  slot->state.read_pending = false;
}

static bool slot_send_request(can_transport_slot_t *slot, const can_transport_request_t *request)
{
  CAN_TxHeaderTypeDef tx_header = {0};
  uint32_t mailbox = 0U;

  if ((slot == NULL) || (slot->hcan == NULL) || (request == NULL))
  {
    return false;
  }

  tx_header.StdId = request->std_id & 0x7FFU;
  tx_header.ExtId = 0U;
  tx_header.IDE = CAN_ID_STD;
  tx_header.RTR = CAN_RTR_DATA;
  tx_header.DLC = request->len;
  tx_header.TransmitGlobalTime = DISABLE;

  if (HAL_CAN_AddTxMessage(slot->hcan, &tx_header, (uint8_t *)request->data, &mailbox) != HAL_OK)
  {
    slot->state.tx_fail_count++;
    slot_capture_error_state(slot, HAL_CAN_GetError(slot->hcan));
    slot_request_recovery(slot);
    return false;
  }

  slot->state.tx_count++;
  slot->state.last_tx_tick_ms = HAL_GetTick();
  slot->state.last_tx_std_id = request->std_id & 0x7FFU;
  slot->tx_hold_until_ms = slot->state.last_tx_tick_ms + CAN_TRANSPORT_SEND_GAP_MS;

  if (request->kind == CAN_TRANSPORT_REQUEST_KIND_READ)
  {
    slot->active_request.active = true;
    slot->active_request.sent_tick_ms = slot->state.last_tx_tick_ms;
    slot->active_request.request = *request;
    slot->state.read_pending = true;
  }

  return true;
}

static bool slot_send_next_queued_request(can_transport_slot_t *slot, bool allow_read)
{
  uint8_t index;

  if ((slot == NULL) || (slot->request_queue_count == 0U))
  {
    return false;
  }

  for (index = 0U; index < slot->request_queue_count; ++index)
  {
    if (!allow_read && (slot->request_queue[index].kind == CAN_TRANSPORT_REQUEST_KIND_READ))
    {
      continue;
    }

    if (!slot_send_request(slot, &slot->request_queue[index]))
    {
      return false;
    }

    slot_pop_request_at(slot, index);
    return true;
  }

  return false;
}

static bool slot_perform_recovery(can_transport_slot_t *slot, uint32_t now)
{
  if (slot == NULL)
  {
    return false;
  }

  if ((now - slot->last_recovery_attempt_tick_ms) < CAN_TRANSPORT_RECOVERY_RETRY_MS)
  {
    return false;
  }

  slot->last_recovery_attempt_tick_ms = now;
  memset(&slot->active_request, 0, sizeof(slot->active_request));
  slot->state.read_pending = false;
  slot_prune_queue_below_priority(slot, CAN_TRANSPORT_PRIORITY_HIGH);
  (void)HAL_CAN_Stop(slot->hcan);

  if (HAL_CAN_Start(slot->hcan) != HAL_OK)
  {
    slot_capture_error_state(slot, HAL_CAN_GetError(slot->hcan));
    slot_mark_fault(slot, true);
    return false;
  }

  if (HAL_CAN_ActivateNotification(slot->hcan, slot->notification_mask) != HAL_OK)
  {
    slot_capture_error_state(slot, HAL_CAN_GetError(slot->hcan));
    slot_mark_fault(slot, true);
    return false;
  }

  slot->state.recovery_count++;
  slot->state.recovery_active = true;
  slot->state.last_recovery_tick_ms = now;
  slot->tx_hold_until_ms = now + CAN_TRANSPORT_SEND_GAP_MS;
  slot->recovery_requested = false;
  return true;
}

static void can_transport_process_slot(can_transport_slot_t *slot)
{
  uint32_t now;

  if ((slot == NULL) || (slot->hcan == NULL))
  {
    return;
  }

  now = HAL_GetTick();

  if (slot->recovery_requested)
  {
    (void)slot_perform_recovery(slot, now);
    return;
  }

  if (slot->active_request.active)
  {
    if ((now - slot->active_request.sent_tick_ms) >= slot->active_request.request.timeout_ms)
    {
      slot_handle_timeout(slot, now);
    }

    return;
  }

  if ((slot->request_queue_count == 0U) || (now < slot->tx_hold_until_ms))
  {
    return;
  }

  (void)slot_send_next_queued_request(slot, true);
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
  memset(slot->request_queue, 0, sizeof(slot->request_queue));
  memset(slot->result_queue, 0, sizeof(slot->result_queue));
  memset(&slot->active_request, 0, sizeof(slot->active_request));
  slot->history_write_index = 0U;
  slot->request_queue_count = 0U;
  slot->result_queue_count = 0U;
  slot->tx_hold_until_ms = 0U;
  slot->last_recovery_attempt_tick_ms = 0U;
  slot->recovery_requested = false;
  slot->next_sequence = 1U;
  slot->notification_mask =
      CAN_IT_RX_FIFO0_MSG_PENDING |
      CAN_IT_RX_FIFO0_FULL |
      CAN_IT_RX_FIFO0_OVERRUN |
      CAN_IT_ERROR_WARNING |
      CAN_IT_ERROR_PASSIVE |
      CAN_IT_BUSOFF |
      CAN_IT_LAST_ERROR_CODE |
      CAN_IT_ERROR;

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

  return (HAL_CAN_ActivateNotification(hcan, slot->notification_mask) == HAL_OK);
}

void can_transport_process(void)
{
  size_t index;

  for (index = 0U; index < (sizeof(s_slots) / sizeof(s_slots[0])); ++index)
  {
    can_transport_process_slot(&s_slots[index]);
  }
}

bool can_transport_queue_write_std(CAN_HandleTypeDef *hcan,
                                   uint32_t std_id,
                                   const uint8_t *data,
                                   uint8_t len,
                                   can_transport_priority_t priority)
{
  can_transport_slot_t *slot = find_slot(hcan);
  can_transport_request_t request;

  if ((slot == NULL) || (data == NULL) || (len > 8U))
  {
    return false;
  }

  if (slot->recovery_requested && (priority < CAN_TRANSPORT_PRIORITY_HIGH))
  {
    return false;
  }

  memset(&request, 0, sizeof(request));
  request.kind = CAN_TRANSPORT_REQUEST_KIND_WRITE;
  request.priority = priority;
  request.sequence = slot->next_sequence++;
  request.std_id = std_id & 0x7FFU;
  request.len = len;
  memcpy(request.data, data, len);

  return slot_enqueue_request(slot, &request);
}

bool can_transport_queue_read_std(CAN_HandleTypeDef *hcan,
                                  uint32_t std_id,
                                  const uint8_t *data,
                                  uint8_t len,
                                  uint32_t timeout_ms,
                                  uint32_t cookie,
                                  uint8_t expected_cmd,
                                  uint16_t expected_index,
                                  can_transport_priority_t priority,
                                  uint32_t *sequence_out)
{
  can_transport_slot_t *slot = find_slot(hcan);
  can_transport_request_t request;

  if ((slot == NULL) || (data == NULL) || (len > 8U) || (timeout_ms == 0U))
  {
    return false;
  }

  if (slot->recovery_requested)
  {
    return false;
  }

  memset(&request, 0, sizeof(request));
  request.kind = CAN_TRANSPORT_REQUEST_KIND_READ;
  request.priority = priority;
  request.sequence = slot->next_sequence++;
  request.std_id = std_id & 0x7FFU;
  request.cookie = cookie;
  request.timeout_ms = timeout_ms;
  request.expected_cmd = expected_cmd;
  request.expected_index = expected_index;
  request.len = len;
  memcpy(request.data, data, len);

  if (!slot_enqueue_request(slot, &request))
  {
    return false;
  }

  if (sequence_out != NULL)
  {
    *sequence_out = request.sequence;
  }

  return true;
}

bool can_transport_get_bus_state(CAN_HandleTypeDef *hcan, can_transport_bus_state_t *state)
{
  can_transport_slot_t *slot = find_slot(hcan);

  if ((slot == NULL) || (state == NULL))
  {
    return false;
  }

  *state = slot->state;
  state->queue_depth = slot->request_queue_count;
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

bool can_transport_take_read_result(CAN_HandleTypeDef *hcan,
                                    uint32_t cookie,
                                    can_transport_read_result_t *result)
{
  can_transport_slot_t *slot = find_slot(hcan);
  uint8_t index;

  if ((slot == NULL) || (result == NULL))
  {
    return false;
  }

  for (index = 0U; index < slot->result_queue_count; ++index)
  {
    if ((cookie != 0U) && (slot->result_queue[index].cookie != cookie))
    {
      continue;
    }

    *result = slot->result_queue[index];

    if ((index + 1U) < slot->result_queue_count)
    {
      memmove(&slot->result_queue[index],
              &slot->result_queue[index + 1U],
              (slot->result_queue_count - index - 1U) * sizeof(slot->result_queue[0]));
    }

    slot->result_queue_count--;
    memset(&slot->result_queue[slot->result_queue_count], 0, sizeof(slot->result_queue[0]));
    return true;
  }

  return false;
}

void can_transport_request_recovery(CAN_HandleTypeDef *hcan)
{
  can_transport_slot_t *slot = find_slot(hcan);

  slot_request_recovery(slot);
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)
{
  can_transport_record_rx(hcan);
}

void HAL_CAN_RxFifo0FullCallback(CAN_HandleTypeDef *hcan)
{
  can_transport_record_rx(hcan);
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)
{
  can_transport_slot_t *slot = find_slot(hcan);
  uint32_t hal_error;

  if (slot == NULL)
  {
    return;
  }

  hal_error = HAL_CAN_GetError(hcan);
  slot->state.error_irq_count++;
  slot_capture_error_state(slot, hal_error);

  if ((hal_error & HAL_CAN_ERROR_EWG) != 0U)
  {
    slot->state.error_warning_count++;
  }

  if ((hal_error & HAL_CAN_ERROR_EPV) != 0U)
  {
    slot->state.error_passive_count++;
  }

  if ((hal_error & HAL_CAN_ERROR_BOF) != 0U)
  {
    slot->state.bus_off_count++;
  }

  slot_request_recovery(slot);
}
