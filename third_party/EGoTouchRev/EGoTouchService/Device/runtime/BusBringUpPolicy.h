#pragma once
#include <optional>

namespace DevicePolicy {
// 0x13 is also the writable burst-mode register. A stopped OEM provider may
// leave it at zero while ACPI confirms the panel is powered. Init restores the
// interface; waiting for a nonzero value before Init would wait forever.
constexpr bool ShouldWaitForBus(bool ioOk, bool busReady,
                                std::optional<bool> panelPowered) noexcept {
    return ioOk && !busReady && panelPowered != true;
}
}
