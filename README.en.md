Ultra-EGoHub is a fork of [OpenEGo Hub](https://github.com/NihilDigit/openego-hub), with an input driver based on [EGoTouchRev](https://github.com/awarson2233/EGoTouchRev). See the [changelog](CHANGELOG.md) for the main changes.

The public build uses the built-in headphone fallback icons. See the [third-party notices](THIRD-PARTY-NOTICES.md) for artwork provenance and rights.

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

<p align="center"><a href="README.md">中文</a> | English</p>

Ultra-EGoHub is a hardware control centre for the HUAWEI MateBook E Go on Windows 11
ARM64, covering touch, pen and keyboard configuration, battery and power management, and
display colour.

It is built as a native ARM64 service alongside ARM64EC host processes. The native EGoTouchRev driver supplies input; ARM64EC hosts provide accessory and
display features through the vendor modules. Keep Huawei PC Manager installed.

> [!IMPORTANT]
> A complete installation of Huawei PC Manager must be retained on the system.
> Accessory status, battery thresholds, and colour management directly rely
> on low-level DLLs shipped with PC Manager; removing related components will cause
> the corresponding features to fail.
>
> This dependency introduces no background overhead: Ultra-EGoHub disables the
> official x64 services of PC Manager and removes its logon autostart entries, so
> they no longer remain resident in the background, eliminating the original
> emulation overhead. Required DLLs are loaded on demand by this project's host
> processes.

---

## Features

- **Touch.** Input is supplied by EGoTouchRev; palm rejection requires hardware validation.
  Version 0.5.17 adds headphone connection popups and supports artwork matching across 17 resource models / 71 colors; see [connection and artwork notes](docs/headphone-connection-images.md). Headphone controls are covered in [protocol and support notes](docs/headphones-protocol.md). Existing touch behavior is preserved; previous response work is described in [drag and response notes](docs/drag-response.md). Version 0.5.18 places the card top 24 DIP below the monitor work area, centres it horizontally and keeps its bounds on-screen. Version 0.5.19 moves image lookup, decode and cropping off the UI thread, shares a bounded bitmap cache, and publishes validated headphone information as it arrives; see [popup loading](docs/popup-loading.md). Version 0.5.20 persists frequently used accessory artwork across processes, uses verified cached headphone colors on reconnect, and gives all notifications eased top-entry and upward-exit motion. Version 0.5.21 waits for status and artwork preparation, then presents a static snapshot without live battery/image updates; caching and top-slide motion remain. Version 0.5.22 fixes notifications lost to delayed connection flags, queries only SPP identity/battery for popups, reuses freshly completed reads and retains bounded retries; unavailable fields remain static unknown values. Version 0.5.23 reads only current batteries for verified cached headphone identities, probes cached state immediately after device changes instead of waiting for the 15-second fallback, and overlaps popup construction with the read.
- **Pen settings.** Accessories → Pen has a Pen hover switch, enabled by default.
  Turning it off suppresses the cursor and hover events while the tip is up, while
  retaining contact reports. Changes apply immediately and persist across restarts.
  Pen input and side-button functionality depend on the current upstream driver support.
- **Keyboard.** Control over whether the detachable keyboard keeps its wireless link once
  detached.
- **Headphone reconnect.** Accessories → Headphones provides Fast reconnect and Reconnect now. Version 0.5.25 starts disconnect attempts after about 250 ms and uses precisely matched passive Bluetooth advertisements to trigger bounded reconnection. Cached fallback checks remain every 2 seconds. Results depend on the headset and driver; see [reconnect notes](docs/headphone-reconnect.md).
- **Consistent accessory layout.** The headphone image frame is 104×72, headphone cards match the pen/keyboard's equal-width responsive columns, and every option has an appropriate Segoe Fluent Icons glyph. Version 0.5.27 unifies headphone group, setting and action icons at 20 DIP in 24×24 alignment frames. Resizing preserves control instances and unsaved EQ drafts.
- **Equalizer dialog.** Version 0.5.28 opens custom EQ from a button into a dialog containing the slot, name and ten bands. Closing and reopening preserves the draft. Only Save and apply writes to the headset; confirmed readback closes the dialog, while failure retains the draft and displays feedback.
- **Keyboard attachment glow.** Version 0.5.31 shows an approximately one-second white edge glow on the primary display when the keyboard changes from detached to attached. Its brightness and width are slightly increased. It fades toward the interior, brightens then fades out, takes no focus and does not block input; the Device notifications preference controls it. Charging changes alone and an already-attached keyboard at startup do not trigger it. The EQ entry becomes a full clickable card with a trailing arrow. Drafts can be edited offline and applied after reconnecting and refreshing.
- **Collapsible headphone menus.** Version 0.5.32 groups sound, device settings, gestures and paired devices into expandable menu lists, keeping the existing icons, options and actions. Groups start expanded and retain their state across refreshes and window resizing. Custom EQ still opens its separate editor from the arrow card.
- **Aligned headphone cards.** Version 0.5.33 matches the menus, paired devices, custom EQ and reconnect setting to the information card's width, with one column of options per menu. Fast reconnect sits below the custom EQ card.
- **EnergyStar efficiency mode.** Version 0.5.34 embeds EnergyStar's background efficiency logic natively, with a default-off switch in Battery that works on AC or battery power. Foreground activation, disabling and exit restore the states changed by this feature; the hub, touch driver and accessory hosts are excluded. See the [integration notes](docs/energy-star.md).
- **Frontend input and scrolling.** Version 0.5.35 uses ScrollView for the explicit page and EQ containers. Finger panning remains enabled; pen clicks and drags remain with child controls instead of the page's legacy pen manipulation routing. Rendering ignores timestamp-only publications, and device layout runs only when visibility or width changes. Driver reports, cadence and scheduling are unchanged. The pen-crash mitigation still requires physical-device validation.
- **Restore pen scrolling.** Version 0.5.36 restores native pen panning and inertia in those ScrollView containers while retaining clicks, control dragging and refresh optimizations. Current hover and EnergyStar preferences remain as chosen by the user. Pen clicking, panning and stability require device testing together.
- **Native touch and pen input.** Version 0.5.38 uses WinUI 3 ScrollViewer for touch, pen panning and inertia, with an explicit transparent hit-testable background and no simulated dragging. It retains stable Windows App SDK 2.6.4 and its input fixes introduced in 0.5.37. Physical pen scrolling and stability with the classic native control still require validation. The matching ARM64 Windows App Runtime 2.6.4 or a compatible newer release is required before installation. Drivers and user preferences remain unchanged.
- **Native input diagnostics.** The user confirmed that 0.5.38 still accepts pen clicks but does not pan inside the app. Version 0.5.39 adds default-off local read-only observation of input types and native scroll state, without replacing WinUI gestures. See [diagnostics](docs/native-input.md).
- **Pen recovery after sleep.** Version 0.5.41 configures the pen after panel power returns and the input path receives a valid frame. It retries an incomplete handshake within a bounded window and handles initialization requests that arrive during recovery.
- **Battery and power.** Smart charging or a charge threshold of your own, plus battery
  health, cycle count and remaining runtime.
- **Colour and display.** sRGB and Display P3 gamuts, colour temperature and an eye
  comfort mode. The values reach the panel through the display driver, from its factory
  calibration.
- **Services.** Status and start/stop control for Huawei's background services.
- **Updates.** A periodic check for new releases; once you confirm, the download,
  checksum and install happen inside the app, with no installer to fetch by hand. The
  check can be turned off, and a version you do not want can be skipped.
- **Device information.** Live battery level, charge state, attach state, firmware
  version and serial number for the pen and the keyboard; model, processor, memory and OS
  build for the machine itself.

---

## Screenshots

Screenshots below are from upstream OpenEGo Hub.

### Accessories

Battery level, connection state, firmware version and serial number for the pen and the
keyboard, along with the side-button configuration.

<p align="center">
  <img src="https://raw.githubusercontent.com/NihilDigit/openego-hub/main/Assets/screenshots/accessories.png" alt="Accessories page" width="820">
</p>

### Tray and connection prompts

The tray sits in the notification area with accessory status, and raises a prompt when
the pen or the keyboard connects.

<p align="center">
  <img src="https://raw.githubusercontent.com/NihilDigit/openego-hub/main/Assets/screenshots/popup-pen.png" alt="Pen connection prompt" width="340">
  <img src="https://raw.githubusercontent.com/NihilDigit/openego-hub/main/Assets/screenshots/popup-keyboard.png" alt="Keyboard connection prompt" width="340">
</p>

### Battery and power

Smart charging or a charge threshold of your own, with battery health, cycle count and
remaining runtime.

<p align="center">
  <img src="https://raw.githubusercontent.com/NihilDigit/openego-hub/main/Assets/screenshots/battery.png" alt="Battery page" width="820">
</p>

### Colour and display

Gamut, colour temperature and the eye comfort mode.

<p align="center">
  <img src="https://raw.githubusercontent.com/NihilDigit/openego-hub/main/Assets/screenshots/screen.png" alt="Display page" width="820">
</p>

### Services

Status and start/stop control for Huawei's background services.

<p align="center">
  <img src="https://raw.githubusercontent.com/NihilDigit/openego-hub/main/Assets/screenshots/services.png" alt="Services page" width="820">
</p>

### Device information

Model, processor, memory and OS build for the machine itself.

<p align="center">
  <img src="https://raw.githubusercontent.com/NihilDigit/openego-hub/main/Assets/screenshots/device.png" alt="Device page" width="820">
</p>

---

## Supported hardware

- **Machine.** HUAWEI MateBook E Go, Windows 11 ARM64.
- **Pen.** HUAWEI M-Pencil — CD52, CD54, CD54R and CD54S; only CD54R has been tested on
  real hardware.
- **Keyboard.** The HUAWEI smart magnetic keyboard.

---

## Installation

### Prerequisites

The factory Huawei drivers and PC Manager have to stay installed. Panel colour
calibration, the charge threshold and the keyboard's wireless link all rely on DLLs that
come with them. Install the [ARM64 Windows App Runtime](https://learn.microsoft.com/windows/apps/windows-app-sdk/downloads) compatible with Windows App SDK 2.6.4. Colour gamut presets require a compatible Qualcomm display library supplied by the system or installed locally.

### Steps

1. Download the latest `Ultra-EGoHub_arm64_vX.Y.Z.msi` from the
   [Releases](https://github.com/Pluvist/Ultra-EGoHub/releases/latest) page.
2. Run the installer. Registering the system service requires administrator rights.
3. The service starts with Windows from then on, and the settings window opens from the
   Start menu.

### Upgrading and uninstalling

- The installer cleans up before it installs: services, program files and shortcuts left
  by any previous version — including pre-rename and test builds — are removed
  automatically, with no manual uninstall needed first.
- User configuration survives an upgrade: settings, logs, and the records needed to
  restore any Huawei services and autostart entries you disabled.
- You can upgrade from the settings window: the background service downloads, verifies
  and installs, the tray and the window step aside while it does, and both come back
  afterwards.
- Huawei components you disabled stay disabled across an upgrade. Coming from v0.4.0 or
  earlier they are restored once more, because those versions' uninstall sequence is
  already recorded on the machine.
- OEM touch remains disabled on exit, failure and uninstall. Other Huawei services
  and logon entries follow the existing uninstall behavior.

### How it behaves

- **Touch driver.** The system service owns input independently of the tray. OEM touch
  remains disabled. After an initial startup failure, it retries five times at two-second
  intervals, then notifies once and stops retrying. Restart the service to begin a new batch.

---

## Build from source

### Requirements

- CMake 3.20 or newer
- Ninja
- The Visual Studio ARM64 MSVC toolchain (`cl.exe`)
- WiX Toolset v4, for the MSI

### Building

The ARM64 build environment has to be loaded into the PowerShell session first;
`scripts\build.ps1` does that itself:

```powershell
# Release build
.\scripts\build.ps1 -Config Release

# Debug build, followed by the test suite
.\scripts\build.ps1 -Config Debug -Test
```

In a shell that has already run `vcvarsarm64.bat`, the presets work directly:

```powershell
cmake --preset arm64-Release
cmake --build --preset arm64-Release
```

### The HAL builds separately

The hardware abstraction layer and its host processes under `hal/` build on their own,
and have to go first: the main project links the `GaokunHal.lib` they produce.

```powershell
cd hal
.\scripts\build.ps1 -Config Debug
cd ..
```

> The service itself is native ARM64. Only the hosts under `hal/` that load Huawei's x64
> DLLs are compiled as ARM64EC, and they come from the HAL's own build script.

### Development loop

Register the Debug service once from an elevated shell, then iterate with the development
scripts:

```powershell
# Register the Debug service
scripts\install_debug_service.bat

# Rebuild and restart it (needs administrator rights)
pwsh -File scripts\dev-cycle.ps1

# Restore the installed Release service
pwsh -File scripts\dev-cycle.ps1 -RestoreRelease
```

### Packaging

WiX produces the ARM64 MSI:

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

Or let the packaging script do the whole thing:

```powershell
scripts\pack_release_version.bat
```

---

## Layout

- `EGoTouchService/`: the system service. `Device/` for acquisition and virtual HID
  injection, `Tsa/` for the vendor algorithm adapter, `Host/` for the system interfaces.
- `hal/`: the hardware abstraction layer and its ARM64EC hosts, which isolate and drive
  Huawei's x64 DLLs.
- `Common/`: cross-process channel definitions, shared-memory layouts and common
  configuration.
- `Tools/`: the user-session applications — the tray (`EGoTouchTray`) and the WinUI 3
  settings window (`EGoTouchSettings`).
- `docs/`: reverse-engineered protocol documentation and design notes.
- `scripts/`: build, test, packaging and development scripts.

---

## Credits

This project is derived from **[EGoTouchRev](https://github.com/awarson2233/EGoTouchRev)**
(MIT License, © Detach2233) and inherits its Himax frame acquisition, the pen's MCU
protocol, the VHF virtual HID injection layer and the service runtime. The full licence
notice is in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

The following projects and community work were consulted along the way, with thanks:

- Thanks to Nshpiter and the HuaweiPods contributors for documenting the headphone Bluetooth protocol. Ultra-EGoHub independently implements its Windows headphone controls, using HuaweiPods as a reference for battery reporting, noise cancellation, transparency mode, and equalizer commands. [HuaweiPods (GPL-3.0)](https://github.com/Nshpiter/HuaweiPods) · [Protocol and implementation notes](docs/headphones-protocol.md)
- [MateBook-E-Pen](https://github.com/eiyooooo/MateBook-E-Pen) (by eiyooooo)
- [goodies](https://github.com/matebook-e-go/goodies) (by dantmnf)
- [EgoTools](https://github.com/SaKongA/EgoTools) (by SaKongA)
- [HuaweiPenEraserService](https://github.com/qwqVictor/HuaweiPenEraserService) (by qwqVictor)

---

## Licence

Released under the [MIT License](LICENSE).

Per its terms, any redistribution in source or binary form has to keep the copyright
notices in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

---

## Disclaimers

- **Trademarks.** This project has no official affiliation with, or endorsement from,
  HUAWEI, Himax or any other rights holder. Product names, trademarks and company names
  belong to their respective owners.
- **Third-party assets.** At runtime the tray tries to load the pen and status icons from
  a locally installed Huawei PC Manager, and falls back to built-in vector drawing when it
  is not present. Those copyrighted vendor assets are in neither the source nor the
  releases.
- **Use at your own risk.** This project works against low-level hardware drivers and
  system services, and exists for interoperability research and technical exchange. You
  assume the risk of using it; the authors and contributors accept no liability for
  hardware faults, data loss or system instability arising from it.
