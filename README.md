# PA Host

这是新项目的 Linux/Kylin 上位机工程，目标是替代旧 Windows 上位机中与本项目无关的 WiFi、TCP、多型号兼容逻辑。

当前定位：

```text
RS422 与 ARM pa_controller 通讯
发送 PA/FPGA 控制命令
显示 ARM 返回状态
打开和查看本地 .tiraw 图像
提供旧上位机风格的图像交互和 ROI 分析入口
后续接入光口图像接收链路
```

## 目录

```text
src/MainWindow.*     Qt 主窗口，负责菜单、图像交互、ROI/分析弹窗、窗宽窗位
src/SerialClient.*   RS422 串口按行收发
src/PaProtocol.*     当前 ARM ASCII 命令和响应解析
src/TiRawImage.*     Windows 样例 .tiraw 16-bit 灰度图读取、自动窗宽窗位、ROI 统计
src/ImageView.*      图像显示、缩放、平移、ROI 框选、保存
doc/kylin-handover.md 给 Kylin 机器继续开发时看的交接文档
doc/git-commit-note-20260710.md 本次阶段性交付的中文提交说明
```

## 构建

Kylin 机器需要安装 Qt Widgets、Qt SerialPort 和 CMake。安装好后：

```sh
cd /home/zhe/app/pa_host
cmake -S . -B build
cmake --build build -j
./build/pa_host
```

当前工程使用 CMake 构建，`qmake` 不是必须项。Kylin 上如果 `pkg-config --modversion Qt5Core Qt5Widgets Qt5SerialPort`
能看到版本号，可以先直接尝试 CMake 构建。Qt Creator 可以直接打开 `CMakeLists.txt`，但会默认使用自己的
shadow build 目录，例如 `/home/zhe/app/build-pa_host-Desktop-Default`，这属于正常构建产物。

## 当前界面能力

当前版本已经把旧 Windows 上位机里最常用的一段图像查看工作流补齐：

```text
顶部菜单：文件 / RS422 / PA/FPGA / 视图 / 校准 / 工具 / 帮助
顶部模式条：Idle / Continuous / 手动上图 / 停止上图
左侧：图像列表
中间：图像画布
右侧：图像操作 / 窗宽窗位 / 图像信息
底部：型号 / 串口 / 连接状态 / 工作模式 / 图像尺寸 / 缩放
```

图像交互：

```text
滚轮、Ctrl+滚轮：缩放
左键拖动：平移图像
Ctrl+左键拖框：ROI 分析，弹出“分析测试”窗口
Shift+左键拖框：按 ROI 重新计算窗位/窗宽
再次普通左键点击：清除当前 ROI 框
鼠标移动：右侧显示当前像素坐标和值
```

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

界面文案中 `SEND_IMAGE` 已统一显示为“手动上图”。

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

当前读取逻辑：

```text
1. 检查 16 字节头和 "TiRayRaw" 魔数
2. 读取 little-endian 的 version / bytes_per_pixel / height / width
3. 校验文件大小必须等于 16 + width * height * 2
4. 读取 width * height 个 uint16 灰度像素
5. 计算全图 min/max、自动窗宽窗位、ROI 统计
```

自动窗宽窗位当前不是简单 `min/max` 拉伸，而是按旧上位机样例行为改成接近
`0.6% ~ 99.4%` 直方图分位的算法。对测试图
`20260707-104636-873-00000001.tiraw`，当前程序能算到：

```text
窗位 = 3937
窗宽 = 1948
```

与旧上位机记录的 `3939 / 1948` 基本一致。

这仍然只是当前样例结论，后续拿到正式 SDK 文档或光口图像协议后要再确认。

可用开发辅助脚本检查样例：

```sh
python3 tools/check_tiraw.py /home/zhe/app/windows/tidetector/CollectImage/20260707-1035/20260707-104636-873-00000001.tiraw
```

## 当前边界

已经完成：

```text
Qt Widgets 主窗口与样式整理
RS422 菜单化控制入口
.tiraw 16-bit 灰度图读取
旧上位机风格自动窗宽窗位
缩放、平移、旋转、翻转、保存
像素值显示
ROI 统计
Ctrl+ROI 分析测试弹窗
Shift+ROI 按区域重算窗位窗宽
```

还没完成：

```text
真实光口/PCIe 图像接收
ESF/LSF/MTF 与旧软件完全一致的公式校准
正式校准流程
高帧率实时显示与多线程采集管线
安装包与部署脚本
```
