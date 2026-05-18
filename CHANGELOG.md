# Changelog

本项目从 `v1.0.0` 开始使用正式版本标签管理。

## v2.0.3

发布时间：2026-05-18

### 新增

- 新增 `steering_max_rpm` 配置项，固件、App 配置页、状态页、会话同步和设备档案同步已全部打通。
- `config_ack` / `status_ack` / `chassis_status` 新增 `steering_max_rpm` / `smr` 回传字段，App 可显示并回读当前最大转向输出 RPM。

### 修复

- 修复此前转向闭环最大输出只能在固件内写死、无法像驱动最大速度一样通过 App 保存到 Flash 的问题。
- Flash 配置版本升级到 `0x00070000`，旧 `0x00060000` 及更旧配置会自动导入；旧配置没有 `steering_max_rpm` 时默认补 `2000 RPM`。
- 编译产物已同步刷新：根目录 `STM32F407_Ackermann_Chassis_strict_mssc.hex` 和 `.bin` 已覆盖。

## v2.0.2

发布时间：2026-05-17

### 修复

- 修复机械转向闭环回不到中、微调时电机原地不动以及在死区边界来回拍打的问题。
  - 起因 1（卡死不动）：`STEERING_POSITION_MIN_RPM` 之前是 `12 RPM`，远低于 `MSSC` 控制器内部最低转速门限（参见《无刷电机伺服控制器 MSSC 通讯协议及寄存器说明 V1.0》第 2.4 节寄存器 `0x0051` "电机最小转速"，资料明确建议设置在 `200 RPM` 以下，实测整套驱控一体最低有效命令在 `150~200 RPM` 区间）。命令低于该门限会被控制器钳到 `0`，电机不动，闭环卡死。
  - 起因 2（边界拍打）：纯比例 + 单一死区 + 最低 RPM 兜底，会出现"刚跨进死区就停 → 过冲少许 → 又跨出死区被钳到 200 RPM 反向打回 → 又过冲"的 bang-bang 振荡。MSSC 最低门限越高，这个振荡幅度反而越大。
  - 修复：把闭环改成滞回控制（hysteresis）。新增两条门槛：`STEERING_HOLD_DEADBAND_DEG = 1.5°`（已 engaged 时，误差落进这条门就停下并脱开）和 `STEERING_ENGAGE_DEADBAND_DEG = 3.0°`（未 engaged 时，误差超过这条门才允许重新驱动电机）。两条门槛之差构成滞回带，吸收 MSSC 最低 RPM 必然带来的过冲，不会立刻反向回打。
  - 同时把 `STEERING_POSITION_MIN_RPM` 抬到 `200 RPM`，`CONTROL_MAX_STEER_RPM` 从 `80` 提到 `300`，`STEERING_POSITION_KP` 从 `4` 提到 `12`，让闭环在大误差时跑得快、小误差时不会被门限钳死。
- 修复闭环最低 RPM 方向判定的歧义：原来在 `|rpm_command| < MIN_RPM` 时按 `rpm_command` 自身符号取方向，KP·err 接近 0 但仍跨过死区时方向不可靠；现在统一按 `angle_error` 的符号决定方向。
- 修复点动校准（`steering_jog`）和单步测试（`steer_test`）会下发低于最低门限的 RPM 导致电机不动的问题；两条命令现在都会自动把非零幅值抬到 MSSC 最低有效转速。

### 新增

- 控制状态结构体新增 `steer_engaged` 字段表示滞回状态，反馈不新鲜或差速模式下强制脱开，恢复 / 重启 / 停车后从 false 重新进入。
- 校准点动幅值钳位从 `±200` 放开到 `±500`（最低门限本身就是 `200`，原范围会把所有命令直接挤到边界）。
- App 校准页"按住左转 / 按住右转 / 方向盘左打 / 方向盘右打"四个按钮的硬编码点动 RPM 由 `±60` 改为 `±200`，与 MSSC 最低有效转速一致，所见即所得。固件里仍然有"非零幅值不低于 MIN_RPM"的兜底，App 改动只是为了让显示和实际下发对齐，不再依赖固件兜底放大。

