#include "usb_cdc_shell.h"

#include "stm32f4xx_hal.h"
#include "usbd_cdc_if.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define USB_CDC_SHELL_LINE_MAX 256U
#define USB_CDC_SHELL_TX_MAX   768U

static char s_rx_accumulator[USB_CDC_SHELL_LINE_MAX];
static char s_ready_line[USB_CDC_SHELL_LINE_MAX];
static volatile uint8_t s_line_ready = 0U;
static size_t s_rx_length = 0U;

void usb_cdc_shell_init(void)
{
  s_rx_length = 0U;
  s_line_ready = 0U;
  s_rx_accumulator[0] = '\0';
  s_ready_line[0] = '\0';
}

void usb_cdc_shell_feed_bytes(const uint8_t *data, uint32_t len)
{
  uint32_t i;

  if (data == NULL)
  {
    return;
  }

  for (i = 0U; i < len; ++i)
  {
    char ch = (char)data[i];

    if (ch == '\r')
    {
      continue;
    }

    if (ch == '\n')
    {
      if ((s_rx_length > 0U) && (s_line_ready == 0U))
      {
        s_rx_accumulator[s_rx_length] = '\0';
        memcpy(s_ready_line, s_rx_accumulator, s_rx_length + 1U);
        s_line_ready = 1U;
      }

      s_rx_length = 0U;
      continue;
    }

    if (s_rx_length < (USB_CDC_SHELL_LINE_MAX - 1U))
    {
      s_rx_accumulator[s_rx_length++] = ch;
    }
  }
}

bool usb_cdc_shell_get_line(char *line, size_t max_len)
{
  size_t length;

  if ((line == NULL) || (max_len == 0U) || (s_line_ready == 0U))
  {
    return false;
  }

  length = strlen(s_ready_line);
  if (length >= max_len)
  {
    length = max_len - 1U;
  }

  memcpy(line, s_ready_line, length);
  line[length] = '\0';
  s_line_ready = 0U;
  return true;
}

bool usb_cdc_shell_write(const char *text)
{
  uint32_t start_tick;
  size_t len;

  if (text == NULL)
  {
    return false;
  }

  len = strlen(text);
  if (len == 0U)
  {
    return true;
  }

  start_tick = HAL_GetTick();
  while ((HAL_GetTick() - start_tick) < 50U)
  {
    uint8_t status = CDC_Transmit_FS((uint8_t *)text, (uint16_t)len);
    if (status == USBD_OK)
    {
      return true;
    }
    HAL_Delay(1U);
  }

  return false;
}

bool usb_cdc_shell_printf(const char *fmt, ...)
{
  char buffer[USB_CDC_SHELL_TX_MAX];
  va_list args;

  va_start(args, fmt);
  (void)vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  return usb_cdc_shell_write(buffer);
}
