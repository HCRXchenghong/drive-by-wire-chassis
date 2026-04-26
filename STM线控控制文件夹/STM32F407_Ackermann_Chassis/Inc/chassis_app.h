#ifndef CHASSIS_APP_H
#define CHASSIS_APP_H

#include "main.h"

#include <stdint.h>

void chassis_app_init(ADC_HandleTypeDef *hadc,
                      CAN_HandleTypeDef *drive_can,
                      CAN_HandleTypeDef *steer_can,
                      UART_HandleTypeDef *ewm22_uart);
void chassis_app_process(void);
void chassis_app_usb_rx(const uint8_t *data, uint32_t len);

#endif
