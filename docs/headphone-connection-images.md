# 耳机连接弹窗与内置高清图片（0.5.17）

以下记录描述 0.5.17 本地构建。自 v0.5.41 起，公开源码和 MSI 附带来源清单，耳机界面使用内置替代图标。

耳机连接时复用笔/键盘的 WinUI 通知窗口：相同主题、桌面定位、无焦点激活、渐变与尺寸过渡，
约 3 秒自动消失。显示准确型号/配色图片、名称及左耳、右耳、盒电量，未知电量不伪造。
耳机弹窗也受现有“设备接入提醒”开关控制，关闭提醒不会为连接事件拉起设置程序。

托盘监听 Windows 蓝牙 HCI ACL 连接通知，后台只读缓存枚举每 15 秒兜底；首次启动基线静默，
短断线约 3 秒消抖，重复连接消息不重复弹窗。监视器本身不建立控制连接、不改变蓝牙配对或设置。
设置窗口关闭时仍可由托盘懒启动隐藏窗口显示提醒，无需为此常驻整个设置界面。
连接提醒先显示，电量/身份与图片随后补齐；刷新图片不会重启驻留或使已消失的提醒再出现。
页面事务和通知取数避免同地址并行 RFCOMM。它们都与触控输入工作线程分开。

0.5.18 按用户实测修正位置：以弹窗上边缘定位到当前屏幕工作区顶部下方 24 DIP，水平居中，
并限制窗口尺寸在带边距的可用区域内。较高的耳机卡片不再因中心锚点而越过屏幕上沿。

## 官方资源检查及全量内置

HuaweiPods 提交 `dc5b9d14e9030be95b178699d674d1a3217e1e6d` 并不内置所有耳机的高清图，
它根据设备身份从[华为官方 AAM001 资源库](https://smarthome-drcn.dbankcdn.cn/device/guide/AAM001/00016D/00016D.json)取得配置和配色资源包。

按用户要求，当前能够从上游可靠映射到的全部官方配色资源已直接纳入源码与 MSI：

| 项目 | 结果 |
| --- | --- |
| 已知资源型号 | 17 个 |
| 全部配色包 | 71 个 |
| 静态产品图 | 661 张 |
| PNG 总字节 | 48,358,636（约 46.1 MiB） |
| 资源获取失败 | 0 |
| 排除 | 578 个动画/APNG、123 个小图标 |

高分辨率规则为至少一边 640 像素，同时补入每色最佳主图/单耳/盒产品图；动画及小图标不属于此次高清静态产品图。
所有 71 个身份都有主图；其中 7 个身份没有独立左右耳图，仍保留官方主图。
Pro 5 配色 17 主图为 1008×1008，单耳图为 640×640。最大图 3024×1638。

全部图位于 `Tools/EGoTouchSettings/Assets/Headphones/{modelId}_{colorId}/`。
`Manifest.json` 按图记录官方配置、ZIP 来源、原始条目、尺寸、SHA256 和主图选择。
ZIP/PNG CRC、尺寸和全部文件 SHA256 已校验。下载器使用 HTTP Range 仅提取图片，避免下载完整大资源包。
图片随安装包部署到 `Assets/Headphones`，运行时**不联网下载**，可离线显示。

运行时仅按经过 DeviceInfo 验证的型号和配色选择内置图，不采用官方 defaultSubModelId 猜颜色。
无可信身份或未取得图片时使用现有 Fluent 耳机图标。不会把别的机型图片当成当前耳机。
上游尚未提供可靠图片身份映射的 FreeBuds Pro 4、初代 FreeClip、SE 5 等型号，本版未补造产品图。

## 归属与复现

华为官方产品图的权利归原权利人；官方 CDN 来源不等于这些图片另获 MIT/GPL 授权。
本仓库代码许可不覆盖第三方产品图片，来源清单应随图片保留。HuaweiPods 的 Kotlin、APK、系统 Hook
和项目内置 PNG 没有复制到这里，新增图片直接来自华为官方 CDN。

使用 Python 3 运行 `python scripts/fetch_headphone_images.py` 可复查/恢复内置资源；标准库即可，
下载中间记录存放 `build/headphone-asset-import`。此命令会联网，正常构建不需要再次联网。
`scripts/GenerateHeadphoneAssets.ps1` 为全部 PNG/JSON 生成稳定的 WiX 组件清单，标准打包入口已接入。

## English

Version 0.5.17 reuses the pen connection popup for Huawei headphones, with matching
themes, no-focus presentation, animation and a three-second dwell. A tray-side HCI
event monitor detects connections even when Settings is closed; a cached 15-second
scan covers missed events. Initial baselines are quiet and short disconnections are
debounced. Optional identity/battery reads do not block presentation or the touch worker.

All reliably mapped official static high-resolution images are bundled: 17 resource
models, 71 colors, 661 PNGs, 48,358,636 bytes. All downloads/CRCs/hashes were checked.
Runtime uses no network and matches verified DeviceInfo model/color rather than a
guessed default. Models without a reliable upstream resource identity retain the icon.
Official Huawei artwork remains the property of its rights holders; code licensing
does not relicense the artwork. Manifest.json retains per-file provenance and hashes.

Version 0.5.18 anchors the card top 24 DIP below the current monitor work area,
centres it horizontally and bounds its size within the available area. Taller
headphone cards no longer extend above the screen around the old centre anchor.
