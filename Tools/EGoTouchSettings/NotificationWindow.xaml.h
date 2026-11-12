#pragma once

#include "NotificationWindow.g.h"
#include "HeadphoneClient.h"

#include <chrono>
#include <functional>

namespace winrt::EGoTouchSettings::implementation {

struct NotificationWindow : NotificationWindowT<NotificationWindow> {
    NotificationWindow();
    ~NotificationWindow();

    void ShowConnected(const PenStatus::State& state);
    void ShowKeyboardConnected(const PenStatus::State& state);
    // UI-thread only. Reuse the device page's decoded artwork without presenting
    // a notification or changing its timers, batteries or current view.
    void PreparePenImage(
        uint32_t modelId,
        const Microsoft::UI::Xaml::Media::ImageSource& source,
        const Windows::Foundation::Size& pixelSize);
    void PrepareKeyboardImage(
        const Microsoft::UI::Xaml::Media::ImageSource& source,
        const Windows::Foundation::Size& pixelSize);
    Windows::Foundation::IAsyncAction PrepareConnectionAsync(PenStatus::State state, bool keyboard);
    // Compatibility prewarm; callers that present a toast should await the
    // method above before taking their immutable presentation snapshot.
    void PrepareConnectionImages(const PenStatus::State& state);
    void ShowHeadphoneConnected(
        const HuaweiHeadphones::Snapshot& state,
        const Microsoft::UI::Xaml::Media::ImageSource& imageSource = nullptr);
    void UpdateHeadphoneState(
        const HuaweiHeadphones::Snapshot& state,
        const Microsoft::UI::Xaml::Media::ImageSource& imageSource = nullptr);
    void UpdateState(const PenStatus::State& state);
    void ShowDeviation();
    void ShowToolChanged(bool eraser);
    void ApplyTheme(Microsoft::UI::Xaml::ElementTheme theme);
    void HideNotification();
    bool NeedsStateRefresh() const noexcept;
    void SetRefreshDemandChanged(std::function<void()> cb);
    void Shutdown();

private:
    HWND WindowHandle();
    void ConfigureWindow();
    void SelectView(Microsoft::UI::Xaml::UIElement const& view);
    void SyncFrameTheme();
    RECT TargetBounds(int widthDip, int heightDip);
    void PositionAndShow(int widthDip, int heightDip);
    RECT AboveScreenBounds(const RECT& bounds) const;
    void StartWindowAnimation(const RECT& from, const RECT& to,
                              double fromOpacity, double toOpacity,
                              int milliseconds, bool easeOut, std::function<void()> completed);
    void AdvanceWindowAnimation(std::chrono::steady_clock::time_point now);
    void StopWindowAnimation();
    void SlideOut(int milliseconds, std::function<void()> completed);
    void PresentView(Microsoft::UI::Xaml::UIElement const& view, int widthDip, int heightDip,
                     std::function<void()> apply);
    void RestartDwellTimer();
    void ApplyConnectedState(const PenStatus::State& state,
        const Microsoft::UI::Xaml::Media::ImageSource& source,
        const Windows::Foundation::Size& pixelSize);
    void ApplyKeyboardState(const PenStatus::State& state,
        const Microsoft::UI::Xaml::Media::ImageSource& source,
        const Windows::Foundation::Size& pixelSize);
    void ApplyHeadphoneState(const HuaweiHeadphones::Snapshot& state);
    void ApplyHeadphoneImage(const Microsoft::UI::Xaml::Media::ImageSource& imageSource);
    void ApplyNoUpscaleSize(
        Microsoft::UI::Xaml::Controls::Image const& image,
        Windows::Foundation::Size const& pixelSize,
        double maxWidthDip,
        double maxHeightDip);
    Windows::Foundation::IAsyncAction LoadPenImage(uint32_t modelId);
    Windows::Foundation::IAsyncAction LoadKeyboardImage();

    Microsoft::UI::Xaml::DispatcherTimer m_dwellTimer{nullptr};
    uint64_t m_presentationGeneration = 0;
    bool m_presented = false;
    bool m_connectionView = false;
    bool m_shutdown = false;
    Microsoft::UI::Xaml::UIElement m_pendingView{nullptr};
    std::function<void()> m_onRefreshDemandChanged;
    struct WindowAnimation {
        bool active = false;
        bool easeOut = true;
        HWND hwnd = nullptr;
        RECT from{}, to{}, lastBounds{};
        double fromOpacity = 0.0, toOpacity = 1.0, lastOpacity = -1.0;
        std::chrono::steady_clock::time_point started{}, lastAdvanced{};
        int milliseconds = 0;
        uint64_t generation = 0;
        std::function<void()> completed;
    } m_animation;
    winrt::event_token m_renderingToken{};
    uint64_t m_animationSerial = 0;
    Microsoft::UI::Xaml::DispatcherTimer m_animationWatchdog{nullptr};
    LONG m_slideScreenTop = 0;
    uint32_t m_penModelId = 0;
    uint32_t m_penImageSourceModelId = 0;
    uint64_t m_penImageGeneration = 0;
    uint64_t m_keyboardImageGeneration = 0;
    bool m_penImageLoading = false;
    bool m_keyboardImageLoading = false;
    ULONGLONG m_penImageRetryAt = 0;
    ULONGLONG m_keyboardImageRetryAt = 0;
    Windows::Foundation::Size m_penImagePixelSize{};
    Windows::Foundation::Size m_keyboardImagePixelSize{};
    Microsoft::UI::Xaml::Media::ImageSource m_penImageSource{nullptr};
    Microsoft::UI::Xaml::Media::ImageSource m_keyboardImageSource{nullptr};
    uint64_t m_headphoneAddress = 0;
    std::string m_headphoneArtworkKey;
};

} // namespace winrt::EGoTouchSettings::implementation

namespace winrt::EGoTouchSettings::factory_implementation {

struct NotificationWindow : NotificationWindowT<NotificationWindow, implementation::NotificationWindow> {};

} // namespace winrt::EGoTouchSettings::factory_implementation
