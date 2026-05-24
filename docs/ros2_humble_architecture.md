# ROS2 Humble 上位机架构

本文档描述 `v2.1.0` 新增的 ROS2 上位机层。当前 ROS2 环境为：

- Ubuntu 22.04
- ROS2 Humble
- STM32 连接方式：USB CDC
- 控制入口：`/cmd_vel`
- 底盘反馈：`/vehicle_status`

## 总体结构

```mermaid
flowchart LR
    A["导航 / 遥控 / 测试节点"] -->|"/cmd_vel<br/>geometry_msgs/Twist"| B["cmd_vel_usb_bridge<br/>ROS2 Humble 节点"]

    B -->|USB CDC JSON<br/>ros_control| C["STM32F407 底盘主控"]
    C -->|USB CDC JSON<br/>status_ack / chassis_status| B

    B -->|"/vehicle_status<br/>drive_by_wire_chassis_bridge/VehicleStatus"| D["上位机 UI / 调试节点 / 自动驾驶状态机"]

    C --> E["MSSD-60EHB_2D<br/>后轮驱动控制器"]
    C --> F["MSSC<br/>前轮转向控制器"]
    C --> G["本地踏板 / 挡位 / 急停 IO"]
```

ROS2 层只有一个核心节点：

```text
drive_by_wire_chassis_bridge/cmd_vel_usb_bridge
```

它负责两件事：

1. 把 ROS2 标准速度话题 `/cmd_vel` 转成 STM32 能理解的 USB JSON 控制帧。
2. 把 STM32 回读的 JSON 状态转成 ROS2 强类型底盘反馈话题 `/vehicle_status`。

## 全链路分层图

```mermaid
flowchart TB
    subgraph Host["Ubuntu 22.04 上位机 / ROS2 Humble"]
        Nav["导航栈 / 键盘遥控 / 测试脚本"]
        CmdTopic["/cmd_vel<br/>geometry_msgs/msg/Twist"]
        BridgeNode["cmd_vel_usb_bridge"]
        StatusTopic["/vehicle_status<br/>VehicleStatus"]
        DebugUi["调试终端 / 上位机 UI / 自动驾驶状态机"]
    end

    subgraph UsbLink["USB CDC 串口链路"]
        UsbJson["换行分隔 JSON<br/>ros_control / get_status / status_ack"]
    end

    subgraph Mcu["STM32F407 主控固件"]
        UsbShell["usb_cdc_shell"]
        ChassisApp["chassis_app<br/>命令解析 / 状态机 / 安全仲裁"]
        Config["vehicle_config<br/>App 标定 / Flash 参数"]
        BoardIo["board_io<br/>踏板 / 挡位 / 硬件急停"]
        CanTransport["can_transport<br/>CAN 事务门控 / 超时 / 恢复"]
    end

    subgraph Actuators["底盘执行器"]
        Drive["MSSD-60EHB_2D<br/>左右后驱轮"]
        Steer["MSSC<br/>前轮机械转向"]
        Handwheel["MSSC<br/>线性方向盘反馈"]
    end

    Nav --> CmdTopic --> BridgeNode
    BridgeNode --> StatusTopic --> DebugUi
    BridgeNode <--> UsbJson
    UsbJson <--> UsbShell
    UsbShell <--> ChassisApp
    Config --> ChassisApp
    BoardIo --> ChassisApp
    ChassisApp <--> CanTransport
    CanTransport <--> Drive
    CanTransport <--> Steer
    CanTransport <--> Handwheel
```

## ROS2 节点内部结构