### 解决的问题

- 现在转向回中既不会"卡在偏离位置上不动"也不会"在中心位置反复抖打"，而是真的能稳定收敛并在到位后保持静止。
- 现在闭环路径、点动路径、单步测试路径的最低 RPM 行为完全一致，不会出现"App 测试转得动、闭环不转"的割裂情况。
- 编译产物已同步刷新：根目录 `STM32F407_Ackermann_Chassis_strict_mssc.hex` 和 `.bin` 已覆盖。

## v2.0.1

发布时间：2026-04-27

### 新增

- 固件新增 `CAN` 事务门控层：同一物理总线同一时刻只允许一个未完成读事务，读请求必须等匹配回包或超时后才能继续。
- 固件新增 `CAN` 错误闭环：接入 `ERROR_WARNING / ERROR_PASSIVE / BUSOFF / LAST_ERROR_CODE / ERROR`，并回传 `fault_code / fault_domain / fault_message_zh / can_recovery_active / can1_bus_off_count / can2_bus_off_count / can1_last_error_code / can2_last_error_code / outputs_locked_by_fault`。
- 机械转向控制新增“目标角 + 真实位置反馈”闭环，状态回传新增 `steer_target_angle_deg / steer_actual_angle_deg`。
- App 新增通信与安全故障中文弹窗，并在状态跃迁时只弹一次。
- 更新 `调试脚本/mssd_dual_wheel_can.ps1`、`调试脚本/mssc_steering_can.ps1` 和 `调试脚本/usb_can_rear_drive_steering_test.html`，统一改成按回复或超时串行的事务模型。

### 修复

- 修复旧版 `recovery_active` 处理里“恢复后自己把自己锁死，导致后续读写都排不进去”的风险。
- 修复 HTML 调试页事务队列在某次超时后可能整条 Promise 链失效、后续所有命令都被卡住的问题。
- 修复恢复后可能把“新高字 + 旧低字”拼成假新鲜速度 / 假新鲜转向位置的问题，现已要求重新获取完整反馈组合后才解除反馈故障。
- 修复 PowerShell 调试脚本读请求可能先把串口回包吞掉、后续等待函数反而读不到对应回复的问题。
- 修复 App 端部分新增安全弹窗中文文案乱码问题。

### 解决的问题

- 现在固件、App、HTML 调试页、PowerShell 调试脚本已经统一到同一套真实 CAN 事务模型上，不再是“固件按回复串行，工具还在盲发”的割裂状态。
- 现在 CAN 故障不再只是“看起来没回包”，而是能明确区分总线错误、回复超时、恢复中和输出锁停状态。
- 现在转向回中不再依赖自然停，而是基于真实位置反馈做闭环回中，安全边界比之前清晰得多。

## v1.0.2

发布时间：2026-04-27

### 新增

- 新增 `drive_max_rpm` 配置项，固件、App 配置页、状态页、会话同步和设备档案同步已全部打通。
- 新增 `调试脚本/usb_can_rear_drive_steering_test.html`，支持通过浏览器 `Web Serial` 分别连接驱动轮和转向 USB-CAN，做后驱双轮与前转向离线联调。
- `config_ack` / `status_ack` / `chassis_status` 新增 `drive_max_rpm` 回传字段，App 可直接显示当前最大驱动输出 RPM。

### 修复

- 修复此前最大驱动输出只能在固件内写死、无法通过 App 真正配置的问题。
- 修复旧版 Flash 配置结构中没有 `drive_max_rpm` 字段时，升级后无法平滑继承旧配置的问题；当前已兼容导入 `v1.0.1` 的配置结构。
- 重新核对并刷新当前 `Debug` 目录下的 `elf / hex / bin / map / list` 产物，用于本次 `v1.0.2` 标签归档。

### 解决的问题

