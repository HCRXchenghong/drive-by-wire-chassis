# 线控底盘调试记录

## 当前文件

- `mssd_dual_wheel_can.ps1`
  - 双轮驱动器 `MSSD-60EHB_2D` 的 CANable2 / SLCAN 控制脚本
- `mssc_steering_can.ps1`
  - 转向电机伺服控制器 `MSSC` 的 CANable2 / SLCAN 控制脚本
- `参考文件`
  - 厂家 PDF 协议文档
- `_pdf_txt`
  - 从 PDF 提取出的文本，便于搜索寄存器

## 已验证硬件链路

- USB-C 转 CAN 转换器枚举为 `COM5`
- 设备识别为 `CANable2`
- SLCAN 串口速率为 `115200`
- CAN 总线速率为 `500kbps`
- 当前实测节点 ID 为 `1`

说明：

- `115200` 是 PC 到 CANable2 的串口速率
- `500kbps` 是 CAN 总线波特率

## 双轮驱动器 MSSD-60EHB_2D

参考：

- `参考文件/无刷电机驱动器MSSD-60EHB_2D  CAN通讯协议及寄存器说明书V1.0.pdf`

关键寄存器：

- 右轮目标速度：`0x0039/0x003A`
- 左轮目标速度：`0x003D/0x003E`
- 右轮急停：`0x0038`
- 左轮急停：`0x003C`

对应 CAN 索引：

- 右轮速度：`0x4039`
- 左轮速度：`0x403D`
- 右轮急停：`0x4038`
- 左轮急停：`0x403C`

已实测成功命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\mssd_dual_wheel_can.ps1 -Run -RightRpm 500 -LeftRpm -500 -DurationSeconds 2
```

实测发送帧：

- 右轮 `+500 RPM`：`24 39 40 00 00 00 F4 01`
- 左轮 `-500 RPM`：`24 3D 40 00 FF FF 0C FE`
- 右轮 `0 RPM`：`24 39 40 00 00 00 00 00`
- 左轮 `0 RPM`：`24 3D 40 00 00 00 00 00`

实测返回：

- 右轮启动返回 `60 39 40 00 00 00 00 00`
- 左轮启动返回 `60 3D 40 00 00 00 00 00`
- 两条停止指令也都返回 `0x60`

停止命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\mssd_dual_wheel_can.ps1 -Run -StopOnly
```

## 转向伺服控制器 MSSC

参考：

- `参考文件/无刷电机驱动器MSSC CAN通讯协议说明V1.0.pdf`
- `参考文件/无刷电机伺服控制器MSSC通讯协议及寄存器说明V1.0.pdf`

默认参数：

- CAN 波特率：`500kbps`
- 节点 ID：`1`
- CAN 模式寄存器：`0x00C4`
  - `0` 表示 CAN 通讯
  - `1` 表示 CANopen

关键寄存器：

- 停止：`0x0030`
  - `0` 正常停止
  - `1` 紧急制动
  - `2` 自由停止
- 速度闭环目标速度：`0x0031`
  - 单位 `RPM`
  - 正数正转，负数反转，`0` 为正常停止
- 位置闭环速度：`0x0032`
- 位置闭环类型：`0x0033`
- 位置闭环目标位置高/低半字：`0x0034/0x0035`

对应 CAN 索引：

- 停止：`0x4030`
- 速度闭环目标速度：`0x4031`
- 位置闭环速度：`0x4032`
- 位置闭环类型：`0x4033`
- 位置闭环目标位置：`0x4034/0x4035`

转向电机低速试转命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\mssc_steering_can.ps1 -Run -SpeedRpm 300 -DurationSeconds 2
```

转向电机停止命令：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\mssc_steering_can.ps1 -Run -StopOnly
```

实测结论：

- 2026-04-23 已确认转向电机可以通过 CAN 控制转动
- 当前控制器原始配置为：
  - `0x006B = 0`，速度闭环
  - `0x006C = 0`，单电位器控制
- 仅写 `0x0031` 速度命令时，用户侧不容易观察到动作
- 将 `0x006C` 临时改为 `10` 后，再写 `0x0031`，实测实时速度可到 `288~309 RPM`
- 切换到 `0x006C = 10` 后，实际转向机构已确认动作正常
- 电机最小转速寄存器 `0x0051 = 150 RPM`
- 因此脚本默认会先写：
  - `0x006B = 0`
  - `0x006C = 10`
  - 再下发速度命令

诊断读数：

- 300RPM 指令期间，实时速度约 `288~309 RPM`
- 错误状态 `0`
- 说明控制器已经在执行 CAN 转速命令
- 最终实际观察到转向电机/机构已经转动，当前 CAN 控制链路验证通过

## 调试建议

- 第一次接转向控制器时，先让电机脱离机构或确保不会撞限位
- 第一次试转建议先用 `300 RPM`
- 如果方向反了，把 `-SpeedRpm 300` 改成 `-SpeedRpm -300`
- 如果控制器不响应，先检查节点 ID、是否为 CAN 模式、以及是否仍是 `500kbps`
