#include "pch.h"

#include "NotificationWindow.xaml.h"
#include "AccessoryImageLoader.h"
#include "PopupTimingTrace.h"

#include <dwmapi.h>

#include <cmath>

#if __has_include("NotificationWindow.g.cpp")
#include "NotificationWindow.g.cpp"
#endif

using namespace winrt;
using namespace Microsoft::UI::Windowing;
using namespace Microsoft::UI::Xaml;

namespace winrt::EGoTouchSettings::implementation {

namespace {
constexpr int kConnectionWidthDip = 336;
constexpr int kConnectionHeightDip = 102;
constexpr int kHeadphoneWidthDip = 400;
constexpr int kHeadphoneHeightDip = 220;
constexpr int kDeviationWidthDip = 336;
constexpr int kDeviationHeightDip = 58;
constexpr int kToolWidthDip = 200;
constexpr int kToolHeightDip = 58;
constexpr int kEntranceMs = 240;
constexpr int kExitMs = 200;
constexpr int kSwapExitMs = 120;
constexpr double kPenImageMaxWidthDip = 304.0;
constexpr double kPenImageMaxHeightDip = 28.0;
constexpr double kKeyboardImageMaxWidthDip = 96.0;
constexpr double kKeyboardImageMaxHeightDip = 66.0;
constexpr UINT kDwmwaWindowCornerPreference = 33;
constexpr int kDwmCornerRound = 2;
constexpr UINT kDwmwaUseImmersiveDarkMode = 20;
constexpr UINT kDwmwaBorderColor = 34;
// COLORREF 是 0x00BBGGRR。取值贴着亚克力卡片在两种主题下的实际明度，见 ApplyTheme。
constexpr COLORREF kBorderColorDark = 0x00303030;
constexpr COLORREF kBorderColorLight = 0x00DFDFDF;

}

NotificationWindow::NotificationWindow() {
    InitializeComponent();
    ConfigureWindow();

    NotificationRoot().ActualThemeChanged(
        [this](FrameworkElement const&, IInspectable const&) { SyncFrameTheme(); });
    // 元素上树之前 ActualTheme 还不是生效值，首次同步要等到这里。
    NotificationRoot().Loaded(
        [this](IInspectable const&, RoutedEventArgs const&) { SyncFrameTheme(); });

    m_dwellTimer = DispatcherTimer();
    m_dwellTimer.Interval(std::chrono::seconds(3));
}

NotificationWindow::~NotificationWindow() {
    if (m_dwellTimer) m_dwellTimer.Stop();
    StopWindowAnimation();
}

HWND NotificationWindow::WindowHandle() {
    winrt::EGoTouchSettings::NotificationWindow projected = *this;
    const auto windowNative = projected.as<IWindowNative>();
    HWND hwnd = nullptr;
    check_hresult(windowNative->get_WindowHandle(&hwnd));
    return hwnd;
}

void NotificationWindow::ConfigureWindow() {
    const HWND hwnd = WindowHandle();
    const WindowId id = GetWindowIdFromWindow(hwnd);
    const Microsoft::UI::Windowing::AppWindow appWindow =
        Microsoft::UI::Windowing::AppWindow::GetFromWindowId(id);
    if (const auto presenter = appWindow.Presenter().try_as<OverlappedPresenter>()) {
        presenter.SetBorderAndTitleBar(false, false);
        presenter.IsResizable(false);
        presenter.IsMaximizable(false);
        presenter.IsMinimizable(false);
    }

    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);

