// Native adaptation of EnergyStar, commit 18872289ac4de7d22208f1fc5b156abaa904dff8.
// Copyright 2022 Bingxing Wang
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// If you are Microsoft (and/or its affiliates) employee, vendor or contractor
// who is working on Windows-specific integration projects, you may use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so
// without the restriction above.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>
#include "EnergyStarManager.h"
#include "EnergyStarPolicy.h"
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

namespace {
constexpr UINT kHousekeepingMs = 5 * 60 * 1000;
constexpr size_t kMaximumProcesses = 8192;
constexpr size_t kMaximumModifiedProcesses = 512;
constexpr size_t kScanBatch = 8;

struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE handle) noexcept : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(Handle const&) = delete;
    Handle& operator=(Handle const&) = delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
            value = std::exchange(other.value, nullptr);
        }
        return *this;
    }
    explicit operator bool() const noexcept { return value && value != INVALID_HANDLE_VALUE; }
};

bool SupportedWindows() noexcept {
    using VersionFn = LONG (WINAPI*)(PRTL_OSVERSIONINFOW);
    const auto module = GetModuleHandleW(L"ntdll.dll");
    const auto version = module ? reinterpret_cast<VersionFn>(GetProcAddress(module, "RtlGetVersion")) : nullptr;
    RTL_OSVERSIONINFOW info{};info.dwOSVersionInfoSize = sizeof(info);
    return version && version(&info) >= 0 && info.dwBuildNumber >= 22000;
}

std::vector<unsigned char> TokenData(HANDLE token, TOKEN_INFORMATION_CLASS kind) {
    DWORD size = 0;GetTokenInformation(token, kind, nullptr, 0, &size);
    if (!size || size > 65536) return {};
    std::vector<unsigned char> data(size);
    return GetTokenInformation(token, kind, data.data(), size, &size) ? std::move(data) : std::vector<unsigned char>{};
}

bool ReadPower(HANDLE process, EnergyStarPolicy::PowerState& state) noexcept {
    PROCESS_POWER_THROTTLING_STATE power{};power.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    if (!GetProcessInformation(process, ProcessPowerThrottling, &power, sizeof(power))) return false;
    state = {power.ControlMask, power.StateMask};return true;
}

bool WritePower(HANDLE process, EnergyStarPolicy::PowerState state) noexcept {
    PROCESS_POWER_THROTTLING_STATE power{};power.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    power.ControlMask = state.controlMask;power.StateMask = state.stateMask;
    return SetProcessInformation(process, ProcessPowerThrottling, &power, sizeof(power)) != FALSE;
}

std::wstring ImageName(HANDLE process) {
    wchar_t path[32768]{};DWORD length = static_cast<DWORD>(std::size(path));
    return QueryFullProcessImageNameW(process, 0, path, &length) ? std::wstring(path, length) : std::wstring{};
}
}

struct EnergyStarManager::Impl {
    struct Modified {
        Handle process;
        DWORD priority = 0;
        EnergyStarPolicy::PowerState power;
        bool priorityChanged = false;
        bool powerChanged = false;
    };

    std::atomic<uint32_t> status{Valid | (SupportedWindows() ? Supported : 0u)};
    std::atomic<bool> desired{false}, stopping{false};
    std::atomic<uint64_t> command{0};
    std::mutex apiMutex, stopMutex;
    Handle wake;
    std::thread worker;
    bool completed = false;
    uint64_t handledCommand = 0;
    DWORD session = 0;
    std::vector<unsigned char> userSid;
    bool active = false, failed = false, foregroundDirty = false;
    HWINEVENTHOOK hook = nullptr;
    UINT_PTR timer = 0;
    HWND foregroundWindow = nullptr;
    bool foregroundFrame = false;
    ULONGLONG processTreeTick = 0;
    std::vector<EnergyStarPolicy::ProcessNode> processes;
    std::vector<uint32_t> foreground;
    std::vector<uint32_t> scan;
    size_t scanPosition = 0;
    std::map<DWORD, Modified> modified;
#ifdef OPENEGO_ENERGYSTAR_TESTING
    DWORD testOnlyPid = 0;
    Handle testIdentity;
    FILETIME testCreation{};
    std::atomic<uint32_t> testForeground{0};
    std::atomic<uint64_t> testForegroundVersion{1};
    uint64_t testObservedVersion = 0;
#endif

