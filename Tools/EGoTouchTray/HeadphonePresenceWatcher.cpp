#include "HeadphonePresenceWatcher.h"
#include "HeadphoneConnectionPolicy.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bluetoothapis.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "bthprops.lib")

namespace {
using Clock = std::chrono::steady_clock;
using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::Advertisement;
constexpr auto kHintInterval = std::chrono::seconds(1);
constexpr auto kFailureRetry = std::chrono::seconds(30);

bool EligibleTarget(uint64_t address) {
    if (!address || address > 0xffffffffffffULL) return false;
    BLUETOOTH_DEVICE_INFO info{sizeof(info)};
    info.Address.ullLong = address;
    return BluetoothGetDeviceInfo(nullptr, &info) == ERROR_SUCCESS &&
        (info.fAuthenticated || info.fRemembered) &&
        HeadphoneConnections::IsHuaweiHeadphoneName(info.szName);
}

struct PresenceState {
    std::mutex mutex;
    std::condition_variable changed;
    // Serializes callback delivery with target invalidation. Recursive so a
    // callback may stop/re-target the owner without self-deadlocking.
    std::recursive_mutex delivery;
    bool stopping = false, pending = false, watcherStopped = false, radioChanged = false;
    uint64_t target = 0, generation = 0, session = 0;
    Clock::time_point nextHint{};
    HeadphonePresenceWatcher::Callback callback;
};

void Deactivate(std::shared_ptr<PresenceState> const& state) {
    if (!state) return;
    {
        std::lock_guard delivery(state->delivery);
        std::lock_guard lock(state->mutex);
        state->stopping = true;
        state->target = 0;
        state->pending = false;
        ++state->generation;
        ++state->session;
    }
    state->changed.notify_one();
}

void FinishThread(std::thread thread) {
    if (!thread.joinable()) return;
    // Worker/callback own only PresenceState. Detaching from a callback is safe:
    // Deactivate already suppresses all future hints and the worker exits next.
    if (thread.get_id() == std::this_thread::get_id()) thread.detach();
    else thread.join();
}

struct WatcherSession {
    BluetoothLEAdvertisementWatcher watcher{nullptr};
    winrt::event_token received{}, stopped{};
    bool hasReceived = false, hasStopped = false;
    void Reset() noexcept {
        if (!watcher) return;
        try { watcher.Stop(); } catch (...) {}
        if (hasReceived) try { watcher.Received(received); } catch (...) {}
        if (hasStopped) try { watcher.Stopped(stopped); } catch (...) {}
        watcher = nullptr;
        hasReceived = hasStopped = false;
    }
    ~WatcherSession() { Reset(); }
};

// Every WinRT object is created, stopped and released on this MTA worker. Event
// handlers hold weak state only; late Stopped/Received events cannot reach an
// object destroyed on another thread or affect a replacement session.
void Run(std::shared_ptr<PresenceState> state) noexcept {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        struct ApartmentCleanup { ~ApartmentCleanup() { winrt::uninit_apartment(); } } apartment;
        WatcherSession session;
        bool active = false;
        uint64_t observedGeneration = 0;
        auto nextStart = Clock::time_point{};
        for (;;) {
            uint64_t target = 0, generation = 0, serial = 0;
            bool reset = false, start = false, deliver = false;
            {
                std::unique_lock lock(state->mutex);
                if (state->stopping) break;
                const auto now = Clock::now();
                target = state->target;
                generation = state->generation;
                if (generation != observedGeneration) {
                    observedGeneration = generation;
                    ++state->session;
                    state->watcherStopped = state->pending = false;
                    state->nextHint = {};
                    nextStart = {};
                    reset = active;
                }
                if (state->watcherStopped) {
                    state->watcherStopped = state->pending = false;
                    ++state->session;
                    reset = active;
                    nextStart = now + kFailureRetry;
                }
                if (state->radioChanged) {
                    state->radioChanged = false;
                    if (!active || reset) nextStart = {};
                }
                if (!target && active) reset = true;
                if (!reset && target && !active && now >= nextStart) {
                    start = true;
                    serial = ++state->session;
                }
                deliver = !reset && active && state->pending && now >= state->nextHint;
                if (!reset && !start && !deliver) {
                    auto deadline = Clock::time_point::max();
                    if (target && !active) deadline = nextStart;
                    if (active && state->pending) deadline = state->nextHint;
                    if (deadline == Clock::time_point::max()) state->changed.wait(lock);
                    else state->changed.wait_until(lock, deadline);
                    continue;
                }
            }
            if (reset) { session.Reset(); active = false; continue; }
            if (start) {
                // Cached pairing/name validation; no inquiry, consent, service
                // discovery or connection attempt is made by this helper.
                if (!EligibleTarget(target)) { nextStart = Clock::now() + kFailureRetry; continue; }
                {
                    std::lock_guard lock(state->mutex);
                    if (state->stopping || state->generation != generation || state->target != target) continue;
                }
                try {
                    session.watcher = BluetoothLEAdvertisementWatcher();
                    session.watcher.ScanningMode(BluetoothLEScanningMode::Passive);
                    const std::weak_ptr<PresenceState> weak = state;
                    session.received = session.watcher.Received([weak, target, generation, serial](auto const&, BluetoothLEAdvertisementReceivedEventArgs const& args) noexcept {
                        try {
                            if (args.BluetoothAddressType() != BluetoothAddressType::Public ||
                                args.BluetoothAddress() != target) return;
                            if (auto current = weak.lock()) {
                                {
                                    std::lock_guard lock(current->mutex);
                                    if (current->stopping || current->target != target ||
                                        current->generation != generation || current->session != serial) return;
                                    current->pending = true;
                                }
                                current->changed.notify_one();
                            }
                        } catch (...) {}
                    });
                    session.hasReceived = true;
                    session.stopped = session.watcher.Stopped([weak, generation, serial](auto const&, auto const&) noexcept {
                        if (auto current = weak.lock()) {
                            {
                                std::lock_guard lock(current->mutex);
                                if (current->stopping || current->generation != generation || current->session != serial) return;
                                current->watcherStopped = true;
                            }
                            current->changed.notify_one();
                        }
                    });
                    session.hasStopped = true;
                    session.watcher.Start();
                    active = true;
                } catch (...) {
                    {
                        std::lock_guard lock(state->mutex);
                        if (state->session == serial) ++state->session;
                        state->pending = state->watcherStopped = false;
                    }
                    session.Reset();
                    active = false;
                    nextStart = Clock::now() + kFailureRetry;
                }
                continue;
            }
            if (deliver) {
                std::lock_guard delivery(state->delivery);
                HeadphonePresenceWatcher::Callback callback;
                {
                    std::lock_guard lock(state->mutex);
                    if (state->stopping || state->generation != generation || state->target != target || !state->pending) continue;
                    state->pending = false;
                    state->nextHint = Clock::now() + kHintInterval;
                    callback = state->callback;
                }
                // Revalidate at the bounded hint cadence, never once per packet.
                if (!EligibleTarget(target)) {
                    std::lock_guard lock(state->mutex);
                    state->watcherStopped = true;
                    continue;
                }
                try { if (callback) callback(target); } catch (...) {}
            }
        }
        session.Reset();
    } catch (...) {
        // Initialization/capability errors disable this optional hint source.
        // Normal cached/HCI monitoring remains owned by the caller.
    }
}
}

