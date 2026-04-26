# Arduino Nano E22-T LoRa Hand Controller

这份说明现在按你的实际情况来写：

- 主控板是 `Arduino Nano`
- Nano 侧只有 `5V`
- 无线模块按 `E22-T系列模块_UserManual_CN.pdf`
- 摇杆是两个 `TMR` 霍尔摇杆

## 先说最关键的一句

`Nano 只有 5V` 这件事没有问题，但要分成两件事看：

- `E22-T` 的 `VCC` 可以吃 `5V` 供电
- `E22-T` 的 `RXD/TXD` 不是 `5V TTL`，而是 `3.3V TTL`

所以：

- `E22-T TXD -> Nano RX` 可以直接接
- `Nano TX -> E22-T RXD` 不能直接接，必须加分压或者电平转换

## 供电怎么接

### 如果你手里是 `E22-T22D / T22S / 22dBm`

- 模块发射峰值电流大约 `100~140mA`
- 可以先试 `5V` 供电
- 最稳妥还是用一条稳定的 `5V` 电源线

### 如果你手里是 `E22-T30D / T30S / 30dBm`

- 模块发射峰值电流大约 `460~620mA`
- 不要从 Nano 板子的小电源能力硬带
- 必须给它单独的 `5V` 稳定电源

### 不管哪种模块，都必须这样

- `Nano GND`
- `E22 GND`
- 摇杆 GND
- 外部 5V 电源 GND

这几个地必须全部共地。

## E22-T 直插版接线

下面按常见 `E22-T22D / E22-T30D` 直插版写：

- `E22 VCC` -> `5V`
- `E22 GND` -> `GND`
- `E22 TXD` -> `Nano D10`
- `E22 RXD` -> 从 `Nano D11` 经过分压后接入
- `E22 M0` -> `Nano D4`
- `E22 M1` -> `Nano D5`
- `E22 AUX` -> `Nano D12`

## Nano 全引脚怎么接

下面是按“你这版手柄代码”对应的 Nano 引脚表。

### 电源相关

- `5V` -> 左摇杆 `VCC`
- `5V` -> 右摇杆 `VCC`
- `5V` -> `E22 VCC`
  - 仅限 `22dBm` 模块可直接先这样试
  - 如果是 `30dBm`，建议改成外部独立 `5V`
- `GND` -> 左摇杆 `GND`
- `GND` -> 右摇杆 `GND`
- `GND` -> `E22 GND`
- `GND` -> 外部电源地
- `3.3V` -> 不接
- `VIN` -> 不接
- `RST` -> 不接
- `AREF` -> 不接

### 数字口

- `D0` -> 不接
- `D1` -> 不接
- `D2` -> 不接
- `D3` -> 不接
- `D4` -> `E22 M0`
- `D5` -> `E22 M1`
- `D6` -> 不接，预留
- `D7` -> 不接，预留
- `D8` -> 不接，预留
- `D9` -> 不接，预留
- `D10` -> 接 `E22 TXD`
- `D11` -> 通过分压后接 `E22 RXD`
- `D12` -> 接 `E22 AUX`
- `D13` -> 不接外设，板载 LED 自用

### 模拟口

- `A0` -> 左摇杆 `X`
- `A1` -> 左摇杆 `Y`
- `A2` -> 右摇杆 `X`
- `A3` -> 右摇杆 `Y`
- `A4` -> 不接，预留
- `A5` -> 不接，预留
- `A6` -> 不接，预留
- `A7` -> 不接，预留

## 最重要的一个分压

`Nano D11 -> E22 RXD` 不能直连。

最简单接法：

```text
Nano D11 ---- 4.7k ----+---- E22 RXD
                       |
                      10k
                       |
                      GND
```

这就是把 Nano 的 `5V` 串口输出压低到接近 `3.3V`。

如果你手里有现成 `3.3V UART` 电平转换模块，那就比这个更好。

## 两个摇杆怎么接

默认按下面接：

### 左摇杆

- 左摇杆 `VCC` -> `5V`
- 左摇杆 `GND` -> `GND`
- 左摇杆 `VRx` -> `A0`
- 左摇杆 `VRy` -> `A1`

### 右摇杆

- 右摇杆 `VCC` -> `5V`
- 右摇杆 `GND` -> `GND`
- 右摇杆 `VRx` -> `A2`
- 右摇杆 `VRy` -> `A3`

## 代码里每个引脚的用途

当前代码就是这样定义的：

- `D4` 控 `M0`
- `D5` 控 `M1`
- `D10` 收 `E22 TXD`
- `D11` 发给 `E22 RXD`
- `D12` 读 `AUX`
- `A0` 左摇杆 X
- `A1` 左摇杆 Y
- `A2` 右摇杆 X
- `A3` 右摇杆 Y

当前控制量映射：

- 左摇杆 `Y` -> `throttle`
- 右摇杆 `X` -> `steer`
- 左摇杆 `X` -> `auxX`
- 右摇杆 `Y` -> `auxY`

## E22-T 工作模式怎么接

现在这版程序上电后会自动把模块切到透明模式：

- `M1 = 0`
- `M0 = 0`

也就是：

- `D5` 输出低电平
- `D4` 输出低电平

配置模式则是：

- `M1 = 1`
- `M0 = 0`

也就是：

- `D5` 高
- `D4` 低

## 你现在最建议的实际接法

如果你现在只是先把手柄跑起来，按这个来最稳：

1. `Nano` 用 USB 供电
2. 两个摇杆都接 `5V`
3. `E22-T22D` 先接 `5V`
4. `Nano D11 -> 分压 -> E22 RXD`
5. `E22 TXD -> Nano D10`
6. `E22 M0 -> D4`
7. `E22 M1 -> D5`
8. `E22 AUX -> D12`
9. 所有地线共地

## 当前文件

- 主程序: [ArduinoNano_EWM22_LoRa_HandController.ino](/c:/Users/HCRXc/Desktop/线控底盘/ArduinoNano_EWM22_LoRa_HandController/ArduinoNano_EWM22_LoRa_HandController.ino)
- LoRa 配置示例: [LoRa_radio_config_examples.txt](/c:/Users/HCRXc/Desktop/线控底盘/ArduinoNano_EWM22_LoRa_HandController/LoRa_radio_config_examples.txt)

## 下一步最适合做什么

下一步我建议我继续帮你做其中一个：

1. 我把 `E22-T` 的配置流程直接做成一个 Nano 配置小程序，插上就能配模块
2. 我给你把手柄加上 `急停按钮` 和 `失联保护` 的代码骨架
