# CommonScope 拍照屏改造小智

华强北拍照屏保留原绿板、屏幕和按键，新增 ESP32-C3 SuperMini、INMP441、MAX98357A 与喇叭，让屏幕显示小智状态与字幕。本仓提供改造源码、接线对照和首次刷写/热点操作说明。视频：[40块华强北拍照屏爆改小智](https://www.bilibili.com/video/BV1r9yjzjE84)。

## 从哪里开始

1. [先认板和接线](docs/wiring.md)：TXW818 绿板和 ESP32-C3 是两个不同的目标，不能混刷。
2. [初版拍照屏：第一次刷机与设备热点](docs/txw818-first-flash-hotspot.md)：保留原绿板基线、打开原机 Wi-Fi 入口、核对 OTA，刷后重连设备热点。
3. [ESP32-C3：从源码编译、首次 USB 刷写与热点配网](docs/c3-build-and-first-boot.md)：固定 XiaoZhi 上游提交，先用按键版确认上电和配网。
4. [公开范围及验证](docs/verification.md)：实机历史和本仓新验证分开列；不把能编译或能写入等同于整机验收。

## 源码目录

| 目录 | 内容 |
| --- | --- |
| firmware/c3/prototype | 初版 C3 板型增量、应用接入和构建入口 |
| firmware/c3/current | 6.4.3 实物版本对应的 C3 自研源码快照，含按键、串口、连续对话状态机及签名校验；见构建边界 |
| firmware/shared | 两板共享按键、轮询与校验逻辑，以及主机测试 |
| firmware/panel/ptt | 原绿板的独立按键逻辑和对接端口约定 |
| firmware/panel/wifi | 绿板 Wi-Fi 运行状态机、AP 保留/回退逻辑及主机测试 |

这个仓库不是厂商固件下载站。**TXW818 完整应用、BootROM、原厂 BIN、旧私用 OTA 包、签名或配网资料不在仓库**；公开的绿板 C 模块还需要有权使用的板级 SDK 和适配层，不能单独刷进绿板。如果当前原机没有可用热点或没有对应的合法首刷镜像，不要拿 C3 固件代替。

[MIT 许可证](LICENSE)与[第三方版权说明](THIRD_PARTY_NOTICES.md)。第三方工具与模型不随本仓重新分发。
