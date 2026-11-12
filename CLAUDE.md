# Repository guidance

OpenEGo Hub targets Windows 11 ARM64. This branch intentionally replaces the old
GaokunThpHost vendor input chain with the pinned EGoTouchRev source snapshot in
third_party/EGoTouchRev. See docs/egotouch-driver.md for architecture, known input
limitations and validation still required. Older documents about the vendor touch
host are historical. Do not restore that host when changing the input provider.

Build from PowerShell: hal/scripts/build.ps1 -Config Release first, then
scripts/build.ps1 -Config Release -Test. Hub uses MSVC for its WinUI settings app;
the upstream driver is built separately with clang-cl by cmake/EGoTouchDriver.cmake.
The driver always uses Release to avoid exposing upstream debug IPC. Keep the
upstream revision and local modifications documented in its UPSTREAM.txt.

Keep README.md and README.en.md synchronized. Do not claim stylus functionality
without hardware verification. Preserve the tray IPC enum ABI. The user requires OEM touch to remain disabled,
with no restoration on exit, failure or uninstall. The service owns input without a tray
lease. Initial failure gets five retries, then persistent error 8 and a single tray
notification; heartbeat renewals must not reset this budget. The public product version lives in CMakeLists.txt and
Common/include/AppVersion.h; change both together. Run device-free tests after a
successful build and verify Release for edits to DeviceRuntime or ServiceHost.

Version 0.5.7 neutral pen report suppression was reverted in 0.5.8 after the user
reported new visual drag stutter. Preserve per-valid-frame cadence and distinguish
driver input timing from compositor presentation. Do not claim physical smoothness
from device-free tests or internal timing alone. See docs/drag-response.md.

The user confirmed smoother dragging after replacing the vendor driver's real-time
process priority with NORMAL and an ABOVE_NORMAL input worker. Persist this isolated
input-thread policy. Core::TouchAt must normalize Move and pending-lift as continuous
contact independently of Windows-pad feature flags; initial debounce is unreported.

Version 0.5.10 halves only small-finger edge rejection margins and retains large
contact/palm handling. Out::PostProcess must not discard other active/new contacts
when one contact lifts. TsaOutputTapIntegrationTest links the real algorithm library;
its test-only executable is never packaged or installed as part of the driver.

Version 0.5.12 fixed observed alternating-tap drops by reducing only quiet, confirmed
small-finger grip admission from 50 ms to 8 ms. Preserve explicit grip, palm, noise,
exception and initial-debounce guards. Contact tracing is default-off, bounded and
contains no coordinates; do not enable or ship its ProgramData marker by default.
The user confirmed repeated taps normal without obvious extra mistouches.

Version 0.5.14 restores the proven 0.5.12 8 ms guarded finger admission after
the user reported visual stutter on 0.5.13. This is a conservative mitigation;
input timings did not establish the cause. Production solver mode skips the
unused decoded heatmap/suffix pass; diagnostics and parser-only mode retain it.
Preserve valid-frame touch/neutral-pen cadence and all existing admission guards.

Version 0.5.15 optimizes private stylus matrix work with equivalent output. Dirty
ranges must clear stale samples before the next active report, including after
disconnect/noise/invalid frames. Preserve legacy anchor bounds; validate against
the independent dense oracle and the previous release. Do not infer whole-system
CPU savings or physical smoothness from function microbenchmarks. The headless
Release driver omits unused GUI log queues but keeps file logs and input cadence.

