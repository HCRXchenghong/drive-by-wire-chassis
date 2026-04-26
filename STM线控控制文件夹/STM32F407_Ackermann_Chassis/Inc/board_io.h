#ifndef BOARD_IO_H
#define BOARD_IO_H

#include "main.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
  GEAR_DRIVE = 0,
  GEAR_REVERSE = 1,
} gear_state_t;

typedef struct
{
  uint16_t throttle_raw;
  uint16_t brake_raw;
  gear_state_t gear;
  bool hardware_estop_active;
} board_io_state_t;

void board_io_init(ADC_HandleTypeDef *hadc);
void board_io_process(void);
const board_io_state_t *board_io_get(void);
const char *board_io_gear_name(gear_state_t gear);

#endif
