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

# 0.5.13 正常短按当帧放行

按用户要求进一步缩短短按间隔，将 0.5.12 中正常小手指的额外握持确认等待从 8 ms 改为 0 ms。满足原有快速路径条件并进入有效 Down 的触点不再等待下一采样帧才放行；Move 同样不增加等待。

条件保持不变：非握持质量、已确认 Down/Move、当前和历史最大长轴均小于 20、无异常、无噪声、没有手掌/抓握/多指握持/长条标记。初始按下防抖、峰值与信号检测、明确握持、手掌、大触点及噪声路径仍保留原规则。没有修改 Windows 双击速度、系统设置、硬件扫描周期或抬起规则，也没有注入额外点击。“0 ms”只表示该额外等待被移除，不表示整机触控零延迟。

真实算法测试同时检查零等待的 Down（durationMs=0）和 Move，以及所有原有保护例外；保留连续 100 次新按下与旧抬起重叠回放、多指拖动测试、缩小的边缘判定带和已确认的调度策略。临时状态诊断默认关闭。

Version 0.5.13 removes the remaining 8 ms grip-admission wait for already-confirmed,
quiet, small non-palm finger contacts. A valid Down can proceed in the same sample;
Move has no added wait. Existing initial confirmation, signal/noise, palm, large-object
and explicit-grip safeguards remain. Windows double-click settings, hardware scan
period and release logic are unchanged. Zero extra wait is not zero end-to-end latency.
The real-core regression checks immediate Down/Move admission and all guard exceptions.

---

# 0.5.12 快速交替点击与短按响应

针对用户在不同任务栏按钮间交替点击仍漏点的反馈，临时启用只记录状态的有界诊断，分别观察原始触点、过滤前后输出和成功写入 HID 的按下/抬起。记录不含坐标、应用窗口或屏幕内容，最多 512 条状态变化后自动关闭，正常安装默认不开启。

修复前记录到的 35 次完整、已识别按下接触中，24 次形成 HID 按下、11 次被握持待定过滤压住；漏掉的短按持续约 50–75 ms。原来的 Windows 握持规则统一等待 50 ms，部分短按在放行前就已结束。

现在仅对下列触点将握持确认等待从 50 ms 缩短到 8 ms：质量分类未判为握持、已经进入有效 Down/Move、当前和历史最大长轴均小于 20、无异常、无噪声，且不含多指握持、邻近手掌、手掌形状、长条或抓握标记。明确握持、大触点、手掌、噪声、异常和初始未确认防抖保留原规则；没有绕过原始峰值/信号按下检测，也没有用模拟鼠标点击补点。此 8 ms 是指定内部规则的等待上限，不代表整机端到端触控延迟。

真实算法库测试先复现原 50 ms 等待失败，再验证新规则及大触点、手掌、异常、噪声、疑似握持和初始防抖例外；完整 ARM64 Release 构建和全部 23 项 device-free 测试通过。

本机升级 0.5.12 后，同一诊断方法记录的后续 32 次完整、已识别点击全部形成 HID 按下，未再观察到该过滤路径丢点。用户随后反馈“连点已正常，没有明显误触”。这些是本次实机片段的结果，不是所有触控场景的漏点率保证。诊断已达到上限停止，并删除了启用标记，后续重启也默认关闭。

保留 0.5.10 的普通手指边缘判定带减半、同帧抬起/新按下逐点输出，以及已确认有效的调度、原报送节奏、笔悬停关闭设置、原厂触控持续禁用和五次重试通知策略。

Version 0.5.12 reduces only the grip-admission wait for quiet, already-confirmed,
small non-palm fingers from 50 ms to 8 ms. Grip, noise, exception, large-object and
initial-debounce protections remain. A bounded, default-off state-only trace located
11 filtered contacts among 35 confirmed contacts before the change; the subsequent
32 confirmed contacts all reached HID. The user confirmed normal repeated taps with
no obvious extra mistouches. These are observed trial results, not a universal miss
rate or end-to-end latency guarantee. The trace stopped at its event cap and its
enable marker was removed. All 23 device-free tests and the Release build pass.

---

# 0.5.10 边缘点击与连续点击

用户要求缩小边缘防误触宽度并改善连续点击失灵。

普通小触点的边缘握持判定带缩小 50%，距离仍用原算法的 Q8.8 传感器单位：未旋转时 Dim1 从 0x200 降到 0x100（侧边触摸场景从 0x300 降到 0x180），Dim2 从 0x100 降到 0x80；旋转时两轴从 0x200 降到 0x100。仅在原有小触点条件（长轴小于 20）及形状/比例检查通过时更早判为安全触点。大触点、手掌、异常信号、视频场景角落例外和其它防误触判据保留。该比例不是整块屏幕新增固定像素死区，也不改变坐标映射。