```mermaid
flowchart TB
    subgraph ROS2["Ubuntu 22.04 / ROS2 Humble"]
        CMD["/cmd_vel<br/>geometry_msgs/msg/Twist"]
        STATUS["/vehicle_status<br/>VehicleStatus"]
        PARAMS["launch 参数<br/>wheel_diameter_m<br/>drive_max_rpm<br/>min_drive_rpm<br/>steer_sign<br/>status_poll_rate_hz"]
    end

    subgraph Bridge["cmd_vel_usb_bridge.py"]
        RX["接收 /cmd_vel"]
        DRIVE["linear.x<br/>m/s -> wheel RPM -> throttle axis"]
        STEER["angular.z<br/>rad/s -> steer axis"]
        SAFE["超时保护<br/>cmd_vel stale -> N档 + 0油门 + 回中"]
        TX["发送 ros_control JSON"]
        POLL["周期发送 get_status"]
        PARSE["解析 status_ack / chassis_status"]
        PUB["发布 VehicleStatus"]
    end

    subgraph STM32["STM32F407"]
        USB["USB CDC JSON"]
        CONTROL["底盘控制状态机<br/>App 标定中心 / 左右极限 / 安全仲裁"]
        CAN["CAN 控制执行器"]
    end

    CMD --> RX
    PARAMS --> DRIVE
    PARAMS --> STEER
    RX --> DRIVE
    RX --> STEER
    RX --> SAFE
    DRIVE --> TX
    STEER --> TX
    SAFE --> TX
    TX --> USB

    POLL --> USB
    USB --> PARSE
    PARSE --> PUB
    PUB --> STATUS

    USB --> CONTROL
    CONTROL --> CAN
```

## STM32 固件内部结构

```mermaid
flowchart TB
    subgraph Inputs["输入来源"]
        UsbRx["USB CDC JSON<br/>App / ROS2 / 调试命令"]
        UartRx["EWM22 / BLE UART<br/>App 控制帧"]
        LocalIo["本地 IO<br/>踏板 / 挡位 / 硬件急停"]
        CanFeedback["CAN 反馈<br/>轮速 / 转向位置 / 方向盘"]
    end

    subgraph Firmware["STM32F407_Ackermann_Chassis 固件"]
        UsbShell["usb_cdc_shell<br/>按行接收 / 回复"]
        CommandDispatch["process_line_with_reply_target<br/>命令分发"]
        RemoteParser["ewm22_link<br/>app_control / usb_control / ros_control"]
        ConfigStore["vehicle_config<br/>drive_max_rpm / steering_max_rpm<br/>转向中心与左右极限"]
        BoardIo["board_io<br/>ADC 踏板 / GPIO 挡位 / 急停"]
        ControlState["control_state<br/>控制权 / 目标速度 / 转向目标"]
        Safety["安全仲裁<br/>故障锁停 / 软停 / 急停 / 超时"]
        MotionCalc["运动解算<br/>后驱左右目标 RPM<br/>App 标定转向目标"]
        SteerLoop["转向闭环<br/>位置反馈 / 滞回 / 最小 RPM"]
        StatusReply["reply_status<br/>status_ack / chassis_status"]
        CanTransport["can_transport<br/>串行读事务 / 写确认 / BUSOFF 恢复"]
    end

    subgraph Controllers["CAN 控制器"]
        Mssd["drive_controller_mssd<br/>MSSD-60EHB_2D"]
        MsscSteer["steering_controller_mssc<br/>前轮 MSSC"]
        MsscWheel["steering_controller_mssc<br/>方向盘 MSSC"]
    end

    UsbRx --> UsbShell --> CommandDispatch
    UartRx --> RemoteParser --> ControlState
    CommandDispatch --> RemoteParser
    CommandDispatch --> ConfigStore
    CommandDispatch --> StatusReply
    LocalIo --> BoardIo --> Safety
    ConfigStore --> MotionCalc
    ConfigStore --> SteerLoop
    ControlState --> Safety --> MotionCalc
    MotionCalc --> SteerLoop
    MotionCalc --> CanTransport
    SteerLoop --> CanTransport
    CanTransport <--> Mssd
    CanTransport <--> MsscSteer
    CanTransport <--> MsscWheel
    Mssd --> CanFeedback
    MsscSteer --> CanFeedback
    MsscWheel --> CanFeedback
    CanFeedback --> ControlState
    CanFeedback --> StatusReply
    Safety --> StatusReply
    BoardIo --> StatusReply
    ConfigStore --> StatusReply
    StatusReply --> UsbShell
```

STM32 固件的关键边界：

