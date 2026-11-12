#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace VhfStylusState {
inline void ApplyTool(std::array<uint8_t, 17>& bytes, bool eraser) noexcept {
    if (!(bytes[1] & 0x20u)) {
        const auto id = bytes[0];
        bytes.fill(0);
        bytes[0] = id;
        return;
    }
    const bool down = (bytes[1] & 1u) != 0;
    bytes[1] &= 0xF3u;
    if (eraser) {
        bytes[1] = static_cast<uint8_t>((bytes[1] & 0xFEu) | 0x04u | (down ? 0x08u : 0u));
    }
}
inline bool Active(const std::array<uint8_t, 17>& bytes) noexcept {
    return (bytes[1] & 0x20u) != 0;
}
inline void KeepReleasePosition(std::array<uint8_t, 17>& bytes,
                                const std::array<uint8_t, 17>& last) noexcept {
    if (!Active(bytes)) {
        for (std::size_t i = 3; i < 7; ++i) bytes[i] = last[i];
    }
}
}
