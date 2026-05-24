# drive_by_wire_chassis_bridge

Version: 2.1.0

ROS2 Humble bridge for the STM32F407 drive-by-wire chassis firmware.

The firmware already accepts newline-delimited JSON over USB CDC:

```json
{"cmd":"set_remote_mode","mode":"TAKEOVER"}
{"cmd":"ros_control","seq":1,"gear":"D","throttle":300,"steer":-120,"aux_x":0,"aux_y":0,"buttons":0}
```

This package subscribes to `geometry_msgs/msg/Twist` on `/cmd_vel` and sends
`ros_control` frames to the STM32 virtual serial port at 20 Hz.

It also polls the STM32 status JSON and publishes chassis feedback on
`/vehicle_status` as `drive_by_wire_chassis_bridge/msg/VehicleStatus`.

`linear.x` is converted using the real wheel diameter:

```text
target_wheel_rpm = linear.x / (pi * wheel_diameter_m) * 60
throttle = target_wheel_rpm / drive_max_rpm * 1000
```

With a 0.25 m wheel and STM32 `drive_max_rpm=150`, full throttle is about
1.96 m/s. For example, `linear.x=0.18 m/s` is only about 13.75 RPM.

The current vehicle needs about 150 RPM before the rear hub motors visibly
start moving, but `min_drive_rpm` defaults to 100 for safer bench testing. Any
`linear.x` above `linear_deadband_mps` but below that threshold is lifted to
100 RPM. Raise `min_drive_rpm` to 150 only when you want the wheel to reliably
start. The STM32 firmware already lifts steering motor commands to at least
200 RPM; the bridge also lifts small non-zero steer requests to
`min_steer_axis=80` so the firmware target angle clears its engage deadband.

Important: with STM32 `drive_max_rpm=150`, `min_drive_rpm=100` maps to about
67% throttle and about 1.31 m/s with a 0.25 m wheel.

## Build

```bash
cd /home/robot/ros2_ws
colcon build --packages-select drive_by_wire_chassis_bridge --symlink-install
source install/setup.bash
```

## Serial Permission

The STM32 currently enumerates as:

```text
/dev/ttyACM0
/dev/serial/by-id/usb-STMicroelectronics_STM32_Virtual_ComPort_2089378D4152-if00
```

On Ubuntu, `/dev/ttyACM0` is owned by `root:dialout`. Add the ROS user to
`dialout`, then log out and back in:

```bash
sudo usermod -aG dialout robot
```

Temporary test-only alternative:

```bash
sudo chmod a+rw /dev/ttyACM0
```

## Run

```bash
source /home/robot/ros2_ws/install/setup.bash
ros2 launch drive_by_wire_chassis_bridge cmd_vel_usb_bridge.launch.py
```

Useful launch overrides:

```bash
ros2 launch drive_by_wire_chassis_bridge cmd_vel_usb_bridge.launch.py \
  port:=/dev/ttyACM0 \
  vehicle_status_topic:=/vehicle_status \
  status_poll_rate_hz:=10.0 \
  wheel_diameter_m:=0.25 \
  drive_max_rpm:=150.0 \
  min_drive_rpm:=100.0 \
  linear_deadband_mps:=0.02 \
  max_angular_speed_radps:=1.0 \
  wheelbase_m:=1.72 \
  max_steering_angle_deg:=30.0 \
  min_steer_axis:=80 \
  steer_sign:=1.0
```

If steering is reversed, run with `steer_sign:=-1.0`.

Check chassis feedback:

```bash
ros2 topic echo /vehicle_status
ros2 interface show drive_by_wire_chassis_bridge/msg/VehicleStatus
```

The status message includes STM32 control mode, gear, takeover state,
soft-stop/emergency-stop state, controller faults, left/right target RPM,
left/right feedback RPM, estimated linear speed, steering target/feedback,
CAN and remote communication counters, BLE state, the last ROS command sent by
the bridge, and the original raw JSON status line.

## Safety Behavior

- On connect, the node sends `get_status` and `set_remote_mode=TAKEOVER`.
- If `/cmd_vel` stops for `cmd_vel_timeout_sec`, it sends neutral frames.
- On shutdown, it sends one neutral frame and returns the STM32 to `MONITOR`.
