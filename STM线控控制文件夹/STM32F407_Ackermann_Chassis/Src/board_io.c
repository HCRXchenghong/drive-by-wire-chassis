#include "board_io.h"

static ADC_HandleTypeDef *s_hadc = NULL;
static board_io_state_t s_board_io;
static uint32_t s_last_sample_tick = 0U;

static uint16_t board_io_read_adc_channel(uint32_t channel)
{
  ADC_ChannelConfTypeDef sConfig = {0};
  uint32_t total = 0U;
  uint32_t sample_count = 0U;

  if (s_hadc == NULL)
  {
    return 0U;
  }

  sConfig.Channel = channel;
  sConfig.Rank = 1U;
  sConfig.SamplingTime = ADC_SAMPLETIME_144CYCLES;

  for (uint32_t i = 0U; i < 16U; i++)
  {
    if (HAL_ADC_ConfigChannel(s_hadc, &sConfig) != HAL_OK)
    {
      continue;
    }

    if (HAL_ADC_Start(s_hadc) != HAL_OK)
    {
      continue;
    }

    if (HAL_ADC_PollForConversion(s_hadc, 10U) == HAL_OK)
    {
      total += HAL_ADC_GetValue(s_hadc);
      sample_count++;
    }

    (void)HAL_ADC_Stop(s_hadc);
  }

  if (sample_count == 0U)
  {
    return 0U;
  }

  return (uint16_t)((total + (sample_count / 2U)) / sample_count);
}

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

  s_board_io.throttle_raw = board_io_read_adc_channel(ADC_CHANNEL_0);
  s_board_io.brake_raw = board_io_read_adc_channel(ADC_CHANNEL_1);
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
