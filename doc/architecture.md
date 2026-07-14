# PA Host 架构与扩展边界

当前阶段优先保证控制链路、图像链路和界面工作流可以独立演进。旧软件算法尚未拿到时，内置算法只作为可运行的默认实现，不应成为 UI 的直接依赖。

## 模块划分

```text
pa_host（Qt 界面）
  MainWindow / ImageView
        |
        +---- IImageSource ----------- LocalReplaySource
        |        |                         |
        |        +---- ImageFrame ---------+---- ImageSession
        |
        +---- IImageAlgorithms -------- BuiltinImageAlgorithms
        |                                  |
        |                                  +-- MtfAnalysis
        |                                  +-- 当前窗宽窗位算法
        |
        +---- pa_core
        |       TiRawImage / PaProtocol / 图像算法接口
        |
        +---- pa_transport
                SerialClient（RS422 控制链路）
```

CMake 目标：

```text
pa_core       图像数据、协议解析和算法接口，不依赖窗口
pa_transport  串口收发基础设施
pa_host       Qt Widgets 界面，只负责交互和模块组装
pa_host_tests 无界面自动测试，只链接 pa_core
```

## 算法替换

界面只依赖 `IImageAlgorithms`，目前默认使用 `BuiltinImageAlgorithms`。接口包含：

```text
autoWindowLevel  全图自动窗宽窗位
roiWindowLevel   ROI 窗宽窗位
analyzeMtf       ESF / LSF / MTF 分析
exportMtf        分析结果导出
```

拿到旧软件算法源码后，推荐新增 `LegacyImageAlgorithms`，实现上述接口。替换入口在 `MainWindow` 构造时注入，不需要修改 ROI 交互、分析弹窗、图像显示或菜单代码。

如果旧代码只提供部分算法，可以让新实现对已获得的算法调用旧代码，其余功能继续委托给 `BuiltinImageAlgorithms`，避免一次性迁移。

## 图像输入链路

当前已经有统一的输入契约：

```text
本地单文件 / 本地连续回放 / 未来 PCIe
              -> IImageSource（完整 ImageFrame）
              -> ImageSession（当前帧与算法调用）
              -> MainWindow / ImageView
```

`LocalReplaySource` 已接入文件菜单，可选择多个 `.tiraw` 并以 1~120 fps 循环回放。它只负责产生完整帧、错误和统计；UI 不直接读取回放文件。连续同尺寸帧保留当前视图变换。首帧使用可取消的成员定时器异步投递，停止、快速重启和回调内重启通过运行代际隔离，旧回调不能启动新一轮定时器。

当前回放序列固定为 2～3 帧，因此 `LocalReplaySource` 在启动时一次性加载有效帧，后续循环通过 Qt 隐式共享复用像素内存。主窗口最多缓存 4 张窗宽窗位映射后的 `QImage`，`ImageView` 在 128 MiB 上限内缓存对应 `QPixmap`。首帧立即提交，后续显示节拍跟随用户设置的 1～120 fps，并通过累计纳秒时间基准补偿 Qt 5 整数毫秒定时器的误差；来不及显示的中间帧计入显示丢帧，不进入无界队列。右侧全图 ROI 统计在首帧提交后延迟执行，最多每秒调度一次，并按固定帧路径缓存结果。上述按路径缓存只用于内容固定的本地回放，未来 PCIe 动态帧不能复用。

主窗口的普通图像列表保存文件路径和缩略图，不缓存每一项的完整 16-bit 像素。缩略图直接从原始像素降采样到目标尺寸，不构造完整显示图。用户切换列表项时重新加载对应文件，避免大量图像同时常驻造成内存快速增长。列表移除只删除 UI 项，不操作源文件。只有用户主动启动的小序列回放会在运行期间缓存所选原始帧。

导出分为两类：`TiRaw` 和 `RAW16 LE` 由 `TiRawImage` 直接写出原始 16-bit 像素；PNG、TIFF、BMP、JPEG 由当前窗宽窗位渲染结果写出 8-bit 显示图像。原始导出使用 `QSaveFile` 原子提交，避免失败时留下不完整文件。DCM 在正式 DICOM 元数据和编码要求确认前不实现。

`TiRawImage` 同时支持：

```text
load(path)                         从本地文件加载
loadData(bytes, sourceName, error) 从内存帧加载
```

PCIe 接入后建议的数据流：

```text
PCIe 驱动/厂商 SDK
        -> 采集线程
        -> 有界帧队列
        -> 帧协议校验与组帧
        -> TiRawImage::loadData
        -> 主线程更新当前图像
        -> 窗宽窗位映射
        -> ImageView 显示
```

采集线程不能直接操作 `MainWindow` 或 `ImageView`。它只应输出完整帧和采集状态，主线程通过 Qt queued signal 接收。队列必须有容量上限；实时显示来不及处理时丢弃旧显示帧，不能无限积压。

新 PCIe 实现应继承 `IImageSource` 或在其适配层输出同样的 `ImageFrame`，无需修改 `ImageSession`、图像算法和窗口代码。当前 `experimental/pcie` 是未参与编译的接口草案，不能当作驱动实现使用。

正式 PCIe 协议如果不是 `.tiraw` 带头格式，应新增独立解码器，将硬件帧转换为 `TiRawImage`，不要在 UI 中解析字节。

## 控制链路

RS422 只负责控制命令和状态：

```text
MainWindow -> PaProtocol -> SerialClient -> ARM
ARM -> SerialClient -> PaProtocol -> MainWindow 状态显示
```

图像数据走光口/PCIe，不应混入 `SerialClient`。未来如果协议改成二进制或增加校验，只替换 `PaProtocol` 及对应控制服务。

## 后续实施顺序

```text
1. 用命令行 PCIe 采集工具稳定接收并保存单帧
2. 明确帧头、长度、序号、时间戳和校验规则
3. 实现采集线程和有界帧队列
4. 通过内存入口接入 TiRawImage
5. 跑通连续显示、停止、错误恢复和丢帧统计
6. 拿到旧算法源码后实现新的 IImageAlgorithms
7. 最后再做性能优化和正式校准流程
```

这个顺序可以先验证基础链路，同时保证图像算法替换不会影响硬件接入和界面交互。