    // SetBorderAndTitleBar(false, false) 不动窗口样式位，WS_CAPTION/WS_DLGFRAME 仍留在那里，
    // DWM 据此在边框最内侧画一条高光——深色卡片外面那圈白边就是它。这条线不受
    // DWMWA_BORDER_COLOR、ImmersiveDarkMode、NCRENDERING_POLICY 和圆角偏好中的任何一个影响，
    // 逐个试过；把样式位剥成纯 WS_POPUP，DWM 就不再画它。
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME | WS_SYSMENU);
    style |= WS_POPUP;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    // 窗口本身是矩形的：亚克力底铺满整个客户区，圆角只能由 DWM 裁。卡片描边因此不能自己
    // 定半径——描边比裁切大时，四角会露出一截被裁掉的窗口底，看起来像圆角外又画了一圈边框。
    // XAML 那边用 OverlayCornerRadius，与这里的 ROUND 是同一档。
    const int corner = kDwmCornerRound;
    (void)DwmSetWindowAttribute(hwnd, kDwmwaWindowCornerPreference, &corner, sizeof(corner));
}

void NotificationWindow::SelectView(UIElement const& view) {
    for (const UIElement candidate :
         {ConnectionView().as<UIElement>(), KeyboardConnectionView().as<UIElement>(),
          HeadphoneConnectionView().as<UIElement>(),
          ToolView().as<UIElement>(), DeviationView().as<UIElement>()}) {
        candidate.Visibility(candidate == view ? Visibility::Visible : Visibility::Collapsed);
    }
}

void NotificationWindow::PreparePenImage(
        uint32_t modelId,
        const Microsoft::UI::Xaml::Media::ImageSource& source,
        const Windows::Foundation::Size& pixelSize) {
    if (m_shutdown || !source || pixelSize.Width <= 0 || pixelSize.Height <= 0) return;
    if (m_penModelId == modelId && m_penImageSourceModelId == modelId &&
        m_penImageSource == source) return;
    ++m_penImageGeneration;
    m_penImageLoading = false;
    m_penModelId = modelId;
    m_penImageSourceModelId = modelId;
    m_penImageRetryAt = 0;
    m_penImageSource = source;
    m_penImagePixelSize = pixelSize;
}

void NotificationWindow::PrepareKeyboardImage(
        const Microsoft::UI::Xaml::Media::ImageSource& source,
        const Windows::Foundation::Size& pixelSize) {
    if (m_shutdown || !source || pixelSize.Width <= 0 || pixelSize.Height <= 0) return;
    if (m_keyboardImageSource == source) return;
    ++m_keyboardImageGeneration;
    m_keyboardImageLoading = false;
    m_keyboardImageRetryAt = 0;
    m_keyboardImageSource = source;
    m_keyboardImagePixelSize = pixelSize;
}

Windows::Foundation::IAsyncAction NotificationWindow::PrepareConnectionAsync(
        PenStatus::State state, bool keyboard) {
    auto lifetime = get_strong();
    if (m_shutdown) co_return;
    const bool pending = keyboard ? m_keyboardImageLoading :
        m_penImageLoading && m_penModelId == state.modelId;
    if (pending) {
        // WinRT async operations have a single Completed handler. Wait for the
        // shared UI-owned preparation state instead of co-awaiting one action
        // twice, and always restore the UI apartment before reading fields.
        const auto ui = apartment_context();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
        while (keyboard ? m_keyboardImageLoading : m_penImageLoading) {
            if (m_shutdown || (!keyboard && m_penModelId != state.modelId) ||
                std::chrono::steady_clock::now() >= deadline) co_return;
            co_await resume_after(std::chrono::milliseconds(10));
            try { co_await ui; } catch (...) { co_return; }
        }
        co_return;
    }
    if (keyboard) co_await LoadKeyboardImage();
    else co_await LoadPenImage(state.modelId);
}

void NotificationWindow::PrepareConnectionImages(const PenStatus::State& state) {
    if (m_shutdown) return;
    if (state.modelId || state.modelName[0] ||
        (state.hasStylusLink && state.stylusLinked) ||
        (state.hasChargingState && state.charging)) {
        LoadPenImage(state.modelId);
    }
    if (state.kbdModelName[0] || (state.hasKbdPresent && state.kbdPresent)) {
        LoadKeyboardImage();
    }
}

