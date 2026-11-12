#pragma once
// Internal filesystem boundary shared with isolated, device-free file tests.
#include "DeviceArtworkIdentityCache.h"
#include <filesystem>

namespace HuaweiHeadphones::Images::KnownDevices::Detail {
std::optional<Identity> LoadFromDirectory(
    const std::filesystem::path& directory, uint64_t address) noexcept;
bool RememberInDirectory(const std::filesystem::path& directory,
                         uint64_t address, const Identity& identity) noexcept;
}
