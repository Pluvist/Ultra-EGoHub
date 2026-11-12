#ifndef OPENEGO_ENERGYSTAR_TESTING
#error This isolated test requires OPENEGO_ENERGYSTAR_TESTING for both source files.
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "EnergyStarManager.h"
#include "EnergyStarPolicy.h"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void Check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct Child {
    HANDLE process = nullptr;
    DWORD pid = 0;
    Child() {
        wchar_t executable[32768]{};
        const DWORD count = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        Check(count && count < std::size(executable), "test executable path");
        std::wstring command = L"\"" + std::wstring(executable, count) + L"\" --energy-star-test-child";
        STARTUPINFOW startup{};startup.cb = sizeof(startup);PROCESS_INFORMATION created{};
        Check(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &startup, &created) != FALSE,
            "create the sole isolated child");
        process = created.hProcess;pid = created.dwProcessId;CloseHandle(created.hThread);
    }
    ~Child() {
        // This is only the exact process handle returned by our CreateProcess.
        // No process search, PID reopening or application-wide cleanup occurs.
        if (process) {
            if (WaitForSingleObject(process, 0) == WAIT_TIMEOUT) TerminateProcess(process, 0);
            WaitForSingleObject(process, 2000);CloseHandle(process);
        }
    }
};
EnergyStarPolicy::PowerState Power(HANDLE process) {
    PROCESS_POWER_THROTTLING_STATE state{};state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    Check(GetProcessInformation(process, ProcessPowerThrottling, &state, sizeof(state)) != FALSE, "read process power policy");
    return {state.ControlMask, state.StateMask};
}
void WaitChildPriority(Child const& child, DWORD priority) {
    const auto deadline = GetTickCount64() + 3000;
    while (GetPriorityClass(child.process) != priority) {
        Check(GetTickCount64() < deadline, "foreground transition restoration deadline");Sleep(10);
    }
}
void WaitState(EnergyStarManager& manager, bool enabled) {
    const auto deadline = GetTickCount64() + 3000;
    for (;;) {
        const auto status = manager.GetStatus();
        if (!(status & EnergyStarManager::Transitioning)) {
            Check(!(status & EnergyStarManager::Failed), "worker startup or restoration failed");
            if (((status & EnergyStarManager::Enabled) != 0) == enabled) return;
        }
        Check(GetTickCount64() < deadline, "asynchronous request completion deadline");Sleep(10);
    }
}
void Restored(Child const& child, DWORD priority, EnergyStarPolicy::PowerState power) {
    Check(GetPriorityClass(child.process) == priority, "restore the child's actual original priority, not NORMAL");
    Check(Power(child.process) == power, "restore exact original execution bits and preserve other power bits");
}
void Throttled(Child const& child) {
    Check(GetPriorityClass(child.process) == IDLE_PRIORITY_CLASS, "isolated child receives IDLE");
    const auto power = Power(child.process);
    Check((power.controlMask & 1) && (power.stateMask & 1), "isolated child receives EcoQoS");
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--energy-star-test-child") {
        Sleep(30000);return 0; // child mode never constructs an engine
    }
    try {
        const DWORD ownPriority = GetPriorityClass(GetCurrentProcess());
        const auto ownPower = Power(GetCurrentProcess());
        Child child;
        // This unrelated bit belongs to the fixture, never to the manager.
        // A real API roundtrip verifies that enabling/restoring EcoQoS retains it.
        PROCESS_POWER_THROTTLING_STATE fixture{};fixture.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        fixture.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED | PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
        fixture.StateMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
        Check(SetProcessInformation(child.process, ProcessPowerThrottling, &fixture, sizeof(fixture)) != FALSE, "prepare only the child power baseline");
        const auto originalPower = Power(child.process);
        const DWORD originalPriority = GetPriorityClass(child.process);
        Check(originalPriority == BELOW_NORMAL_PRIORITY_CLASS, "child starts with a non-default baseline");
        EnergyStarManager manager(child.pid);
        if (!manager.IsSupported()) { std::cout << "Windows 11 EcoQoS unavailable; skipped\n";return 0; }
        Check(!manager.IsEnabled() && manager.SetEnabled(false), "default off performs no work");
        for (unsigned round = 0; round < 3; ++round) {
            Check(manager.SetEnabled(true), "accept isolated enable");WaitState(manager, true);Throttled(child);
            Check(manager.SetEnabled(true), "coalesce repeated enable");WaitState(manager, true);Throttled(child);
            manager.SetTestForeground(child.pid);WaitChildPriority(child, originalPriority);Restored(child, originalPriority, originalPower);
            manager.SetTestForeground(child.pid);Sleep(20);Restored(child, originalPriority, originalPower);
            manager.SetTestForeground(GetCurrentProcessId());WaitChildPriority(child, IDLE_PRIORITY_CLASS);Throttled(child);
            Check(manager.SetEnabled(false), "accept async disable");WaitState(manager, false);Restored(child, originalPriority, originalPower);
        }
        for (unsigned change = 0; change < 20; ++change)
            Check(manager.SetEnabled((change & 1) == 0), "accept rapid request sequence");
        WaitState(manager, false);Restored(child, originalPriority, originalPower);
        Check(manager.SetEnabled(true), "enable after retired workers");WaitState(manager, true);Throttled(child);
        manager.Stop();Restored(child, originalPriority, originalPower);
        Check(!manager.IsEnabled() && !manager.SetEnabled(true), "final Stop cannot restart");
        {
            EnergyStarManager retired(child.pid);
            Check(retired.SetEnabled(true), "accept retired-worker fixture enable");WaitState(retired, true);
            Check(retired.SetEnabled(false), "accept retired-worker fixture disable");WaitState(retired, false);
            Sleep(20);retired.Stop();
            Check(!(retired.GetStatus() & (EnergyStarManager::Transitioning | EnergyStarManager::Enabled | EnergyStarManager::Failed)),
                "stable off followed by Stop stays stable off after reaping a completed worker");
            Restored(child, originalPriority, originalPower);
        }
        {
            EnergyStarManager immediate(child.pid);
            Check(immediate.SetEnabled(true), "accept immediate-stop enable");immediate.Stop();
            Restored(child, originalPriority, originalPower);
        }
        {
            EnergyStarManager destructor(child.pid);
            Check(destructor.SetEnabled(true), "accept destructor cleanup enable");WaitState(destructor, true);Throttled(child);
        }
        Restored(child, originalPriority, originalPower);
        Check(GetPriorityClass(GetCurrentProcess()) == ownPriority && Power(GetCurrentProcess()) == ownPower,
            "test runner priority and power policy were never changed");
        std::cout << "Isolated child EcoQoS/IDLE, exact restoration, retired-worker restart, rapid requests and shutdown passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n';return 1; }
}
