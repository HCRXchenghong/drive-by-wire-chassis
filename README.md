# 线控底盘项目 README

## 1. 项目定位

本项目是一套面向线控底盘的真实联调工程，不是纯 UI 演示，也不是只做单个控制器测试的临时脚本集合。

当前目标是把下面这条链路真正打通：

1. 手机 `uni-app` 蓝牙 App 作为上位机。
2. `ATK-BLE04` 作为 `BLE <-> UART` 透传桥，广播名建议统一为 `infinite-robot-001~999`。
3. `STM32F407` 作为底盘主控。
4. `MSSD-60EHB_2D` 作为双驱动轮控制器。
5. `MSSC` 作为机械转向电机控制器。
6. 第二个 `MSSC` 作为线性方向盘控制器。
7. 本地踏板、本地前进/后退挡位、本地硬件急停与手机 App 共同参与控制权仲裁。

项目当前强调三件事：

1. `App` 和 `STM32` 的协议字段、状态机、控制模式要互相对应。
2. 所有关键状态尽量来自真实回包、真实 IO、真实配置，而不是伪造 mock。
3. 安全优先，停止类命令必须高于本地和远程的一般驱动命令。

## 1.1 当前 GitHub 发布状态

当前 GitHub 仓库的正式发布版本是：

- `v1.0.0`

这个 `v1.0.0` 不是早期空壳版本，而是当前可以烧录和现场联调的完整基线，已经包含：

- `MSSD` 左右驱动轮目标转速下发与真实速度回读。
- `drive_max_rpm` 最大驱动输出配置、保存、回读和 App 显示。
- CAN 事务门控、故障锁停、自动恢复和中文故障状态回传。
- 机械转向位置反馈闭环与线性方向盘检测。
- 本地踏板、本地挡位、App 急停 / 缓停、本地硬件急停。
- 当前现场接线版本的 `PB11` 硬件急停高电平有效逻辑。

根目录保留当前可烧录文件：

- `STM32F407_Ackermann_Chassis_strict_mssc.hex`
- `STM32F407_Ackermann_Chassis_strict_mssc.bin`

当前 `hex` 文件 SHA256：

```text
41885C5FD3E7C1A75657BA0B800DF21C4FDB85B3249CF93B62BDEEC74E992024
```

说明：历史开发过程中曾经出现过 `v1.0.1 / v1.0.2 / v2.0.1` 这些本地或旧远端标签名，但当前 GitHub 发布以 `v1.0.0` 为准。

---

## 2. 当前保留目录

根目录当前保留的内容，都是后续联调仍然有价值的部分：

- `STM线控控制文件夹/STM32F407_Ackermann_Chassis`
  - 主控固件工程。
- `UniApp_WireControl_MobileApp`
  - 手机 App 工程，使用 `uni-app / HBuilderX`。
- `ArduinoNano_EWM22_LoRa_HandController`
  - 未来可复用的 Nano + LoRa 手柄预留工程。
- `参考文件`
  - EWM22、E22、控制器手册 PDF 与文本提取。
- `项目资料`
  - 旧版 IOC、设计说明等仍可参考的资料。
- `调试脚本`
  - 保留驱动轮控制器、转向控制器的离线联调脚本；其中 `mssd_dual_wheel_can.ps1`、`mssc_steering_can.ps1`、`usb_can_rear_drive_steering_test.html` 都已经补齐“按回复或超时串行”的 CAN 读事务模型，避免盲发读包把控制器总线拖挂。

当前已经明确不再依赖的内容，应当视为历史残留，不作为主链路的一部分：

- 与当前主链路无关的临时浏览器白屏排障脚本。
- 白屏排障时生成的 `_tmp_*` 截图和日志。
- 纯 UI 预览专用的假状态注入逻辑。

---

## 3. 系统总架构

### 3.1 主控制链路

当前主控制链路如下：

1. 手机 App 通过 BLE 扫描并连接 `ATK-BLE04`。
2. App 通过 BLE 特征值发送 JSON 命令和控制帧。
3. `ATK-BLE04` 把这些数据通过 UART 透传给 `STM32F407`。
4. `STM32F407` 解析：
   - 远程控制帧
   - 参数配置命令
   - 线性矫正命令
   - 停止命令
5. `STM32F407` 根据底盘形式、驱动形式、挡位、摇杆输入和本地踏板输入，计算：
   - 左轮目标转速
   - 右轮目标转速
   - 机械转向目标转速
6. `STM32F407` 通过 CAN 控制：
   - 驱动轮控制器 `MSSD-60EHB_2D`
   - 机械转向控制器 `MSSC`
   - 线性方向盘控制器 `MSSC`
7. `STM32F407` 周期回传：
   - `chassis_status`
   - `status_ack`
   - `config_ack`
   - `capability_status`
   - `calibration_status`
   - `linear_steering_status`
8. App 根据这些真实回包刷新页面状态。

### 3.2 当前真实数据来源说明

为了避免“看起来对了，实际上是假数据”的问题，当前系统的数据来源边界如下：

- App 档案页的设备名称、档案描述：
  - 来自本地保存的底盘档案。
- App 参数配置页的参数：
  - 以 `STM32 config_ack` 和 `status_ack/chassis_status` 回包为准。
- App 状态页的在线状态：
  - 以 `STM32` 最近一次状态回包时间戳为准。
- App 控制页的摇杆数值：
  - 是 App 当前真实下发给 STM32 的操作者命令值，不是伪造遥测。
- 驱动 CAN / 转向 CAN 是否就绪：
  - 来自 STM32 固件内部 `can_transport` 状态。
- 机械转向反馈：
  - 来自机械转向 `MSSC` 的真实 CAN 反馈。
- 线性方向盘是否检测到：
  - 来自第二个 `MSSC` 节点的真实 CAN 回包状态。
- 踏板 ADC：
  - 来自 STM32 ADC 实采样。
