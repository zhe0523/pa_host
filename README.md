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
src/AppLogService.*  分级运行日志、文件滚动和诊断文本导出
src/AppSettings.*    最近目录、串口参数和命令超时配置
src/ImageListPanel.* 左侧图像列表、缩略图、选择、移除和右键菜单
src/ImageExportService.* 原始图像和显示图像导出服务
src/ImageAlgorithms.* 图像算法接口和当前内置实现，旧算法从这里替换
src/ImageSource.*    图像源抽象和本地 .tiraw 连续回放实现
src/ImageAcquisitionController.* 图像源会话、状态、停止顺序和统计汇总
src/ImageSession.*   当前图像帧、显示渲染和算法调用的应用层边界
src/FramePresentationController.* 最新帧选择、显示限速、实际 FPS 和丢帧统计
src/ReplayPresentationScheduler.* 1～120 fps 显示时间调度策略
src/PaDeviceController.* RS422 设备状态、命令生命周期、响应和超时管理
src/ILineTransport.* 控制链路的按行传输接口，支持模拟传输测试
src/SerialClient.*   RS422 串口按行收发
src/PaProtocol.*     当前 ARM ASCII 命令和响应解析
src/TiRawImage.*     Windows 样例 .tiraw 16-bit 灰度图读取、自动窗宽窗位、ROI 统计
src/ImageView.*      图像显示、缩放、平移、ROI 框选、保存
doc/kylin-handover.md 给 Kylin 机器继续开发时看的交接文档
doc/architecture.md   模块边界、算法替换和 PCIe 图像链路设计
doc/communication-data-link-draft.md RS422 控制通信与 PCIe 图像数据链路协议草案
doc/git-commit-note-20260710.md 图像交互阶段的中文提交说明
doc/git-commit-note-20260714.md 架构解耦与图像回放阶段的中文提交说明
doc/git-commit-note-20260714-image-list-replay-performance.md 图像列表、导出与回放性能阶段的中文提交说明
doc/git-commit-note-20260714-ui-replay-decoupling.md 图像界面与回放呈现解耦阶段的中文提交说明
doc/git-commit-note-20260714-device-control-decoupling.md RS422 设备控制链路解耦阶段的中文提交说明
doc/git-commit-note-20260714-logging-settings.md 运行日志与应用配置基础设施阶段的中文提交说明
doc/git-commit-note-20260714-communication-protocol-correction.md 通信协议纠正与数据链路草案阶段的中文提交说明
doc/git-commit-note-20260715-image-acquisition-controller.md 图像采集会话解耦阶段的中文提交说明
```

## 构建

Kylin 机器需要安装 Qt Widgets、Qt SerialPort 和 CMake。安装好后：

```sh
cd /home/zhe/app/pa_host
cmake -S . -B build
cmake --build build -j
./build/pa_host
```

在 Makefiles/Ninja 这类单配置生成器下，如果没有显式指定，工程默认使用 `RelWithDebInfo`，即启用优化并保留调试符号。图像加载、窗宽窗位转换和 ROI 扫描不应使用空的 `CMAKE_BUILD_TYPE` 或未优化构建做性能判断。需要完整调试构建时显式使用 `-DCMAKE_BUILD_TYPE=Debug`。

当前工程使用 CMake 构建，`qmake` 不是必须项。Kylin 上如果 `pkg-config --modversion Qt5Core Qt5Widgets Qt5SerialPort`
能看到版本号，可以先直接尝试 CMake 构建。Qt Creator 可以直接打开 `CMakeLists.txt`，但会默认使用自己的
shadow build 目录，例如 `/home/zhe/app/build-pa_host-Desktop-Default`，这属于正常构建产物。

### Windows 构建与调试

Windows 已验证可使用 Qt 5.12.12，并可在 Qt Creator 或 CLion 中直接打开 `CMakeLists.txt`。建议每个 IDE
使用独立构建目录，避免共享 `CMakeCache.txt`。

```text
Qt Creator：选择 Qt 5.12.12 对应的 Desktop Kit
CLion：Toolchain、CMake Profile 和 Qt 编译器保持一致
MinGW 版 Qt 必须配套 MinGW，MSVC 版 Qt 必须配套 MSVC
```

命令行示例：

```bat
cmake -S . -B build-win -G Ninja -DCMAKE_PREFIX_PATH=C:\Qt\5.12.12\mingw73_64
cmake --build build-win
```

发布时不能只复制 `pa_host.exe`，需要在 Qt 命令行环境执行：

```bat
windeployqt build-win\pa_host.exe
```

程序会记住最近打开和保存图片的目录。Windows 首次运行默认打开系统“图片”目录；串口列表使用 `COMx`，
Linux/Kylin 使用 `/dev/tty*`。

## 自动测试

测试默认由 CMake 的 `BUILD_TESTING` 选项启用，不依赖 Qt Test 或真实硬件。构建和运行：

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
cd build
ctest --output-on-failure
```

