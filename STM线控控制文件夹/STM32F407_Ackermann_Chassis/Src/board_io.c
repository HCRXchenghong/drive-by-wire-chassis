#include "board_io.h"

static ADC_HandleTypeDef *s_hadc = NULL;
static board_io_state_t s_board_io;
static uint32_t s_last_sample_tick = 0U;

static void board_io_update_gear(void)
{
  GPIO_PinState pin_state = HAL_GPIO_ReadPin(Gear_Mode_In_GPIO_Port, Gear_Mode_In_Pin);
  s_board_io.gear = (pin_state == GPIO_PIN_SET) ? GEAR_REVERSE : GEAR_DRIVE;
}

static void board_io_update_hardware_estop(void)
{
  GPIO_PinState pin_state = HAL_GPIO_ReadPin(HW_ESTOP_In_GPIO_Port, HW_ESTOP_In_Pin);
  s_board_io.hardware_estop_active = (pin_state == GPIO_PIN_RESET);
}

static void board_io_update_adc(void)
{
  if (s_hadc == NULL)
  {
    return;
  }

  if (HAL_ADC_Start(s_hadc) != HAL_OK)
  {
    return;
  }

  if (HAL_ADC_PollForConversion(s_hadc, 5U) != HAL_OK)
  {
    (void)HAL_ADC_Stop(s_hadc);
    return;
  }
  s_board_io.throttle_raw = (uint16_t)HAL_ADC_GetValue(s_hadc);

  if (HAL_ADC_PollForConversion(s_hadc, 5U) != HAL_OK)
  {
    (void)HAL_ADC_Stop(s_hadc);
    return;
  }
  s_board_io.brake_raw = (uint16_t)HAL_ADC_GetValue(s_hadc);

  (void)HAL_ADC_Stop(s_hadc);
}

void board_io_init(ADC_HandleTypeDef *hadc)
{
  s_hadc = hadc;
  s_board_io.throttle_raw = 0U;
  s_board_io.brake_raw = 0U;
  s_board_io.gear = GEAR_DRIVE;
  s_board_io.hardware_estop_active = false;
  board_io_update_gear();
  board_io_update_hardware_estop();
}

void board_io_process(void)
{
  uint32_t now = HAL_GetTick();

  if ((now - s_last_sample_tick) < 10U)
  {
    return;
  }

  s_last_sample_tick = now;
  board_io_update_gear();
  board_io_update_hardware_estop();
  board_io_update_adc();
}

const board_io_state_t *board_io_get(void)
{
  return &s_board_io;
}

const char *board_io_gear_name(gear_state_t gear)
{
  return (gear == GEAR_REVERSE) ? "R" : "D";
}
