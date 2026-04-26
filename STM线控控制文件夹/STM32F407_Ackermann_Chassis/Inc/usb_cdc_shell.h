#ifndef USB_CDC_SHELL_H
#define USB_CDC_SHELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void usb_cdc_shell_init(void);
void usb_cdc_shell_feed_bytes(const uint8_t *data, uint32_t len);
bool usb_cdc_shell_get_line(char *line, size_t max_len);
bool usb_cdc_shell_write(const char *text);
bool usb_cdc_shell_printf(const char *fmt, ...);

#endif