Version 0.5.16 adds native Huawei headphone controls in Accessories. Keep this
per-user background RFCOMM client separate from the touch service. Only query
paired devices; exclude audio service UUIDs and never guess raw channels. Unknown
models may receive read-only identity/battery queries, not model-specific writes.
Keep model/selector isolation, CRC/TLV validation and readback-based confirmation.
Polling is limited to visible Accessories; cancellation and background cleanup
must not close a newer session. Never override a user's unsaved EQ draft on a
battery refresh. Pair removal requires an explicit in-app confirmation and a
record from the latest device list. See docs/headphones-protocol.md. FreeBuds
Pro 5 connected and returned batteries locally; do not claim all setting writes
or all model/firmware combinations have been tested. Preserve the stable touch
driver/scheduling and OEM-disabled policy.
FreeBuds Pro 5 (00016D) also returned 21 settings; latency off/on/off write/readback was verified and restored to the original off state. Other writes remain individually unverified.
On 2026-11-01 the user confirmed the installed 0.5.16 headphone UI displays normally and ANC/transparency switching works on the connected FreeBuds Pro 5. Preserve this validated protocol/model baseline.

Version 0.5.17 adds event-driven headphone connection notifications and bundled
official artwork (17 resource models, 71 colors, 661 static PNGs). Keep the tray
monitor read-only/cached, startup baseline quiet, duplicate/disconnect grace, and
the existing device-notification preference. Settings should not need to stay
open. Popup reads must not race the visible page's RFCOMM transaction or reuse
stale reconnect battery data. Image completion must not extend/re-show popups or
overwrite newer batteries. Match verified model AND color; never guess a default.
Keep the source manifest/provenance, package all images for offline use, and run
the generator in scripts/GenerateHeadphoneAssets.ps1 during MSI packaging. Official
artwork is not relicensed by this repository's code license. Preserve touch baseline.

Version 0.5.18 fixes the user-observed headphone popup extending above the screen.
Anchor notification top edges 24 DIP below the monitor work-area top and clamp
sizes to available space. Never centre a tall card around the former 10% anchor.
All 661 images remain bundled; preserve the touch binary and prior protocol behavior.

Version 0.5.19 moves popup image lookup/decoding/cropping off the UI thread and
shares a byte-exact, bounded read-only SoftwareBitmap cache. Preserve the alpha>2
crop threshold, 2% padding and original pixels. Main-page images may be passed to
the notification window only for the verified loaded model, not a pending model.
Background-only Settings startup defers unrelated main-page initialization;
initialize headphone sliders before a visible-page refresh. Headphone popup reads
publish validated identity/battery progressively and finish immediately only when
both validated replies arrive; ACKs and unavailable levels do not qualify. Other
setting/write/readback timeouts remain unchanged. Never use cached batteries on
reconnect, re-show a dismissed popup, or let an older callback replace final data.
Preserve 24 DIP top-edge placement, offline images and the unchanged touch binary.

Version 0.5.20 adds per-user persistent artwork caches: at most eight verified
headphone address/model/color identities, and eight cropped bitmaps/32 MiB.
Only real ReadIdentity evidence (Snapshot::identityConfirmed) may update the
device index. Validate paired/connected state and current model before using a
cached identity. Cached artwork never supplies batteries or control settings;
battery-first replies must retain the verified picture until fresh identity
supersedes it. Corrupt/old cache data falls back safely to the original image.
All notifications enter from the screen top with non-linear easing and leave
upwards. Preserve the final 24 DIP top edge, no-focus behavior and readable dwell.
Use high-resolution time, frame-driven movement and a bounded off-screen fallback;
cancel stale generation callbacks and never leave permanent animation timers.
Preserve the stable touch binary, priorities and OEM-disabled policy.

Version 0.5.21 explicitly supersedes progressive/live notification updates.
The user requires every popup to wait for its data/artwork preparation and then
remain a static snapshot throughout its entrance, dwell and exit. Notification
NeedsStateRefresh is always false; compatibility UpdateState methods do nothing.
Image preparation changes caches only, never a currently displayed card. Pen and
keyboard await PrepareConnectionAsync before Show. Headphones complete a one-shot
read, clear old batteries in NotificationState, prepare artwork and Present once;
no placeholders followed by later updates and no notification-driven polling.
Do not restore live refresh when optimizing cached images. Preserve the complete
main-page client session/unsaved EQ draft, per-user caches, top-slide motion and
the unchanged driver/OEM-disabled policy.

