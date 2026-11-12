#include "pch.h"
#include "NativeInputTrace.h"

#include <shlobj.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <iomanip>
#include <locale>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace NativeInputTrace {
namespace Xaml = winrt::Microsoft::UI::Xaml;
namespace Controls = winrt::Microsoft::UI::Xaml::Controls;
namespace Input = winrt::Microsoft::UI::Xaml::Input;
namespace PointerInput = winrt::Microsoft::UI::Input;
using winrt::Windows::Foundation::IInspectable;

namespace {
constexpr uint32_t kMaxRows = 80;
constexpr size_t kMaxAncestors = 24;
constexpr size_t kMaxScrollViewers = 12;

std::string Quote(std::string_view text) {
    std::string result = "\"";
    for (const unsigned char c : text) {
        if (c == '\\' || c == '"') { result += '\\'; result += static_cast<char>(c); }
        else if (c >= 32) result += static_cast<char>(c);
    }
    return result + '"';
}

std::string Number(double value) {
    if (!std::isfinite(value)) return "null";
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::fixed << std::setprecision(3) << value;
    return stream.str();
}

const char* Boolean(bool value) noexcept { return value ? "true" : "false"; }

const char* DeviceName(PointerInput::PointerDeviceType type) noexcept {
    switch (type) {
    case PointerInput::PointerDeviceType::Touch: return "Touch";
    case PointerInput::PointerDeviceType::Pen: return "Pen";
    case PointerInput::PointerDeviceType::Mouse: return "Mouse";
    case PointerInput::PointerDeviceType::Touchpad: return "Touchpad";
    default: return "Unknown";
    }
}

bool HasSystem(Input::ManipulationModes mode) noexcept {
    return (mode & Input::ManipulationModes::System) != Input::ManipulationModes::None;
}

std::string ClassName(IInspectable const& object) {
    auto value = winrt::to_string(winrt::get_class_name(object));
    if (value.size() > 180) value.resize(180);
    return Quote(value);
}

std::string ScrollFields(Controls::ScrollViewer const& viewer) {
    std::string result = "\"scrollable_height\":" + Number(viewer.ScrollableHeight()) +
        ",\"viewport_height\":" + Number(viewer.ViewportHeight()) +
        ",\"vertical_offset\":" + Number(viewer.VerticalOffset()) +
        ",\"vertical_scroll_mode\":" + std::to_string(static_cast<int>(viewer.VerticalScrollMode())) +
        ",\"is_enabled\":" + Boolean(viewer.IsEnabled()) +
        ",\"is_loaded\":" + Boolean(viewer.IsLoaded());
    const auto content = viewer.Content();
    result += ",\"content_exists\":" + std::string(Boolean(static_cast<bool>(content)));
    if (content) result += ",\"content_class\":" + ClassName(content);
    if (const auto element = content.try_as<Xaml::UIElement>()) {
        const auto mode = element.ManipulationMode();
        result += ",\"content_manipulation_mode\":" + std::to_string(static_cast<uint32_t>(mode)) +
            ",\"content_has_system\":" + Boolean(HasSystem(mode));
    }
    return result;
}

std::string RawPressedFields(uint32_t pointerId) {
    // The ID and all pointer coordinates remain local and are never serialized.
    const DWORD previousError = GetLastError();
    POINTER_INFO info{};
    SetLastError(ERROR_SUCCESS);
    const bool infoOk = GetPointerInfo(pointerId, &info) != FALSE;
    const DWORD infoError = infoOk ? ERROR_SUCCESS : GetLastError();
    POINTER_PEN_INFO pen{};
    SetLastError(ERROR_SUCCESS);
    const bool penOk = GetPointerPenInfo(pointerId, &pen) != FALSE;
    const DWORD penError = penOk ? ERROR_SUCCESS : GetLastError();
    SetLastError(previousError);
    return "\"raw_info_success\":" + std::string(Boolean(infoOk)) +
        ",\"raw_info_error\":" + std::to_string(infoError) +
        ",\"raw_pointer_type\":" + (infoOk ? std::to_string(static_cast<uint32_t>(info.pointerType)) : "null") +
        ",\"raw_pointer_flags\":" + (infoOk ? std::to_string(static_cast<uint32_t>(info.pointerFlags)) : "null") +
        ",\"raw_pen_success\":" + Boolean(penOk) +
        ",\"raw_pen_error\":" + std::to_string(penError) +
        ",\"raw_pen_flags\":" + (penOk ? std::to_string(static_cast<uint32_t>(pen.penFlags)) : "null");
}

Xaml::RoutedEvent PointerEvent(size_t index) {
    switch (index) {
    case 0: return Xaml::UIElement::PointerPressedEvent();
    case 1: return Xaml::UIElement::PointerReleasedEvent();
    case 2: return Xaml::UIElement::PointerCaptureLostEvent();
    case 3: return Xaml::UIElement::PointerCanceledEvent();
    default: return Xaml::UIElement::PointerMovedEvent();
    }
}
} // namespace