Windows 可在 CLion 的 CTest 面板运行 `pa_host_tests`，也可以在构建目录执行：

```bat
ctest --output-on-failure
```

当前测试覆盖：

```text
PA/ARM 命令字符串和 OK/ERR 响应解析
PA 设备连接、单命令在途、STATUS、超时和传输错误恢复
AppSettings 配置默认值、持久化和数值边界
AppLogService 日志格式、滚动归档和诊断导出
.tiraw 正常文件头、尺寸、像素读取
.tiraw 错误魔数、错误位深、长度不匹配和短文件
0.6% ~ 99.4% 自动窗宽窗位
ROI 均值、最小/最大、标准差和行噪声
手动窗宽窗位的 8-bit 显示映射
缩略图尺寸下的直接降采样显示映射
图像算法接口的自动/ROI 窗宽窗位契约
内存字节流解析为 TiRawImage
ImageSession 文件加载、当前帧、ROI 和显示渲染
ImageExportService 格式元数据、后缀、RAW16 和显示图导出
ReplayPresentationScheduler 60 fps 补偿、迟到追帧和帧率边界
FramePresentationController 最新帧覆盖、停止丢帧和统计周期重置
ImageAcquisitionController 启停、首帧、源销毁、自然结束和迟到帧隔离
LocalReplaySource 帧序号、停止状态、快速重启、坏文件跳过和统计
ESF / LSF / MTF 基础分析与 CSV 导出
```

## 代码结构

工程按构建目标拆分：

```text
pa_core       图像数据、协议解析、算法接口和默认算法
pa_transport  RS422 串口收发
pa_host       Qt 界面和模块组装
pa_host_tests 无界面核心测试
pa_image_benchmark 真实 TiRaw 性能基准工具
```

`MainWindow` 只负责模块组装和跨模块工作流：图像列表内部行为由 `ImageListPanel` 管理，文件编码由 `ImageExportService` 管理，图像源启停和会话统计由 `ImageAcquisitionController` 管理，显示限速由 `FramePresentationController` 管理，纯时间计算由 `ReplayPresentationScheduler` 管理。主窗口不直接依赖具体 MTF 或窗宽窗位实现，而是通过 `IImageAlgorithms` 调用。后续拿到旧软件算法源码后，新增接口实现并在程序启动时注入即可。详细边界见 `doc/architecture.md`。

RS422 控制链路由 `PaDeviceController` 管理连接状态、单条在途命令、响应和 5 秒超时；`MainWindow` 不再解析 ASCII 响应。未连接或命令执行中时，PA/FPGA 命令会自动禁用。设备返回有效响应、发生超时或传输错误后，控制器会统一恢复或切换错误状态。错误状态下如果串口仍保持打开，可以直接重试命令。

运行日志由 `AppLogService` 统一生成，格式包含时间、级别和来源，并写入应用数据目录下的 `logs/pa_host.log`。单个文件默认最多 5 MiB，保留 5 个归档。通过“视图 -> 运行日志”打开底部日志 Dock；“工具 -> 导出诊断信息”生成包含运行环境、控制参数和文本日志的诊断文件，不包含图像像素。“工具 -> 命令超时设置”可调整并保存响应超时。

构建时启用 `BUILD_TESTING` 后，可以用真实的 2～3 帧序列复测预加载、显示转换、缩略图和全图统计耗时：

```sh
./build/pa_image_benchmark frame1.tiraw frame2.tiraw frame3.tiraw
```

## 当前界面能力

当前版本已经把旧 Windows 上位机里最常用的一段图像查看工作流补齐：