Version 0.5.22 fixes the user-reported missed/slow headphone notification path.
A real ACL event must not be vetoed by a lagging cached fConnected bit: use paired
device/type validation and preserve explicit ACL authority over contradictory
snapshots for 5 seconds. Metadata retries are bounded and idle polling unchanged.
NotificationConnectionOptions restrict notification opens to standard SPP/2s;
normal control discovery retains its original budget and write/readback policy.
Notification reads query identity/battery once, never the full settings profile.
Only actual ReadBattery evidence advances batteryReadTime. Reuse a page snapshot
only if its battery was read after the notification request; busy completion must
replay the pending event rather than silently discard it. No fresh fields means
static unknown values, not cached batteries or a lost connection notification.
Keep static presentation, per-user artwork cache, top-slide motion and unchanged
touch/OEM policy. Do not ship guessed RFCOMM channels: probes did not establish a
stable benefit from endpoint caching and it was not added to the product.

Version 0.5.23 follows the user's confirmation that notifications return but
still take over five seconds. A verified paired model/color cache permits a
battery-only notification read; it must not mark identity as freshly confirmed
or persist a cached identity as new evidence. Unknown devices retain full reads,
and the normal page still initializes identity/settings after a fast session.
Generic device changes trigger immediate cached scans plus a bounded short burst,
not permanent rapid polling, inquiry or unsolicited RFCOMM. Idle fallback is 15s.
Notification HWND construction overlaps the one-shot radio read; static display
and animations remain. PopupTimingTrace is opt-in via Cache/trace-popup, bounded
128KiB and contains only stage/tick/process/error codes, never addresses/names.
Remove the diagnostic marker after validation. Preserve all touch/OEM policy.

Version 0.5.24 targets paired headphone reconnection, as clarified by the user.
Fast reconnect is an HKCU preference, on by default, with a UI switch and explicit
Reconnect now action. Enabled cached fallback checks run every 2s (disabled 15s);
these are not radio inquiry or a change to Windows' global scanning schedule.
Automatic requests target only the last Huawei headset observed truly connected
in this tray session. After 3s disconnect grace, at most four requests at round
offsets 0/5/15/30s; duplicates do not extend it. Reconnection/disable/unsupported
stops the round. Never cycle all paired devices or retry rapidly forever.
Use documented KSPROPERTY_ONESHOT_RECONNECT only on a revalidated paired Huawei
device whose unplugged render endpoint can be mapped to its exact address.
An accepted request is not connection evidence; preserve actual ACL/cache state
notifications. Disabled/unknown endpoints, audio routing, pairing, global radio,
and headset settings must remain untouched. Keep all static popup and touch/OEM
invariants. See docs/headphone-reconnect.md for hardware validation limits.

Version 0.5.25 supersedes 0.5.24 timing after the user clarified that actual
PC/headset connection remains slow. Explicit disconnect requests start after
~250ms, independently of the notification debounce. Passive BLE presence only
matches Public plus the exact paired classic target address; this association
was observed on the local FreeBuds Pro 5. Never guess random associations or
retain unrelated advertisements. Continuous presence cannot restart/extend a
round; require a new quiet episode. Stop observation when disabled, connected
or target cleared; retry watcher errors no sooner than 30s. Save only a target
observed truly connected; startup restore revalidates paired/type and selects
listening only. Transient missing/rebuilt objects use Failed, while genuine
unsupported or ambiguous identity stops. All requests remain bounded and never
change audio routing, pairing or device enablement. Keep static popup and
stable touch/OEM behavior. Do not infer audible availability from one endpoint:
the user reported normal PC audio while the observed A2DP endpoint was UNPLUGGED,
so actual output-path evidence must be checked before claiming audio failure.

