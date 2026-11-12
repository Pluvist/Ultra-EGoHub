# 华为耳机控制（0.5.16）

配件页增加耳机卡片，沿用当前 `SettingsCardStyle`、`SettingsCardCompactStyle`、分组标题、
原生 ComboBox / Expander 与 BatteryIndicator。宽窗口下位于笔和键盘卡片下方，窄窗口纵向排列。
耳机功能由当前用户的设置程序直接访问 Windows 蓝牙，不依赖电脑管家的配件组件或触控服务，
不改变已经由用户确认流畅的 0.5.15 触控配置。

## 协议来源与实现边界

参考 [HuaweiPods](https://github.com/Nshpiter/HuaweiPods) 的公开协议观察，固定参照提交
`dc5b9d14e9030be95b178699d674d1a3217e1e6d`。
该参考项目使用 GPL-3.0；本项目未打包其 APK、Kotlin 实现或 Android Hook。0.5.17 另外直接从华为官方 CDN 导入产品图，见[内置图片说明](headphone-connection-images.md)。
本次新增 Windows 代码依据通信格式、命令参数和机型能力事实独立实现，采用本仓库许可。
已有第三方 EGoTouchRev 的许可与来源保持不变。

源文件参照：

- `HuaweiAncPackets.kt`：电量、设备身份、降噪与通透命令。
- `HuaweiAncLevelProfile.kt`：各机型降噪档位与通透子模式的差异。
- `HuaweiRfcommResponseParser.kt` / `HuaweiDeviceInfoIdentity.kt`：校验、字段与型号身份。
- `HuaweiWearDetectionController.kt` / `HuaweiLowLatencyController.kt`：佩戴与低时延。
- 各型号 controller、`HuaweiGestureController.kt` / `HuaweiEqualizerCodec.kt`：经过型号隔离的设置。

Windows 通信依据 Microsoft 的 [Bluetooth and connect](https://learn.microsoft.com/en-us/windows/win32/bluetooth/bluetooth-and-connect)。
使用 AF_BTH / RFCOMM 与已配对设备连接，优先 SPP UUID
`00001101-0000-1000-8000-00805F9B34FB`，其次使用系统已记录的非音频服务 UUID。
不扫描猜测原始通道号，不向 HFP/HSP/A2DP/AVRCP 音频服务发送 AAM 数据，不修改系统配对或音频配置。

## 线格式

| 部分 | 含义 |
| --- | --- |
| `5A` | 帧起始 |
| 2 字节大端长度 | 整帧长度减 5 |
| `00` | 通道 / 保留字段 |
| service、command | 各 1 字节 |
| fields | 通常为 tag、1 字节长度、value 的 TLV 序列 |
| 2 字节 CRC | 大端 CRC-16/XMODEM，多项式 0x1021，初值 0，覆盖此前全部字节 |

例如电量查询为 `5A0009000108010002000300FBB9`。
响应电量在 01/08 或 01/27 的字段 02（左、右、盒）与 03（充电标记）。
超过 100 的电量编码表示未知，不显示成虚假的 100%。

| 服务 / 命令 | 用途 |
| --- | --- |
| 01/07 | 设备身份，资源型号、前缀、model code 与配色需相互一致 |
| 2B/04、2B/2A | 噪声控制写入 / 状态，现代状态字段 01 为 `[submode, mode]` |
| 2B/10、2B/11 | 佩戴检测写入 / 状态 |
| 2B/6C | 低时延，字段 01 写入、字段 02 查询 |
| 2B/A2、2B/A3 | 高清音质写入 / 状态 |
| 2B/B4 | 带 feature selector 的智慧功能，必须验证 selector 后才接受值 |
| 2B/49、2B/4A | 音效与 10 段均衡器写入 / 状态 |
| 01/1F、01/20、01/25、01/26 | 双击 / 三击写入及查询 |
| 2B/16、2B/17、2B/1E、2B/1F | 长按、滑动写入及查询 |
| 2B/92、2B/93 | 捏合，携带 slot 与通话 / 媒体 context |
| 2B/31 | FreeBuds 7i 已配对设备列表与显式移除 |

少数上游抓包记录的滑动音量 setter 使用固定 side descriptor，而非响应的普通 TLV 表达。
实现仅为匹配型号保留这两个固定命令；不暴露任意原始命令入口，也不放松响应解析。

## 型号能力

控制项按型号白名单生成，未适配型号只尝试读取通用身份和电量。
用户改名后可通过经过校验的 DeviceInfo 识别；已读取到的新资源型号优先于名称猜测。

| 型号 | 本版控制范围 |
| --- | --- |
| FreeBuds 3 | 降噪开关、9 档方向、左右双击；原 Android HFP 电量监听未在 Windows 复用 |
| FreeBuds 4 / 4E | 降噪两档、佩戴、3 种音效、双击与长按 |
| FreeBuds 5 | 降噪三档、佩戴、音效、高清音质与低时延 |
| FreeBuds 5i | 三态噪声与四档强度、佩戴、音效、高清音质、低时延、双击 |
| FreeBuds SE 4 ANC | 三态噪声、防风噪、音效、均衡器和低时延；智慧动态档沿用上游待核验状态 |
| FreeBuds 6i | 三态噪声与人声通透、佩戴、音效、均衡器、低时延、左右双击 / 三击 / 长按 / 滑动 |
| FreeBuds 6 | 降噪三档、佩戴、音效、自适应音量、掉落提醒、盒提示音、低时延 |
| FreeBuds Pro 3 | 三态噪声与四档强度、通透人声、佩戴、低时延、长按、捏合与滑动音量 |
| FreeBuds Pro 4 | 电量与降噪 / 关闭；没有状态回读时不伪造“已确认” |
| FreeBuds Pro 5 | 三态噪声与四档强度、三种通透、佩戴、音效与均衡器、高清音质、低时延、自适应音量、头动 / 语音、空间音频、双设备开关、盒提示音、耳塞材质、三击 / 捏合 / 滑动音量 |
| FreeBuds 7 | 三态噪声与三档强度、佩戴、音效与均衡器、高清音质、低时延、智慧设置、空间音频、双击 / 三击 / 捏合 / 滑动音量 |
| FreeBuds 7i | 三态噪声与四档强度、佩戴、音效与均衡器、高清音质、低时延、头动、空间音频、双击 / 三击 / 长按 / 滑动音量、已配对设备列表与移除 |
| FreeClip | 通用电量，无主动降噪 |
| FreeClip 2 / 典藏版 | 电量、手势、空间音频与场景、音效、佩戴 / 提醒 / 音量等开关、头动动作；没有主动降噪 |
| FreeArc | 电量、音效、均衡器、双击 / 三击 / 长按 / 滑动，无主动降噪 |
| Eyewear / Eyewear 3 | 通用电量，无主动降噪 |
| Eyewear 2 | 电量、双击和低时延 |
| FreeBuds SE 5 / 其他未知型号 | 上游此提交没有控制能力证据，不套用 SE 4 ANC 或 5i 的写命令 |

这些是协议适配范围，不是所有机型在 Windows 上均已实测通过的保证。
Android / HyperOS 的系统弹窗、超级岛、融合设备中心、HFP 私有监听和账号功能没有移植到 Windows。
高清音质选项不会为 Windows 增加它本身没有的蓝牙编解码器。

## 状态与资源使用

蓝牙事务在后台执行，不堵塞 UI 或触控工作线程。只在配件页可见时每 30 秒读取电量，
连接首次建立及手动刷新才批量查询设置；隐藏、切页和关闭取消查询并释放旧连接。
设置仅在用户操作时写入，不在连接后强制套用默认值。CRC 错误、断包、重复 TLV、错误 service / command / selector
不能满足回读；ACK 或 send 成功也不等于设置已应用。没有确认时显示未确认，保留刷新入口。
切换窗口可见性后，旧事务回调不得覆盖新会话；均衡器草稿不会被周期电量刷新覆盖。

自定义音效限定 10 段、每段 -6.0 至 +6.0 dB、3 个保存位置和受限长度名称。
FreeBuds 7i 的已配对设备移除需在应用中明确确认，且仅能选择刚读取到的记录。

## 验证

新增 `HuaweiHeadphonesProtocolTest`：抓包字节、CRC 标准向量、任意分包位置、粘包、坏校验、重复字段、
型号隔离、ACK 与状态区别、均衡器范围，以及已配对列表命令。全部无需设备的回归测试共 25 项。
本机曾在实际设置窗口成功连接 FreeBuds Pro 5，并显示左右耳和充电盒电量；宽、窄布局已查看。
现场进一步读取了 FreeBuds Pro 5（00016D）的 21 项设置状态。低时延完成了关闭→开启→关闭的真实写入与回读测试，并恢复到原始关闭状态。自动化测试没有修改音效、降噪或配对记录。用户随后在已安装的 0.5.16 中确认：界面显示正常，降噪 / 通透切换也已生效。其余各项写入后的实际效果仍需用户逐项验证。

## English

Version 0.5.16 adds a native headphone card to Accessories, using the existing settings
card/header/battery components. A per-user background RFCOMM client communicates with
paired headsets independently of the touch driver. Model-specific controls cover ANC,
transparency, wear detection, latency, audio effects/EQ, gestures and the verified smart
settings above. Unknown models cannot receive guessed control writes. FreeBuds SE 5 is
not in the referenced upstream control whitelist.

The wire format and model facts were analyzed from HuaweiPods commit
dc5b9d14e9030be95b178699d674d1a3217e1e6d. Its Android implementation and system hooks are not bundled. Version 0.5.17 separately imports official CDN artwork, with provenance retained. The Windows implementation uses this repository's license.
Responses require framing, CRC and field validation; setting state is confirmed by
readback rather than successful transmission. Idle/hidden pages do not poll, cancelled
sessions cannot overwrite newer state and EQ drafts survive battery refreshes.

Protocol tests cover captured frames, fragmentation, model isolation and strict
readback. A local FreeBuds Pro 5 connection and battery reads were observed in the
actual UI. A later native probe read 21 settings and verified a low-latency off/on/off write/readback cycle, restoring the original off state. The user also confirmed correct UI display and working ANC/transparency switching in the installed 0.5.16. Other setting changes have not all been verified on hardware.
Android-specific integrations and private HFP battery hooks are outside this Windows
implementation. The user-validated 0.5.15 touch driver and its scheduling stay intact.