    ~Impl() { if (worker.joinable()) worker.detach(); }
    static Impl*& CallbackOwner() noexcept { static thread_local Impl* owner = nullptr;return owner; }
    static void CALLBACK ForegroundEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) noexcept {
        // OUTOFCONTEXT dispatches on this worker. Avoid process calls and
        // reentrancy here; always resolve the actual current window afterwards.
        if (auto self = CallbackOwner()) self->foregroundDirty = true;
    }
    void PublishLocked(bool transitioning) noexcept {
        transitioning = transitioning || (!stopping.load() && command.load() != handledCommand);
        status.store(Valid | (status.load() & Supported) |
            ((active || !modified.empty()) ? Enabled : 0u) |
            (transitioning ? Transitioning : 0u) | (failed ? Failed : 0u), std::memory_order_release);
    }
    void Publish(bool transitioning = false) noexcept {
        try { std::lock_guard lock(apiMutex);PublishLocked(transitioning); }
        catch (...) { status.fetch_or(Failed); }
    }
    void RejectCurrentRequest() noexcept {
        try {
            std::lock_guard lock(apiMutex);
            if (command.load() == handledCommand) desired.store(false);
            failed = true;PublishLocked(false);
        } catch (...) { status.fetch_or(Failed); }
    }
    bool InitializeUser() {
        if (!userSid.empty()) return true;
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) return false;
        HANDLE raw = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) return false;
        Handle token(raw);auto data = TokenData(token.value, TokenUser);
        if (data.empty()) return false;
        const auto sid = reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid;
        if (!IsValidSid(sid)) return false;
        const DWORD length = GetLengthSid(sid);userSid.resize(length);
        return CopySid(length, userSid.data(), sid) != FALSE;
    }
    bool OrdinaryUserProcess(HANDLE process) {
        HANDLE raw = nullptr;if (!OpenProcessToken(process, TOKEN_QUERY, &raw)) return false;
        Handle token(raw);DWORD returned = 0, processSession = 0, uiAccess = 0;TOKEN_ELEVATION elevation{};
        if (!GetTokenInformation(token.value, TokenSessionId, &processSession, sizeof(processSession), &returned) || processSession != session ||
            !GetTokenInformation(token.value, TokenElevation, &elevation, sizeof(elevation), &returned) || elevation.TokenIsElevated ||
            !GetTokenInformation(token.value, TokenUIAccess, &uiAccess, sizeof(uiAccess), &returned) || uiAccess) return false;
        const auto user = TokenData(token.value, TokenUser);if (user.empty()) return false;
        const auto sid = reinterpret_cast<const TOKEN_USER*>(user.data())->User.Sid;
        if (!IsValidSid(sid) || !EqualSid(userSid.data(), sid)) return false;
        const auto integrity = TokenData(token.value, TokenIntegrityLevel);if (integrity.empty()) return false;
        const auto level = reinterpret_cast<const TOKEN_MANDATORY_LABEL*>(integrity.data())->Label.Sid;
        if (!IsValidSid(level)) return false;
        const auto count = *GetSidSubAuthorityCount(level);
        return count && *GetSidSubAuthority(level, count - 1) <= SECURITY_MANDATORY_MEDIUM_RID;
    }
    bool CaptureProcessTree() {
        Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));if (!snapshot) return false;
        std::vector<EnergyStarPolicy::ProcessNode> current;current.reserve(256);
        PROCESSENTRY32W entry{};entry.dwSize = sizeof(entry);
        if (!Process32FirstW(snapshot.value, &entry)) return false;
        do {
            if (stopping.load() || !desired.load()) return false;
            current.push_back({entry.th32ProcessID, entry.th32ParentProcessID, entry.szExeFile});
        } while (current.size() < kMaximumProcesses && Process32NextW(snapshot.value, &entry));
        processes = std::move(current);processTreeTick = GetTickCount64();return true;
    }
    static BOOL CALLBACK ChildProcess(HWND window, LPARAM context) noexcept {
        auto& roots = *reinterpret_cast<std::vector<uint32_t>*>(context);DWORD pid = 0;
        if (GetWindowThreadProcessId(window, &pid) && pid && roots.size() < 64 &&
            std::find(roots.begin(), roots.end(), pid) == roots.end()) {
            try { roots.push_back(pid); } catch (...) { return FALSE; }
        }
        return TRUE;
    }
    EnergyStarPolicy::ForegroundSelection ForegroundRoots(HWND window) {
        DWORD pid = 0;GetWindowThreadProcessId(window, &pid);if (!pid) return {};
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
        if (!process) return {};
        const auto image = ImageName(process.value);std::vector<uint32_t> children;
        if (EnergyStarPolicy::Detail::EqualName(EnergyStarPolicy::Detail::Basename(image), L"ApplicationFrameHost.exe"))
            EnumChildWindows(window, ChildProcess, reinterpret_cast<LPARAM>(&children));
        return EnergyStarPolicy::ResolveForeground(pid, image, children);
    }
    bool IsBackgroundNow(DWORD pid) {
        if (stopping.load() || !desired.load()) return false;
#ifdef OPENEGO_ENERGYSTAR_TESTING
        const auto current = testForeground.load();
        return current != 0 && current != pid;
#else
        const HWND current = GetForegroundWindow();
        if (!current || current != foregroundWindow) { foregroundDirty = true;return false; }
        DWORD currentPid = 0;GetWindowThreadProcessId(current, &currentPid);
        if (!currentPid || currentPid == pid) return false;
        if (std::binary_search(foreground.begin(), foreground.end(), pid)) return false;
        // Frame children can change before an asynchronously delivered event.
        // Recheck them immediately before each mutation, not only at enumeration.
        if (foregroundFrame) {
            const auto selection = ForegroundRoots(current);
            if (!selection.valid) { foregroundDirty = true;return false; }
            const auto family = EnergyStarPolicy::ForegroundFamily(processes, selection.roots);
            if (std::binary_search(family.begin(), family.end(), pid)) { foregroundDirty = true;return false; }
        }
        return true;
#endif
    }
    bool Restore(Modified& item) noexcept {
        if (WaitForSingleObject(item.process.value, 0) == WAIT_OBJECT_0) {
            item.priorityChanged = item.powerChanged = false;return true;
        }
        if (item.powerChanged) {
            EnergyStarPolicy::PowerState current;
            if (ReadPower(item.process.value, current) && WritePower(item.process.value, EnergyStarPolicy::RestoreExecution(current, item.power)))
                item.powerChanged = false;
        }
        if (item.priorityChanged) {
            const DWORD current = GetPriorityClass(item.process.value);
            // Relinquish an external priority decision instead of overwriting it.
            if (current && (current != IDLE_PRIORITY_CLASS || SetPriorityClass(item.process.value, item.priority))) item.priorityChanged = false;
        }
        return !item.priorityChanged && !item.powerChanged;
    }
    void RestoreForeground() noexcept {
        for (const auto pid : foreground) {
            const auto found = modified.find(pid);
            if (found != modified.end() && Restore(found->second)) modified.erase(found);
        }
    }
    void PruneExited() noexcept {
        for (auto entry = modified.begin(); entry != modified.end(); )
            if (WaitForSingleObject(entry->second.process.value, 0) == WAIT_OBJECT_0) entry = modified.erase(entry);else ++entry;
    }
    void Throttle(DWORD pid) {
#ifdef OPENEGO_ENERGYSTAR_TESTING
        if (!testOnlyPid || pid != testOnlyPid || !testIdentity) return;
#endif
        if (!pid || pid == GetCurrentProcessId() || !IsBackgroundNow(pid)) return;
        if (const auto found = modified.find(pid); found != modified.end()) {
            if (WaitForSingleObject(found->second.process.value, 0) == WAIT_OBJECT_0) modified.erase(found);
            else return;
        }
        if (modified.size() >= kMaximumModifiedProcesses) PruneExited();
        if (modified.size() >= kMaximumModifiedProcesses) return;
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION | SYNCHRONIZE, FALSE, pid));
        if (!process || EnergyStarPolicy::IsBypassed(ImageName(process.value)) || !OrdinaryUserProcess(process.value)) return;