- `usb_cdc_shell` 只负责 USB CDC 收发和按行组包。
- `chassis_app` 负责 JSON 命令分发、控制权、安全状态、运动目标和状态回包。
- `vehicle_config` 保存 App 标定和运行参数，例如 `drive_max_rpm`、`steering_max_rpm`、转向中心点、左极限和右极限。
- `can_transport` 负责 CAN 事务门控、回复匹配、超时统计和恢复。
- `drive_controller_mssd` 面向后轮驱动控制器 `MSSD-60EHB_2D`。
- `steering_controller_mssc` 面向前轮转向控制器和线性方向盘控制器 `MSSC`。

## STM32 命令分发图

```mermaid
flowchart LR
    JsonLine["USB / UART JSON 行"] --> Dispatch["命令分发"]

    Dispatch --> StatusCmd["get_status<br/>help"]
    Dispatch --> ConfigCmd["set_app_config<br/>save_app_config"]
    Dispatch --> ModeCmd["set_remote_mode<br/>set_control_enable"]
    Dispatch --> ControlCmd["app_control<br/>usb_control<br/>ros_control"]
    Dispatch --> TestCmd["drive_test<br/>steer_test<br/>stop_all"]
    Dispatch --> CalibCmd["steering_jog<br/>calibration / linear steering"]

    StatusCmd --> StatusAck["status_ack / capability_status"]
    ConfigCmd --> ConfigAck["config_ack"]
    ModeCmd --> StatusAck
    ControlCmd --> RemoteFrame["remote frame<br/>gear / throttle / steer"]
    TestCmd --> DirectCan["直接测试 CAN 输出"]
    CalibCmd --> ConfigStore["App 标定 / 方向盘校准参数"]

    RemoteFrame --> Safety["安全仲裁"]
    Safety --> Motion["运动与转向目标"]
    Motion --> CanOut["CAN 输出"]
```

ROS2 桥接层正常使用的是：

- `get_status`
- `set_remote_mode`
- `ros_control`

调试或现场排障时才会使用：

- `drive_test`
- `steer_test`
- `stop_all`
- `set_app_config`

## 控制话题 `/cmd_vel`

订阅话题：

```text
/cmd_vel
geometry_msgs/msg/Twist
```

当前使用字段：

- `linear.x`：车辆前后速度，单位 `m/s`
- `angular.z`：车辆转向角速度，单位 `rad/s`

当前不使用字段：

- `linear.y`
- `linear.z`
- `angular.x`
- `angular.y`

桥接节点会把 `/cmd_vel` 转成 STM32 JSON：

```json
{"cmd":"ros_control","seq":1,"gear":"D","throttle":300,"steer":-120,"aux_x":0,"aux_y":0,"buttons":0}
```

档位由 `throttle` 自动决定：

- `throttle > 0`：`gear = D`
- `throttle < 0`：`gear = R`
- `throttle == 0`：`gear = N`

## 速度换算

ROS2 输入是标准速度，单位为 `m/s`。STM32 驱动轮控制使用 RPM 量级，所以桥接节点做如下换算：

```text
target_wheel_rpm = linear.x / (pi * wheel_diameter_m) * 60
throttle_axis = target_wheel_rpm / drive_max_rpm * 1000
```

当前实车轮径默认：

```text
wheel_diameter_m = 0.25
```

因此：

```text
100 RPM ≈ 1.31 m/s
150 RPM ≈ 1.96 m/s
200 RPM ≈ 2.62 m/s
```

相关参数在 launch 中修改：

```bash
ros2 launch drive_by_wire_chassis_bridge cmd_vel_usb_bridge.launch.py \
  wheel_diameter_m:=0.25 \
  drive_max_rpm:=150.0 \
  min_drive_rpm:=100.0
```

## 控制换算细节图

```mermaid
flowchart LR
    Twist["/cmd_vel<br/>linear.x / angular.z"] --> Split["桥接节点拆分"]

    Split --> Linear["linear.x<br/>m/s"]
    Linear --> Rpm["target_wheel_rpm<br/>m/s -> RPM"]
    Rpm --> MinRpm["min_drive_rpm 兜底"]
    MinRpm --> Throttle["throttle_axis<br/>-1000 ~ 1000"]
    Throttle --> Gear["gear<br/>D / N / R"]

    Split --> Angular["angular.z<br/>rad/s"]
    Angular --> SteerNorm["归一化转向请求"]
    SteerNorm --> MinSteer["min_steer_axis 兜底"]
    MinSteer --> SteerAxis["steer_axis<br/>-1000 ~ 1000"]

    Gear --> Json["ros_control JSON"]
    Throttle --> Json
    SteerAxis --> Json
    Json --> Stm32["STM32 App 标定坐标系<br/>中心 / 左极限 / 右极限"]
```

