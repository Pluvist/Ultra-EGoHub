#pragma once
#include <array>
#include <cstdint>

namespace PenHoverPolicy {
// Apply after eraser/barrel transforms. Use actual tip contact, since eraser
// reports may clear Tip Switch while retaining Eraser/Invert bits.
inline void Apply(std::array<uint8_t,17>& bytes,bool enabled,bool tipDown){
    if(enabled||tipDown)return;
    const uint8_t reportId=bytes[0];
    bytes.fill(0);
    bytes[0]=reportId; // tip up + out of range, without coordinates or pressure
}
}