- 本地挡位状态：
  - 来自 STM32 GPIO 输入。
- 硬件急停状态：
  - 来自 STM32 `PB11` 外部急停输入。

必须诚实说明的一点：

- 当前固件已经把运动状态拆成“命令状态”和“实际状态”两层：
  - `vehicle_moving_command`
    - 用于表达最近是否仍有非零驱动目标命令。
  - `vehicle_moving_actual`
    - 用于表达 `MSSD-60EHB_2D` 真实速度回读是否显示车轮仍在转动。
- 当前兼容字段 `vehicle_moving` 不再单纯等于命令状态：
  - 有真实速度反馈时，优先采用 `vehicle_moving_actual`。
  - 没有真实速度反馈时，回退到 `vehicle_moving_command`。
- 因此，现在的运动锁已经不是纯命令推断，而是“实际反馈优先、命令状态兜底”的双状态策略。
- 仍需如实说明：
  - 右轮实际速度寄存器 `0x09/0x0A` 来自手册明确说明。
  - 左轮实际速度寄存器已按 2026-04-27 的 USB-CAN 实测结果确认为 `0x14/0x15`。
  - 后续如果系统重新枚举串口号，调试命令里的 `-Port` 需要按实际口位替换。

---

## 4. 控制器与执行器组成

### 4.1 驱动轮控制器

- 型号：`MSSD-60EHB_2D`
- 角色：双驱动轮控制器
- 当前实现能力：
  - 左右轮目标转速下发
  - 左右轮实际转速寄存器轮询回读
  - 普通停止
  - 自由滑行停止
  - 紧急停止

相关源码：

- `STM线控控制文件夹/STM32F407_Ackermann_Chassis/Inc/drive_controller_mssd.h`
- `STM线控控制文件夹/STM32F407_Ackermann_Chassis/Src/drive_controller_mssd.c`

#### 4.1.1 驱动轮离线联调脚本

当前保留的双驱动轮离线联调脚本为：

- `调试脚本/mssd_dual_wheel_can.ps1`

当前脚本支持：

- 目标转速下发
- 普通停机 / 可选急停
- 实时轮询速度回读寄存器 `0x09/0x0A/0x14/0x15`
- “右轮单跑 / 左轮单跑 / 双轮同时跑”顺序测试
- “右轮正反 / 左轮正反”方向检查模式，便于现场判断物理前进方向是否接反
- 停机后再次读取反馈，确认反馈是否同步归零
- 当当前 USB-CAN 路径没有收到任何回包帧时，脚本会明确提示“本轮仅验证了命令发送，不代表真实速度回读已打通”

推荐现场命令：

```powershell
powershell -ExecutionPolicy Bypass -File .\调试脚本\mssd_dual_wheel_can.ps1 -Port COMx -NodeId 1 -RightRpm 500 -LeftRpm -500 -DurationSeconds 2 -SequenceTest -ReadFeedback -Run
```

如果当前最关心的是“正号和负号到底哪个才是物理前进方向”，推荐直接运行方向检查模式：

```powershell
powershell -ExecutionPolicy Bypass -File .\调试脚本\mssd_dual_wheel_can.ps1 -Port COMx -NodeId 1 -RightRpm 200 -LeftRpm 200 -DurationSeconds 1.2 -DirectionCheck -ReadFeedback -Run
```

如果只是先确保驱动轮彻底停下，可以用：

```powershell
powershell -ExecutionPolicy Bypass -File .\调试脚本\mssd_dual_wheel_can.ps1 -Port COMx -NodeId 1 -StopOnly -EmergencyStopOnStop -Run
```

#### 4.1.2 浏览器 USB-CAN 联调页

仓库里另外保留一个浏览器调试页：

- `调试脚本/usb_can_rear_drive_steering_test.html`

它的定位不是替代正式 App，而是做“离线直连控制器”的现场联调工具。

当前支持：

- 一个 USB-CAN 口接后驱双轮控制器
- 另一个 USB-CAN 口接前转向控制器
- 浏览器内分别选择两个串口
- 直接点按钮做：
  - 双轮正反转
  - 左轮单转 / 右轮单转
  - 自定义左右轮 RPM
  - 机械转向左打 / 右打
  - 实际轮速读取
  - 转向位置读取

使用前提：

1. 使用支持 `Web Serial` 的浏览器
   - 推荐 `Edge` 或 `Chrome`
2. 两个控制器各占一个 USB-CAN 串口
3. 页面内分别连接驱动口和转向口

这个页面当前适合做：

- 现场确认哪一路串口对应哪一个控制器
- 快速验证后驱双轮和前转向是否通电、是否能响应
- 在不经过 BLE 透传桥 / App 主链路时，单独验证控制器行为

### 4.2 机械转向控制器

- 型号：`MSSC`
- 角色：机械转向执行器
- 当前接入方式：
  - 与线性方向盘控制器共用同一条 `CAN2` 总线
- 当前节点号：
  - 不再写死在 App 侧
  - 由配置参数 `steer_can_node_id` 决定
  - 默认值 `1`

#### 4.2.1 机械转向离线联调脚本

当前保留的机械转向联调脚本为：

- `调试脚本/mssc_steering_can.ps1`

当前脚本支持：

- 切换速度闭环模式
- 下发机械转向转速命令
- 下发停止模式
- 按 `CAN 读请求 -> 等待 0x42 回包或超时` 的串行事务模型读取位置高低字
- 可选 `-ReadPosition`，在动作前后读取 `0x13/0x14` 两个位置字做现场核对

推荐现场命令：

```powershell
powershell -ExecutionPolicy Bypass -File .\调试脚本\mssc_steering_can.ps1 -Port COM5 -NodeId 1 -SpeedRpm 300 -DurationSeconds 1.5 -ControlMode 10 -ReadPosition -Run
```

### 4.3 线性方向盘控制器