## 转向换算

`angular.z` 会被转成 STM32 的 `steer` 轴值：

```text
steer axis range = -1000 ~ 1000
```

STM32 端仍然使用 App 标定的转向中心点、左极限和右极限：

- `steer = 0`：回到 App 标定中心点
- `steer = -1000`：转向到 App 标定左侧范围
- `steer = 1000`：转向到 App 标定右侧范围

如果现场方向相反，使用：

```bash
ros2 launch drive_by_wire_chassis_bridge cmd_vel_usb_bridge.launch.py \
  steer_sign:=-1.0
```

## 控制权和安全仲裁图

```mermaid
flowchart TB
    Cmd["控制命令<br/>App / USB / ROS2"] --> Fresh["远程帧是否新鲜"]
    Fresh --> Mode{"remote_mode"}
    Mode -->|"MONITOR"| Local["本地控制优先<br/>踏板 / 挡位"]
    Mode -->|"TAKEOVER"| Remote["远程接管控制"]

    Local --> Safety
    Remote --> Safety["安全仲裁"]
    Board["本地硬件急停"] --> Safety
    Soft["软停 / 急停 latch"] --> Safety
    Fault["CAN 故障 / 反馈超时 / 输出锁停"] --> Safety
    Stale["remote timeout / cmd_vel stale"] --> Safety

    Safety --> Allow{"outputs_allowed"}
    Allow -->|"false"| Stop["停止输出<br/>drive safety stop<br/>steer neutral"]
    Allow -->|"true"| Calc["计算目标<br/>左右轮 RPM / 转向目标"]
    Calc --> Can["CAN 下发"]
    Stop --> Status["状态回包<br/>fault_code / olf / ssa / esa / hea"]
    Can --> Status
```

这个图里需要特别注意：

- ROS2 节点退出时会发送中位控制帧，并把 STM32 切回 `MONITOR`。
- `/cmd_vel` 超时后，ROS2 节点会继续发送安全中位帧。
- STM32 端仍然保留本地硬件急停、软停、急停、CAN 故障锁停和远程帧新鲜度判断。

## 底盘反馈话题 `/vehicle_status`

发布话题：

```text
/vehicle_status
drive_by_wire_chassis_bridge/msg/VehicleStatus
```

查看命令：

```bash
ros2 topic echo /vehicle_status
ros2 interface show drive_by_wire_chassis_bridge/msg/VehicleStatus
```

主要反馈字段：

- `gear`：STM32 当前反馈档位
- `control_owner`：当前控制来源
- `remote_mode`：`MONITOR` 或 `TAKEOVER`
- `remote_takeover`：远程是否接管
- `control_enabled`：底盘输出是否允许
- `outputs_locked_by_fault`：是否因故障锁停
- `soft_stop_active`：软停状态
- `emergency_stop_active`：急停状态
- `hardware_estop_active`：硬件急停状态
- `fault_code` / `fault_domain`：故障码和故障域
- `left_target_rpm` / `right_target_rpm`：左右驱动轮目标 RPM
- `left_wheel_rpm` / `right_wheel_rpm`：左右驱动轮实际反馈 RPM
- `linear_speed_mps`：由实际 RPM 反算出的线速度
- `steering_target_raw`：转向目标原始值
- `steering_feedback_raw`：转向反馈原始值
- `remote_source`：远程来源，例如 `USB_JSON`
- `ble_connected`：BLE 是否连接
- `last_cmd_gear`：ROS2 节点最近一次下发的档位
- `last_cmd_throttle_axis`：ROS2 节点最近一次下发的油门轴值
- `last_cmd_steer_axis`：ROS2 节点最近一次下发的转向轴值
- `cmd_vel_stale`：是否因为 `/cmd_vel` 超时进入安全中位命令
- `raw_json`：STM32 原始 JSON 状态包