- 现在最大驱动输出不再是隐藏常量，而是一个真实、可保存、可回读、可显示的配置参数。
- 现在可以不用 PowerShell，直接通过浏览器页面分别连接两个 USB-CAN 口，对后驱双轮和前转向做离线按钮联调。

## v1.0.1

发布时间：2026-04-27

### 新增

- 新增 `MSSD-60EHB_2D` 速度回读状态链路，其中右轮 `0x09/0x0A` 为手册明确地址，左轮已由 2026-04-27 的 USB-CAN 实测确认到 `0x14/0x15`。
- 固件新增 `vehicle_moving_command` 与 `vehicle_moving_actual` 双状态并存。
- 手机 App 新增左右驱动轮前进方向即时校正，点击后立即写入 F407 配置并同步影响目标下发与反馈符号。
- 更新 `调试脚本/mssd_dual_wheel_can.ps1`，支持候选回读寄存器轮询、单轮/双轮顺序测试、方向检查模式和停机后二次确认。
- `status_ack` / `chassis_status` 新增以下真实状态字段：
  - `vehicle_moving_command`
  - `vehicle_moving_actual`
  - `drive_feedback_valid`
  - `left_actual_rpm`
  - `right_actual_rpm`
  - `left_actual_rpm_valid`
  - `right_actual_rpm_valid`
  - `left_actual_rpm_age_ms`
  - `right_actual_rpm_age_ms`
- 手机 App 控制页新增“命令运动状态 / 实际运动状态 / 驱动反馈状态”显示。
- 手机 App 遥测区新增左右轮实际转速显示。

### 修复

- 修复此前 `vehicle_moving` 主要依赖目标转速命令的问题。
- 修复“命令已经回零，但车辆仍可能靠惯性滑行”时，单一状态无法准确表达的问题。
- 修复“急停解除时只看命令状态，不看真实轮速”带来的安全边界不清问题。
- 修复驱动轮物理正方向与控制逻辑正方向不一致时，必须重新改代码或手工换线才能校正的问题。
- 修复 USB-CAN 外部联调没有收到回包时，容易把“命令链路打通”和“真实速度回读打通”混为一谈的问题。
- 修复 F407 `Debug` 工程链接脚本仍指向失效绝对路径、且当前 ARM GCC 不支持 `-fcyclomatic-complexity` 时无法在本机重新编译的问题，现已可重新生成 `elf / hex / bin / map / list`。

### 解决的问题

- 现在固件可以区分：
  - 控制器是否还在被命令驱动
  - 车轮是否还在真实转动
- 现在 App 可以直接看到：
  - 左右轮目标转速
  - 左右轮实际转速
  - 当前运动锁到底来自命令侧还是实际反馈侧
- 现在远程急停解除会优先参考真实速度反馈，车辆仍有真实转速时会阻止直接解除。

### 当前策略

- `vehicle_moving_command`
  - 用于表达“最近是否仍有非零驱动目标命令”。
- `vehicle_moving_actual`
  - 用于表达“真实速度回读是否显示车轮仍在转动”。
- `vehicle_moving`
  - 作为兼容字段继续保留。
  - 当前为“实际运动优先，实际反馈缺失时回退到命令运动，并兼容矫正点动锁”的服务锁状态。

## v1.0.0

发布时间：2026-04-27

### 初始发布内容

- 发布 `STM32F407` 底盘主控工程。
- 发布 `uni-app` 手机控制 App 工程。
- 发布 `ATK-BLE04 BLE <-> UART` 透传控制主链路。
- 发布底盘参数配置、线性矫正、远程监视 / 接管、缓停 / 急停、本地踏板 / 本地挡位 / 硬件急停整套联调骨架。
- 发布完整项目资料、参考手册、调试脚本和仓库级 README。

### 已知边界

- 当时的 `vehicle_moving` 仍以目标转速命令保持逻辑为主。
- 真实驱动轮速度回读尚未接入到主状态机。