- 型号：`MSSC`
- 角色：线性方向盘执行器
- 当前接入方式：
  - 与机械转向控制器共线挂在 `CAN2`
- 当前节点号：
  - 由配置参数 `handwheel_can_node_id` 决定
  - 默认值 `2`

### 4.4 主控

- MCU：`STM32F407ZGTx`
- 工程位置：
  - `STM线控控制文件夹/STM32F407_Ackermann_Chassis`

主控负责：

1. 读取本地 ADC 踏板。
2. 读取本地挡位 GPIO。
3. 读取硬件急停 GPIO。
4. 处理 BLE 透传过来的 App 命令。
5. 做本地/远程控制权仲裁。
6. 做底盘运动学分配。
7. 下发 CAN 命令到驱动轮和转向控制器。
8. 把状态回传到 App。
9. 把配置和矫正值保存进 F407 内部 Flash。

---

## 5. 固件当前核心能力

### 5.1 配置项

当前 STM32 已支持以下配置项，并可真实保存到内部 Flash：

- `front_track_mm`
- `wheelbase_mm`
- `rear_track_mm`
- `chassis_type`
  - `ACKERMANN`
  - `DIFF`
- `drive_axle`
  - `FWD`
  - `RWD`
- `left_drive_inverted`
- `right_drive_inverted`
- `linear_steering_enabled`
- `pedal_config`
  - `BRAKE_THROTTLE`
  - `ESTOP_THROTTLE`
- `drive_max_rpm`
- `steer_can_node_id`
- `handwheel_can_node_id`

### 5.2 Flash 保存

配置不是只存 RAM，也不是伪保存。

当前实际保存机制：

- 保存地址：
  - `0x080E0000`
- 扇区：
  - `FLASH_SECTOR_11`
- 存储头：
  - `magic`
  - `storage_version`
  - `config`
  - `checksum`

相关源码：

- `STM线控控制文件夹/STM32F407_Ackermann_Chassis/Src/vehicle_config.c`
- `STM线控控制文件夹/STM32F407_Ackermann_Chassis/Inc/vehicle_config.h`

当前存储版本：

- `VEHICLE_CONFIG_VERSION = 0x00040000`
- `VEHICLE_CONFIG_STORAGE_VERSION = 0x00040000`

并且当前已经兼容：

- 自动识别并导入 `v1.0.1` 使用的 `0x00030000` 版本配置结构
- 在旧配置里没有 `drive_max_rpm` 字段时，为其补上默认值 `500 RPM`

### 5.3 节点号可配置化

本次已经把这两个最重要的转向相关节点号改成配置项，不再要求 App 或 README 里默认写死：

- `steer_can_node_id`
- `handwheel_can_node_id`

生效方式：

1. App 配置页输入节点号。
2. 发送 `set_app_config`。
3. STM32 收到后更新配置。
4. `controllers_bind_from_config()` 重新绑定两个 `MSSC` 控制器实例。
5. 保存后，下次上电仍然保持。

必须诚实说明：

- 当前驱动轮控制器绑定用的 `DRIVE_CAN_NODE_ID` 仍是固件内固定值 `1`。
- 用户这次明确要求的是把 `STEER_CAN_NODE_ID` 和 `HANDWHEEL_CAN_NODE_ID` 做成可配置参数，这一项已经完成。
- 如果后续你也希望驱动轮控制器节点号可配置，可以再按同样模式把 `drive_can_node_id` 也加进 `vehicle_config_t`。

### 5.4 最大驱动输出配置

当前最大驱动输出不再是固件内部写死常量，而是正式配置项：

- 字段名：
  - `drive_max_rpm`
- 默认值：
  - `500`
- 当前约束范围：
  - `50 ~ 5000 RPM`

这条链路当前已经全部接通：

1. App 配置页可输入 `driveMaxRpm`
2. `set_app_config` 会把它编码成 `drive_max_rpm`
3. STM32 保存到 `vehicle_config_t`
4. Flash 保存结构写入该字段
5. `config_ack`
6. `status_ack`
7. `chassis_status`

都会真实回传当前值。

这意味着：

- 你在 App 里改的最大驱动输出，不是前端假显示
- 它会真实影响本地 / 远程驱动目标转速换算上限
- 下次上电后仍然保持

### 5.5 CAN 事务门控

当前固件里的 `can_transport` 不再是“固定周期盲发 + 被动收包”的模型，而是改成了真实的事务调度器。

当前实现约束如下：

1. 同一条物理 CAN 总线同一时刻只允许一个未完成读事务。
2. 读请求发出后，必须等到：
   - 收到匹配 `StdId + index + cmd` 的回包，或者
   - 到达单次超时
   之后，才允许继续发下一条读请求。
3. 写命令虽然不等待协议 ACK，但也必须占用同一个发送窗口。
4. 每次发送后都保留 `10 ms` 静默间隔，避免读写交错顶在一起。
5. 机械转向和线性方向盘如果共用同一条 `CAN2`，它们天然会共用同一个事务调度槽位，不会各发各的。

这样做的目的不是“把系统改慢”，而是为了避免之前那种典型风险：

- 主机正在盲发下一条读包
- 控制器恰好同时回上一条读包
- USB-CAN / SLCAN / 控制器缓存边界处理不好
- 最终导致数据错位、回包识别错误，严重时让总线或转接器进入假死状态

### 5.6 CAN 故障锁停与自动恢复

当前固件对 CAN 异常不再只是“看有没有回包”，而是正式接入：

- `ERROR_WARNING`
- `ERROR_PASSIVE`
- `BUSOFF`
- `LAST_ERROR_CODE`
- `ERROR`

并记录：

- `HAL_CAN_GetError()`
- `TEC`
- `REC`
- `LEC`
- `bus_off_count`
- `last_recovery_tick_ms`

当前恢复链路如下：

