#pragma once
#include <windows.h>
#include <cstdlib>
#include <cwchar>

namespace Service {
// SCM services are not child processes. Release the device
// if the Hub disappears. OEM input remains disabled.
class HubParentGuard {
    HANDLE m_parent = nullptr;
    bool m_orphaned = false;
public:
    ~HubParentGuard() { if (m_parent) CloseHandle(m_parent); }
    bool Open(DWORD argc, LPWSTR* argv) {
        for (DWORD i = 1; i < argc; ++i) {
            if (wcscmp(argv[i], L"--parent") != 0) continue;
            if (++i >= argc) return false;
            wchar_t* end = nullptr;
            const unsigned long pid = wcstoul(argv[i], &end, 10);
            if (!pid || !end || *end) return false;
            m_parent = OpenProcess(SYNCHRONIZE, FALSE, pid);
            return m_parent && WaitForSingleObject(m_parent, 0) == WAIT_TIMEOUT;
        }
        return false; // This private driver must be started by the Hub.
    }
    HANDLE Handle() const { return m_parent; }
    void MarkOrphaned() { m_orphaned = true; }
};
} // namespace Service
