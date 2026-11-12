# 耳机快速重连 / Headphone reconnect

0.5.25 针对已配对耳机重连，使用 Windows 官方发现与音频接口，保留系统全局扫描周期和无线电配置。

- “配件 → 耳机 → 加快耳机重连”默认启用，可关闭并记住设置。HCI 连接事件仍立即处理；缓存连接状态兜底从 15 秒缩短至 2 秒。这是读取系统缓存，不是主动射频搜索。
- 自动重试只针对最近真正连接过的华为耳机。明确断开后约 250 毫秒开始本轮，按 0、1、3、7、15、30 秒偏移最多六次；与通知的 3 秒去重分开。迟到的请求合并，不连续补发。
- 功能启用且目标断开时，被动 BLE 监听只接受 Public 类型且与所选已配对耳机经典地址完全相同的广播，不猜随机地址，不保存其他设备的广播内容。本机已观察到 FreeBuds Pro 5 的精确匹配广播。监听不可用时安全回退；错误后至少 30 秒再试。
- 目标重新出现可以触发新的有界轮次，连续广播不延长轮次；至少 5 秒静默才算新的一次出现。关闭功能、目标连接或清零后停止监听。
- 最近目标仅在真正连接过后写入当前用户的 LastHeadphoneAddress。启动恢复重新核验配对和类型，只恢复监听目标，不伪造已连接，也不自动轮流连接所有配对设备。
- “立即重连”只对当前列表中明确选择的未连接华为耳机提交请求；随后最多观察 8 秒的真实连接状态。提交成功不代表连接完成。
- 请求通过 Windows 官方 `KSPROPERTY_ONESHOT_RECONNECT` 音频属性。仅对重新核验仍已配对、且音频端点能够可靠匹配到蓝牙地址的设备发出；不改变默认音频输出、配对关系、无线电、设备启停状态或耳机设置。音频驱动不支持时安全停止；可继续使用 Windows 蓝牙设置。
- 耳机必须开机、在范围内且可接受连接。多设备连接策略、耳机固件与驱动可能限制重连，不能保证固定秒数完成。连接弹窗仍一次读取完本轮数据和图片后静态显示。

Version 0.5.25 targets paired headphone reconnection through documented Windows discovery and audio APIs, preserving the global scan interval and radio settings.

- Accessories → Headphones → Fast reconnect is on by default and persists per user. HCI events remain immediate; cached connection fallback checks change from 15 to 2 seconds. These checks do not perform radio inquiry.
- Automatic attempts target only the most recently connected Huawei headset. A confirmed disconnect starts a round after about 250 ms, with up to six attempts at offsets 0, 1, 3, 7, 15 and 30 seconds. Notification debounce remains independently 3 seconds. Overdue attempts coalesce rather than burst.
- While enabled and disconnected, passive BLE observation accepts only Public advertisements exactly matching the selected paired classic address. Random addresses are never guessed and unrelated advertisement content is not stored. Exact FreeBuds Pro 5 matches were observed locally. Unavailable observation falls back safely, with at least 30 seconds between failure retries.
- A new presence episode can trigger a finite round. Continuous advertisements do not extend it; at least five quiet seconds are needed for another episode. Disabling, connecting or clearing the target stops observation.
- Only an actually connected last target is persisted per user in LastHeadphoneAddress. Startup restoration revalidates pairing and type and selects a listening target without pretending a connection or cycling all paired devices.
- Reconnect now targets the explicitly selected disconnected Huawei headset, then observes real connection state for at most 8 seconds. Acceptance of a request does not establish successful reconnection.
- Requests use the documented Windows `KSPROPERTY_ONESHOT_RECONNECT` audio property. Pairing is revalidated and the audio endpoint must map reliably to the Bluetooth address. Default audio routing, pairing, radio configuration, device enablement and headset settings are unchanged. Unsupported drivers stop safely; Windows Bluetooth settings remain available.
- The headset must be powered on, reachable and willing to connect. Firmware, drivers and multipoint policy affect the result. No fixed reconnection time is promised. Popups retain their prepared, static data/artwork snapshot.

References:

- [Windows audio reconnect property](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/ksproperty-oneshot-reconnect)
- [Accessing audio properties through IKsControl](https://learn.microsoft.com/en-us/windows/win32/coreaudio/using-the-ikscontrol-interface-to-access-audio-properties)
- [Bluetooth device search parameters](https://learn.microsoft.com/en-us/windows/win32/api/bluetoothapis/ns-bluetoothapis-bluetooth_device_search_params)
- [Passive advertisement watcher](https://learn.microsoft.com/en-us/uwp/api/windows.devices.bluetooth.advertisement.bluetoothleadvertisementwatcher)
