#pragma once
#include <windows.h>

namespace InputSchedulingPolicy {
// Keep rendering, system input and background device work in the normal dynamic
// priority range. Only the short, blocking input worker gets a one-step boost.
inline bool ApplyProcess() noexcept {
    return SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS) != FALSE;
}
inline bool ApplyInputThread() noexcept {
    return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL) != FALSE;
}
}