```text
顶部菜单：文件 / RS422 / PA/FPGA / 视图 / 校准 / 工具 / 帮助
顶部模式条：Idle / Continuous / 手动上图 / 停止上图
左侧：带缩略图的图像列表，可点击切换和移除
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

图像输入：

```text
文件 -> 打开 TiRaw 图像：可一次选择一张或多张本地图像
文件 -> 回放 TiRaw 序列：选择多张图像、设置 1~120 fps 后循环回放
文件 -> 停止图像回放：停止本地回放
```

回放使用和未来 PCIe 相同的 `IImageSource -> ImageAcquisitionController -> FramePresentationController -> ImageSession -> ImageView` 更新路径。连续同尺寸帧不会重置缩放、平移、旋转或翻转状态；状态栏显示实际显示 FPS。PCIe 尚未接入。

普通图像列表只保存文件路径和缩略图，不常驻缓存所有 16-bit 原始图像。点击列表项时重新加载对应文件；选中一项或多项后，可点击“移除选中图像”、按 Delete，或使用右键菜单移除。移除只影响列表，不会删除磁盘上的源文件。

本地回放针对当前固定的 2～3 帧使用预加载：开始回放时读取并解析一次原始图像，循环时复用内存帧；日志会记录成功预加载帧数和耗时。稳定的本地帧携带 `contentCacheKey`，主窗口最多缓存 4 张当前窗宽窗位下的 8-bit 显示图，`ImageView` 还会在 128 MiB 上限内缓存对应 `QPixmap`，避免无显卡或远程桌面环境反复做显示格式转换。未来 PCIe 动态帧默认不提供该键，因此不会按来源名误用旧缓存。首帧立即提交，后续显示节拍跟随用户设置的 1～120 fps；累计时间基准会自动补偿整数毫秒定时误差。处理速度跟不上输入时只保留最新帧并统计显示丢帧，不累积延迟。状态栏同时显示实际显示 fps 和目标 fps。全图 ROI 统计延迟到首帧提交后执行，并按稳定内容键缓存。调整窗宽窗位、停止或重新开始回放会自动清空相关缓存。

图像列表缩略图直接从 16-bit 原始像素降采样到目标尺寸，不再先生成一张完整的 8-bit 大图，因此打开多张大图或首次回放时的界面阻塞更小。

右键点击图像列表项可从“导出当前图像”子菜单选择格式：

```text
TiRaw       保留 TiRayRaw 文件头和原始 16-bit little-endian 像素
RAW16 LE    仅导出原始 16-bit little-endian 像素，不包含宽高和文件头
PNG / TIFF  导出当前窗宽窗位映射后的 8-bit 显示图像
BMP / JPEG  导出当前窗宽窗位映射后的 8-bit 显示图像
```

DCM 暂未实现。DICOM 需要明确设备、检查和图像元数据以及编码规范，不能只修改文件扩展名。

配置会保存最近图像、保存、导出和诊断目录，以及最后成功连接的串口、波特率和命令超时。Kylin 使用 Qt 的用户配置目录，Windows 使用当前用户配置，工程目录不会生成配置文件。

## 与 ARM 的当前协议

当前按 `pa_controller` 的临时 ASCII 行协议开发，命令以 `\r\n` 结束：

```text
PING
STATUS
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
OK STATUS pa=0x00000000 com=0x00000000 rst=0x00000000 wr_state=0 wr_end=0 corr_state=0 corr_end=0
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
图像算法接口与默认实现解耦
本地 TiRaw 序列连续回放
多图打开、缩略图列表、点击切换和批量移除
图像列表右键导出 TiRaw、RAW16、PNG、TIFF、BMP 和 JPEG
连续帧实际 FPS、错误和完成统计
RS422 命令状态、超时和结构化 STATUS 处理
图像采集会话状态、统一停止顺序和迟到帧隔离
分级滚动日志、运行日志 Dock 和诊断文本导出
串口参数、命令超时和最近目录持久化
相同尺寸连续帧保持当前图像视图状态
ESF / LSF / MTF 曲线 CSV 导出
核心模块无硬件自动测试
```

还没完成：

```text
真实光口/PCIe 图像接收
ESF/LSF/MTF 与旧软件完全一致的公式校准
正式校准流程
高帧率实时显示与多线程采集管线
安装包与部署脚本
```
