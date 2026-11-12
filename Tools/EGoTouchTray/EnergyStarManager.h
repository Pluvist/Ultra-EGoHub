#pragma once
#include <cstdint>
#include <memory>

// Optional, per-user EnergyStar policy. No worker or process changes exist until
// explicitly enabled. Foreground apps recover only values changed by this engine.
class EnergyStarManager {
public:
    enum Status : uint32_t {
        Supported = 1, Enabled = 2, Transitioning = 4, Failed = 8, Valid = 256
    };

    EnergyStarManager() noexcept;
#ifdef OPENEGO_ENERGYSTAR_TESTING
    // Test builds cannot mutate anything except this retained child identity.
    explicit EnergyStarManager(uint32_t onlyProcessId) noexcept;
    void SetTestForeground(uint32_t processId) noexcept;
#endif
    ~EnergyStarManager();
    EnergyStarManager(EnergyStarManager const&) = delete;
    EnergyStarManager& operator=(EnergyStarManager const&) = delete;

    // Accepts an asynchronous request, without enumerating or changing processes
    // on the caller's thread. Query GetStatus for startup/restore completion.
    bool SetEnabled(bool enabled) noexcept;
    bool IsEnabled() const noexcept;
    bool IsSupported() const noexcept;
    uint32_t GetStatus() const noexcept;
    // Final shutdown: cancel scans, unhook on its registering thread and restore
    // all retained process handles before joining. Ordinary disable is async.
    void Stop() noexcept;

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};
