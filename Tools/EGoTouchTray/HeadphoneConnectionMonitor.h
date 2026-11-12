#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <memory>

// Per-user classic Bluetooth monitor. Connection-cache observation is read-only;
// explicit or bounded reconnect requests use the Windows audio API.
// Passive BLE presence listens only for the last verified disconnected target.
// Never performs inquiry, opens RFCOMM, changes pairing, or changes audio routing.
class HeadphoneConnectionMonitor {
public:
    HeadphoneConnectionMonitor();
    ~HeadphoneConnectionMonitor();
    HeadphoneConnectionMonitor(HeadphoneConnectionMonitor const&)=delete;
    HeadphoneConnectionMonitor& operator=(HeadphoneConnectionMonitor const&)=delete;
    bool Start(HWND owner,UINT notificationMessage);
    void Stop();
    // Queue only: enabling uses a 2s cache fallback, disabling 15s and cancels
    // automatic requests. Enabling never reconnects an old disconnected device.
    void SetFastReconnect(bool enabled);
    // Explicit one-shot request, independent of the automatic preference.
    // True means queued/coalesced, not paired/connected or successfully started.
    bool RequestReconnect(uint64_t address);
    // Call from the owning window's WM_DEVICECHANGE handler. A recognized
    // notification only queues a wake/event; all Bluetooth API work is off-UI.
    bool HandleDeviceChange(WPARAM event,LPARAM data);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
