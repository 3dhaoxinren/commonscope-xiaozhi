# TXW818 绿板：获取底包与合并开机热点代码

这里区分两种材料：**可用于原机升级的 OTA 包**，以及**可编译、可合并源码的同板型 SDK／厂商工程**。只有 BIN 或 ZIP 不能用来给本仓的 C 代码做源码合并。

## 底包怎么找（可交给自己的 agent）

1. **先认板。** 从自己设备的标签、PCB 印字和正常版本界面记录整机型号、板号／修订、TXW818 子型号、当前主控版本和厂商提供的分区信息。项目早期记录涉及 TXW818-C04L 和主控 1.1.7；不能只认芯片型号或 ZIP 名字。
2. **走正常供应渠道。** 先查整机厂商／卖家的支持页，或者在厂商正常升级界面核对 OTA。可通过 [CommonScope 应用页面的 App support](https://play.google.com/store/apps/details?id=cn.com.buildwin.general) 或卖家询问该批次的原厂离线 OTA、恢复包，以及可用的同板型源码工程。若 App 只支持在设备上升级，视为不能从该入口取得离线包；不猜下载地址，不调用未公开接口。
3. **源码从 SDK 渠道找。** [泰芯下载中心](https://taixin-semi.com/en/downloads) 的 TXW81x 有覆盖 TXW818 的 SDK。它是芯片开发资料，**不是 CommonScope 成品镜像**。应向板商索取本批次的项目、板级驱动和构建配置；不能用通用 demo 直接替代原机固件。
4. **本地核验。** 若设备所有者手上已有厂商明确支持导出的备份，只用于对应自有设备的恢复，不把整片 Flash、配对信息或密钥交给 agent 上传。让 agent 只读计算字节数和 SHA-256，记录官方来源、匹配板号、镜像类型、厂商校验和许可范围。无匹配材料就停在“待厂商确认”，不让 agent 自动刷机。

项目历史原厂 **1.1.7 主控 OTA 的解包 BIN** 对照：658448 字节，SHA-256：

    81086fb54e27452c2022c8dccfe2f94d8b8a477bff80e1719389992920d4154b

这个摘要只用于判断是否和项目历史样本字节一致，不代表厂商签名、下载授权或其他批次的兼容性；不是整个 ZIP 或整片 Flash 的摘要。未找到可核实的 CommonScope 1.1.7 官方直链，本仓不托管该底包。

## 将开机热点合并进 SDK（C99 或更新）

把 [panel_boot_ap.c](../firmware/panel/boot-ap/panel_boot_ap.c) 加进同板型的固件构建目标，并将 [panel_boot_ap.h](../firmware/panel/boot-ap/panel_boot_ap.h) 所在目录加入头文件搜索路径。项目方适配以下三个回调，名称只是示例，不是未知 SDK 的真实符号：

| 回调 | 对应 SDK 的正常操作 |
| --- | --- |
| `network_ready` | 驱动、网络任务及本机配置已初始化，且当前启动模式允许原机热点。 |
| `ap_ready` | 原机 AP 已运行，网络接口和 DHCP 服务都初始化完成。 |
| `request_start` | 调用正常 UI 的热点启动动作，复用原机的 AP 配置和认证；已启动或请求已排队也算受理。 |

在原机网络任务的串行轮询里调用一次 `panel_boot_ap_step`，例如每 100–250 ms。不要从中断或两个任务并行调用；有隐私、飞行模式或明确禁止开热点的启动路径时，不调用。固件若已有热点完成事件，让 `ap_ready` 读该状态。本模块不生成 SSID、口令或诊断密钥，不开维护端口，不覆盖原厂配置、不改升级签名校验。它在检测到 DHCP 就绪后停止发起 AP 请求；连续失败最多调用三次，每次间隔至少三秒。

```c
#include "panel_boot_ap.h"

/* board_net_ready、board_ap_ready、board_ap_start 都由板级代码实现；
 * 签名分别是 bool (void *)、bool (void *)、bool (void *)。 */
static PanelBootAp boot_ap;
static const PanelBootApPort boot_ap_port = {
    .ctx = 0,
    .network_ready = board_net_ready,
    .ap_ready = board_ap_ready,
    .request_start = board_ap_start,
};

/* 放在允许开热点的启动模式，由同一个网络任务周期调用。 */
void camera_ap_boot_tick(uint32_t monotonic_ms) {
    PanelBootApStatus s = panel_boot_ap_step(&boot_ap, &boot_ap_port, monotonic_ms);
    /* READY / EXHAUSTED 可以作为 UI 状态；由板级代码按需记录状态变化。 */
    (void)s;
}
```

`firmware/panel/wifi/` 是另一套已启动 AP 以后的 AP/STA 逻辑；只需要开机热点时不用合并它。按 SDK 的正常构建、签名和升级方式生成镜像。合并完成后，实机用同板号备用设备验证：冷开机能搜到原机热点，客户端能获得 DHCP 地址，关机再冷启动可重复；测试完成前不要把构建结果当通用首刷包发布。

### 本仓主机验证

运行 `python tools/run-host-tests.py`（也可用 `--cc` 指定编译器）。测试用模拟驱动验证初始化等待、已有热点、等待 DHCP、重试上限与计时回绕等场景；不连接硬件，也不证明已有同版 SDK 已完成集成。
