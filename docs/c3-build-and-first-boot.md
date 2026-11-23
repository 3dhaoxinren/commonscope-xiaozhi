# ESP32-C3：从源码编译、首次刷机和热点配网

## 构建

需要 Git、Python 3.9+ 与 ESP-IDF 6.0.1 或更高的 6.x（项目推荐 release/v6.1）。以下命令在本仓根目录运行，先激活 ESP-IDF 环境：

    git clone https://github.com/78/xiaozhi-esp32.git xiaozhi-upstream
    git -C xiaozhi-upstream checkout --detach 1b7644a6fba2b025ed36b4ac6309f122cc3cea14
    python tools/apply-c3.py xiaozhi-upstream --variant current
    cd xiaozhi-upstream
    python scripts/build.py commonscope-c3-ptt --name commonscope-c3-ptt --language zh-CN

也可以从另一个**干净**的固定上游 checkout 开始，将 current 替换为 prototype。不要在已有改动、不同提交或混用分区的仓库上叠加。脚本只改源码目录，不连接设备。

默认配置按本项目 ESP32-C3 SuperMini 的 4 MiB、无 PSRAM、按键版处理。公开快照保留了选择唤醒词时的代码分支，但不附原部署的唤醒模型嵌入文件，也不附已经配好的 LAN 证书、密钥或 NVS 数据。**默认按键配置不依赖缺失模型**；不要直接启用缺少模型文件的唤醒词选项。仓库中备用 A/B 分区 CSV 不等于已经迁移的镜像，不要在不同分区表间只换 app。

## 第一刷（新增 C3；不是原绿板）

1. 先与整机 5 V 隔离，单独用 C3 的 USB 连接电脑；保留自己的 Flash 备份及恢复方案。核对芯片为 ESP32-C3、串口与实物对应、Flash 容量与构建一致。原拍照屏上的 Type-C 不等于这里的 C3 下载口。
2. 在刚完成构建的 C3 工作树，用对应的构建目录、自己核对过的串口执行 idf.py -p PORT flash；如需日志再执行 idf.py -p PORT monitor。第一次不要只写 app.bin；ESP-IDF 会按本次工程生成的地址表写入分区表、应用和其他镜像组件。PORT 应替换为本机识别出的地址。上传失败不要把另一个设备选进来试。
3. 无已保存网络时，等待 C3 的小智 AP。以小智播报、串口或手机扫描显示的名称为准，不要连标有 CommonScope- 的绿板热点。手机连接小智 AP 并保持连接，打开它提示的配网页面，选自己的 2.4 GHz Wi-Fi。链接：[小智官方配网与绑定](https://docs.xiaozhi.me/zh/getting-started/wifi-and-binding.html)。
4. 联网后在 [小智控制台](https://xiaozhi.me/console/) 添加设备，输入设备自己的激活码。激活码、家庭网络密码和 LAN 配对材料不要提交到仓库。
5. 单独跑通启动和配网再按[接线表](wiring.md)接回整机。恢复整机供电时拔掉 C3 USB。完整问答、字幕、按键联动还需要配套绿板版本，原厂绿板不会因此自动获得新协议。

回退：源码层可丢弃这份独立 checkout 后重建；设备层用自己的、已经验证过的正确板型和分区布局备份恢复，不用仓库中的差分代替备份。
