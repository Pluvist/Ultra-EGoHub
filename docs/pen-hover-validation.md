# 0.5.6 笔悬停验证记录

2026-11-01，Windows 11 ARM64，Release 构建。

- 完整构建成功，17 项 device-free 测试通过。
- 报告测试覆盖开启时保持原始数据、关闭时抑制普通/橡皮擦悬停，以及保留实际接触的坐标、压力和倾角。共享通道测试覆盖实时切换和写端重建；控制/状态通道测试覆盖新标志和原有宿主健康位兼容。
- 0.5.6 MSI 解包后，服务、驱动、托盘、设置程序与编译产物哈希一致，四者均为 ARM64。驱动服务保持按需启动。
- 本机升级返回 0。界面关闭后 INI 写入 `hover_enabled=0`；驱动约 200 ms 内读到设置。开启、关闭均已观察到服务与驱动日志。
- 带关闭设置升级并重启服务后，13:19:03.692 驱动读取 `enabled=0`，13:19:04.401 才确认 master 帧与 VHF 就绪；界面仍显示关闭。
- 升级后继续切换开关成功。验证结束时恢复默认开启状态。HuaweiThpService 始终 Stopped/Disabled，替换驱动 Running。
- 首轮界面测试曾发生一次 Microsoft.InputStateManager.dll 异常退出（0xc0000409）；设置和驱动指令已生效。重新打开后及最终安装版本未复现，根因尚未确定，不能据此宣称该异常已修复。
- 尚未完成物理笔悬停/落笔、压感、倾角、休眠唤醒的实机测试。上游标明主动笔集成断开；本改动只控制已有 VHF 笔报告，不恢复上游缺失能力。

Version 0.5.6 passes the full Release build, 17 device-free tests, extracted MSI
payload/architecture checks and a local upgrade. UI changes reach the driver and
persist before input startup. Physical pen behavior remains unverified. One early
settings-window crash in Microsoft.InputStateManager.dll did not recur after
reopening or in the final installed build; its root cause remains unknown.
