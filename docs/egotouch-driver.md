# 0.5.15 保持输出不变的效率优化 / equivalent-output efficiency work

按用户要求，本版保留 0.5.14 的采样频率、报点节奏、8 ms 正常手指确认阈值、
滤波/防误触参数、落笔/抬起及中性笔包；不通过减少上报换取低占用。

优化 HimaxAfeAdapter 的笔矩阵展开：无有效输出用途（无信号、未连接、笔噪声过高）
时提前返回；有效帧只清理上次写过的连续范围；在 ARM64 上使用保持整数值和次序不变的
向量拷贝。边缘窗口保留上游对异常锚点的既有处理语义。重置仍清空完整矩阵。

驱动以 headless Release 服务构建时不再把每条文件日志额外复制进两份 GUI 队列，
减少字符串分配和常驻缓冲；文件日志保留，上游独立工具和 Debug 默认行为不变。

与归档的 0.5.14 实现对照 16,000 帧，master/stylus 元数据和全部返回矩阵单元逐值一致。
序列覆盖移动窗口、边缘与异常锚点、无笔、断连、噪声、校验失败、短包及重置。
新增独立的 dense-grid oracle 回归测试，完整重建矩阵以检测残留样本和边界错误。
本机交替重复的局部微基准中，笔适配函数耗时减少约 22%（无笔）、72%（普通笔帧）、
36%（边缘笔帧）、94%（不用于输出的笔数据）。这些比例只适用于被测函数，
不是整机 CPU 或内存占用降幅；实际节电、系统占用和起滑流畅度仍需设备实测。

Version 0.5.15 preserves input cadence, finger admission, filtering and rejection
parameters, hover behavior and neutral pen reports. Stylus matrix work is skipped
when its result cannot be returned. Active grids clear only previous written ranges
and use exact integer ARM64 vector copies, retaining boundary-anchor semantics.
The Hub's headless Release driver omits two unused GUI log queues while retaining
file logs. Standalone upstream tools and Debug retain their default logging behavior.

A comparison against the archived 0.5.14 implementation over 16,000 mixed frames
found identical master/stylus metadata and every returned grid cell. A new dense-grid
oracle regression checks stateful transitions independently of range caching.
Local alternating microbenchmarks reduced this adapter function's time by about
22% without a pen, 72% for interior pen windows, 36% at edges and 94% when pen data
cannot be emitted. These are function-level results, not whole-system resource
savings. Device measurements and visual confirmation are still required.

---

# 0.5.14 卡顿回归处理 / stutter regression mitigation

用户在 0.5.13 反馈窗口仍跟手、但移动画面不顺滑。现场输入统计为平均 6.7–8.3 ms，
最大约 8.7 ms，无 >16 ms 活动上报间隔；进程 Normal、输入线程 AboveNormal 的策略仍有效。
这些是驱动内部统计，不能证明合成器无掉帧，也不能证明 0 ms 握持等待是根因。

本版把正常小手指的握持确认阈值恢复为用户确认连点正常的 0.5.12 配置（8 ms），
撤回 0.5.13 的零额外等待尝试。该阈值只用于新触点放行，不是每个 Move 都等待 8 ms；
仍远小于原始 50 ms，不改变 Windows 双击设置、采样周期或抬起时机。

正式版正常解算不再重复转换 2400 格热力图和复制 suffix：触控与笔管线均直接使用 rawPtr，
这些解析结果没有正式版输出消费者。诊断构建和 parser-only 模式继续解析。保留每有效帧上报、
中性笔包、手掌/噪声保护、连点与多指拖动修复，以及原厂触控始终禁用的策略。

The user reported visual drag stutter after 0.5.13 while tracking remained continuous.
Driver timings and priorities did not reveal a long input stall; compositor smoothness
is not measured by those counters. This release restores the user-validated 0.5.12
8 ms finger admission threshold as a conservative regression mitigation, not a proven
root-cause fix. It also skips the unused decoded heatmap/suffix pass in production
solver mode, retaining it for diagnostics and parser-only operation. Report cadence,
neutral pen reports, release handling and the isolated worker boost are preserved.
Hardware confirmation of visual smoothness is still required.

---

# 0.5.13 缩短短按间隔

已确认的正常手指短按额外握持等待由 8 ms 改为 0 ms，初始检测和手掌/噪声保护保留。
这不是整机零延迟；完整条件见[拖拽与响应说明](drag-response.md)。

# 0.5.12 连续短按

正常小手指的握持确认等待从 50 ms 降到 8 ms，噪声、手掌和初始防抖保留。
本机后续 32 次已识别点击全部进入 HID，用户确认连点正常、没有明显误触。
临时诊断已关闭。完整条件和验证范围见[拖拽与响应说明](drag-response.md)。

# 0.5.10 边缘与连点

普通手指边缘握持判定带缩小 50%，保留大触点和手掌保护；修复一指抬起时清空整帧导致下一点击丢失。
实际算法库回归测试已证明修复前失败、修复后通过。详细记录见[拖拽与响应说明](drag-response.md)。