连续点击修复位于真实 TSACore 的 Out::PostProcess：原逻辑在同帧有任一抬起时清空全部触点，可能丢掉另一触点刚发生的按下。现在按触点剔除未上报或已被抑制的项，保留其它新按下、移动和有效回放触点；HID 层仍按原编号逐点抬起。未缩短抗噪防抖或模拟鼠标点击，也未增加帧缓冲、滤波、限速或新的等待。

新增集成测试直接链接驱动使用的 TsaCore 库：修复前在第一组“旧点抬起、新点同帧按下”失败；修复后连续 100 组交替点击及一指抬起、另一指持续拖动通过。该测试从真实输出裁剪经过 Core::TouchAt、触点映射、HID 编码及状态账本，验证下一次按下不会丢失且最后仅有新触点保持按下。另有边缘阈值、横竖屏及大触点不旁路的边界测试。

完整 ARM64 Release 构建及 23 项 device-free 测试通过。保留普通进程/输入线程高一级调度、原报送节奏、笔悬停设置、原厂触控持续禁用和五次重试通知策略。实机连点漏点率及边缘误触变化仍需实际操作确认。

Version 0.5.10 halves the small-finger grip exclusion margins while retaining the
original shape/ratio gate, large-contact and palm protections. TSACore output now
prunes contacts individually instead of clearing an entire frame when one contact
lifts. A real-library regression test fails before the fix, then passes 100 overlapping
up/new-down sequences and multi-finger drag continuity through HID encoding. All 23
device-free tests and the Release build pass. No extra debounce wait, smoothing,
prediction, rate limit or synthetic mouse injection is introduced. Hardware tap and
edge-mistouch behavior still require confirmation.

---

# 0.5.9 调度与点击生命周期

在 0.5.8 运行中查到整个上游驱动使用 REALTIME_PRIORITY_CLASS（普通线程基础优先级 24）。本机将进程改为 Normal、仅输入 worker 改为 AboveNormal 后，用户反馈“明显变好”，仍有触控起步卡顿和任务栏按钮偶发点击无效。