1. 一旦出现 CAN 错误中断、读事务超时、或发送失败，当前总线先进入故障态。
2. 与该总线相关的运动输出被软件锁停。
3. 该总线的读轮询会先停下来，不再继续盲发。
4. 固件调用 `HAL_CAN_Stop()` / `HAL_CAN_Start()` 并重新挂中断，做自动恢复。
5. 恢复后不会立刻解锁运动。
6. 只有真正重新收到一轮新鲜反馈，才解除总线恢复态。

这里“新鲜反馈”不是只看一个孤立的字：

- 驱动轮必须重新拿到左右轮速度高低字的完整组合，才会把驱动反馈故障清掉。
- 机械转向必须重新拿到位置高低字的完整组合，才会把转向反馈故障清掉。

这一步是为了避免“恢复后拿到一个新高字 + 一个旧低字，就被误判成新位置 / 新速度”的假新鲜问题。

### 5.7 机械转向闭环与限位策略

当前机械转向不再只是简单按摇杆开环给一个转速。

当前策略变为：

1. 先把摇杆横向输入映射成目标转向原始值。
2. 再根据机械转向控制器真实位置反馈，换算成当前角度。
3. 用“目标角 - 当前角”的误差去算转向目标 RPM。
4. 摇杆回中时，目标角回到中心点，转向电机也会闭环回中，而不是靠自然停。

限位策略：

- 左满 / 右满不会直接撞校准极限，而是钳位到该侧 `95%`。
- `10°` 点和最大限位点允许重合。
- 如果两者重合，系统仍会以“中心点为 0°”来计算目标角和实际角，不会把几何关系算乱。

App 新增的相关真实状态包括：

- `steer_target_angle_deg`
- `steer_actual_angle_deg`
- `fault_code`
- `fault_domain`
- `fault_message_zh`
- `drive_can_fault`
- `steer_can_fault`
- `handwheel_can_fault`
- `can_recovery_active`
- `can1_bus_off_count`
- `can2_bus_off_count`
- `can1_last_error_code`
- `can2_last_error_code`
- `outputs_locked_by_fault`

---

## 6. 远程优先 / 超时回本地 / 单踏板 / S 挡滑行

这一节是整个项目里最关键的控制语义说明，也是本次 README 必须单独展开的一节。

### 6.1 远程优先，不等于永久剥夺本地

系统里“远程优先”的真实含义是：

1. 蓝牙 App 连接成功后，默认只进入“监视模式”。
2. 本地踏板、本地挡位仍可继续控制车辆。
3. 如果 App 侧选择“接管车辆”，STM32 进入远程接管模式。
4. 只要远程控制帧持续新鲜，远程控制优先于本地驱动输入。
5. 一旦远程控制帧超时，系统自动回到本地控制。

也就是说：

- 不是“手机一连上，本地就彻底失效”。
- 而是“手机连上后随时可以监视，也可以主动申请接管”。
- 接管只有在远程帧持续有效的前提下才真正保持。

### 6.2 监视模式与接管模式

远程控制模式有两种：

- `MONITOR`
  - 远程在线，但不拿驱动控制权。
  - 本地踏板、本地挡位继续控制。
  - App 仍可以查看状态。
  - App 仍可以下发缓停和急停。
- `TAKEOVER`
  - 远程获得驱动控制权。
  - 本地一般驱动输入失效。
  - 但本地急停仍保留最高优先级。

固件中的实现字段：

- `remote_control_mode`
- `remote_takeover_enabled`
- `control_owner`
- `local_control_active`

### 6.3 超时回本地

远程接管不是永久锁死。

当前固件超时策略：

- `REMOTE_CONTROL_TIMEOUT_MS = 250 ms`

含义：

1. App 在接管模式下需要持续发控制帧。
2. 只要连续 `250 ms` 没收到新鲜远程帧，STM32 立即认为远程驱动控制失效。
3. 控制权回到本地输入。

这就是“超时回本地”。

优点：

- App 崩溃、蓝牙中断、前台切后台、控制页停止发包时，车辆不会长时间卡在远程接管状态。
- 本地操作者不需要断电重启即可恢复控制。

### 6.4 App 侧监视 / 接管交互

当前 App 交互逻辑是：

1. 蓝牙连上后，即使本地已经在开车，也允许连接。
2. 如果检测到本地控制正在生效，App 弹窗给两个选项：
   - `接管车辆`
   - `监视控制`
3. 如果用户选择 `监视控制`
   - 只看状态，不拿驱动控制权。
4. 如果用户在监视模式下去拖动摇杆
   - 再次弹窗确认是否接管。
5. 确认接管后
   - 远程帧开始持续发送，STM32 进入远程优先。

### 6.5 停止类命令的最高优先级

无论本地还是远程正在控制，下面这些停止类动作都优先级最高：

1. `PB11` 外部硬件急停输入
2. 本地急停踏板模式下的左踏板急停
3. App 急停
4. App 缓停

这是当前系统的停止优先级规则。

注意：

- “高优先级”指的是它们会覆盖一般的驱动命令。
- 但缓停和急停本身不是同一级。
- 急停比缓停更强。

### 6.6 缓停与急停的区别

当前 App 控制页已经拆成两个独立按钮：

- 左侧橙色：`缓停`
- 右侧红色：`急停`

语义如下：

- 缓停：
  - STM32 进入 `soft_stop` 锁定
  - 驱动目标归零
  - 走普通停止逻辑
- 急停：
  - STM32 进入 `estop` 锁定
  - 走紧急停止逻辑
  - 优先级更高

二者共同特点：

- 都高于本地和远程的一般驱动命令
- 都不依赖“当前是否接管车辆”
- 也就是在监视模式下，App 仍然可以发停

### 6.7 单踏板模式

当前单踏板模式对应配置：

- `pedal_config = ESTOP_THROTTLE`

物理含义：

- 左踏板：急停
- 右踏板：单踏板驱动

行为逻辑：