# 0.5.9 调度与点击修复

普通进程优先级、仅输入线程高一级；修正 Windows 触点接口的移动与待抬起状态。
实时调整的顺滑度改善已有用户反馈；起步和任务栏点击仍待新版实测。
详细记录见[拖拽与响应说明](drag-response.md)。

# 0.5.8 卡顿回归调整

根据用户实测撤回 0.5.7 的中性笔报告省略，恢复原有节奏，保留拖拽连续性修复。
详细改动、分阶段耗时统计及验证限制见[拖拽与响应说明](drag-response.md)。

# 0.5.7 拖拽与响应

当前版本修复缺帧时的接触状态维护、明确抬起和设备写入恢复，并减少重复无笔写入。
详细行为与验证范围见[拖拽与响应说明](drag-response.md)。

# 0.5.6 笔悬停开关

“配件 → 笔 → 笔悬停”默认开启；关闭时只屏蔽笔尖未接触屏幕时的光标和悬停，
不丢弃实际落笔的坐标、压力、倾角或橡皮擦报告。此开关不增加上游原本缺失的笔功能。
Hub 将设置保存到 `%ProgramData%/OpenEGoHub/pen_settings.ini` 的 `[Pen] hover_enabled`，
通过仅 SYSTEM 可写的 `Global\OpenEGoHubPenHover` 共享通道传给独立驱动。
驱动启动输入前读取设置，运行时每 200 ms 检查更新。过滤发生在橡皮擦转换之后，
以真实 tipDown 判断落笔，避免误屏蔽清除了 Tip Switch 位的橡皮擦笔画。
关闭时若当前处于悬停，会发送离开范围报告；正在进行的笔画保持到抬笔。
托盘发送命令与心跳共用锁，并在发送前重新打开控制通道，以支持服务重启。
控制与状态通道复用保留标志位，未改变原有载荷大小和托盘命令编号。

Accessories → Pen includes a persistent, live Pen hover switch (on by default).
The driver reads it before input startup and every 200 ms. Tip-up reports are made
neutral and out of range after eraser transforms; actual contact reports remain
unchanged. This does not restore stylus features disconnected upstream.

# 0.5.5 当前策略（取代下方旧版本的原厂回退说明）

HuaweiThpService 始终为 SERVICE_DISABLED。启动驱动前禁用并停止原厂服务，
服务存活期间每 5 秒检查一次；启动失败、托盘退出、租约过期、父进程退出、
服务退出、关机和卸载都不会启用或启动原厂触控。

驱动由系统服务启动和监督，独立于托盘。首次启动失败后，间隔 2 秒再重试 5 次；
仍失败时停止重试并持续发布 RetriesExhausted（状态通道错误值 8）。
托盘收到该状态时发送一次本地 Windows 通知；尚未登录时失败，登录打开托盘后仍会通知。
普通心跳、退出并重开托盘、休眠唤醒都不会重置已耗尽的重试批次。
重新启动 OpenEGoHubService 可开启新批次。用户主动退出控制面板与托盘时，驱动继续运行。

OEM touch is permanently disabled. The system service supervises input independently
of the tray. An initial startup failure gets five retries, two seconds apart; exhaustion
is latched as provider error 8 and produces one native tray notification, including
when the tray connects after the failure. Only restarting the supervisor starts a new
batch. No exit, failure, suspend, parent-crash or uninstall path restores OEM input.

# EGoTouchRev 驱动替换

本分支从 OpenEGo Hub 的 GaokunThpHost / THP_Service.dll 输入链切换为 EGoTouchRev。
来源为 https://github.com/awarson2233/EGoTouchRev ，固定提交
`6365e327f2aea50024e8ba96d5cb810ca39e21ad`（1.5.0）。源码位于
`third_party/EGoTouchRev`，保留上游许可证和来源记录，无需下载子模块。

## 构建

需要 Windows ARM64 MSVC、Windows SDK、CMake、Ninja、WinUI 构建依赖，以及
支持 ARM64 的 clang-cl（Visual Studio 的 LLVM 工具组件或 LLVM 安装）。
主程序继续使用 MSVC；驱动单独使用 clang-cl Release 构建，避免 TSACore 的
Clang 编译选项影响 WinUI。即使 Hub 使用 Debug，驱动也使用 Release，以关闭调试 IPC。

```powershell
.\hal\scripts\build.ps1 -Config Release
.\scripts\build.ps1 -Config Release -Test
```

产物 `build/arm64-Release/EGoTouchDriver.exe` 与 Hub 服务位于同一目录。
现有 CI 和 MSI 构建会自动构建、打包此文件；不再构建或打包 GaokunThpHost。
直接运行驱动不会接管设备，必须由 Hub 通过 SCM 传入 `--parent` 启动。
服务名保留 `OpenEGoHubThpHost`，以便升级替换已有的服务注册。
不要同时安装或启动独立版 EGoTouchService，它会竞争同一设备。

## 运行

