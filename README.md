Ultra-EGoHub 派生自 [OpenEGo Hub](https://github.com/NihilDigit/openego-hub)，触控驱动基于 [EGoTouchRev](https://github.com/awarson2233/EGoTouchRev)。版本变化见[简短修改日志](CHANGELOG.md)。

公开版本使用内置耳机替代图标。华为产品图的来源与权利说明见 [第三方声明](THIRD-PARTY-NOTICES.md)。

<p align="center">
  <img src="https://raw.githubusercontent.com/NihilDigit/openego-hub/main/Assets/brand/openego-hub-256.png" alt="" width="96">
</p>

<h1 align="center">Ultra-EGoHub</h1>

<p align="center">
  <a href="https://github.com/Pluvist/Ultra-EGoHub/releases/latest"><img src="https://img.shields.io/github/v/release/Pluvist/Ultra-EGoHub?display_name=tag&label=release" alt="Release"></a>
  <a href="https://github.com/Pluvist/Ultra-EGoHub/actions/workflows/build.yml"><img src="https://img.shields.io/github/actions/workflow/status/Pluvist/Ultra-EGoHub/build.yml?branch=main&label=build" alt="Build"></a>
  <img src="https://img.shields.io/badge/Windows%20on%20ARM-ARM64-0078D4?logo=arm&logoColor=white" alt="Windows on ARM">
  <a href="LICENSE"><img src="https://img.shields.io/github/license/Pluvist/Ultra-EGoHub" alt="License"></a>
</p>

<p align="center">中文 | <a href="README.en.md">English</a></p>

Ultra-EGoHub 是面向 HUAWEI MateBook E Go（Windows 11 ARM64）的硬件控制中心，提供触控管理、手写笔与键盘配置、电池电源管理及显示色彩调节等功能。

系统架构由原生 ARM64 服务与 ARM64EC 宿主进程组成，直接调用原厂功能模块执行底层算法，接管硬件交互，支持在停用华为电脑管家及后台服务的情况下正常使用全部硬件特性。

> [!IMPORTANT]
> 系统中必须保留完整的华为电脑管家安装。配件状态、电池阈值与色彩管理等功能直接依赖电脑管家自带的底层 DLL，删除相关组件会导致对应功能失效。
>
> 该依赖不会增加后台常驻开销：Ultra-EGoHub 会停用电脑管家的官方 x64 服务，并移除其登录自启项，相关进程不再常驻后台，消除了原有的转译开销；所需 DLL 仅由本项目的宿主进程按需加载。

---

## 主要功能

- **触控管理**：由 EGoTouchRev 提供触控输入；防误触能力需实机验证。
  0.5.17 新增耳机连接弹窗和按型号、配色匹配图片的能力，见[连接提示与图片说明](docs/headphone-connection-images.md)。耳机控制范围见[耳机协议与支持范围](docs/headphones-protocol.md)。保留现有触控效果；此前的响应优化见[拖拽与响应说明](docs/drag-response.md)。 0.5.18 将连接弹窗按上边缘定位到可用区域顶部下方 24 DIP，保持水平居中并限制边界，避免高卡片超出屏幕。 0.5.19 将三类弹窗的资源查找、解码和裁切移到后台，复用有界图片缓存，并按有效回复及时显示耳机信息；见[弹窗加载优化](docs/popup-loading.md)。 0.5.20 增加跨进程的常用配件图片缓存，耳机重连优先使用已验证配色；全部通知统一非线性顶部滑入和上滑退出。 0.5.21 按用户要求改为状态与图片全部准备好后再弹出静态快照；显示期间不补图、不刷新电量，保留图片缓存与顶部动画。 0.5.22 修复耳机连接标记滞后造成的漏弹，通知只查询 SPP 身份与电量；复用本轮已完成读取并保留短重试，读取失败显示静态未知值而不丢通知。 0.5.23 对已验证的常用耳机仅读取本次电量，并在设备变化后立即补查连接，避免无 HCI 消息时等待 15 秒兜底；弹窗窗口构建与读取并行。
- **手写笔设置**：在“配件 → 笔”中提供“笔悬停”开关，默认开启。关闭后抑制未落笔时的光标与悬停事件，保留落笔报告；设置立即生效并在重启后保留。笔输入及侧键能力受上游驱动当前支持范围限制。
- **磁吸键盘配置**：支持键盘脱离主机后的无线连接开关控制。
- **耳机快速重连**：在“配件 → 耳机”提供开关与“立即重连”按钮。0.5.25 将断开后的首次请求提前至约 250 毫秒，利用精确匹配的被动蓝牙广播触发有界重连；缓存兜底每 2 秒检查一次。实际效果取决于耳机与驱动，见[重连说明](docs/headphone-reconnect.md)。
- **统一配件界面**：耳机展示图缩至 104×72，耳机卡片与笔、键盘共用宽窗双列、窄窗单列的等宽布局，并为所有耳机选项匹配 Segoe Fluent Icons。0.5.27 将耳机分组、设置与按钮图标统一为 20 DIP / 24×24 对齐框。改变窗口大小保留原控件与未保存的均衡器草稿。
- **均衡器编辑框**：0.5.28 将自定义均衡器改为按钮入口，在弹出编辑框中调整保存位置、名称和 10 个频段。关闭并重新打开保留草稿；点击“保存并应用”才写入耳机，读回成功后关闭，失败时保留草稿并提示。
- **键盘吸附光晕**：0.5.31 在键盘从未吸附变为已吸附时，主屏四周显示约 1 秒的白色渐变光晕，向内透明，亮起后淡出；亮度和范围略微增强，不占用焦点或阻挡操作，并跟随“设备提示”开关。仅充电状态变化不会触发；启动时已吸附也不会补播。均衡器入口同步改为整张卡片可点击、右侧箭头引导的样式；离线可编辑草稿，连接并刷新后保存。
- **耳机折叠菜单**：0.5.32 将声音、使用设置、手势及耳机已配对设备改为可展开、收起的菜单列表，保留原图标、选项及操作。默认展开，刷新和窗口缩放保留展开状态；自定义均衡器仍由右箭头卡片打开独立编辑窗口。
- **耳机卡片对齐**：0.5.33 将折叠菜单、已配对设备、自定义均衡器及重连设置统一为耳机信息卡片的宽度，组内选项单列排列；“加快耳机重连”位于自定义均衡器下方。
- **EnergyStar 节能**：0.5.34 原生内置 EnergyStar 的后台效率模式逻辑，在“电池”页提供默认关闭的开关，接电或电池供电时均可使用。切回前台、关闭功能和退出时恢复本功能修改的状态，并排除本软件与触控等宿主进程。详见 [集成说明](docs/energy-star.md)。
- **前端输入与滚动**：0.5.35 将各页及均衡器窗口的显式滚动容器改为 ScrollView，手指仍可滚动，笔交由子控件处理点击与拖动，避免进入页面的旧式笔滚动重定向。渲染比较排除无语义变化的发布时间戳，设备布局仅在可见性或宽度变化时重排。驱动报文、采样和调度保持不变；笔崩溃修复效果需要实机落笔验证。
- **恢复笔滑动**：0.5.36 在上述 ScrollView 中恢复原生笔滚动与惯性，同时保留点击、控件拖动及刷新优化。当前笔悬停和 EnergyStar 设置保持用户选择；请同时实测笔点击、滑动及稳定性。
- **原生触控与笔输入**：0.5.38 使用 WinUI 3 原生 ScrollViewer 处理触控、笔平移与惯性，并为滚动区域设置透明可命中的背景，不模拟拖动。前端保留 0.5.37 升级的 Windows App SDK 2.6.4 稳定版及其输入修复；改用经典原生控件后，本机笔滑动和稳定性仍需实测。安装前需具备对应 ARM64 Windows App Runtime 2.6.4 或兼容更新版，驱动与用户设置保持不变。
- **原生输入诊断**：用户实测 0.5.38 的笔点击正常，但仍不能在软件内滑动。0.5.39 增加默认关闭的本地只读记录，用于定位实际输入类型和原生滚动状态，不替代 WinUI 手势处理。参见 [诊断说明](docs/native-input.md)。
- **睡眠唤醒后恢复手写笔**：0.5.41 在面板恢复供电、触控链路收到有效帧后重新配置笔。握手失败时会有限重试，并处理恢复过程中迟到的初始化请求。
- **电池与电源**：支持智能充电模式与自定义充放电阈值；提供电池健康度、充放电循环次数及剩余续航时间查询。
- **色彩与显示**：支持 sRGB 与 P3 色域切换、色温调节及护眼模式，参数直接通过显示驱动应用面板出厂校准数据。
- **服务管理**：提供华为后台服务的状态监控与启停控制。
- **自动更新**：定期检查新版本，确认后在应用内完成下载、校验与安装，不必手动下载安装包；可关闭自动检查，也可跳过指定版本。
- **设备信息**：实时显示手写笔与键盘的电量、充放电状态、磁吸连接状态、固件版本和硬件序列号；显示主机型号、处理器规格、内存容量及系统版本。

---

## 界面展示

以下截图来自上游 OpenEGo Hub。

### 配件管理
显示手写笔与键盘的电量、连接状态、固件版本及序列号，并提供侧键功能配置。

<p align="center">
  <img src="https://raw.githubusercontent.com/NihilDigit/openego-hub/main/Assets/screenshots/accessories.png" alt="配件管理界面" width="820">
</p>

### 状态托盘与连接提示
常驻任务栏托盘显示配件状态，并在手写笔或键盘连接时显示状态提示。

<p align="center">
  <img src="https://raw.githubusercontent.com/NihilDigit/openego-hub/main/Assets/screenshots/popup-pen.png" alt="手写笔连接提示" width="340">
  <img src="https://raw.githubusercontent.com/NihilDigit/openego-hub/main/Assets/screenshots/popup-keyboard.png" alt="磁吸键盘连接提示" width="340">
</p>

### 电池与电源
提供智能充电模式与自定义充电阈值，并显示电池健康度、循环次数及剩余续航时间。

<p align="center">
  <img src="https://raw.githubusercontent.com/NihilDigit/openego-hub/main/Assets/screenshots/battery.png" alt="电池设置界面" width="820">
</p>

### 色彩与显示
提供色域切换、色温调节与护眼模式配置。

<p align="center">
  <img src="https://raw.githubusercontent.com/NihilDigit/openego-hub/main/Assets/screenshots/screen.png" alt="显示设置界面" width="820">
</p>

### 服务管理
提供华为后台服务的状态监控与启停控制。

<p align="center">
  <img src="https://raw.githubusercontent.com/NihilDigit/openego-hub/main/Assets/screenshots/services.png" alt="服务管理界面" width="820">
</p>

### 设备信息
显示主机型号、处理器规格、内存容量及系统版本等硬件信息。

<p align="center">
  <img src="https://raw.githubusercontent.com/NihilDigit/openego-hub/main/Assets/screenshots/device.png" alt="设备信息界面" width="820">
</p>

---

## 硬件支持

- **主机**：HUAWEI MateBook E Go（Windows 11 ARM64）
- **手写笔**：HUAWEI M-Pencil（支持 CD52、CD54、CD54R、CD54S；其中 CD54R 已完成真机测试）
- **键盘**：华为智能磁吸键盘

---

## 安装与使用

### 前置依赖
运行环境需保留系统原装的华为硬件驱动及华为电脑管家组件，屏幕色彩校准、电池阈值及键盘无线连接依赖其底层动态链接库。请安装与 Windows App SDK 2.6.4 兼容的 [ARM64 Windows App Runtime](https://learn.microsoft.com/windows/apps/windows-app-sdk/downloads)。色域预设依赖系统提供或本地安装的兼容 Qualcomm 色域库。

### 安装步骤
1. 前往 [Releases](https://github.com/Pluvist/Ultra-EGoHub/releases/latest) 页面下载最新版安装包 `Ultra-EGoHub_arm64_vX.Y.Z.msi`。
2. 运行安装程序完成部署（注册系统服务需要管理员权限）。
3. 安装完成后，后台服务将随系统启动，用户可通过开始菜单打开设置中心。

### 升级与卸载
- 安装包自带清理流程：安装前自动移除旧版本（含改名前的早期版本与测试版）遗留的服务、程序文件与快捷方式，无需先手动卸载。
- 用户配置在升级中保留，包括各项设置、日志与华为服务及自启项的还原记录。
- 设置中心可直接升级：确认后由后台服务下载、校验并安装，托盘与设置窗口在安装期间退出，装完自动恢复。
- 曾在设置中心停用的华为组件，升级后保持停用。从 v0.4.0 及更早版本升级时仍会被还原一次，那些版本的卸载流程已固化在本机。
- 原厂触控服务始终保持禁用，退出、异常和卸载均不尝试恢复它；其他华为后台服务与登录自启项仍按原有卸载流程处理。

### 运行机制
- **触控接管**：程序运行期间接管触控输入通道；原厂触控服务保持禁用。
- **触控驱动**：由系统服务独立运行，退出托盘不停止触控。首次启动失败后再重试 5 次，每次间隔 2 秒；仍失败则通知一次并停止自动重试，重启服务可开启新一轮。

---

## 源码构建

### 环境要求
- CMake 3.20 或更高版本
- Ninja 构建系统
- Visual Studio ARM64 MSVC 工具链（`cl.exe`）
- WiX Toolset v4（用于生成 MSI 安装包）

### 构建步骤

构建前需在 PowerShell 中加载 ARM64 编译环境。主构建脚本 `scripts\build.ps1` 会自动配置环境：

```powershell
# 编译 Release 版本
.\scripts\build.ps1 -Config Release

# 编译 Debug 版本并执行自动化测试
.\scripts\build.ps1 -Config Debug -Test
```

在已执行 `vcvarsarm64.bat` 的环境中，可直接使用 CMake Presets：

```powershell
cmake --preset arm64-Release
cmake --build --preset arm64-Release
```

### HAL 模块独立编译
`hal/` 目录下的硬件抽象层与宿主程序需优先独立编译，主工程依赖其生成的 `GaokunHal.lib`：

```powershell
cd hal
.\scripts\build.ps1 -Config Debug
cd ..
```

> 注：核心服务为原生 ARM64 程序；`hal/` 目录下用于加载 x64 DLL 的宿主程序编译为 ARM64EC，由 HAL 专属构建脚本生成。

### 调试与开发流程
在开发机上以管理员权限注册 Debug 服务后，可使用开发脚本进行迭代：

```powershell
# 注册 Debug 服务
scripts\install_debug_service.bat

# 快速更新与重启服务（需管理员权限）
pwsh -File scripts\dev-cycle.ps1

# 恢复运行已安装的 Release 服务
pwsh -File scripts\dev-cycle.ps1 -RestoreRelease
```

### 安装包制作
使用 WiX 生成 ARM64 MSI 安装包：

```powershell
dotnet tool install --global wix
wix extension add -g WixToolset.UI.wixext
wix extension add -g WixToolset.Util.wixext
pwsh -File scripts/GenerateHeadphoneAssets.ps1 -AssetRoot Tools/EGoTouchSettings/Assets/Headphones -OutputPath build/arm64-Release/HeadphoneAssets.wxs -ManifestOnly
wix build -ext WixToolset.UI.wixext -ext WixToolset.Util.wixext -arch arm64 -d BuildVersion=1.2.3 `
    -d BuildOutputDir=build\arm64-Release -d HalOutputDir=hal\build\Release `
    scripts\OpenEGoHubSetup.wxs build\arm64-Release\HeadphoneAssets.wxs -loc scripts\zh-CN.wxl `
    -out build\Ultra-EGoHub_arm64_v1.2.3.msi
```

或直接执行自动化打包脚本：

```powershell
scripts\pack_release_version.bat
```

---

## 项目结构

- `EGoTouchService/`：核心系统服务源码（`Device/` 硬件采集与虚拟 HID 注入、`Tsa/` 厂商算法适配、`Host/` 系统服务接口）。
- `hal/`：硬件抽象层与 ARM64EC 宿主进程，负责隔离并调用华为 x64 动态链接库。
- `Common/`：跨进程通信接口定义、共享内存数据结构及公共配置模块。
- `Tools/`：用户态应用程序，包含托盘监控程序（`EGoTouchTray`）与 WinUI 3 设置界面（`EGoTouchSettings`）。
- `docs/`：协议逆向文档与技术设计规格说明。
- `scripts/`：自动化编译、测试、安装打包与开发辅助脚本。

---

## 致谢与衍生说明

本项目基于 **[EGoTouchRev](https://github.com/awarson2233/EGoTouchRev)**（MIT License，© Detach2233）派生开发，继承了其 Himax 帧采集逻辑、手写笔 MCU 通信机制、VHF 虚拟 HID 注入架构及服务运行框架。完整授权声明参见 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)。

开发过程中参考了以下开源项目及社区成果：

- 感谢 Nshpiter 和 HuaweiPods 的贡献者公开耳机蓝牙协议资料。Ultra-EGoHub 的 Windows 耳机控制独立实现，电量读取、降噪与通透、均衡器等功能参考了这些通信格式和命令。 [HuaweiPods（GPL-3.0）](https://github.com/Nshpiter/HuaweiPods) · [协议与实现说明](docs/headphones-protocol.md)
- 感谢 Bingxing Wang（imbushuo）开发 EnergyStar。Ultra-EGoHub 电池页的节能功能改编自该项目，使用 Windows EcoQoS 管理后台应用的能效。 [EnergyStar 项目](https://github.com/imbushuo/EnergyStar) · [集成说明](docs/energy-star.md) · [原始许可证](third_party/EnergyStar/LICENSE)
- [MateBook-E-Pen](https://github.com/eiyooooo/MateBook-E-Pen)（by eiyooooo）
- [goodies](https://github.com/matebook-e-go/goodies)（by dantmnf）
- [EgoTools](https://github.com/SaKongA/EgoTools)（by SaKongA）
- [HuaweiPenEraserService](https://github.com/qwqVictor/HuaweiPenEraserService)（by qwqVictor）

---

## 开源协议

本项目基于 [MIT License](LICENSE) 开源。

依据协议条款，任何形式的代码或二进制分发均须保留 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) 中的版权声明。

---

## 免责声明

- **商标与产权**：本项目与 HUAWEI、Himax 及其他相关权利方无官方关联或授权。文档中提及的产品名称、商标及公司名称均属其合法权利人所有。
- **第三方资产**：托盘程序在运行时尝试加载本地已安装华为电脑管家中的手写笔与状态图标资源；未检测到安装时自动回退至内置矢量绘制。受版权保护的原厂素材不包含在本项目的源码或发布包中。
- **风险提示**：本项目涉及底层硬件驱动与系统服务交互，主要用于互操作性研究与技术交流。用户须自行承担使用风险，作者及贡献者对使用过程中可能导致的硬件故障、数据丢失或系统不稳定不承担法律责任。