Version 0.5.26 is presentation-only: headphone image frame 104x72 with original
HD assets intact, matching equal-width pen/keyboard cards (two columns at the
existing 900-DIP content breakpoint, one below it). Zero unused column spacing
in the single-column state. Segoe Fluent Icons mappings cover all headphone
controls, with glyph presence verified against the installed font. Reflow must
only reposition existing elements; never rebuild controls or clear EQ drafts
because the window was resized. Preserve all headphone protocol, reconnect,
notification, touch binary and OEM-disabled behavior.

Version 0.5.27 unifies all headphone group, setting, action and fallback font
icons at 20 DIP with 24x24 alignment frames, following the user's visual review.
Keep these sizes consistent across the dedicated headphone icon styles.
Use complete-size Headphone/TouchPointer/Touch/Swipe/ScrollUpDown glyphs rather
than miniature Headphone0/NUIFP cursor components; equal font size alone did
not give those miniature outlines a consistent visual size.

Version 0.5.28 moves custom EQ from the inline expander into a persistent,
scrollable ContentDialog launched by an equal-width button. Reopening preserves
the draft; only explicit Save and apply can write. Await the existing client's
exact readback under a Primary-button deferral; failure keeps the dialog and
draft, including when an exception supplies no valid device snapshot. Pin the
opening address/epoch, block concurrent refresh/popups/dialogs, and allow main
close/hide to cancel safely. Do not reinitialize controls on each opening.
Keep protocol writes, packet framing and all driver/OEM behavior unchanged.

Version 0.5.29 gives the EQ launcher the same card fill/border/radius as other
settings, with a trailing right chevron. Known supported models may open and
edit drafts offline; Save remains disabled until the existing client is ready.
Keyboard charging feedback uses only the existing read-only channel: a valid
noncharging-to-charging edge, never connection notifications or startup state.
Unknown snapshots must not become false edges; channel/host failure resets the
baseline. Consume suppressed edges, debounce repeated changes, and respect
DeviceNotifications/install/exit state. The one-second white edge glow uses a
small cached atlas and GPU opacity animation on the full primary monitor,
click-through/noactivate, without a per-frame CPU redraw or idle timer/surface.
Preserve all input, OEM-disabled, Bluetooth and notification data policies.

Version 0.5.30 slightly increases the glow to a 36-DIP band with 0.72 peak
alpha, keeping its one-second timeline. Combine WS_EX_LAYERED with
WS_EX_TRANSPARENT and initialize alpha 255 for reliable cross-thread input
passthrough while DirectComposition controls animated opacity.

Version 0.5.31 changes the glow trigger to physical detached-to-attached,
using hasKbdDetached and !kbdDetached from the existing status channel only.
Charging, battery and kbdPresent/link presence are not attachment evidence.
Keep startup/recovery quiet, unknown snapshots unknown, and existing debounce
and notification/exit guards. Preserve the approved 0.5.30 glow appearance.

Version 0.5.32 presents headphone control groups and paired devices as native
expandable menus, retaining original icons, labels, choices and handlers.
Keep group expansion state across refresh/reflow and model rebuilds by group
name. Reflow persistent rows only; never reset controls or EQ drafts. Preserve
the modal custom-EQ arrow card and the 0.5.31 physical-attachment glow trigger.

Version 0.5.33 aligns headphone menus, paired devices, EQ and fast reconnect
with the information card: left column in wide view, full width in narrow view,
single-column menu items. Move the existing fast-reconnect card below EQ, after
any visible paired-devices card. Allocate Advanced rows by visibility so models
without EQ/peers have no empty rows or overlap. Preserve all existing instances,
expansion state, icons, options, event handlers and device/driver behavior.

Version 0.5.34 embeds a native EnergyStar adaptation in the interactive tray,
with a default-off Battery toggle and persisted EnergyStarEnabled preference.
Keep the upstream source/commit and complete license in third_party/EnergyStar
and shipped THIRD-PARTY-NOTICES. Only eligible same-user/session background
processes may receive EcoQoS/idle priority; explicitly bypass every hub/driver/
hal host so the validated touch process/thread priorities cannot change.
Use foreground events and five-minute housekeeping on a separate worker.
Report actual asynchronous state through append-only tray IPC, not merely the
registry preference. Restore original priorities and only owned throttling bits
on foreground/disable/exit, retaining process identity to avoid PID reuse.
Off means no hook/timer/worker. Preserve all OEM, touch, accessory and layout
behavior, including the 0.5.33 aligned cards and attachment-triggered glow.

