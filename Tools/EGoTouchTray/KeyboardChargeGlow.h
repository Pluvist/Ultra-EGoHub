#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <memory>

// A one-second, noninteractive white glow along the primary screen's edges.
// Start/Play run on the owner's UI thread. No window, GPU surface or timer is
// retained while idle. Reentrant Play calls do not restart/extend the effect.
class KeyboardChargeGlow {
public:
    KeyboardChargeGlow();
    ~KeyboardChargeGlow();
    KeyboardChargeGlow(KeyboardChargeGlow const&) = delete;
    KeyboardChargeGlow& operator=(KeyboardChargeGlow const&) = delete;

    bool Start(HWND owner);
    void Play();
    // UI-thread shutdown is immediate. An off-thread Stop safely asks that
    // thread to close the window; its private state lives until WM_NCDESTROY.
    void Stop();

private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};
