from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription(
        [
            DeclareLaunchArgument(
                "port",
                default_value="/dev/serial/by-id/usb-STMicroelectronics_STM32_Virtual_ComPort_2089378D4152-if00",
            ),
            DeclareLaunchArgument("cmd_vel_topic", default_value="/cmd_vel"),
            DeclareLaunchArgument("vehicle_status_topic", default_value="/vehicle_status"),
            DeclareLaunchArgument("vehicle_frame_id", default_value="base_link"),
            DeclareLaunchArgument("status_poll_rate_hz", default_value="10.0"),
            DeclareLaunchArgument("wheel_diameter_m", default_value="0.25"),
            DeclareLaunchArgument("drive_max_rpm", default_value="150.0"),
            DeclareLaunchArgument("min_drive_rpm", default_value="100.0"),
            DeclareLaunchArgument("linear_deadband_mps", default_value="0.02"),
            DeclareLaunchArgument("max_angular_speed_radps", default_value="1.0"),
            DeclareLaunchArgument("wheelbase_m", default_value="1.72"),
            DeclareLaunchArgument("max_steering_angle_deg", default_value="30.0"),
            DeclareLaunchArgument("min_steer_axis", default_value="80"),
            DeclareLaunchArgument("steer_sign", default_value="1.0"),
            Node(
                package="drive_by_wire_chassis_bridge",
                executable="cmd_vel_usb_bridge",
                name="cmd_vel_usb_bridge",
                output="screen",
                parameters=[
                    {
                        "port": LaunchConfiguration("port"),
                        "cmd_vel_topic": LaunchConfiguration("cmd_vel_topic"),
                        "vehicle_status_topic": LaunchConfiguration("vehicle_status_topic"),
                        "vehicle_frame_id": LaunchConfiguration("vehicle_frame_id"),
                        "status_poll_rate_hz": LaunchConfiguration("status_poll_rate_hz"),
                        "wheel_diameter_m": LaunchConfiguration("wheel_diameter_m"),
                        "drive_max_rpm": LaunchConfiguration("drive_max_rpm"),
                        "min_drive_rpm": LaunchConfiguration("min_drive_rpm"),
                        "linear_deadband_mps": LaunchConfiguration("linear_deadband_mps"),
                        "max_angular_speed_radps": LaunchConfiguration("max_angular_speed_radps"),
                        "wheelbase_m": LaunchConfiguration("wheelbase_m"),
                        "max_steering_angle_deg": LaunchConfiguration("max_steering_angle_deg"),
                        "min_steer_axis": LaunchConfiguration("min_steer_axis"),
                        "steer_sign": LaunchConfiguration("steer_sign"),
                    }
                ],
            ),
        ]
    )
