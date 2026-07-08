# PA Host

这是新项目的 Linux/Kylin 上位机工程骨架，目标是替代旧 Windows 上位机中与本项目无关的 WiFi、TCP、多型号兼容逻辑。

当前定位：

```text
RS422 与 ARM pa_controller 通讯
发送 PA/FPGA 控制命令
显示 ARM 返回状态
打开和查看本地 .tiraw 图像
后续接入光口图像接收链路
```

## 目录

```text
src/MainWindow.*     Qt 主窗口，搭建左图像列表/中图像显示/右控制面板
src/SerialClient.*   RS422 串口按行收发
src/PaProtocol.*     当前 ARM ASCII 命令和响应解析
src/TiRawImage.*     Windows 样例 .tiraw 16-bit 灰度图读取
src/ImageView.*      图像显示、缩放、旋转、翻转、保存
doc/kylin-handover.md 给 Kylin 机器继续开发时看的交接文档
```

## 构建

Kylin 机器需要安装 Qt Widgets、Qt SerialPort 和 CMake。安装好后：

```sh
cd /home/zhe/sdk/app/pa_host
cmake -S . -B build
cmake --build build -j
./build/pa_host
```

当前工程使用 CMake 构建，`qmake` 不是必须项。Kylin 上如果 `pkg-config --modversion Qt5Core Qt5Widgets Qt5SerialPort`
能看到版本号，可以先直接尝试 CMake 构建。

## 与 ARM 的当前协议

当前按 `pa_controller` 的临时 ASCII 行协议开发，命令以 `\r\n` 结束：

```text
PING
STATUS
WAIT_IRQ
LOAD_TEMPLATE
MAKE_OFFSET
MAKE_GAIN
CONFIG_TEMPLATE
START_CORR
SEND_IMAGE
QUIT
```

响应示例：

```text
OK PONG
OK STATUS int=0x00000000 pa=0x00000000 com=0x00000000 rst=0x00000000 wr_state=0 wr_end=0 corr_state=0 corr_end=0
ERR UNKNOWN
```

## 图像格式

从旧 Windows 上位机保存的 `.tiraw` 样例反推：

```text
0x00: "TiRayRaw" 8 字节
0x08: uint16 version，样例为 1
0x0A: uint16 bytes_per_pixel，样例为 2
0x0C: uint16 height
0x0E: uint16 width
0x10: uint16 little-endian 灰度像素
```

这只是当前样例结论，后续拿到正式 SDK 文档或光口图像协议后要再确认。

可用开发辅助脚本检查样例：

```sh
python3 tools/check_tiraw.py /home/zhe/sdk/app/windows/tidetector/CollectImage/20260707-1035/20260707-104636-873-00000001.tiraw
```
