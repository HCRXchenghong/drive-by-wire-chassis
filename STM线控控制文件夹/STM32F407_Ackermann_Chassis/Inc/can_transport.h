#ifndef CAN_TRANSPORT_H
#define CAN_TRANSPORT_H

#include "main.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  uint32_t rx_count;
  uint32_t tx_count;
  uint32_t tx_fail_count;
  uint32_t timeout_count;
  uint32_t recovery_count;
  uint32_t error_irq_count;
  uint32_t error_warning_count;
  uint32_t error_passive_count;
  uint32_t bus_off_count;
  uint32_t last_rx_tick_ms;
  uint32_t last_tx_tick_ms;
  uint32_t last_fault_tick_ms;
  uint32_t last_recovery_tick_ms;
  uint32_t last_std_id;
  uint32_t last_tx_std_id;
  uint32_t last_hal_error;
  uint8_t last_dlc;
  uint8_t queue_depth;
  uint8_t tx_error_counter;
  uint8_t rx_error_counter;
  uint8_t last_error_code;
  bool read_pending;
  bool fault_active;
  bool recovery_active;
  uint8_t last_data[8];
} can_transport_bus_state_t;

typedef struct
{
  uint32_t rx_count;
  uint32_t tick_ms;
  uint32_t std_id;
  uint8_t dlc;
  uint8_t data[8];
} can_transport_rx_frame_t;

typedef enum
{
  CAN_TRANSPORT_PRIORITY_LOW = 0,
  CAN_TRANSPORT_PRIORITY_NORMAL = 1,
  CAN_TRANSPORT_PRIORITY_HIGH = 2,
  CAN_TRANSPORT_PRIORITY_CRITICAL = 3
} can_transport_priority_t;

typedef enum
{
  CAN_TRANSPORT_READ_RESULT_REPLY = 0,
  CAN_TRANSPORT_READ_RESULT_TIMEOUT = 1
} can_transport_read_result_type_t;

typedef struct
{
  uint32_t sequence;
  uint32_t cookie;
  uint32_t tick_ms;
  uint32_t std_id;
  uint16_t index;
  uint8_t dlc;
  uint8_t data[8];
  can_transport_read_result_type_t type;
} can_transport_read_result_t;

bool can_transport_start(CAN_HandleTypeDef *hcan, uint32_t filter_bank, uint32_t slave_start_filter_bank);
void can_transport_process(void);
bool can_transport_queue_write_std(CAN_HandleTypeDef *hcan,
                                   uint32_t std_id,
                                   const uint8_t *data,
                                   uint8_t len,
                                   can_transport_priority_t priority);
bool can_transport_queue_read_std(CAN_HandleTypeDef *hcan,
                                  uint32_t std_id,
                                  const uint8_t *data,
                                  uint8_t len,
                                  uint32_t timeout_ms,
                                  uint32_t cookie,
                                  uint8_t expected_cmd,
                                  uint16_t expected_index,
                                  can_transport_priority_t priority,
                                  uint32_t *sequence_out);
bool can_transport_get_bus_state(CAN_HandleTypeDef *hcan, can_transport_bus_state_t *state);
bool can_transport_bus_seen_recently(CAN_HandleTypeDef *hcan, uint32_t age_ms);
bool can_transport_get_frame_by_rx_count(CAN_HandleTypeDef *hcan,
                                         uint32_t rx_count,
                                         can_transport_rx_frame_t *frame);
bool can_transport_take_read_result(CAN_HandleTypeDef *hcan,
                                    uint32_t cookie,
                                    can_transport_read_result_t *result);
void can_transport_request_recovery(CAN_HandleTypeDef *hcan);

#endif
