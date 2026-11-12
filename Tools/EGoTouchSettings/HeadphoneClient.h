#pragma once
#include "HuaweiHeadphonesProtocol.h"
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace HuaweiHeadphones {
struct Device { uint64_t address = 0; std::wstring name; bool connected = false; };
struct ConnectionOptions {
    std::chrono::milliseconds openBudget{4000};
    bool standardSppOnly = false;
    bool batteryOnly = false; // Caller already validated the cached model/color for this device.
};
inline constexpr ConnectionOptions NotificationConnectionOptions{std::chrono::milliseconds{2000}, true};
struct Snapshot {
    uint64_t address = 0;
    Model model = Model::Unknown;
    std::wstring name, message = L"请先在 Windows 蓝牙设置中连接耳机。";
    std::string resourceId, colorId;
    bool identityConfirmed = false; // ReadIdentity evidence, never set by an artwork cache.
    bool ready = false, failed = false;
    std::array<Battery,3> battery{};
    std::chrono::steady_clock::time_point batteryReadTime{};
    std::map<std::string,int> values;
    std::optional<std::array<int,10>> equalizer;
    std::vector<PairedPeer> peers;
    int errorCode = 0;
};
// Only called on the UI's background coroutine. Cancel is nonblocking and is
// safe on the window thread. No service/admin privileges or periodic worker.
class Client {
public:
    using Progress = std::function<void(const Snapshot&)>;
    Client();
    ~Client();
    static std::vector<Device> Devices();
    static std::optional<Device> PairedDevice(uint64_t address); // Windows cache; connection flag may lag events
    static std::optional<Device> ConnectedDevice(uint64_t address);
    Snapshot Refresh(const Device& device, bool full, const Progress& progress = {});
    Snapshot NotificationState(const Device& device, ConnectionOptions options = {}); // complete fresh battery snapshot, preserves page session
    Snapshot ConnectionState(const Device& device, const Progress& progress = {}, ConnectionOptions options = {}); // identity/battery only
    Snapshot Set(uint64_t address, std::string_view id, int value);
    Snapshot SetEqualizer(uint64_t address, int slot, std::string_view name, std::span<const int> gains);
    Snapshot RemovePeer(uint64_t address, const std::array<uint8_t,6>& peer);
    void Cancel();
    void ReleaseCancelled(); // background cleanup after Cancel; never closes a newer session
private:
    Snapshot RefreshImpl(const Device& device, bool full, const Progress& progress, bool notificationRead,
                         ConnectionOptions options = {});
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