1. 右脚踩得越深，目标速度越大。
2. 右脚松得越多，目标速度越小。
3. 右脚完全松开，目标速度下降到 `0 rpm`。
4. 驱动控制器根据速度闭环把车速降下来。

这不是“一松开就机械硬刹停”的逻辑。

它更接近：

- 特斯拉那种“单踏板感”
- 但本项目当前是通过“速度目标逐步回零”来模拟
- 不是通过专用回生制动接口实现

所以这里要明确：

- 语义上是单踏板
- 体验上是渐进减速
- 物理上仍然取决于驱动电机、减速器、负载和控制器速度环参数

### 6.8 双踏板模式

当前双踏板模式对应配置：

- `pedal_config = BRAKE_THROTTLE`

物理含义：

- 左踏板：刹车
- 右踏板：油门

当前行为：

1. 右脚踩下：
   - 提高目标转速
2. 左脚踩下：
   - 普通减速 / 停车
3. 两脚都松开：
   - 驱动控制器进入 `FREE_STOP`
   - 车辆滑行，不主动制动

### 6.9 D 挡缓停

远程 `D` 挡规则：

1. 只允许前进方向动力。
2. 摇杆松手后，App 命令值逐步衰减到零。
3. STM32 收到零油门后走普通停止逻辑。

效果：

- 松手回中
- 车辆缓慢停止

### 6.10 S 挡滑行

远程 `S` 挡规则：

1. 只允许前进方向动力。
2. 摇杆松手后，App 立即不再给油。
3. STM32 侧把 `S` 挡零油门解释为 `FREE_STOP`。

效果：

- 电机不再继续给扭矩
- 但不主动制动
- 车辆按当前机械惯性滑行

### 6.11 R 挡倒车

远程 `R` 挡规则：

1. 只允许倒车方向动力。
2. 仍允许左右转向。
3. 摇杆松手后，App 命令值渐进回零。
4. STM32 按普通停止处理。

---

## 7. 本地控制与远程控制并存策略

### 7.1 本地控制永远存在

本地控制输入并没有因为 App 接入而被删除。

本地侧当前仍然存在：

- 本地踏板 ADC
- 本地前进 / 后退挡位输入
- 本地硬件急停输入

### 7.2 蓝牙可以随时连接

无论底盘当前是否在本地运行：

- 蓝牙都可以连接
- App 都可以查看状态
- App 都可以发缓停和急停

### 7.3 接管后本地何时失效

本地一般驱动输入在下面条件成立时失效：

1. App 进入 `TAKEOVER`
2. 且远程控制帧持续新鲜

此时：

- 本地油门 / 刹车 / 前后挡的一般驱动作用被远程覆盖
- 但本地急停和硬件急停不失效

### 7.4 接管失效后何时回本地

只要下面任一条件成立，系统就会回本地：

1. 远程发包超时
2. App 停止发送控制帧
3. 远程链路断开
4. App 主动切回 `MONITOR`

### 7.5 当前状态字段

固件回包里和控制权相关的关键字段有：

- `control_owner`
- `remote_mode`
- `remote_takeover_enabled`
- `local_control_active`
- `vehicle_moving`
- `vehicle_moving_command`
- `vehicle_moving_actual`
- `drive_feedback_valid`
- `soft_stop_active`
- `emergency_stop_active`
- `hardware_estop_active`

App 现在已经按这些真实字段显示状态，而不是本地乱猜。

---

## 8. App 当前页面功能

### 8.1 档案页

展示：

- 已连接底盘名称
- 底盘编码
- 通信模块
- 蓝牙设备名
- 驱动控制器型号
- 转向控制器型号
- 底盘形式
- 驱动形式
- 两个转向相关 CAN 节点号

### 8.2 状态页

展示：

- BLE 在线状态
- 驱动 CAN 是否就绪
- 转向 CAN 是否就绪
- 机械转向反馈是否有效
- 当前控制权归属
- 当前远程模式
- 本地控制是否正在生效
- 车辆是否处于运动锁状态
- 缓停 / 急停 / 硬件急停状态
- 实时左右轮目标转速
- 实时转向目标转速
- 实时踏板 ADC
- 远程挡位
- 转向反馈值与反馈时延

### 8.3 控制页

具备：

- 挡位滑条
  - `D / S / N / R`
- 虚拟摇杆
- 当前控制权和远程模式状态显示
- 缓停按钮
- 急停按钮
- 本地控制生效时的“接管 / 监视”弹窗
- 监视模式下触碰摇杆时的再次确认接管弹窗

### 8.4 参数配置页

具备：

- 底盘形式选择：
  - `线性阿克曼`
  - `线性差速`
- 驱动形式选择：
  - `前驱`
  - `后驱`
- 前轮距输入
- 轴距输入
- 后轮距输入
- 机械转向 CAN 节点号输入
- 线性方向盘 CAN 节点号输入
- 线性方向盘使能开关
- 进入线性矫正页面
- 保存并下发配置

安全约束：

- 如果车辆处于运动锁状态
  - App 弹窗“请停车后再试”
  - 不允许保存配置
  - 不允许进入矫正页
  - 不允许执行方向盘启用检测

### 8.5 线性矫正页

该页面现在是独立页面，独立保存，不和普通配置页共用一个保存按钮。

页面顺序：

1. 油门刹车矫正
2. 机械转向矫正
3. 线性方向盘矫正

支持：

- 踏板模式选择
- 左踏板五轮踩下 / 松开平均采样
- 右踏板五轮踩下 / 松开平均采样
- 机械转向左右点动
- 机械转向记录：
  - 左最大限位角
  - 中心位置
  - 右最大限位角
  - 左 10°
  - 右 10°
- 线性方向盘左右点动
- 线性方向盘记录：
  - 左最大限位角
  - 中心位置
  - 右最大限位角
  - 左 10°
  - 右 10°
- 独立保存并下发整张矫正表

安全约束：