Version 0.5.35 addresses frontend input/scrolling without changing the stable
touch driver. Actual .34 ARM64 crash dumps resolve to InputSinkInfrastructure::
HandleDManipResponse_OnMIT, E_UNEXPECTED on WM_POINTERROUTEDRELEASED, not the
different PopulateContactInFrame crash in the broad public issue. Native Win32
pen/raw-property/frame queries all succeeded; enabling hover did not prevent
the crash. Keep the user's hover-off preference and EnergyStar-on choice.
Use ScrollView with vertical touch scrolling and IgnoredInputKinds=Pen for the
seven explicit page/EQ containers, preserving child control input and original
content. Do not replace NavigationView/ComboBox internal templates. Ignore only
updatedAtUnixMs in UI state comparison, retaining real timestamps/freshness and
all notification/state changes. Gate unchanged device layout. Physical pen and
scroll validation remains required; do not claim confirmed crash resolution
from compilation or the static dump alone.

Version 0.5.36 restores native pen panning in the seven modern ScrollViews by
setting IgnoredInputKinds=None. The user reported that the .35 Pen exclusion
allowed only clicking. Keep ScrollView, timestamp-only refresh filtering and
layout gates; do not add manual ScrollTo loops or pointer capture state machines.
The modern control owns pen redirection and inertia. Current hover/EnergyStar
choices belong to the user; do not carry the old temporary A/B setting restore
into later installers. Physical pen pan/click and crash stability still need
validation; enabling native pen does not prove the old MIT failure is absent.

Version 0.5.37 follows the user's explicit requirement for WinUI 3 native touch.
Upgrade Microsoft.WindowsAppSDK from 1.8.260710003 to stable 2.6.4 and keep the
seven native ScrollViews with IgnoredInputKinds=None and vertical scrolling.
The 2.4+ runtime release notes include touch/pen exit and composition redirection
crash fixes related to the observed area, without proving the same root cause.
Do not add custom pointer handlers, capture, manual ScrollTo loops or injected
input to emulate pen scrolling. Keep native control gestures and inertia. The
ARM64 Windows App Runtime 2.6.4 or a compatible newer release is a prerequisite;
the local machine already has 2.6.4 installed. Preserve driver cadence, OEM
disabled policy, current preferences, timestamp filtering and layout gates.
Physical pen pan/click and crash stability still require user validation.

Version 0.5.38 follows user feedback that .37 still allowed only pen clicks,
while Windows Settings could pan with the same pen. The .37 process was verified
to load Runtime.2 2.6.4.0 input modules; no unexpected elevation or app pointer
interception was found. Use classic native ScrollViewer for the seven explicit
page/EQ containers with a transparent hit-testable background, vertical scrolling
enabled and horizontal/zoom disabled. Retain SDK 2.6.4, native gestures/inertia,
child controls, all content and refresh/layout optimizations. No manual panning,
pointer capture, injection or global pen setting changes. Do not replace internal
NavigationView/ComboBox templates or alter the stable driver and preferences.
Native pen scrolling and crash stability remain subject to physical validation.

Version 0.5.39 follows confirmed .38 feedback that pen clicks work but panning
still does not, while Windows Settings pans normally. Keep native ScrollViewer
and SDK 2.6.4. Add default-off, bounded local observation only when the per-user
Cache/trace-native-input marker exists. Never set Handled, capture, inject or
manually scroll. No coordinates, user text or device identity in logs. Pointer
movement only increments in-memory counters; write bounded event records on a
background writer. Remove handlers on close/destruction. This is diagnostic
instrumentation, not a confirmed fix; preserve all driver and UI behavior.
Remove the local marker after the hardware observation session.
