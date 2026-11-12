#pragma once

#include <winrt/Microsoft.UI.Xaml.h>
#include <memory>

namespace NativeInputTrace {

// UI-thread lifetime. No handlers, output, or worker without the opt-in marker.
class Session final {
public:
    Session() noexcept = default;
    ~Session() noexcept;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    void Start(winrt::Microsoft::UI::Xaml::UIElement const& root) noexcept;
    void Stop() noexcept;

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};

} // namespace NativeInputTrace