- 只要车辆处于运动锁状态
  - 不允许开始踏板采样
  - 不允许继续采样
  - 不允许点动转向
  - 不允许记录矫正点
  - 不允许保存矫正表
  - App 弹窗“请停车后再试”

---

## 9. STM32 当前引脚与接线

这一节尽量写成接线手册，而不是一句“看 IOC”。

### 9.1 总体接线概览

当前主控使用的关键引脚如下：

| 功能 | STM32 引脚 | 方向 | 说明 |
| --- | --- | --- | --- |
| 油门 ADC | `PA0` | 输入 | 右踏板模拟量 |
| 刹车 / 急停踏板 ADC | `PA1` | 输入 | 左踏板模拟量 |
| ATK-BLE04 UART TX | `PA9` | 输出 | STM32 发到 ATK-BLE04 |
| ATK-BLE04 UART RX | `PA10` | 输入 | ATK-BLE04 发到 STM32 |
| 本地前进 / 后退挡位 | `PB10` | 输入 | `0=D, 1=R` |
| 硬件急停输入 | `PB11` | 输入 | 高电平有效 |
| 机械转向 + 方向盘 CAN RX | `PB12` | 输入 | `CAN2_RX` |
| 机械转向 + 方向盘 CAN TX | `PB13` | 输出 | `CAN2_TX` |
| 驱动轮 CAN RX | `PB8` | 输入 | `CAN1_RX` |
| 驱动轮 CAN TX | `PB9` | 输出 | `CAN1_TX` |
| USB FS D- | `PA11` | 双向 | USB 调试 |
| USB FS D+ | `PA12` | 双向 | USB 调试 |

### 9.2 油门与刹车踏板

当前 ADC 分配：

- `PA0 -> Throttle_ADC`
- `PA1 -> Brake_ADC`

逻辑含义：

- 右踏板模拟量接 `PA0`
- 左踏板模拟量接 `PA1`

无论你最终把左踏板定义成“刹车”还是“急停”，物理 ADC 采样点不变，只是配置语义不同。

当前固件里：

- `throttle_raw` 对应右踏板 ADC
- `brake_raw` 对应左踏板 ADC

### 9.3 本地前进 / 后退挡位输入

当前 GPIO：

- `PB10 -> Gear_Mode_In`

当前极性：

- `GPIO_PIN_RESET -> DRIVE`
- `GPIO_PIN_SET -> REVERSE`

也就是说：

- `PB10 = 0` 视为前进挡 `D`
- `PB10 = 1` 视为倒挡 `R`

当前 `.ioc` 中给 `PB10` 配了 `GPIO_PULLDOWN`，因此如果外部断开，默认会偏向前进挡。

### 9.4 硬件急停输入

当前 GPIO：

- `PB11 -> HW_ESTOP_In`

当前逻辑：

- 高电平有效
- 也就是：
  - `PB11 = 1` 表示硬件急停按下
  - `PB11 = 0` 表示未触发

用户当前接法是：

1. `3.3V` 通过 `1k` 电阻接到急停按钮一侧
2. 急停按钮另一侧接 `PB11`

这样形成：

- 平时：`PB11` 由内部下拉保持低电平，未急停
- 按下：`PB11` 被拉到高电平，立即急停

当前代码中对 `PB11` 的配置：

- `.ioc` 标注为 `GPIO_Input`
- `GPIO_PULLDOWN`
- 按下时依赖外部 `3.3V -> 1k -> 急停按钮 -> PB11` 拉高

相关代码：

- `STM线控控制文件夹/STM32F407_Ackermann_Chassis/Inc/main.h`
- `STM线控控制文件夹/STM32F407_Ackermann_Chassis/Src/main.c`
- `STM线控控制文件夹/STM32F407_Ackermann_Chassis/Src/board_io.c`

### 9.5 ATK-BLE04 与 STM32 的 UART 接线

当前 UART 使用：

- `USART1`

引脚：

- `PA9 -> ATK-BLE04_RXD`
- `PA10 -> ATK-BLE04_TXD`

接线关系注意要交叉：

- `STM32 PA9 (TX)` -> `ATK-BLE04 RXD`
- `STM32 PA10 (RX)` -> `ATK-BLE04 TXD`
- `STM32 GND` -> `ATK-BLE04 GND`
- `STM32 3.3V/模块允许的供电` -> `ATK-BLE04 VCC`

当前波特率：

- `115200`

这条链路负责：

- 手机 App JSON 命令透传
- 手机 App 控制帧透传
- STM32 状态 JSON 回传

### 9.6 驱动轮控制器 CAN 接线

当前驱动轮控制器走：

- `CAN1`

引脚：

- `PB8 -> CAN1_RX`
- `PB9 -> CAN1_TX`

推荐总线连接顺序：

- `STM32 CAN1_TX -> CAN 收发器 TXD`
- `STM32 CAN1_RX -> CAN 收发器 RXD`
- 收发器 `CANH/CANL -> MSSD-60EHB_2D CANH/CANL`
- 两端按总线规范加终端电阻

注意：

- `STM32` 不能直接裸连差分总线
- 需要 CAN 收发器芯片
- 例如 `TJA1050 / SN65HVD230 / MCP2551` 一类

### 9.7 机械转向与线性方向盘 CAN 接线

当前两台 `MSSC` 共用一条转向 CAN：

- `CAN2`

引脚：

- `PB12 -> CAN2_RX`
- `PB13 -> CAN2_TX`

总线关系：

- `STM32 CAN2` 与两个 `MSSC` 节点挂同一总线
- 区分靠 CAN 节点号，不靠独立串口

默认建议：

- 机械转向 `MSSC` 节点号：`1`
- 线性方向盘 `MSSC` 节点号：`2`

但现在这两个号已经可在 App 配置页里改。

### 9.8 USB 调试

当前 USB 使用：

- `PA11 -> USB FS DM`
- `PA12 -> USB FS DP`

作用：

