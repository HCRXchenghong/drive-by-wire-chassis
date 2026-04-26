# Changelog

本项目从 `v1.0.0` 开始使用正式版本标签管理。

## v1.0.1

发布时间：2026-04-27

### 新增

- 新增 `MSSD-60EHB_2D` 真实速度回读链路。
- 固件新增 `vehicle_moving_command` 与 `vehicle_moving_actual` 双状态并存。
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
- 发布 `EWM22 BLE <-> UART` 透传控制主链路。
- 发布底盘参数配置、线性矫正、远程监视 / 接管、缓停 / 急停、本地踏板 / 本地挡位 / 硬件急停整套联调骨架。
- 发布完整项目资料、参考手册、调试脚本和仓库级 README。

### 已知边界

- 当时的 `vehicle_moving` 仍以目标转速命令保持逻辑为主。
- 真实驱动轮速度回读尚未接入到主状态机。