#ifdef OPENEGO_ENERGYSTAR_TESTING
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(process.value, &creation, &exit, &kernel, &user) ||
            CompareFileTime(&creation, &testCreation) != 0) return;
#endif
        PROCESS_PROTECTION_LEVEL_INFORMATION protection{};
        if (!GetProcessInformation(process.value, ProcessProtectionLevelInfo, &protection, sizeof(protection)) || protection.ProtectionLevel != PROTECTION_LEVEL_NONE) return;
        Modified item;item.priority = GetPriorityClass(process.value);
        // Elevated scheduling can be intentional for input/audio/latency work.
        if (item.priority != NORMAL_PRIORITY_CLASS && item.priority != BELOW_NORMAL_PRIORITY_CLASS && item.priority != IDLE_PRIORITY_CLASS) return;
        if (!ReadPower(process.value, item.power) || !IsBackgroundNow(pid)) return;
        item.process = std::move(process);
        // Allocate the ownership record before the first OS mutation so even an
        // allocation failure cannot leave an untracked changed process behind.
        auto [found, inserted] = modified.try_emplace(pid, std::move(item));if (!inserted) return;
        auto& owned = found->second;
        const auto eco = EnergyStarPolicy::EnableEco(owned.power);
        if (eco != owned.power) {
            if (!IsBackgroundNow(pid) || !WritePower(owned.process.value, eco)) { modified.erase(found);return; }
            owned.powerChanged = true;
        }
        if (owned.priority != IDLE_PRIORITY_CLASS) {
            if (!IsBackgroundNow(pid) || !SetPriorityClass(owned.process.value, IDLE_PRIORITY_CLASS)) {
                if (Restore(owned)) modified.erase(found);return;
            }
            owned.priorityChanged = true;
        }
        if (!owned.powerChanged && !owned.priorityChanged) modified.erase(found);
    }
    void SyncForeground() {
        foregroundDirty = false;
#ifdef OPENEGO_ENERGYSTAR_TESTING
        const auto root = testForeground.load();if (!root) return;
        const HWND window = nullptr;const bool frame = false;
        const std::vector<uint32_t> roots{root};
#else
        const HWND window = GetForegroundWindow();if (!window) return;
        const auto selection = ForegroundRoots(window);if (!selection.valid) return;
        const bool frame = selection.frame;const auto& roots = selection.roots;
#endif
        // Restore exact foreground/UWP roots before even the cheap process-tree
        // refresh, then include cached descendants and same-image app parents.
        for (const auto pid : roots) {
            const auto found = modified.find(pid);
            if (found != modified.end() && Restore(found->second)) modified.erase(found);
        }
        if (GetTickCount64() - processTreeTick >= 1000) (void)CaptureProcessTree();
        auto previous = std::move(foreground);
#ifdef OPENEGO_ENERGYSTAR_TESTING
        // Synthetic focus isolates a windowless child from its test controller.
        // Family calculation is independently covered by the pure policy test.
        foreground = roots;
#else
        foreground = EnergyStarPolicy::ForegroundFamily(processes, roots);
#endif
        foregroundWindow = window;foregroundFrame = frame;RestoreForeground();
        for (const auto pid : previous)
            if (!std::binary_search(foreground.begin(), foreground.end(), pid)) Throttle(pid);
    }
    bool Begin() {
        failed = false;
        if (!InitializeUser()) return false;
        CallbackOwner() = this;
#ifndef OPENEGO_ENERGYSTAR_TESTING
        hook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, ForegroundEvent, 0, 0,
            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (!hook) return false;
#endif
        timer = SetTimer(nullptr, 0, kHousekeepingMs, nullptr);
        if (!timer || !CaptureProcessTree()) return false;
        active = true;foregroundDirty = true;SyncForeground();QueueScan();return true;
    }
    void QueueScan() {
        PruneExited();
        scan.clear();scan.reserve(processes.size());scanPosition = 0;
        for (const auto& process : processes)
            if (!EnergyStarPolicy::IsBypassed(process.image)) scan.push_back(process.pid);
    }
    void Cleanup() noexcept {
        active = false;
        if (hook) { UnhookWinEvent(hook);hook = nullptr; }
        CallbackOwner() = nullptr;
        if (timer) { KillTimer(nullptr, timer);timer = 0; }
        scan.clear();scanPosition = 0;foreground.clear();foregroundWindow = nullptr;foregroundDirty = false;
        // All calls operate on retained handles and are local OS operations;
        // exited PIDs disappear. No reopening by PID, sleeps or unbounded retry.
        for (unsigned attempt = 0; attempt < 3 && !modified.empty(); ++attempt)
            for (auto entry = modified.begin(); entry != modified.end(); )
                if (Restore(entry->second)) entry = modified.erase(entry);else ++entry;
        if (!modified.empty()) failed = true;
    }
    void Run() noexcept {
        MSG message{};PeekMessageW(&message, nullptr, 0, 0, PM_NOREMOVE);
        handledCommand = 0;
        while (!stopping.load(std::memory_order_acquire)) {
            try {
                for (unsigned count = 0; count < 64 && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE); ++count) {
                    if (message.message == WM_QUIT) { stopping.store(true);break; }
                    if (message.message == WM_TIMER && message.wParam == timer && active && desired.load()) {
                        if (CaptureProcessTree()) { foregroundDirty = true;QueueScan(); }
                    } else { TranslateMessage(&message);DispatchMessageW(&message); }
                }
                if (stopping.load()) break;
#ifdef OPENEGO_ENERGYSTAR_TESTING
                const auto focusVersion = testForegroundVersion.load();
                if (focusVersion != testObservedVersion) { testObservedVersion = focusVersion;foregroundDirty = true; }
#endif
                const uint64_t requested = command.load(std::memory_order_acquire);
                if (requested != handledCommand) {
                    handledCommand = requested;Publish(true);
                    if (!desired.load()) { Cleanup();Publish(); }
                    else if (!active) {
                        Cleanup();
                        if (!modified.empty() || !Begin()) {
                            const bool rejected = desired.load();Cleanup();
                            if (rejected) RejectCurrentRequest();else Publish();
                        }
                        else Publish(scanPosition < scan.size());
                    } else Publish(scanPosition < scan.size());
                }
                if (active && desired.load()) {
                    if (foregroundDirty) SyncForeground();
                    for (size_t count = 0; count < kScanBatch && scanPosition < scan.size() && desired.load() && !stopping.load(); ++count)
                        Throttle(scan[scanPosition++]);
                    if (scanPosition == scan.size()) { scan.clear();scanPosition = 0;Publish(); }
                }
                if (!active && !modified.empty()) { PruneExited();Publish(); }
            } catch (...) { Cleanup();RejectCurrentRequest(); }
            if (stopping.load()) break;
            // Serialize retirement with SetEnabled. A new enable either keeps
            // this worker alive or reaps it and starts a fresh one afterwards.
            if (!active && modified.empty()) {
                std::lock_guard lock(apiMutex);
                if (!desired.load() && command.load() == handledCommand) {
                    PublishLocked(false);completed = true;return;
                }
            }
            // Only a failed restoration retains this exceptional wait. Observe
            // exits quietly so no handles/Enabled state survive a dead target;
            // never issue an unbounded series of restore writes.
            const DWORD timeout = !active && !modified.empty() ? 5000 :
                (active && scanPosition < scan.size() ? 0 : INFINITE);
            HANDLE event = wake.value;
            if (MsgWaitForMultipleObjectsEx(1, &event, timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE) == WAIT_FAILED) {
                failed = true;stopping.store(true);break;
            }
        }
        Publish(true);Cleanup();Publish();
        try { std::lock_guard lock(apiMutex);completed = true; } catch (...) {}
    }
};