Windows::Foundation::IAsyncAction NotificationWindow::LoadPenImage(uint32_t modelId) {
    auto lifetime = get_strong();
    if (m_shutdown) co_return;
    const ULONGLONG now = GetTickCount64();
    const bool modelChanged = modelId != m_penModelId;
    if (m_penImageSource && m_penImageSourceModelId == modelId) {
        if (modelChanged) {
            m_penModelId = modelId;
            ++m_penImageGeneration;
            m_penImageLoading = false;
        }
        co_return;
    }
    if (!modelChanged && (m_penImageLoading || now < m_penImageRetryAt)) co_return;

    // Preparation only changes the cache. The visible toast owns a source/state
    // snapshot captured by ShowConnected and is never altered by this work.
    m_penModelId = modelId;
    m_penImageLoading = true;
    m_penImageRetryAt = now + 5000;
    const uint64_t generation = ++m_penImageGeneration;
    const auto ui = apartment_context();
    AccessoryImages::Asset asset;
    co_await resume_background();
    try { asset = AccessoryImages::ResolvePen(modelId); } catch (...) {}
    try { co_await ui; } catch (...) { co_return; }
    if (m_shutdown || generation != m_penImageGeneration || modelId != m_penModelId) co_return;

    try {
        if (asset) {
            Microsoft::UI::Xaml::Media::Imaging::SoftwareBitmapSource source;
            const auto pixelSize = co_await AccessoryImages::DecodeCroppedAsync(
                std::move(asset.encoded), source);
            if (m_shutdown || generation != m_penImageGeneration ||
                modelId != m_penModelId) co_return;
            if (pixelSize.Width > 0 && pixelSize.Height > 0) {
                PreparePenImage(modelId, source, pixelSize);
                co_return;
            }
        }
    } catch (...) {
        // Resource updates may briefly produce incomplete files; keep this
        // failure local to preparation and permit a later request to retry.
    }
    if (m_shutdown || generation != m_penImageGeneration || modelId != m_penModelId) co_return;
    m_penImageLoading = false;
    if (m_penImageSource && m_penImageSourceModelId != modelId) {
        m_penImageSource = nullptr;
        m_penImagePixelSize = {};
    }
}

Windows::Foundation::IAsyncAction NotificationWindow::LoadKeyboardImage() {
    auto lifetime = get_strong();
    if (m_shutdown || m_keyboardImageSource || m_keyboardImageLoading) co_return;
    const ULONGLONG now = GetTickCount64();
    if (now < m_keyboardImageRetryAt) co_return;
    m_keyboardImageLoading = true;
    m_keyboardImageRetryAt = now + 5000;
    const uint64_t generation = ++m_keyboardImageGeneration;
    const auto ui = apartment_context();
    AccessoryImages::Asset asset;
    co_await resume_background();
    try { asset = AccessoryImages::ResolveKeyboard(); } catch (...) {}
    try { co_await ui; } catch (...) { co_return; }
    if (m_shutdown || generation != m_keyboardImageGeneration) co_return;

    try {
        if (asset) {
            Microsoft::UI::Xaml::Media::Imaging::SoftwareBitmapSource source;
            const auto pixelSize = co_await AccessoryImages::DecodeCroppedAsync(
                std::move(asset.encoded), source);
            if (m_shutdown || generation != m_keyboardImageGeneration) co_return;
            if (pixelSize.Width > 0 && pixelSize.Height > 0) {
                PrepareKeyboardImage(source, pixelSize);
                co_return;
            }
        }
    } catch (...) {
        // Use the same resource policy as the device page and retry later.
    }
    if (m_shutdown || generation != m_keyboardImageGeneration) co_return;
    m_keyboardImageLoading = false;
}

