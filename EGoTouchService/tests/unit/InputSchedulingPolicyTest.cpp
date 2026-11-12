#include "runtime/InputSchedulingPolicy.h"
#include <thread>
#include <iostream>

int main() {
    const DWORD original = GetPriorityClass(GetCurrentProcess());
    if (!InputSchedulingPolicy::ApplyProcess() ||
        GetPriorityClass(GetCurrentProcess()) != NORMAL_PRIORITY_CLASS) {
        std::cerr << "Input process must use normal scheduling\n";
        return 1;
    }
    bool ok = false;
    std::thread worker([&] {
        ok = InputSchedulingPolicy::ApplyInputThread() &&
             GetThreadPriority(GetCurrentThread()) == THREAD_PRIORITY_ABOVE_NORMAL &&
             GetPriorityClass(GetCurrentProcess()) == NORMAL_PRIORITY_CLASS;
    });
    worker.join();
    // The input worker's boost must not affect the service/control thread.
    ok = ok && GetThreadPriority(GetCurrentThread()) == THREAD_PRIORITY_NORMAL;
    if (original != 0) SetPriorityClass(GetCurrentProcess(), original);
    if (!ok) { std::cerr << "Input-only thread scheduling policy failed\n"; return 1; }
    std::cout << "Normal process and isolated input-thread boost verified\n";
    return 0;
}
