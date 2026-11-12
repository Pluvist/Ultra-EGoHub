#include "HubParentGuard.h"

#include <string>

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && wcscmp(argv[1], L"--child") == 0) return 0;

    wchar_t service[] = L"OpenEGoHubThpHost";
    wchar_t parent[] = L"--parent";
    wchar_t invalid[] = L"invalid";
    wchar_t zero[] = L"0";
    wchar_t* missing[] = {service};
    wchar_t* truncated[] = {service, parent};
    wchar_t* malformed[] = {service, parent, invalid};
    wchar_t* zeroPid[] = {service, parent, zero};
    {
        Service::HubParentGuard guard;
        if (guard.Open(1, missing) || guard.Open(2, truncated) ||
            guard.Open(3, malformed) || guard.Open(3, zeroPid)) return 1;
    }

    // Open the parent while suspended so the test is independent of scheduling.
    // No service is started or restored by this test.
    wchar_t self[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, self, MAX_PATH)) return 2;
    std::wstring command = L"\"" + std::wstring(self) + L"\" --child";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(self, command.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup, &process)) return 3;

    std::wstring pid = std::to_wstring(process.dwProcessId);
    wchar_t* args[] = {service, parent, pid.data()};
    Service::HubParentGuard guard;
    const bool opened = guard.Open(3, args);
    const bool alive = opened && WaitForSingleObject(guard.Handle(), 0) == WAIT_TIMEOUT;
    ResumeThread(process.hThread);
    const bool exited = WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0;
    const bool observed = opened && WaitForSingleObject(guard.Handle(), 0) == WAIT_OBJECT_0;
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return alive && exited && observed ? 0 : 4;
}