void NotificationWindow::ApplyNoUpscaleSize(
        Controls::Image const& image,
        Windows::Foundation::Size const& pixelSize,
        double maxWidthDip,
        double maxHeightDip) {
    if (pixelSize.Width <= 0 || pixelSize.Height <= 0) return;
    const UINT dpi = GetDpiForWindow(WindowHandle());
    const double rasterScale = dpi ? static_cast<double>(dpi) / 96.0 : 1.0;
    const double nativeWidthDip = pixelSize.Width / rasterScale;
    const double nativeHeightDip = pixelSize.Height / rasterScale;
    const double fit = std::min({
        1.0,
        maxWidthDip / nativeWidthDip,
        maxHeightDip / nativeHeightDip,
    });
    // Width/Height 换算回当前窗口的物理像素后不超过源图尺寸：只缩不放。
    image.Width(std::max(1.0, nativeWidthDip * fit));
    image.Height(std::max(1.0, nativeHeightDip * fit));
}

RECT NotificationWindow::TargetBounds(int widthDip, int heightDip) {
    const UINT dpi = GetDpiForWindow(WindowHandle());
    const UINT scale = dpi ? dpi : 96;

    POINT cursor{};
    GetCursorPos(&cursor);
    const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{sizeof(info)};
    RECT screen{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    if (GetMonitorInfoW(monitor, &info)) screen = info.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &screen, 0);

    // Anchor the card's top edge, so taller headphone notifications cannot
    // extend above the screen. Keep all notification sizes inside the work area.
    const int availableWidth = std::max(1L, screen.right - screen.left);
    const int availableHeight = std::max(1L, screen.bottom - screen.top);
    const int margin = std::max(1, MulDiv(24, scale, 96));
    const int insetX = std::min(margin, (availableWidth - 1) / 2);
    const int insetY = std::min(margin, (availableHeight - 1) / 2);
    const int width = std::clamp(MulDiv(widthDip, scale, 96), 1, availableWidth - 2 * insetX);
    const int height = std::clamp(MulDiv(heightDip, scale, 96), 1, availableHeight - 2 * insetY);
    const int left = screen.left + (availableWidth - width) / 2;
    const int top = screen.top + insetY;
    return RECT{left, top, left + width, top + height};
}