反馈速度换算：

```text
linear_speed_mps = average(left_wheel_rpm, right_wheel_rpm) * pi * wheel_diameter_m / 60
```

## 底盘反馈链路图

```mermaid
flowchart LR
    DriveFb["MSSD 实际轮速<br/>lar / rar"] --> StatusBuilder["STM32 reply_status"]
    SteerFb["MSSC 转向反馈<br/>sf / sfa / sta / saa"] --> StatusBuilder
    IoFb["踏板 / 挡位 / 急停 IO<br/>thr / brk / g / hea"] --> StatusBuilder
    FaultFb["故障与 CAN 状态<br/>fc / fd / dcf / scf / olf"] --> StatusBuilder
    RemoteFb["远程状态<br/>rm / rto / rs / rv / ra"] --> StatusBuilder

    StatusBuilder --> JsonStatus["status_ack JSON"]
    JsonStatus --> RosParser["ROS2 状态解析"]
    RosParser --> VehicleStatus["/vehicle_status<br/>VehicleStatus"]

    VehicleStatus --> Ui["调试 UI / ros2 topic echo"]
    VehicleStatus --> Autonomy["自动驾驶状态机"]
    VehicleStatus --> Logger["日志与故障记录"]
```

STM32 状态字段到 ROS2 字段的核心映射：

| STM32 JSON 字段 | ROS2 `VehicleStatus` 字段 | 含义 |
| --- | --- | --- |
| `g` | `gear` | STM32 当前反馈档位 |
| `rm` | `remote_mode` | `MONITOR` / `TAKEOVER` |
| `rto` | `remote_takeover` | 远程是否接管 |
| `fc` / `fd` | `fault_code` / `fault_domain` | 故障码和故障域 |
| `lr` / `rr` | `left_target_rpm` / `right_target_rpm` | 左右轮目标 RPM |
| `lar` / `rar` | `left_wheel_rpm` / `right_wheel_rpm` | 左右轮实际反馈 RPM |
| `str` / `sf` | `steering_target_raw` / `steering_feedback_raw` | 转向目标和反馈原始值 |
| `sta` / `saa` | `steering_target_angle_deg` / `steering_actual_angle_deg` | 转向目标角和实际角 |
| `rs` | `remote_source` | 远程来源，例如 `USB_JSON` |
| `ble_connected` | `ble_connected` | BLE 连接状态 |

## 控制权和安全行为

```mermaid
sequenceDiagram
    participant ROS as ROS2 cmd_vel_usb_bridge
    participant STM as STM32F407
    participant CAN as CAN 执行器

    ROS->>STM: get_status
    STM-->>ROS: status_ack
    ROS->>STM: set_remote_mode TAKEOVER
    loop publish_rate_hz
        ROS->>STM: ros_control JSON
        STM->>CAN: drive / steer command
    end
    loop status_poll_rate_hz
        ROS->>STM: get_status
        STM-->>ROS: status_ack
        ROS-->>ROS: publish /vehicle_status
    end
    ROS->>STM: shutdown neutral command
    ROS->>STM: set_remote_mode MONITOR
```

节点启动后默认进入 `TAKEOVER`，让 ROS2 上位机接管控制。节点退出时会发送中位安全命令，并让 STM32 回到 `MONITOR`。

如果只想查看底盘反馈、不想让 ROS2 接管：

```bash
ros2 launch drive_by_wire_chassis_bridge cmd_vel_usb_bridge.launch.py \
  enable_remote_takeover:=false
```

## 文件位置

ROS2 桥接包：

```text
ros2_ws/src/drive_by_wire_chassis_bridge
```

节点源码：

```text
ros2_ws/src/drive_by_wire_chassis_bridge/drive_by_wire_chassis_bridge/cmd_vel_usb_bridge.py
```

消息定义：

```text
ros2_ws/src/drive_by_wire_chassis_bridge/msg/VehicleStatus.msg
```

启动文件：

```text
ros2_ws/src/drive_by_wire_chassis_bridge/launch/cmd_vel_usb_bridge.launch.py
```