struct Session::Impl : std::enable_shared_from_this<Session::Impl> {
    ~Impl() noexcept { Stop(); }

    struct Gesture {
        uint64_t moves = 0;
        float lastX = 0, lastY = 0;
        bool positionChanged = false;
    };
    struct ScrollWatch {
        winrt::weak_ref<Controls::ScrollViewer> viewer;
        winrt::event_token started{}, completed{}, changed{};
    };

    winrt::weak_ref<Xaml::UIElement> root;
    std::array<IInspectable, 5> handlers{};
    std::map<uint32_t, Gesture> gestures;
    std::vector<ScrollWatch> scrolls;
    bool active = false;
    uint32_t accepted = 0, nativeRows = 0;
    HANDLE file = INVALID_HANDLE_VALUE;
    std::mutex queueMutex;
    std::condition_variable queueReady;
    std::deque<std::string> queue;
    bool stopping = false, limitReached = false;
    std::thread writer;

    bool Initialize(Xaml::UIElement const& element) {
        PWSTR localFolder = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &localFolder))) return false;
        std::wstring directory;
        try { directory = std::wstring(localFolder) + L"\\OpenEGoHub\\Cache\\"; }
        catch (...) { CoTaskMemFree(localFolder); throw; }
        CoTaskMemFree(localFolder);
        const DWORD attributes = GetFileAttributesW((directory + L"trace-native-input").c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
        // A marker already requires the directory to exist. Startup may clear the
        // previous file; event handlers never open or write a file on the UI thread.
        file = CreateFileW((directory + L"native-input.jsonl").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        writer = std::thread([this] { WriteLoop(); });
        root = winrt::make_weak(element);
        active = true;
        const std::weak_ptr<Impl> weak = weak_from_this();
        constexpr std::array names{"pointer_pressed", "pointer_released", "pointer_capture_lost", "pointer_canceled"};
        for (size_t i = 0; i < names.size(); ++i) {
            handlers[i] = winrt::box_value(Input::PointerEventHandler{[weak, name = names[i]](auto const&, auto const& args) {
                if (const auto self = weak.lock()) self->Observe(name, args);
            }});
            element.AddHandler(PointerEvent(i), handlers[i], true);
        }
        handlers[4] = winrt::box_value(Input::PointerEventHandler{[weak](auto const&, auto const& args) {
            if (const auto self = weak.lock()) self->Move(args);
        }});
        element.AddHandler(PointerEvent(4), handlers[4], true);
        Push("\"event\":\"start\",\"max_rows\":80,\"coordinates_recorded\":false,\"read_only\":true");
        return true;
    }

    void Push(std::string fields) noexcept {
        if (!active || accepted >= kMaxRows) return;
        try {
            if (fields.size() > 24000) return;
            std::string line = "{\"sequence\":" + std::to_string(accepted + 1) +
                ",\"tick_ms\":" + std::to_string(GetTickCount64()) + ',' + fields + "}\n";
            {
                std::lock_guard lock(queueMutex);
                if (stopping) return;
                queue.push_back(std::move(line));
                ++accepted;
                limitReached = accepted == kMaxRows;
            }
            queueReady.notify_one();
        } catch (...) {}
    }

    void WriteLoop() noexcept {
        bool ioOk = true;
        for (;;) {
            std::string line;
            {
                std::unique_lock lock(queueMutex);
                queueReady.wait(lock, [this] { return stopping || limitReached || !queue.empty(); });
                if (queue.empty()) break;
                line = std::move(queue.front());
                queue.pop_front();
            }
            if (!ioOk) continue;
            size_t position = 0;
            while (position < line.size()) {
                DWORD written = 0;
                if (!WriteFile(file, line.data() + position, static_cast<DWORD>(line.size() - position), &written, nullptr) || !written) {
                    ioOk = false;
                    break;
                }
                position += written;
            }
        }
        if (ioOk) FlushFileBuffers(file);
    }

    void Watch(Controls::ScrollViewer const& viewer) {
        for (const auto& entry : scrolls) if (entry.viewer.get() == viewer) return;
        if (scrolls.size() >= kMaxScrollViewers) return;
        const size_t index = scrolls.size();
        scrolls.push_back(ScrollWatch{winrt::make_weak(viewer)});
        const std::weak_ptr<Impl> weak = weak_from_this();
        scrolls[index].started = viewer.DirectManipulationStarted([weak, index](auto const&, auto const&) {
            if (const auto self = weak.lock()) self->NativeResult(index, "direct_manipulation_started");
        });
        scrolls[index].completed = viewer.DirectManipulationCompleted([weak, index](auto const&, auto const&) {
            if (const auto self = weak.lock()) self->NativeResult(index, "direct_manipulation_completed");
        });
        scrolls[index].changed = viewer.ViewChanged([weak, index](auto const&, Controls::ScrollViewerViewChangedEventArgs const& args) {
            if (!args.IsIntermediate()) if (const auto self = weak.lock()) self->NativeResult(index, "native_view_changed_final");
        });
    }

    std::string Ancestors(Input::PointerRoutedEventArgs const& args) {
        auto current = args.OriginalSource().try_as<Xaml::DependencyObject>();
        std::string result = "[";
        for (size_t depth = 0; current && depth < kMaxAncestors; ++depth) {
            if (depth) result += ',';
            result += "{\"class\":" + ClassName(current);
            if (const auto element = current.try_as<Xaml::UIElement>()) {
                const auto mode = element.ManipulationMode();
                result += ",\"manipulation_mode\":" + std::to_string(static_cast<uint32_t>(mode)) +
                    ",\"has_system\":" + Boolean(HasSystem(mode)) +
                    ",\"hit_test_visible\":" + Boolean(element.IsHitTestVisible());
            }
            if (const auto control = current.try_as<Controls::Control>()) result += ",\"is_enabled\":" + std::string(Boolean(control.IsEnabled()));
            if (const auto element = current.try_as<Xaml::FrameworkElement>()) result += ",\"is_loaded\":" + std::string(Boolean(element.IsLoaded()));
            if (const auto viewer = current.try_as<Controls::ScrollViewer>()) {
                result += ",\"scroll_viewer\":{" + ScrollFields(viewer) + '}';
                Watch(viewer);
            }
            result += '}';
            current = Xaml::Media::VisualTreeHelper::GetParent(current);
        }
        return result + ']';
    }

    void Observe(std::string_view event, Input::PointerRoutedEventArgs const& args) noexcept {
        if (!active || accepted >= kMaxRows) return;
        try {
            const auto pointer = args.Pointer();
            const uint32_t id = pointer.PointerId();
            const auto point = args.GetCurrentPoint(nullptr);
            const auto position = point.Position();
            if (event == "pointer_pressed") {
                if (gestures.size() >= 16) gestures.erase(gestures.begin());
                gestures[id] = Gesture{0, position.X, position.Y, false};
            }
            const auto found = gestures.find(id);
            const Gesture gesture = found != gestures.end() ? found->second : Gesture{};
            const auto properties = point.Properties();
            std::string fields = "\"event\":" + Quote(event) +
                ",\"xaml_type\":" + Quote(DeviceName(pointer.PointerDeviceType())) +
                ",\"point_type\":" + Quote(DeviceName(point.PointerDeviceType())) +
                ",\"handled\":" + Boolean(args.Handled()) +
                ",\"in_contact\":" + Boolean(point.IsInContact()) +
                ",\"in_range\":" + Boolean(pointer.IsInRange()) +
                ",\"barrel_pressed\":" + Boolean(properties.IsBarrelButtonPressed()) +
                ",\"is_eraser\":" + Boolean(properties.IsEraser()) +
                ",\"gesture_known\":" + Boolean(found != gestures.end()) +
                ",\"move_count\":" + std::to_string(gesture.moves) +
                ",\"position_changed\":" + Boolean(gesture.positionChanged);
            if (event == "pointer_pressed") fields += ',' + RawPressedFields(id);
            fields += ",\"ancestors\":" + Ancestors(args);
            Push(std::move(fields));
            if (event == "pointer_released" || event == "pointer_canceled") gestures.erase(id);
        } catch (winrt::hresult_error const& error) {
            Push("\"event\":\"observer_error\",\"hresult\":" + std::to_string(error.code().value));
        } catch (...) { Push("\"event\":\"observer_error\""); }
    }

    void Move(Input::PointerRoutedEventArgs const& args) noexcept {
        if (!active || accepted >= kMaxRows) return;
        try {
            const auto pointer = args.Pointer();
            if (!pointer.IsInContact()) return;
            const auto found = gestures.find(pointer.PointerId());
            if (found == gestures.end()) return;
            auto& gesture = found->second;
            const auto position = args.GetCurrentPoint(nullptr).Position();
            ++gesture.moves;
            gesture.positionChanged = gesture.positionChanged || position.X != gesture.lastX || position.Y != gesture.lastY;
            gesture.lastX = position.X;
            gesture.lastY = position.Y;
        } catch (...) {}
    }

    void NativeResult(size_t index, std::string_view event) noexcept {
        if (!active || accepted >= kMaxRows || nativeRows >= 20 || index >= scrolls.size()) return;
        try {
            if (const auto viewer = scrolls[index].viewer.get()) {
                ++nativeRows;
                Push("\"event\":" + Quote(event) + ",\"scroll_viewer_index\":" + std::to_string(index + 1) + ',' + ScrollFields(viewer));
            }
        } catch (...) {}
    }

    void Stop() noexcept {
        active = false;
        try {
            if (const auto element = root.get()) for (size_t i = 0; i < handlers.size(); ++i) {
                if (handlers[i]) try { element.RemoveHandler(PointerEvent(i), handlers[i]); } catch (...) {}
            }
            for (const auto& entry : scrolls) if (const auto viewer = entry.viewer.get()) {
                try { if (entry.started.value) viewer.DirectManipulationStarted(entry.started); } catch (...) {}
                try { if (entry.completed.value) viewer.DirectManipulationCompleted(entry.completed); } catch (...) {}
                try { if (entry.changed.value) viewer.ViewChanged(entry.changed); } catch (...) {}
            }
        } catch (...) {}
        root = {};
        handlers = {};
        scrolls.clear();
        gestures.clear();
        {
            std::lock_guard lock(queueMutex);
            stopping = true;
        }
        queueReady.notify_one();
        if (writer.joinable()) writer.join();
        if (file != INVALID_HANDLE_VALUE) { CloseHandle(file); file = INVALID_HANDLE_VALUE; }
    }
};

Session::~Session() noexcept { Stop(); }

void Session::Start(Xaml::UIElement const& root) noexcept {
    Stop();
    if (!root) return;
    try {
        auto candidate = std::make_shared<Impl>();
        if (candidate->Initialize(root)) m_impl = std::move(candidate);
    } catch (...) {}
}

void Session::Stop() noexcept {
    if (m_impl) m_impl->Stop();
    m_impl.reset();
}

} // namespace NativeInputTrace