void NotificationWindow::PositionAndShow(int widthDip, int heightDip) {
    PopupTimingTrace::Record("popup.slide_start");
    if (m_shutdown) return;
    if (!SystemBackdrop()) {
        SystemBackdrop(Microsoft::UI::Xaml::Media::DesktopAcrylicBackdrop{});
    }
    const RECT bounds = TargetBounds(widthDip, heightDip);
    MONITORINFO info{sizeof(info)};
    m_slideScreenTop = GetMonitorInfoW(
        MonitorFromRect(&bounds, MONITOR_DEFAULTTONEAREST), &info) ? info.rcMonitor.top : 0;
    const RECT from = AboveScreenBounds(bounds);
    const HWND hwnd = WindowHandle();
    // Position while hidden. Show(false) never activates this independent window.
    SetWindowPos(hwnd, HWND_TOPMOST, from.left, from.top,
                 from.right - from.left, from.bottom - from.top, SWP_NOACTIVATE);
    AppWindow().Show(false);
    SetWindowPos(hwnd, HWND_TOPMOST, from.left, from.top,
                 from.right - from.left, from.bottom - from.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    const auto generation = m_presentationGeneration;
    StartWindowAnimation(from, bounds, 0.0, 1.0, kEntranceMs, true,
        [this, generation] {
            if (!m_shutdown && generation == m_presentationGeneration) {
                PopupTimingTrace::Record("popup.visible");
                RestartDwellTimer();
            }
        });
}

RECT NotificationWindow::AboveScreenBounds(const RECT& bounds) const {
    const LONG height = bounds.bottom - bounds.top;
    return RECT{bounds.left, m_slideScreenTop - height - 2,
                bounds.right, m_slideScreenTop - 2};
}

void NotificationWindow::StartWindowAnimation(
        const RECT& from, const RECT& to, double fromOpacity, double toOpacity,
        int milliseconds, bool easeOut, std::function<void()> completed) {
    StopWindowAnimation();
    auto& animation = m_animation;
    animation.active = true;
    animation.easeOut = easeOut;
    animation.hwnd = WindowHandle();
    animation.from = from;
    animation.to = to;
    animation.lastBounds = from;
    animation.fromOpacity = std::clamp(fromOpacity, 0.0, 1.0);
    animation.toOpacity = std::clamp(toOpacity, 0.0, 1.0);
    animation.lastOpacity = animation.fromOpacity;
    animation.started = std::chrono::steady_clock::now();
    animation.lastAdvanced = animation.started;
    animation.milliseconds = std::max(1, milliseconds);
    animation.generation = m_presentationGeneration;
    animation.completed = std::move(completed);
    NotificationRoot().Opacity(animation.fromOpacity);

    const auto weak = get_weak();
    const auto serial = m_animationSerial;
    m_renderingToken = Media::CompositionTarget::Rendering(
        [weak, serial](IInspectable const&, IInspectable const&) {
            if (auto self = weak.get(); self && serial == self->m_animationSerial)
                self->AdvanceWindowAnimation(std::chrono::steady_clock::now());
        });

    // Rendering can pause while the whole HWND is above the monitor, or when
    // Windows throttles a hidden app. Kick once at 16ms, then use a low-frequency
    // watchdog only when no render frame has advanced for at least 40ms.
    m_animationWatchdog = DispatcherTimer();
    m_animationWatchdog.Interval(std::chrono::milliseconds(16));
    m_animationWatchdog.Tick([weak, serial, first = true](IInspectable const& sender, IInspectable const&) mutable {
        const bool kick = first;
        if (first) {
            first = false;
            sender.as<DispatcherTimer>().Interval(std::chrono::milliseconds(64));
        }
        if (auto self = weak.get(); self && serial == self->m_animationSerial) {
            const auto now = std::chrono::steady_clock::now();
            const auto& current = self->m_animation;
            if (!current.active) return;
            const bool deadline = now - current.started >= std::chrono::milliseconds(current.milliseconds);
            if (deadline || now - current.lastAdvanced >= std::chrono::milliseconds(kick ? 12 : 40)) {
                self->AdvanceWindowAnimation(now);
            }
        }
    });
    m_animationWatchdog.Start();
}

void NotificationWindow::AdvanceWindowAnimation(std::chrono::steady_clock::time_point now) {
    auto& animation = m_animation;
    if (!animation.active) return;
    if (m_shutdown || !m_presented || animation.generation != m_presentationGeneration) {
        StopWindowAnimation();
        return;
    }
    const double elapsed = std::chrono::duration<double, std::milli>(now - animation.started).count();
    const double t = std::clamp(elapsed / animation.milliseconds, 0.0, 1.0);
    // A watchdog and render callback may arrive together. Reuse the same clock,
    // skip sub-millisecond duplicate steps, and never derive motion from FPS.
    if (t < 1.0 && now - animation.lastAdvanced < std::chrono::milliseconds(1)) return;
    animation.lastAdvanced = now;
    const double remaining = 1.0 - t;
    const double eased = animation.easeOut ? 1.0 - remaining * remaining * remaining : t * t * t;
    const auto lerp = [eased](LONG from, LONG to) {
        return static_cast<LONG>(std::lround(from + static_cast<double>(to - from) * eased));
    };
    const RECT bounds{lerp(animation.from.left, animation.to.left),
                      lerp(animation.from.top, animation.to.top),
                      lerp(animation.from.right, animation.to.right),
                      lerp(animation.from.bottom, animation.to.bottom)};
    if (!EqualRect(&bounds, &animation.lastBounds)) {
        const bool sameSize = bounds.right - bounds.left == animation.lastBounds.right - animation.lastBounds.left &&
            bounds.bottom - bounds.top == animation.lastBounds.bottom - animation.lastBounds.top;
        SetWindowPos(animation.hwnd, nullptr, bounds.left, bounds.top,
                     bounds.right - bounds.left, bounds.bottom - bounds.top,
                     SWP_NOACTIVATE | SWP_NOZORDER | (sameSize ? SWP_NOSIZE : 0));
        animation.lastBounds = bounds;
    }
    const double opacity = animation.fromOpacity +
        (animation.toOpacity - animation.fromOpacity) * eased;
    if (opacity != animation.lastOpacity) {
        NotificationRoot().Opacity(opacity);
        animation.lastOpacity = opacity;
    }
    if (t < 1.0) return;
    const auto generation = animation.generation;
    auto completed = std::move(animation.completed);
    StopWindowAnimation();
    if (!m_shutdown && generation == m_presentationGeneration && completed) completed();
}

void NotificationWindow::StopWindowAnimation() {
    ++m_animationSerial;
    m_animation.active = false;
    m_animation.completed = nullptr;
    if (m_renderingToken.value) {
        Media::CompositionTarget::Rendering(m_renderingToken);
        m_renderingToken = {};
    }
    if (m_animationWatchdog) m_animationWatchdog.Stop();
}

void NotificationWindow::SlideOut(int milliseconds, std::function<void()> completed) {
    RECT from{};
    GetWindowRect(WindowHandle(), &from);
    StartWindowAnimation(from, AboveScreenBounds(from), NotificationRoot().Opacity(), 0.0,
                         milliseconds, false, std::move(completed));
}

// A replacement continues out from the actual current position. Only when it is
// above the screen do we change content and size, then slide the new card in.
void NotificationWindow::PresentView(
        UIElement const& view, int widthDip, int heightDip, std::function<void()> apply) {
    if (m_shutdown) return;
    const bool wasVisible = m_presented && IsWindowVisible(WindowHandle());
    m_dwellTimer.Stop();
    StopWindowAnimation();
    const auto generation = ++m_presentationGeneration;
    m_presented = true;
    m_pendingView = view;
    const auto swap = [this, generation, view, widthDip, heightDip,
                       apply = std::move(apply)]() {
        if (m_shutdown || generation != m_presentationGeneration) return;
        if (apply) apply();
        m_pendingView = nullptr;
        SelectView(view);
        m_connectionView = view == ConnectionView().as<UIElement>() ||
                           view == KeyboardConnectionView().as<UIElement>();
        NotificationRoot().Opacity(0.0);
        PositionAndShow(widthDip, heightDip);
        if (m_onRefreshDemandChanged) m_onRefreshDemandChanged();
    };

    if (!wasVisible) {
        swap();
        return;
    }
    SlideOut(kSwapExitMs, swap);
    if (m_onRefreshDemandChanged) m_onRefreshDemandChanged();
}

void NotificationWindow::RestartDwellTimer() {
    if (m_dwellTimer) m_dwellTimer.Stop();
    m_dwellTimer = DispatcherTimer();
    m_dwellTimer.Interval(std::chrono::seconds(3));
    const auto generation = m_presentationGeneration;
    m_dwellTimer.Tick([this, generation](IInspectable const& sender, IInspectable const&) {
        sender.as<DispatcherTimer>().Stop();
        if (m_shutdown || !m_presented || generation != m_presentationGeneration) return;
        SlideOut(kExitMs, [this] { HideNotification(); });
    });
    m_dwellTimer.Start();
}

void NotificationWindow::ShowConnected(const PenStatus::State& state) {
    const Media::ImageSource source = m_penImageSourceModelId == state.modelId
        ? m_penImageSource : Media::ImageSource{nullptr};
    const auto pixelSize = m_penImagePixelSize;
    PresentView(ConnectionView(), kConnectionWidthDip, kConnectionHeightDip, [this, state, source, pixelSize] {
        ApplyConnectedState(state, source, pixelSize);
    });
}

void NotificationWindow::ApplyConnectedState(const PenStatus::State& state,
        const Media::ImageSource& source, const Windows::Foundation::Size& pixelSize) {
    PenNameText().Text(state.modelName[0] ? to_hstring(state.modelName) : L"HUAWEI M-Pencil");
    PenBatteryIndicator().SetState(
        state.hasBatteryLevel, state.batteryLevel,
        state.hasChargingState && state.charging);

    PenImage().Source(source);
    if (source) ApplyNoUpscaleSize(PenImage(), pixelSize, kPenImageMaxWidthDip, kPenImageMaxHeightDip);
    PenImage().Visibility(source ? Visibility::Visible : Visibility::Collapsed);
    PenImageFallback().Visibility(source ? Visibility::Collapsed : Visibility::Visible);
}

void NotificationWindow::ShowKeyboardConnected(const PenStatus::State& state) {
    const auto source = m_keyboardImageSource;
    const auto pixelSize = m_keyboardImagePixelSize;
    PresentView(
        KeyboardConnectionView(), kConnectionWidthDip, kConnectionHeightDip, [this, state, source, pixelSize] {
            ApplyKeyboardState(state, source, pixelSize);
        });
}

void NotificationWindow::ApplyKeyboardState(const PenStatus::State& state,
        const Media::ImageSource& source, const Windows::Foundation::Size& pixelSize) {
    KeyboardNameText().Text(
        state.kbdModelName[0] ? to_hstring(state.kbdModelName) : L"HUAWEI 智能磁吸键盘");
    KeyboardBatteryIndicator().SetState(
        state.hasKbdBattery, state.kbdBatteryLevel,
        state.hasKbdCharging && state.kbdCharging);
    KeyboardImage().Source(source);
    if (source) ApplyNoUpscaleSize(
        KeyboardImage(), pixelSize, kKeyboardImageMaxWidthDip, kKeyboardImageMaxHeightDip);
    KeyboardImage().Visibility(source ? Visibility::Visible : Visibility::Collapsed);
    KeyboardImageFallback().Visibility(source ? Visibility::Collapsed : Visibility::Visible);
}

void NotificationWindow::UpdateState(const PenStatus::State&) {
    // Compatibility entrypoint. A displayed or queued toast is an immutable
    // snapshot; fresh data is prepared for the next presentation instead.
}

void NotificationWindow::ShowHeadphoneConnected(
        const HuaweiHeadphones::Snapshot& state,
        const Microsoft::UI::Xaml::Media::ImageSource& imageSource) {
    PresentView(HeadphoneConnectionView(), kHeadphoneWidthDip, kHeadphoneHeightDip,
        [this, state, imageSource] {
            m_headphoneAddress = state.address;
            m_headphoneArtworkKey = state.resourceId + "_" + state.colorId;
            ApplyHeadphoneState(state);
            // A new presentation never inherits a different headset's picture.
            ApplyHeadphoneImage(imageSource);
        });
}

void NotificationWindow::ApplyHeadphoneState(const HuaweiHeadphones::Snapshot& state) {
    HeadphoneNameText().Text(state.name.empty() ? L"华为耳机" : hstring(state.name));
    const auto setBattery = [](auto const& indicator, auto const& battery) {
        const bool known = battery.level && *battery.level >= 0 && *battery.level <= 100;
        indicator.SetState(known, known ? *battery.level : 0, known && battery.charging);
    };
    setBattery(HeadphoneLeftBattery(), state.battery[0]);
    setBattery(HeadphoneRightBattery(), state.battery[1]);
    setBattery(HeadphoneCaseBattery(), state.battery[2]);
    const bool eyewear = state.model == HuaweiHeadphones::Model::Eyewear ||
        state.model == HuaweiHeadphones::Model::Eyewear2;
    HeadphoneLeftLabel().Text(eyewear ? L"左镜腿" : L"左耳");
    HeadphoneRightLabel().Text(eyewear ? L"右镜腿" : L"右耳");
    HeadphoneCaseGroup().Visibility(eyewear ? Visibility::Collapsed : Visibility::Visible);
    HeadphoneCaseColumn().Width(eyewear ? GridLength{0.0, GridUnitType::Pixel}
                                     : GridLength{1.0, GridUnitType::Star});
    HeadphoneBatteryGrid().ColumnSpacing(eyewear ? 0.0 : 16.0);
}

void NotificationWindow::ApplyHeadphoneImage(
        const Microsoft::UI::Xaml::Media::ImageSource& imageSource) {
    HeadphoneImage().Source(imageSource);
    HeadphoneImage().Visibility(imageSource ? Visibility::Visible : Visibility::Collapsed);
    HeadphoneImageFallback().Visibility(imageSource ? Visibility::Collapsed : Visibility::Visible);
}

void NotificationWindow::UpdateHeadphoneState(
        const HuaweiHeadphones::Snapshot&,
        const Microsoft::UI::Xaml::Media::ImageSource&) {
    // Compatibility entrypoint: never mutate a displayed or queued snapshot.
}

void NotificationWindow::ShowDeviation() {
    PresentView(DeviationView(), kDeviationWidthDip, kDeviationHeightDip, nullptr);
}

void NotificationWindow::ShowToolChanged(bool eraser) {
    PresentView(ToolView(), kToolWidthDip, kToolHeightDip, [this, eraser] {
        // 橡皮擦与笔尖各用一个字形，文本本身已经说清楚了是哪一支，图标只是让扫一眼就能分辨。
        ToolPenIcon().Visibility(eraser ? Visibility::Collapsed : Visibility::Visible);
        ToolEraserIcon().Visibility(eraser ? Visibility::Visible : Visibility::Collapsed);
        // 两句等长，切换时卡片的宽度和字的位置都不动；「书写／橡皮」也是设置里那一项
        // 「切换书写与橡皮擦」用的词。
        ToolText().Text(eraser ? L"已切换到橡皮" : L"已切换到书写");
    });
}

// 弹窗是独立的 Window，主题不从主窗口继承，由 MainWindow 在每次弹出前推给它。深浅也一并
// 传进来：跟随系统时 theme 是 Default，而 ActualTheme 在窗口刚建好、内容还没上树时读不到
// 真正生效的那一档——照它设边框色，深色下会得到一条白边，正是要修的东西。
void NotificationWindow::ApplyTheme(ElementTheme theme) {
    NotificationRoot().RequestedTheme(theme);
}

// 窗口的边界由 DWM 画，不归 RequestedTheme 管，得按 ActualTheme 单独交代。
void NotificationWindow::SyncFrameTheme() {
    const bool dark = NotificationRoot().ActualTheme() == ElementTheme::Dark;
    const HWND hwnd = WindowHandle();

    const BOOL immersive = dark ? TRUE : FALSE;
    (void)DwmSetWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &immersive, sizeof(immersive));
    // 卡片自己不描边，边界只剩这一条。DWMWA_COLOR_NONE 对这个无标题栏的窗口不起作用，实测
    // 仍会描一条系统默认色的线，所以给它一个贴着卡片的颜色，让它退到轮廓的位置上。
    const COLORREF border = dark ? kBorderColorDark : kBorderColorLight;
    (void)DwmSetWindowAttribute(hwnd, kDwmwaBorderColor, &border, sizeof(border));
}

bool NotificationWindow::NeedsStateRefresh() const noexcept {
    return false;
}

void NotificationWindow::SetRefreshDemandChanged(std::function<void()> cb) {
    m_onRefreshDemandChanged = std::move(cb);
}

void NotificationWindow::HideNotification() {
    ++m_presentationGeneration;
    if (m_dwellTimer) m_dwellTimer.Stop();
    StopWindowAnimation();
    AppWindow().Hide();
    ShowWindow(WindowHandle(), SW_HIDE);
    SystemBackdrop(nullptr);
    NotificationRoot().Opacity(0.0);
    m_presented = false;
    m_connectionView = false;
    m_pendingView = nullptr;
    if (m_onRefreshDemandChanged) m_onRefreshDemandChanged();
}

void NotificationWindow::Shutdown() {
    if (m_shutdown) return;
    m_shutdown = true;
    ++m_penImageGeneration;
    ++m_keyboardImageGeneration;
    m_onRefreshDemandChanged = nullptr;
    HideNotification();
}

} // namespace winrt::EGoTouchSettings::implementation
