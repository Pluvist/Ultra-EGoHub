#include "runtime/BusBringUpPolicy.h"
#include <optional>

int main() {
    using DevicePolicy::ShouldWaitForBus;
    // Regression: OEM has cleared burst mode, but the panel is powered.
    if (ShouldWaitForBus(true, false, true)) return 1;
    // Keep polling when power is absent or cannot be established.
    if (!ShouldWaitForBus(true, false, false)) return 2;
    if (!ShouldWaitForBus(true, false, std::nullopt)) return 3;
    if (ShouldWaitForBus(true, true, true)) return 4;
    // I/O errors must enter existing failure/backoff handling, not poll forever.
    if (ShouldWaitForBus(false, false, true)) return 5;
    return 0;
}
