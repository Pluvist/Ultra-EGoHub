# EnergyStar integration

OpenEGo Hub 0.5.34 embeds a native adaptation of
[imbushuo/EnergyStar](https://github.com/imbushuo/EnergyStar), pinned at
`18872289ac4de7d22208f1fc5b156abaa904dff8`. The original C# sources, project and
license are preserved in `third_party/EnergyStar`. The runtime is compiled into
the ARM64 tray executable.

## Operation

The switch in Battery is off by default. Its preference is stored as
`HKCU\Software\OpenEGoHub\EnergyStarEnabled`. The settings window submits a
request to the tray and reads its actual status; a saved preference alone is
not treated as proof that the feature is running.

While enabled, the tray uses foreground-window events to restore foreground
responsiveness, and applies execution-speed EcoQoS plus idle priority to
eligible background processes. Like the original project, it applies on both
AC and battery power. A five-minute housekeeping pass discovers background
processes without a fast polling loop. The work runs on its own thread.

Only eligible processes in the current user session are considered. The
original bypass list is retained and extended to protect OpenEGo Hub, its
touch driver and the keyboard, pen, display and power hosts. Session-zero
services, inaccessible processes and excluded system/input processes are not
modified. The feature requests no elevation.

## Shutdown and state ownership

The implementation records original process priority and execution-speed
throttling state before changing a process, and retains process identity to
avoid restoring a reused PID. Disabling the feature or exiting the tray
unhooks foreground events and restores its changes. It does not reset every
process to normal priority or overwrite unrelated power-throttling bits.

Startup, switching and failure states are reported separately. Disabling and
initial process enumeration do not run on the settings UI thread. While off,
there is no foreground hook, housekeeping timer or worker thread.

The touch service, pen-hover preference, OEM-touch-disabled policy, Bluetooth
behavior, keyboard-attachment glow and headphone layout remain independent.

## Attribution

Copyright 2022 Bingxing Wang. EnergyStar's MIT license with its Microsoft
exemption is included in `THIRD-PARTY-NOTICES.md` and
`third_party/EnergyStar/LICENSE`. The native adaptation derives its
foreground/background algorithm and bypass list from upstream
`EnergyManager.cs`, `Program.cs` and `Interop/HookManager.cs`.

API references:

- [SetProcessInformation](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation)
- [SetWinEventHook](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwineventhook)
