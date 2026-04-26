#ifndef CAN_TRANSPORT_H
#define CAN_TRANSPORT_H

#include "main.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
  uint32_t rx_count;
  uint32_t last_rx_tick_ms;
  uint32_t last_std_id;
  uint8_t last_dlc;
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

bool can_transport_start(CAN_HandleTypeDef *hcan, uint32_t filter_bank, uint32_t slave_start_filter_bank);
bool can_transport_send_std(CAN_HandleTypeDef *hcan, uint32_t std_id, const uint8_t *data, uint8_t len);
bool can_transport_get_bus_state(CAN_HandleTypeDef *hcan, can_transport_bus_state_t *state);
bool can_transport_bus_seen_recently(CAN_HandleTypeDef *hcan, uint32_t age_ms);
bool can_transport_get_frame_by_rx_count(CAN_HandleTypeDef *hcan,
                                         uint32_t rx_count,
                                         can_transport_rx_frame_t *frame);

#endif