托盘租约、休眠处理、崩溃重试与恢复 HuaweiThpService 的逻辑仍由 Hub 管理。
驱动监视 Hub 的进程句柄；Hub 崩溃时，驱动先停止输入，再启动原厂服务。
两个服务的电源事件使用不同命名空间，避免相互消费事件或误发关机信号。
Hub 不再启动自己的原始笔事件读线程，BT-MCU 输入由上游驱动独占。
笔电量、固件、键盘、色彩与电源功能仍通过原有配件 HAL 工作。

## 功能限制与验证

上游 README 标明主动笔 / BT-MCU 集成暂时断开；其代码仍包含相关模块。
本次按所指定项目接入，不擅自恢复上游停用的算法，不承诺压感、倾角、防误触、
橡皮擦或侧键功能。保留的配件设置也不能证明笔输入已可用。
Hub 的输入抑制和笔策略尚未跨进程传给该驱动；相关旧设置不能保证生效。

已完成 Release 编译：OpenEGoHubService、EGoTouchDriver、剩余 HAL 组件及
25 个测试目标；两个核心程序的 PE 架构均已检查为 ARM64。
16 个标记为 device-free 的测试全部通过，包括父进程观察、总线启动策略和接管回退测试。
PowerShell 语法与 WiX XML 结构检查通过。

已从微软官方组件包补齐工作目录中的 XAML 构建文件和 SDK 元数据，完整 Release
构建通过，已生成 `OpenEGoHub-EGoTouchRev-0.5.4-arm64.msi`。
安装包解包检查通过，驱动及主程序与编译产物的哈希一致，驱动服务为按需启动。
本机升级安装成功，已确认驱动持续采集帧并打开 VHF。实际发布前仍须在 MateBook E Go 上验证：
多点触控、驱动崩溃回退、Hub 崩溃回退、休眠唤醒与卸载。
手写笔功能须单独确认，不能以构建成功代替测试。

## English

This branch replaces the vendor THP host with the pinned EGoTouchRev 1.5.0 source
snapshot above. The Hub remains MSVC/WinUI; a separate clang-cl Release build produces
EGoTouchDriver.exe. Install the ARM64 MSVC/SDK, CMake/Ninja and LLVM toolchain before
running the two build commands above. The MSI and deployment script include the new
driver and its license. The legacy SCM name is retained for upgrade compatibility.
The Hub owns the lease and OEM fallback; a parent-process guard restores OEM input
after a Hub crash. Power event names are isolated and the Hub's duplicate raw pen
reader is disabled. Accessory, keyboard, display and power hosts remain.

Upstream explicitly marks active stylus integration as temporarily disconnected.
Pressure, tilt, palm rejection, eraser and side-button support are not guaranteed.
Hub input-suppression and pen-policy settings are not forwarded to the driver.
The Release service, driver, remaining HAL components and 25 test targets compiled.
Both input executables were verified as ARM64, and all 16 device-free tests passed.
Official XAML build files and SDK metadata were supplied from the workspace; the full
Release build and ARM64 MSI now pass. Extracted executable hashes match the build, and
the driver service is demand-start. The local MSI upgrade succeeded and input frames
are being acquired with VHF ready. Uninstall, pen writing, suspend and fallback tests
remain required.

## 源码基线

### 0.5.4：初始化等待与启动就绪修复

0.5.3 实机日志显示 ACPI 面板已上电，但可写的 burst 寄存器 0x13 为零，
运行时在 Init 之前无限等待，长期 frames=0；Hub 仅检查 SCM RUNNING，误报接管成功。
0.5.4 在面板确认上电时允许进入已有的初始化与失败退避流程；未确认上电时仍保留等待。
驱动只有收到 master 帧且 VHF 设备可打开后才报告 RUNNING，12 秒内未就绪则停止，
让 Hub 恢复原厂。控制器启动等待调整为 20 秒，覆盖驱动就绪检查与清理时间。

0.5.4 已在本机成功升级安装，日志确认进入 streaming、打开 VHF，并检测到手指唤醒。
16 项 device-free 测试通过。休眠、卸载和手写笔书写效果仍需分别验证。

Version 0.5.4 fixes an initialization deadlock when panel ACPI confirms power but
the writable burst register is zero. SCM RUNNING now requires a master frame and
VHF availability; startup failure is bounded and returns control to the OEM provider.
The local MSI upgrade succeeded and logs show streaming, VHF readiness and a finger
wake event. All 16 device-free tests pass. Suspend, uninstall and pen-writing behavior
remain separate validation items.

OpenEGo Hub 基线：`c3cab6b87dcffb71a3be00be88365ee479ef1d9a`。
源码包包含替换后的完整源码；补丁用于在此基线上应用改动。
手动指定 LLVM 路径时，先导入 ARM64 开发环境，再配置
`cmake --preset arm64-Release -DEGOTOUCH_CLANG_CL=C:/LLVM/bin/clang-cl.exe`，
路径应替换为本机实际安装位置。主程序编译器继续使用 MSVC。