- 通过 Type-C / USB 线接电脑
- 作为 `USB CDC` 调试口
- 可配合本地壳层命令联调

---

## 10. App 到 STM32 的协议要点

### 10.1 常用命令

App 侧主要使用的命令包括：

- `get_app_config`
- `set_app_config`
- `save_app_config`
- `load_app_config`
- `get_status`
- `get_capabilities`
- `query_linear_steering`
- `get_calibration`
- `set_remote_mode`
- `set_remote_stop_state`
- `steering_jog`
- `set_calibration_point`
- `capture_pedal_sample`
- `save_calibration`

### 10.2 控制帧

远程控制帧使用：

- `app_control`

关键字段：

- `seq`
- `gear`
- `throttle`
- `steer`
- `aux_x`
- `aux_y`
- `buttons`

当前 App 里，停止类按钮已经改为单独的 JSON 命令：

- `set_remote_stop_state`

而不是继续依赖控制帧里的 `buttons` 位去模拟 UI 状态。

### 10.3 远程模式命令

App 发送：

```json
{"cmd":"set_remote_mode","mode":"MONITOR"}
```

或：

```json
{"cmd":"set_remote_mode","mode":"TAKEOVER"}
```

STM32 回：

- `status_ack`

并带回：

- `remote_mode`
- `remote_takeover_enabled`
- `control_owner`

### 10.4 停止状态命令

缓停：

```json
{"cmd":"set_remote_stop_state","soft_stop":true,"estop":false}
```

急停：

```json
{"cmd":"set_remote_stop_state","soft_stop":false,"estop":true}
```

清除停止：

```json
{"cmd":"set_remote_stop_state","soft_stop":false,"estop":false}
```

### 10.5 配置命令中的节点号与最大驱动字段

现在配置命令里可以直接带：

- `drive_max_rpm`
- `steer_can_node_id`
- `handwheel_can_node_id`

例如：

```json
{
  "cmd": "set_app_config",
  "chassis_type": "ACKERMANN",
  "drive_axle": "RWD",
  "front_track_mm": 1280,
  "wheelbase_mm": 1720,
  "rear_track_mm": 1260,
  "drive_max_rpm": 500,
  "steer_can_node_id": 1,
  "handwheel_can_node_id": 2,
  "linear_steering_enabled": true,
  "pedal_config": "BRAKE_THROTTLE"
}
```

---

## 11. 运动锁与“请停车后再试”

这是用户特别要求的行为，必须单独说明。

### 11.1 为什么要锁

正在运动时去做下面这些动作，风险都很高：

- 保存参数
- 载入参数
- 做线性矫正
- 点动转向
- 采样踏板
- 记录中心点 / 限位点

因此当前系统做了双层保护：

1. App 前端先判断 `serviceActionsLocked`
2. 固件后端再次用同一策略拒绝执行

当前固件中：

- `vehicle_moving_command`
  - 表示最近仍存在非零驱动目标命令。
- `vehicle_moving_actual`
  - 表示 `MSSD` 真实速度回读显示车轮仍在转。
- `vehicle_moving`
  - 作为兼容字段继续保留，含义是“当前服务锁是否应该生效”。
  - 它会优先参考 `vehicle_moving_actual`，在没有真实速度回读时回退到 `vehicle_moving_command`。
- `serviceActionsLocked`
  - App 侧用于保存、矫正、点动、采样等服务动作的最终锁定判断。
  - 如果当前处于急停或缓停状态，且驱动真实反馈有效，则优先看 `vehicle_moving_actual`。
  - 这样急停 / 缓停后目标命令已经清零时，不会因为旧命令保持窗口一直误弹“车辆正在运动”。

### 11.2 固件侧已经锁住的动作

固件当前会返回 `vehicle_moving` 错误的动作包括：

- `set_app_config`
- `save_app_config`
- `load_app_config`
- `steering_jog`
- `set_calibration_point`
- `capture_pedal_sample`
- `save_calibration`

### 11.3 App 侧表现

App 现在会在以下场景弹窗：

- 保存配置
- 进入线性矫正页
- 开启线性方向盘检测
- 开始踏板采样
- 继续踏板采样
- 点动转向
- 记录矫正点
- 保存矫正表

弹窗统一提示：

- `车辆正在运动，请停车后再试`

---

## 12. 本地 IO 与当前硬件含义

### 12.1 本地挡位

当前本地硬件只有前进 / 后退两个挡位输入：

- `D`
- `R`

它们只影响本地控制分支。

手机 App 的 `D / S / N / R` 是远程控制的逻辑挡位，不等于本地只有四个物理开关。

### 12.2 为什么本地没有 S 挡

用户本次明确说明：

- STM32 本地按钮就是前进后退
- 别的没有

因此当前本地分支不额外做物理 `S` 挡输入。

`S` 挡只存在于远程 App 控制语义中。

### 12.3 本地急停

本地急停来源有两种：

1. `PB11` 硬件急停
2. 单踏板模式下左踏板急停

---

## 13. 线性矫正流程建议

### 13.1 踏板矫正

建议顺序：

1. 进入线性矫正页
2. 选择踏板模式
3. 先做左踏板
4. 再做右踏板
5. 每个踏板执行 5 次踩下 / 松开采样
6. 确认采样完成后再点“独立保存并下发矫正表”

### 13.2 机械转向矫正

建议顺序：

1. 点动左转 / 右转，把机构移动到目标位置
2. 记录中心位置
3. 记录左 10°
4. 记录右 10°
5. 记录左最大限位角
6. 记录右最大限位角

### 13.3 线性方向盘矫正

前提：

- 配置页已开启线性方向盘
- STM32 已检测到真实 CAN 回包

建议顺序和机械转向相同：

1. 左打 / 右打
2. 记录中心
3. 记录左 10°
4. 记录右 10°
5. 记录左右极限

### 13.4 保存策略

矫正页的保存是独立保存：

