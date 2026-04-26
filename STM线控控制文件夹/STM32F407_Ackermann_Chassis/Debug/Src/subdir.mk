################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Src/board_io.c \
../Src/can_transport.c \
../Src/chassis_app.c \
../Src/drive_controller_mssd.c \
../Src/ewm22_link.c \
../Src/main.c \
../Src/steering_controller_mssc.c \
../Src/stm32f4xx_hal_msp.c \
../Src/stm32f4xx_it.c \
../Src/syscalls.c \
../Src/sysmem.c \
../Src/system_stm32f4xx.c \
../Src/usb_cdc_shell.c \
../Src/usb_device.c \
../Src/usbd_cdc_if.c \
../Src/usbd_conf.c \
../Src/usbd_desc.c \
../Src/vehicle_config.c 

OBJS += \
./Src/board_io.o \
./Src/can_transport.o \
./Src/chassis_app.o \
./Src/drive_controller_mssd.o \
./Src/ewm22_link.o \
./Src/main.o \
./Src/steering_controller_mssc.o \
./Src/stm32f4xx_hal_msp.o \
./Src/stm32f4xx_it.o \
./Src/syscalls.o \
./Src/sysmem.o \
./Src/system_stm32f4xx.o \
./Src/usb_cdc_shell.o \
./Src/usb_device.o \
./Src/usbd_cdc_if.o \
./Src/usbd_conf.o \
./Src/usbd_desc.o \
./Src/vehicle_config.o 

C_DEPS += \
./Src/board_io.d \
./Src/can_transport.d \
./Src/chassis_app.d \
./Src/drive_controller_mssd.d \
./Src/ewm22_link.d \
./Src/main.d \
./Src/steering_controller_mssc.d \
./Src/stm32f4xx_hal_msp.d \
./Src/stm32f4xx_it.d \
./Src/syscalls.d \
./Src/sysmem.d \
./Src/system_stm32f4xx.d \
./Src/usb_cdc_shell.d \
./Src/usb_device.d \
./Src/usbd_cdc_if.d \
./Src/usbd_conf.d \
./Src/usbd_desc.d \
./Src/vehicle_config.d 


# Each subdirectory must supply rules for building sources it contributes
Src/%.o Src/%.su Src/%.cyclo: ../Src/%.c Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m4 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F407xx -c -I../Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc -I../Drivers/STM32F4xx_HAL_Driver/Inc/Legacy -I../Middlewares/ST/STM32_USB_Device_Library/Core/Inc -I../Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc -I../Drivers/CMSIS/Device/ST/STM32F4xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Src

clean-Src:
	-$(RM) ./Src/board_io.cyclo ./Src/board_io.d ./Src/board_io.o ./Src/board_io.su ./Src/can_transport.cyclo ./Src/can_transport.d ./Src/can_transport.o ./Src/can_transport.su ./Src/chassis_app.cyclo ./Src/chassis_app.d ./Src/chassis_app.o ./Src/chassis_app.su ./Src/drive_controller_mssd.cyclo ./Src/drive_controller_mssd.d ./Src/drive_controller_mssd.o ./Src/drive_controller_mssd.su ./Src/ewm22_link.cyclo ./Src/ewm22_link.d ./Src/ewm22_link.o ./Src/ewm22_link.su ./Src/main.cyclo ./Src/main.d ./Src/main.o ./Src/main.su ./Src/steering_controller_mssc.cyclo ./Src/steering_controller_mssc.d ./Src/steering_controller_mssc.o ./Src/steering_controller_mssc.su ./Src/stm32f4xx_hal_msp.cyclo ./Src/stm32f4xx_hal_msp.d ./Src/stm32f4xx_hal_msp.o ./Src/stm32f4xx_hal_msp.su ./Src/stm32f4xx_it.cyclo ./Src/stm32f4xx_it.d ./Src/stm32f4xx_it.o ./Src/stm32f4xx_it.su ./Src/syscalls.cyclo ./Src/syscalls.d ./Src/syscalls.o ./Src/syscalls.su ./Src/sysmem.cyclo ./Src/sysmem.d ./Src/sysmem.o ./Src/sysmem.su ./Src/system_stm32f4xx.cyclo ./Src/system_stm32f4xx.d ./Src/system_stm32f4xx.o ./Src/system_stm32f4xx.su ./Src/usb_cdc_shell.cyclo ./Src/usb_cdc_shell.d ./Src/usb_cdc_shell.o ./Src/usb_cdc_shell.su ./Src/usb_device.cyclo ./Src/usb_device.d ./Src/usb_device.o ./Src/usb_device.su ./Src/usbd_cdc_if.cyclo ./Src/usbd_cdc_if.d ./Src/usbd_cdc_if.o ./Src/usbd_cdc_if.su ./Src/usbd_conf.cyclo ./Src/usbd_conf.d ./Src/usbd_conf.o ./Src/usbd_conf.su ./Src/usbd_desc.cyclo ./Src/usbd_desc.d ./Src/usbd_desc.o ./Src/usbd_desc.su ./Src/vehicle_config.cyclo ./Src/vehicle_config.d ./Src/vehicle_config.o ./Src/vehicle_config.su

.PHONY: clean-Src

