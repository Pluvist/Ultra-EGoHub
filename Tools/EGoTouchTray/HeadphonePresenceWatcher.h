#pragma once
#include <cstdint>
#include <functional>
#include <memory>

// Passive presence hints for the last paired Huawei headset selected by the
// monitor. A hint is not evidence that an audio connection has completed.
// Only a Public advertisement with the exact selected classic address matches.
// No advertisement payload or other device identity is retained.
class HeadphonePresenceWatcher {
public:
    using Callback = std::function<void(uint64_t pairedAddress)>;

    HeadphonePresenceWatcher();
    ~HeadphonePresenceWatcher();
    HeadphonePresenceWatcher(HeadphonePresenceWatcher const&) = delete;
    HeadphonePresenceWatcher& operator=(HeadphonePresenceWatcher const&) = delete;

    // Creates a self-owned MTA worker, without scanning until SetTarget(nonzero).
    // true means the worker started; unavailable/denied BLE falls back quietly.
    // Callback runs on that worker, at most once per second. It should only
    // enqueue a hint; the monitor owns its bounded reconnect-round policy.
    bool Start(Callback callback);
    // Caller supplies only its most recent, confirmed paired Huawei target.
    // Zero (or an invalid address) immediately invalidates hints and queues a
    // watcher stop. Repeating the same target does not restart failed scanning.
    void SetTarget(uint64_t pairedAddress);
    // Optional explicit radio-change hint. Failed scanning otherwise retries
    // no sooner than 30 seconds, independently of cached 2-second device polls.
    void NotifyRadioChanged();
    // Waits for callbacks/worker shutdown; also safe from inside Callback.
    // No callback uses this object's address or survives an external Stop.
    void Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
