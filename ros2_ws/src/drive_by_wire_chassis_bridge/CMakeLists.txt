cmake_minimum_required(VERSION 3.8)
project(drive_by_wire_chassis_bridge)

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)
find_package(std_msgs REQUIRED)

rosidl_generate_interfaces(${PROJECT_NAME}
  "msg/VehicleStatus.msg"
  DEPENDENCIES std_msgs
)

install(PROGRAMS
  drive_by_wire_chassis_bridge/cmd_vel_usb_bridge.py
  DESTINATION lib/${PROJECT_NAME}
  RENAME cmd_vel_usb_bridge
)

install(DIRECTORY launch
  DESTINATION share/${PROJECT_NAME}
)

install(FILES README.md CHANGELOG.md
  DESTINATION share/${PROJECT_NAME}
)

ament_export_dependencies(rosidl_default_runtime)

ament_package()