- 不和参数配置页共用保存按钮
- 页面底部按钮只负责保存：
  - 当前踏板模式
  - 当前矫正点

---

## 14. 当前源码关键位置

### 14.1 固件

- 主控制逻辑：
  - `STM线控控制文件夹/STM32F407_Ackermann_Chassis/Src/chassis_app.c`
- 配置存储：
  - `STM线控控制文件夹/STM32F407_Ackermann_Chassis/Src/vehicle_config.c`
- IO 读取：
  - `STM线控控制文件夹/STM32F407_Ackermann_Chassis/Src/board_io.c`
- ATK-BLE04 透传：
  - `STM线控控制文件夹/STM32F407_Ackermann_Chassis/Src/ewm22_link.c`
- 驱动轮控制器适配：
  - `STM线控控制文件夹/STM32F407_Ackermann_Chassis/Src/drive_controller_mssd.c`
- 转向控制器适配：
  - `STM线控控制文件夹/STM32F407_Ackermann_Chassis/Src/steering_controller_mssc.c`

### 14.2 App

- 控制台主页面：
  - `UniApp_WireControl_MobileApp/pages/dashboard/index.vue`
- 线性矫正页面：
  - `UniApp_WireControl_MobileApp/pages/calibration/index.vue`
- 会话状态：
  - `UniApp_WireControl_MobileApp/utils/session-state.js`
- BLE 桥接：
  - `UniApp_WireControl_MobileApp/utils/ewm22-ble-bridge.js`
- 协议定义：
  - `UniApp_WireControl_MobileApp/utils/chassis-protocol.js`
- 状态同步：
  - `UniApp_WireControl_MobileApp/utils/bridge-session-sync.js`

---

## 15. 当前还不能夸大描述的地方

为了避免后续联调时误判，这里把当前还不应该说成“完全闭环”的地方明确列出来。

### 15.1 车辆运动判定

当前运动状态被拆成三层：

1. `vehicle_moving_command`
   - 依据最近一段时间内是否持续有非零目标转速命令。
   - 仍保留 `1500 ms` 的命令保持窗口，避免刚松手就误判为静止。
2. `vehicle_moving_actual`
   - 依据 `MSSD` 真实速度回读。
   - 当前已经接入右轮实时速度寄存器 `0x09/0x0A`。
   - 左轮实际速度按实测确认寄存器 `0x14/0x15` 接入。
   - 实际速度回读有效时，它优先代表“车是不是还在真实转动”。
3. `vehicle_moving`
   - 兼容旧逻辑的服务锁字段。
   - 当前策略是“实际运动优先，实际反馈缺失时回退到命令运动”，并继续兼容矫正点动锁。

这意味着现在已经不再是“只有命令侧的运动判定”。

但它仍然不是：

- 带独立车轮编码器、地速传感器或 IMU 融合的整车级严格运动估计

### 15.2 单踏板体验

当前单踏板是：

- 通过速度目标逐步降到零来模拟的

它还不是：

- 带真实电机制动力矩闭环和能量回收模型的乘用车级实现

### 15.3 驱动轮节点号

当前只有：

- `steer_can_node_id`
- `handwheel_can_node_id`

是配置化的。

驱动轮控制器节点号还没有开放成配置项。

### 15.4 蓝牙链路稳定性

当前 App 和 STM32 协议是实打实接上了，但 BLE 透传质量仍然受下面因素影响：

- 手机机型
- 蓝牙 MTU
- ATK-BLE04 当前固件版本
- 现场干扰

所以“App 和 STM32 功能契合”这句话可以成立，但“任何手机上都绝对零延迟零丢包”这种话目前不能写。

---

## 16. 上电联调建议流程

建议每次按下面顺序来联调，能少掉很多误判：

1. 先给 STM32 上电。
2. 确认 `USB CDC` 能连上。
3. 确认 `ATK-BLE04` 模块已上电，蓝牙可被手机扫描。
4. 确认 `CAN1` 上驱动轮控制器供电正常。
5. 确认 `CAN2` 上机械转向和线性方向盘控制器都供电正常。
6. 不要先开远程，先看 App 状态页是否有真实在线回包。
7. 再测试本地踏板、本地挡位。
8. 再让手机进入监视模式。
9. 最后再做接管控制测试。

如果要做安全测试，推荐顺序：

1. 先测 `PB11` 硬件急停
2. 再测 App 急停
3. 再测 App 缓停
4. 再测本地单踏板左踏板急停

---

## 17. 结论

当前工程已经不再是“只是把页面画出来”或者“只是能驱动一下电机”的阶段，而是一套正在收敛成可用线控底盘主控的真实联调工程。

目前已经明确完成并落地的关键点包括：

1. `STM32F407` 侧真实 Flash 保存配置。
2. `前驱 / 后驱 / 差速 / 阿克曼` 的底盘配置框架。
3. `steer_can_node_id / handwheel_can_node_id` 配置化。
4. `远程监视 / 远程接管 / 超时回本地` 的控制权仲裁。
5. `D 挡缓停 / S 挡滑行 / R 挡倒车 / N 挡空挡` 的远程挡位语义。
6. `左刹车右油门` 和 `左急停右单踏板` 两套本地踏板语义。
7. `PB11` 硬件急停输入。
8. App 侧真实状态显示、监视 / 接管弹窗、双停止按钮。
9. 车辆运动时禁止保存和矫正。

当前最重要的下一步，已经不再是“再做一层假界面”，而是：

1. 带真实硬件做整车级联调。
2. 如果需要更严格的运动锁，再把独立轮速 / 地速 / 车速估计继续纳入 `vehicle_moving_actual`。
3. 如果后续驱动轮控制器节点号也要可配，再补 `drive_can_node_id`。

这份 README 的目标就是让你后面即使隔一段时间再回来，也能直接按接线、协议、状态机和操作流程继续推进，而不需要再从零回忆。
