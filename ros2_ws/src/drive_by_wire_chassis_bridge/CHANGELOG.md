# Changelog

## 2.1.0

- Add ROS2 Humble `cmd_vel` to STM32 USB CDC bridge package.
- Subscribe to `geometry_msgs/msg/Twist` on `/cmd_vel`.
- Convert `linear.x` from m/s to wheel RPM using the configured wheel diameter, then to the STM32 throttle axis.
- Convert `angular.z` to the STM32 steering axis while preserving the firmware's App-calibrated center and left/right limits.
- Send newline-delimited `ros_control` JSON frames over USB CDC.
- Add `drive_by_wire_chassis_bridge/msg/VehicleStatus`.
- Publish STM32 chassis feedback on `/vehicle_status`.
- Include gear, takeover state, stop/fault flags, target/feedback RPM, steering angles, CAN/remote counters, BLE state, and last ROS command fields in `VehicleStatus`.
- Add launch file defaults for the current STM32 virtual serial port and 0.25 m wheel diameter.
- Add safety behavior: remote takeover on connect, neutral output on stale `/cmd_vel`, and MONITOR mode on shutdown.
