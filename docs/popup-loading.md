# 弹窗加载优化（0.5.19）

## 0.5.23：缩短仍超过 5 秒的等待（当前行为）

对经过地址、配对状态及当前型号校验的常用耳机，直接使用已确认的型号/配色，只查询本轮电量，
有效电量一到即结束，不再重复请求完整身份。未知耳机仍走身份加电量读取；快速路径不把缓存身份当成新验证证据。
通知窗口的隐藏构建与电量读取重叠，准备完成后才弹出静态内容。

系统设备变化现在会立即触发只读缓存扫描，并在 100/500/1200/2500 ms 短时跟进。
这样即使某次 HCI 事件缺失，也不必直接等 15 秒兜底；重复提示不延长当前 burst，空闲频率不变。
托盘会先检查通知宿主是否已就绪，再处理等待期限，避免低优先级计时器迟到造成误丢。

为定位真实阶段耗时，`Common/include/PopupTimingTrace.h` 提供默认关闭的本机诊断。
仅存在 `%LOCALAPPDATA%/OpenEGoHub/Cache/trace-popup` 时才启用，最多 128 KiB，写入后台队列。
日志只含阶段、系统 tick、进程号及错误/标记码，不含设备名称、地址、电量数值或输入内容；
删除该标记立即停止采集。阶段区分设备事件、托盘启动、App/XAML、读取和首次显示，不能用局部 PNG 测量冒充总延迟。

Version 0.5.23 uses battery-only reads for verified cached artwork identities,
overlaps hidden popup construction, and adds event-driven bounded cached probes
after device changes. Static content, pairing/model validation and idle polling
remain. Optional bounded timing diagnostics separate detection/startup/read/render
stages without logging device identifiers or data.

## 0.5.22：耳机连接事件与准备路径修复（当前行为）

真实 ACL 连接通知与 Windows 连接状态缓存存在时间差。旧路径接到通知后若 `fConnected` 还未变为真，
会直接取消；托盘又已去重，后续不会补发。这是已确认的漏弹路径。现在按已配对地址和耳机类型验证，
不让单次缓存连接标志否决真实事件。托盘分别处理 ACL 与缓存快照，最近 ACL 状态双向保护 5 秒，
资料暂不可读时只作最多 2 秒的短重试，平时仍是 15 秒只读缓存兜底，不发无线 inquiry。

通知只通过标准 SPP 建链（最多 2 秒）读取一次身份与电量，不再扫描发现服务/其他非音频服务，也不加载全部设置。
正常耳机控制页面保留原发现和设置读回流程；通知先建立的会话仍会在页面首次刷新时补齐完整设置。
若页面已经在当次通知之后完成有效电量读取，直接复用该结果，避免再建一次连接。
页面忙碌会保留一次待处理通知，由操作完成后重试；早期瞬时失败允许一次短重试。

读完后不再用关闭控制通道后的 `fConnected` 缓存值再次否决有效结果。
可选控制服务或电量字段不可用时，显示本轮未知值，不拿旧电量填充，也不因此让连接通知完全消失。
图片准备与硬件读取重叠，末尾图片等待缩为有界 500 ms，超时使用静态图标。
弹窗仍在读取尝试/图片准备结束后一次显示，展示期间不补图或刷新数据；顶部动画保持。

一次本机只读基线测量中，原路径 SPP 建链约 1.5 秒，身份/电量约 0.57 秒；另一次重连失败扫描约 4 秒。
这些是定位路径的单次样本，不代表修复后所有连接的延迟保证。端点直连实验没有稳定收益，产品未增加通道缓存或猜测端口。

Version 0.5.22 treats explicit ACL events separately from delayed connection-cache
flags, keeps bounded metadata retries and replays pending notifications after
page transactions. Notification transport uses SPP identity/battery only, reuses
post-request live reads, and avoids serial full-profile/image waits. Missing
optional fields produce static unknown values instead of silently losing the
event. Static snapshots, persistent artwork and top-slide motion remain.

## 0.5.21：准备完成后显示静态快照（当前行为）

按用户最新要求，本节取代下面旧版本的“弹出后逐步补齐”行为。
笔、键盘先读取一次共享状态并等待图片准备；耳机完成当次身份/电量读取和本地图片准备，
再一次性显示快照。准备期间不显示占位弹窗，设备或窗口切换会使旧准备任务失效。
缺失/失败的可选数据使用未知值或图标，绝不拿旧电量冒充当次状态。

显示期间不再读取或更新弹窗电量，不继续替换图片，也不为弹窗启用状态轮询。
动画仍为非线性顶部滑入、完整停留 3 秒、向上退出；内容在整个动画期间固定。
常用图片缓存仍在准备阶段使用。首次耳机连接的显示时点需要等待本轮设备回复，
这些等待在弹出前完成，以减少显示期间的工作和跳变。

Version 0.5.21 supersedes progressive updates: every notification waits for its
one-shot data and artwork preparation before presentation, then keeps a static
snapshot throughout entrance, dwell and exit. No notification battery polling or
late image updates run during display. Persistent artwork and top-slide motion
remain; first-time device response latency is paid before presentation.

## 0.5.20：常用图片持久缓存与顶部动画