struct HeadphonePresenceWatcher::Impl {
    std::mutex lifecycle;
    std::shared_ptr<PresenceState> state;
    std::thread worker;
};

HeadphonePresenceWatcher::HeadphonePresenceWatcher() : m_impl(std::make_unique<Impl>()) {}
HeadphonePresenceWatcher::~HeadphonePresenceWatcher() { Stop(); }

bool HeadphonePresenceWatcher::Start(Callback callback) {
    if (!callback) return false;
    std::shared_ptr<PresenceState> previous;
    std::thread previousWorker;
    bool started = false;
    {
        std::lock_guard lock(m_impl->lifecycle);
        previous = std::move(m_impl->state);
        previousWorker = std::move(m_impl->worker);
        try {
            auto state = std::make_shared<PresenceState>();
            state->callback = std::move(callback);
            m_impl->worker = std::thread([state] { Run(state); });
            m_impl->state = std::move(state);
            started = true;
        } catch (...) {}
    }
    // Never hold the lifecycle mutex while waiting: an existing callback may
    // reenter Stop/SetTarget, including during a Start replacement.
    Deactivate(previous);
    FinishThread(std::move(previousWorker));
    return started;
}

void HeadphonePresenceWatcher::SetTarget(uint64_t pairedAddress) {
    if (pairedAddress > 0xffffffffffffULL) pairedAddress = 0;
    std::shared_ptr<PresenceState> state;
    { std::lock_guard lock(m_impl->lifecycle); state = m_impl->state; }
    if (!state) return;
    {
        std::lock_guard delivery(state->delivery);
        std::lock_guard lock(state->mutex);
        if (state->stopping || state->target == pairedAddress) return;
        state->target = pairedAddress;
        ++state->generation;
        state->pending = false;
    }
    state->changed.notify_one();
}

void HeadphonePresenceWatcher::NotifyRadioChanged() {
    std::shared_ptr<PresenceState> state;
    { std::lock_guard lock(m_impl->lifecycle); state = m_impl->state; }
    if (!state) return;
    {
        std::lock_guard lock(state->mutex);
        if (state->stopping) return;
        state->radioChanged = true;
    }
    state->changed.notify_one();
}

void HeadphonePresenceWatcher::Stop() {
    std::shared_ptr<PresenceState> state;
    std::thread worker;
    {
        std::lock_guard lock(m_impl->lifecycle);
        state = std::move(m_impl->state);
        worker = std::move(m_impl->worker);
    }
    Deactivate(state);
    FinishThread(std::move(worker));
}
