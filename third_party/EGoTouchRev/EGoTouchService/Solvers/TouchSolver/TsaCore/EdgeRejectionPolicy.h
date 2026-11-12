#pragma once
#include <cstdint>

namespace Solvers::Tsa::EdgeRejectionPolicy {
struct Band { uint16_t dim1, dim2; };
// Q8.8 sensor distances: half the original small-finger grip band in all
// orientations. Large-object, noise, palm and stylus classification stay intact.
inline constexpr Band SmallFingerBand(bool rotated, bool sideTouch) noexcept {
    return rotated ? Band{0x100, 0x100} : Band{uint16_t(sideTouch ? 0x180 : 0x100), 0x80};
}
inline constexpr bool OutsideSmallFingerBand(uint16_t majorMm, uint16_t dist1,
                                             uint16_t dist2, bool rotated, bool sideTouch) noexcept {
    const auto band = SmallFingerBand(rotated, sideTouch);
    return majorMm < 20 && dist1 > band.dim1 && dist2 > band.dim2;
}
}
