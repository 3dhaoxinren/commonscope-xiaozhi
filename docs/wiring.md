# 接线核对（教程中的组合）

板型：CommonScope TXW818 绿板 + ESP32-C3 SuperMini + INMP441 + MAX98357A + 1609 喇叭。先拔 USB、断开原机电池。认 C3 正面、USB 朝上；GPIO5 与供电排针的 5V 是不同脚。原板 UART 先核对为 3.3V 逻辑，再共地对接。

| 起点 | 接到 |
| --- | --- |
| C3 GPIO4 | INMP441 SCK；同时接 MAX98357A BCLK |
| C3 GPIO5 | INMP441 WS；同时接 MAX98357A LRC |
| INMP441 SD | C3 GPIO6 |
| C3 GPIO7 | MAX98357A DIN |
| C3 3V3 | INMP441 VDD |
| INMP441 L/R | INMP441 GND（只选择声道，不接电源） |
| MAX98357A OUT+ / OUT− | 1609 喇叭红 / 黑线；两端都不能接地 |
| 原绿板 TX | C3 GPIO3（接收） |
| C3 GPIO10（发送） | 原绿板 RX |
| 实测稳压模块 OUT 为 5V | C3 5V、MAX98357A VIN |
| 稳压模块 GND | C3 G、MAX98357A GND、INMP441 GND、原绿板 GND 共地 |

电源输入取点依具体板版核对，不把 SYS_IN/EN 当作已确认取电点。上电前量 5V 与 GND 是否短路，确认稳压输出后再接；C3 的 USB 和外接 5V 不同时供电。串口是 1,000,000 / 8N1（改造协议）；厂商出厂调试口不是这条联动路径。