0.5.9 将该调度策略写入驱动：主进程固定为 NORMAL_PRIORITY_CLASS，仅输入线程使用 THREAD_PRIORITY_ABOVE_NORMAL（基础优先级 9）。后台控制、日志及 MCU 线程保持普通级别，启动日志记录实际进程优先级、输入线程 TID 和相对优先级。不会自动回退到 HIGH 或 REALTIME。规范参考：[Windows 线程调度](https://learn.microsoft.com/windows/win32/procthread/scheduling-priorities)。

还修复了 Windows 触点接口的生命周期解释：原接口根据可选 Windows-pad feature 位，只把部分原始事件当成接触；现在已通过算法报送判定的 Down、Move、LiftOffDebounce、LiftOffHold 均映射为持续接触，最终 LiftOff / WindowsPadLiftOff 才抬起。初始按下防抖仍不提前输出。只修正宿主接口 `Core::TouchAt`，不改算法内部 `Report::IsTouchDown` 的含义、不修改防误触阈值。该问题可能把任务栏点击和拖动拆成过短接触，实际按钮响应仍需验证。

新增调度测试直接验证 Windows 进程与工作线程的实际优先级，并确认控制线程未被提高；新增事件测试及 30 次完整短按回放覆盖 Down → Move → 待抬起 → 最终抬起。ARM64 Release 构建和 21 项 device-free 测试通过。

保留 0.5.8 报送节奏、分阶段耗时统计，以及 OEM 始终禁用、失败重试五次通知和已保存的笔悬停设置。没有引入坐标平均、插帧、预测或报送限速。空闲唤醒和任务栏按钮的实机结果不能用单元测试代替。

Version 0.5.9 persists the live-tested scheduling change: a normal process with only
the input worker boosted one level, instead of a real-time process. The user reported
a clear smoothness improvement during that live test. Windows contact normalization
now keeps reported Move and pending-lift states down until final lift; initial-down
debounce and internal algorithm semantics stay unchanged. Repeated tap/lifecycle and
actual Windows scheduling tests pass, along with all 21 device-free tests. First-touch
and taskbar button response still require hardware confirmation. No extra smoothing,
prediction, batching or rate limit is added.

---

# 0.5.8 报送节奏与卡顿回归

用户实测 0.5.7 在窗口始终跟手时出现了此前没有的画面不顺。试用期间日志没有出现坏帧、SOS 或 VHF 发送失败；这些日志不能确定合成画面卡顿的根因。

0.5.8 撤回未经实机验证的重复中性笔报告省略，恢复此前每个有效帧的笔报送节奏；保留 0.5.7 的缺矩阵不误抬起、损坏笔数据不生成假抬起、明确触点释放、末次位置及失败恢复修复。正常发送只更新一次触点账本，活动触点判断改为常数时间，输入已就绪后不再每帧重复进入 VHF 就绪检查。未增加坐标平均、插帧、预测、报送限速或抬起延迟。

新增 `InputTiming` 分阶段统计：接触期间的输出间隔，以及取帧、解算和发送的平均值/最大值和超过 16 ms 的间隔数。统计在输入线程使用固定计数器，每 30 秒汇总写一行日志；不记录坐标、应用窗口或逐帧日志。忽略进入接触前等待芯片 IDLE 的时间，避免把无输入等待误当成拖拽卡顿。这些数字是驱动内部耗时，不是显示器实际呈现延迟，也不能证明窗口没有掉帧。

完整 ARM64 Release 构建和 19 项 device-free 测试通过；MSI 解包内容与最终编译产物一致。新增测试覆盖空闲唤醒、有效接触间隔、阶段卡顿定位、心跳清零和重启。真实画面顺滑度仍以用户实测为准。

Version 0.5.8 reverts redundant-neutral pen report suppression after the user reported
new visual stutter in 0.5.7. Per-valid-frame cadence is restored while drag continuity
and explicit release fixes remain. Touch bookkeeping runs once on successful output,
active-state lookup is constant-time, and the ready check leaves the steady-state path.
Worker-local timing aggregates report active output intervals and read/solve/send
phases every 30 seconds, excluding idle wake waits. No coordinates or application data
are recorded. No smoothing, prediction, batching, rate cap or extra release delay is
introduced. All 19 device-free tests and the Release build pass. Internal timings do
not measure compositor presentation; physical smoothness still needs confirmation.

---

# 0.5.7 拖拽连续性与响应修复

针对手指和笔偶发拖不动、拖动中断的反馈，修复以下输入状态问题：

- 手指没有读取到新矩阵（笔在场时隔帧读取、校验失败）时，不更新触点映射，也不生成抬起。恢复有效矩阵后沿用触点 ID；有效空帧及明确抬起仍立即结束接触。
- 笔通道校验失败时不把填入的“无笔”占位数据送进笔管线，保留最近的 HID 接触状态。有效离开报告仍立即发送；连续五次零帧或校验错误进入现有 SOS 清理与重建流程，避免永久按住。
- 连续坏帧判定移到“双通道均无效”的提前返回之前，同时覆盖手指校验错误及笔零帧。
- VHF 记录实际编码后的触点 ID 和最后位置。停用、休眠、退出及恢复清理均发送带编号、位置和触点总数的明确抬起，取代零触点报告；第二个混合报告的 Contact Count 仍为零。
- 释放与正常发送共用输出锁；部分写入失败保留可能到达系统的触点，重开设备后先释放旧触点再继续发送。
- 橡皮擦模式仅在范围内设置 Invert，接触时才设置 Eraser Switch；无笔时不会把中性报告变成橡皮擦接触。
- 笔离开范围或关闭悬停时，释放报告保留最后成功上报的位置，清除接触和压力，避免离开时坐标跳到原点。
- 保留全部有效触点移动、笔坐标、压力和倾角报告，仅抑制成功发送中性状态后的重复无笔报告，减少手指拖动时的无效同步写入。不增加坐标平均、插帧、抬起延迟或预测。

规范依据：[Windows Touchscreen 必需用法](https://learn.microsoft.com/windows-hardware/design/component-guidelines/touchscreen-required-hid-top-level-collections)、[混合报送模式](https://learn.microsoft.com/windows-hardware/design/component-guidelines/touchscreen-packet-reporting-modes)、[笔状态](https://learn.microsoft.com/windows-hardware/design/component-guidelines/windows-pen-states)。

新增回放测试覆盖 500 次手指移动间穿插缺矩阵、明确抬起及有效空帧、十点触控混合报告释放、部分写入后的待释放状态、500 次笔移动、关闭悬停保留落笔数据，以及橡皮擦悬停/接触/离开状态。

这些测试验证确定的代码路径；不能代替物理手指和笔的拖拽测试，也不能据此给出延迟降低的毫秒数。上游原本断开的笔功能不在本次恢复范围内。原厂触控持续禁用、失败后重试五次并通知，以及笔悬停开关的策略均保留。

2026-11-01 验证：完整 ARM64 Release 构建成功，18 项 device-free 测试全部通过。
MSI 解包后服务、驱动、托盘和设置程序与编译产物一致，四个程序均为 ARM64。
本机 0.5.7 升级后已确认 master 帧与 VHF 就绪，HuaweiThpService 保持 Stopped/Disabled，
原有 `hover_enabled=0` 设置保留。物理拖拽测试及实测延迟仍待确认。

## English

Version 0.5.7 preserves touch identity across frames without a fresh master matrix,
avoids turning corrupt pen samples into lifts, and bounds consecutive corrupt frames.
VHF cleanup sends explicit ups at the last transmitted coordinates. Partial writes
retain cleanup state; device reopening releases old contacts before resuming.
Eraser hover no longer asserts contact or resurrects an absent pen. Only redundant
neutral pen reports are suppressed; all active motion and pressure reports remain.
No extra averaging, prediction or release delay is introduced. Replay tests cover
these state transitions. Physical drag behavior and measured latency still require
hardware validation. Existing OEM-disable, retry and hover-switch policies remain.
