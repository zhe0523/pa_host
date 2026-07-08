# PA Host Kylin 交接文档

本文档用于后续在 Kylin-Server-V10-SP3-2403 机器上继续开发。

## 目标环境

```text
操作系统：Kylin-Server-V10-SP3-2403
CPU：海光 7420D，x86_64 兼容
工程目录：/home/zhe/sdk/app/pa_host
ARM 端工程：/home/zhe/sdk/app/pa_controller
```

## 需要安装的开发包

优先使用系统仓库安装：

```sh
sudo yum install -y gcc gcc-c++ make cmake
sudo yum install -y qt5-qtbase-devel qt5-qtserialport-devel
```

如果系统使用 `dnf`：

```sh
sudo dnf install -y gcc gcc-c++ make cmake
sudo dnf install -y qt5-qtbase-devel qt5-qtserialport-devel
```

如果包名不一致，先用下面命令查：

```sh
yum search qt5 | grep -i serial
yum search qt5 | grep -i base
```

## 环境检查命令

安装后请在 Kylin 机器上执行，并把输出给我看：

```sh
uname -a
cat /etc/os-release
gcc --version
g++ --version
cmake --version
qmake --version
pkg-config --modversion Qt5Core Qt5Widgets Qt5SerialPort
ls -l /dev/ttyS* /dev/ttyUSB* 2>/dev/null
groups
```

如果普通用户没有串口权限，一般需要加入对应用户组。常见组名是 `dialout`、`uucp` 或 `tty`，以 Kylin 实际设备权限为准：

```sh
ls -l /dev/ttyS0 /dev/ttyUSB0
```

## 编译运行

```sh
cd /home/zhe/sdk/app/pa_host
cmake -S . -B build
cmake --build build -j
./build/pa_host
```

## 当前测试方式

### 1. 先测图像查看

打开菜单：

```text
文件 -> 打开 TiRaw 图像
```

可使用旧 Windows 样例：

```text
/home/zhe/sdk/app/windows/tidetector/CollectImage/20260707-1035/*.tiraw
```

预期：

```text
图像能显示
状态栏显示宽高
右侧窗宽窗位能改变显示亮度
缩放、旋转、翻转、保存 PNG 能使用
```

Qt 环境还没装好时，可以先用辅助脚本确认样例文件头：

```sh
cd /home/zhe/sdk/app/pa_host
python3 tools/check_tiraw.py /home/zhe/sdk/app/windows/tidetector/CollectImage/20260707-1035/20260707-104636-873-00000001.tiraw
```

### 2. 再测 RS422/串口

开发板还没有真实 RS422 时，可以让 ARM 端跑 stdio 模式配合伪终端，或者等真实串口接好后直接选择 `/dev/ttySx`。

真实串口接好后的测试步骤：

```text
1. ARM 板运行 pa_controller，选择实际 RS422 tty。
2. 上位机 pa_host 选择对应串口和波特率。
3. 点击“连接”。
4. 点击“心跳”，日志应收到 OK PONG。
5. 点击“读取状态”，日志应收到 OK STATUS ...
```

## 当前代码边界

已经完成：

```text
Qt Widgets 主窗口骨架
RS422 串口连接、断开、发送命令
按行接收 ARM 响应
解析 OK/ERR 和 key=value 状态字段
.tiraw 文件读取和显示
窗宽窗位、缩放、旋转、翻转、保存
```

还没完成：

```text
正式 UI 细节和图标
正式校准向导流程
真实光口图像接收
正式二进制协议或带校验协议
多线程图像处理优化
安装包制作
```

## 重要判断

旧 Windows 上位机可以参考界面工作流和 `.tiraw` 文件格式，但不建议移植旧通信层：

```text
旧通信：WiFi/TCP/多型号 SDK
新通信：RS422 控制，图像走光口/FPGA
```

所以新上位机应该继续保持专用工程，不要把旧 `TiRayLib.dll` 的网络模型搬过来。