EnergyStarManager::EnergyStarManager() noexcept { try { m_impl = std::make_shared<Impl>(); } catch (...) {} }
#ifdef OPENEGO_ENERGYSTAR_TESTING
EnergyStarManager::EnergyStarManager(uint32_t onlyProcessId) noexcept : EnergyStarManager() {
    if (!m_impl) return;
    m_impl->testOnlyPid = onlyProcessId;
    m_impl->testForeground.store(GetCurrentProcessId());
    m_impl->testIdentity = Handle(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, onlyProcessId));
    FILETIME exit{}, kernel{}, user{};
    if (!m_impl->testIdentity || !GetProcessTimes(m_impl->testIdentity.value, &m_impl->testCreation, &exit, &kernel, &user))
        m_impl->testIdentity = Handle{};
}
void EnergyStarManager::SetTestForeground(uint32_t processId) noexcept {
    if (!m_impl) return;
    m_impl->testForeground.store(processId);
    m_impl->testForegroundVersion.fetch_add(1);
    if (m_impl->wake) SetEvent(m_impl->wake.value);
}
#endif
EnergyStarManager::~EnergyStarManager() { Stop(); }
bool EnergyStarManager::SetEnabled(bool enabled) noexcept {
    try {
        const auto self = m_impl;if (!self || (enabled && !IsSupported())) return false;
        std::lock_guard lock(self->apiMutex);if (self->stopping.load()) return false;
        if (self->completed && self->worker.joinable()) { self->worker.join();self->completed = false; }
        if (!enabled && !self->worker.joinable()) return true;
        if (!self->worker.joinable()) {
            self->wake = Handle(CreateEventW(nullptr, FALSE, FALSE, nullptr));
            if (!self->wake) { self->status.fetch_or(Failed);return false; }
            self->worker = std::thread([self] { self->Run(); });
        }
        self->desired.store(enabled, std::memory_order_release);
        self->status.fetch_or(Transitioning, std::memory_order_release);
        self->command.fetch_add(1, std::memory_order_release);
        return SetEvent(self->wake.value) != FALSE;
    } catch (...) { if (m_impl) m_impl->status.fetch_or(Failed);return false; }
}
bool EnergyStarManager::IsEnabled() const noexcept { return (GetStatus() & Enabled) != 0; }
bool EnergyStarManager::IsSupported() const noexcept { return (GetStatus() & Supported) != 0; }
uint32_t EnergyStarManager::GetStatus() const noexcept {
    return m_impl ? m_impl->status.load(std::memory_order_acquire) : Valid | Failed;
}
void EnergyStarManager::Stop() noexcept {
    try {
        const auto self = m_impl;if (!self) return;
        std::lock_guard stoppingLock(self->stopMutex);
        {
            std::lock_guard lock(self->apiMutex);self->stopping.store(true, std::memory_order_release);
            self->desired.store(false, std::memory_order_release);
            if (self->worker.joinable() && !self->completed) self->status.fetch_or(Transitioning);
            if (self->wake) SetEvent(self->wake.value);
        }
        if (self->worker.joinable() && self->worker.get_id() != std::this_thread::get_id()) self->worker.join();
    } catch (...) { if (m_impl) m_impl->status.fetch_or(Failed); }
}
