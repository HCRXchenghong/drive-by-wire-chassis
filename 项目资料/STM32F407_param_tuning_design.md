# STM32F407 Parameter Tuning Design

## Reuse Verdict

The old `STM32F407.ioc` can be reused.

It already had:

- `USB FS` device mode with `CDC`
- `USART1` on `PA9/PA10`
- `USART2` on `PA2/PA3`
- `CAN1` on `PB8/PB9`
- `CAN2` on `PB12/PB13`
- `ADC1` on `PA0/PA1`

It needed a few fixes:

- complete `ADC1` regular conversion sequence for both `PA0` and `PA1`
- add `PB10` pull-down for the gear input
- add pin labels for easier maintenance
- make UART baud rates explicit

## IO Mapping

Recommended mapping for this project:

- `PA11 / PA12`
  - `USB FS DM / DP`
  - used as virtual COM port for PC parameter tuning
- `PA9 / PA10`
  - `USART1 TX / RX`
  - EWM22 module
- `PA2 / PA3`
  - `USART2 TX / RX`
  - expansion UART
- `PB9 / PB8`
  - `CAN1 TX / RX`
  - drive bus
- `PB13 / PB12`
  - `CAN2 TX / RX`
  - steering bus
- `PA0`
  - throttle analog input
- `PA1`
  - brake analog input
- `PB10`
  - gear input
  - high level = `R`
  - low level = `D`
  - external `10k` pull-down recommended

## Vehicle Parameters

The parameter block should be fixed and versioned.

```c
typedef enum
{
    DRIVE_MODE_FWD  = 0,
    DRIVE_MODE_RWD  = 1,
    DRIVE_MODE_AWD  = 2,
    DRIVE_MODE_DIFF = 3,
} drive_mode_t;

typedef struct
{
    uint16_t front_track_mm;
    uint16_t wheelbase_mm;
    uint16_t rear_track_mm;
    uint8_t  drive_mode;
    uint8_t  drive_controller_type;
    uint8_t  steering_controller_type;
    uint8_t  reserved;
    uint32_t version;
} vehicle_config_t;
```

Recommended fixed controller IDs:

- `drive_controller_type = 1`
  - `MSSD-60EHB_2D`
- `steering_controller_type = 1`
  - `MSSC`

## PC Tuning Interface

Recommended first-stage solution:

- expose `USB CDC` as a virtual COM port
- use a simple line-based JSON protocol
- first debug with a serial tool
- later replace with a custom PC GUI if needed

Example commands:

```json
{"cmd":"get_config"}
{"cmd":"set_config","front_track_mm":1280,"wheelbase_mm":1720,"rear_track_mm":1260,"drive_mode":"AWD"}
{"cmd":"save_config"}
{"cmd":"get_status"}
```

Example replies:

```json
{"ok":true,"front_track_mm":1280,"wheelbase_mm":1720,"rear_track_mm":1260,"drive_mode":"AWD","drive_controller_type":"MSSD-60EHB_2D","steering_controller_type":"MSSC"}
{"ok":true,"saved":true}
{"ok":true,"gear":"D","throttle_raw":1320,"brake_raw":18}
```

## Runtime Control Split

Suggested software split:

- `usb_cdc_cli`
  - receive and parse PC tuning commands
- `vehicle_config`
  - hold current geometry and mode
  - load/save from Flash
- `io_sample`
  - sample `PA0` and `PA1`
  - read `PB10`
- `drive_bus`
  - CAN1 output to `MSSD-60EHB_2D`
- `steer_bus`
  - CAN2 output to `MSSC`
- `vehicle_kinematics`
  - convert steering and speed requests into left/right wheel targets

## Recommended Next Firmware Steps

After generating code from CubeMX, implement in this order:

1. verify `USB CDC` enumeration on the PC
2. verify `ADC1` reads both `PA0` and `PA1`
3. verify `PB10` gear input state changes
4. add a `get_status` USB command
5. add `set_config / get_config / save_config`
6. add `CAN1` drive controller wrapper for `MSSD-60EHB_2D`
7. add `CAN2` steering controller wrapper for `MSSC`
8. add drivetrain mode dispatch:
   - `FWD`
   - `RWD`
   - `AWD`
   - `DIFF`

## Important Note

`USB CDC` is the easiest way to get parameter tuning working fast.

That means the PC side does not need a custom driver first. A serial terminal or a small Python GUI can already tune:

- front track
- wheelbase
- rear track
- drivetrain mode
