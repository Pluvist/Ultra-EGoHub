#pragma once
#include "HeadphoneImageIdentity.h"
#include <cstdint>
#include <optional>

namespace HuaweiHeadphones::Images::KnownDevices {

// Optional artwork hints only. Call off the UI thread. The caller must first
// verify that the address is still paired/connected, then recheck this identity
// against the current device model with ConfirmedIdentity before displaying it.
// Load never creates or modifies a file, and returns no battery/name/settings.
std::optional<Identity> Load(uint64_t address) noexcept;

// Call only after a successful identity reply passed ConfirmedIdentity. Keeps
// at most eight most recently confirmed devices; identical newest hints do not
// rewrite the index. File/lock failures silently leave the previous cache intact.
// No threads are created here; the caller schedules this optional disk work.
void Remember(uint64_t address, const Identity& identity) noexcept;

}