常用设备现在跨进程缓存图片。首次识别后，耳机地址、经过校验的资源型号及配色保存在
`%LOCALAPPDATA%/OpenEGoHub/Cache/Artwork/devices-v1.txt`，最多 8 条。
重连仍先确认该地址已配对并连接，再按当前型号校验缓存；本地图片准备好后即展示弹窗，
不必等待本轮蓝牙身份回复。随后用最新身份和电量更新；缓存不保存电量、设备名称或控制设置。
若电量先返回，保留已验证的图片；若新身份确认了不同配色，立即使用新身份，旧图片回调不会改回去。

笔、键盘和耳机的裁切后只读位图保存在 `%LOCALAPPDATA%/OpenEGoHub/AccessoryImages/v1`。
文件以原始图片 SHA-256 和格式版本区分，同时校验元数据与像素负载，最多 8 项、32 MiB。
缓存命中后直接恢复 BGRA 位图；缺失、损坏或版本不匹配时重新加载原图。
读写和整理都在后台，不预解码全部图片，不增加蓝牙轮询或常驻进程。

所有通知统一从目标屏幕顶部外侧非线性滑入，在原来的顶部 24 DIP 留白处停留，退出时向上滑出。
切换通知时取消旧完成回调，连续动作从当前画面接续；图片与电量更新不重启动画或延长驻留。
帧更新使用高精度时间，动画期间才订阅渲染回调，并对屏外或隐藏时的帧停发提供兜底。
这些变化保持原有主题、图片、窗口无焦点提示和触控策略。

Version 0.5.20 persists up to eight verified headphone artwork identities and eight
cropped bitmaps (32 MiB). Reconnect checks pairing/model, prepares cached local
artwork before presentation, then updates live identity and batteries. Cached
data never substitutes for live battery/control values. Strong content/version
keys and integrity checks make damaged or outdated cache entries fall back to
the source image. All notifications use eased top-entry/upward-exit motion with
high-resolution timing, transient frame subscriptions and an off-screen fallback.

## 0.5.19：后台加载与共享解码

笔、键盘、耳机沿用现有图片、名称、电量、主题、动画与驻留方式；保留 0.5.18 的顶部 24 DIP 留白。

- 图片文件/厂商插件查找、解码、透明边界扫描和裁切在后台执行，界面线程只绑定结果。
- PNG 只解码一次，按原 alpha > 2 阈值与 2% 安全边距逐行复制裁切；保留透明度和产品图尺寸。
- 页面和弹窗共用裁切后的只读位图缓存，以完整原始图片字节精确匹配；最多 8 项、32 MiB，按最近使用淘汰。
- 页面已有的笔/键盘图片可直接传给弹窗，不重新解码。笔图共享使用实际加载完成的型号，防止把待切换型号当成已加载型号。
- 只为弹窗启动隐藏设置窗口时，不执行整机信息、电池驱动查询、整机图、色彩参照图或均衡器编辑器初始化。主窗口首次可见后再完成这些内容。
- 托盘异步启动通知窗口，按需计时器投递有界通知队列，取消原来最多 2 秒的消息线程休眠。队列清空即停止计时器；关闭设备提醒、安装或退出时取消待发通知。
- 耳机使用指定地址的本机蓝牙缓存查找，避免枚举全部配对设备。有效身份回复到达后可立即加载对应内置图，有效电量回复到达后立即更新电量。
- 身份和电量均通过现有协议校验后，通知专用读取不再等待额外 250 ms 静默期。回复缺失时仍保留原超时；写入与设置读回的确认规则不变。
- 缓存仅记忆已验证的型号/配色，不把旧电量当成重连后的实时电量。关闭/切换窗口或较新的通知会使旧后台结果失效。

不预解码全部 661 张图片、不增加蓝牙轮询、不改变触控线程或原厂触控禁用策略。
首次启动仍包含 WinUI 运行时和 XAML 建窗成本，耳机硬件响应时间也不能由图片缓存消除。

## 验证

发布时运行 ARM64 Release 构建、设备无关测试及全部内置图片的安装包/安装目录哈希校验。
裁切算法使用实际 PNG 对比旧输出的尺寸和像素；局部解码/裁切耗时不能等同于完整硬件连接到显示的耗时。
实际体验应分别检查首次连接、同一进程内再次连接，以及主界面关闭后的连接。

本机独立对照涵盖 661 张耳机图和 7 份笔/键盘/整机资源，835,812,236 个裁切后像素字节、尺寸及 DPI 均一致。
300 组合成边界/透明阈值/正负 stride 样本及缓存键、LRU、预算与并发复用验证通过。
同一轮局部解码裁切累计旧路径 6500.6 ms、新路径 4425.91 ms；这不是完整弹窗或整机性能的测试结果。
并发首次未命中时仍可能各解码一次，缓存入库去重，后续请求复用已裁切位图。

## English

Image lookup, decoding and alpha-bound cropping now run off the UI thread. A
byte-exact, read-only bitmap cache is bounded to eight entries and 32 MiB. Page
images can be handed directly to pen/keyboard popups using their loaded model.
Background-only startup defers unrelated main-page work until the window appears.

Headphone identity and battery replies are published progressively. A popup read
finishes once both validated replies arrive, instead of waiting for a quiet tail;
missing replies retain the timeout and setting-write confirmation is unchanged.
Cached identity never substitutes for live batteries, and late callbacks cannot
revert a final update or re-show a dismissed popup. Placement, visuals, offline
artwork and the touch binary remain unchanged.
