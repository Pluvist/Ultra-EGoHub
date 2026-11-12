#pragma once
#include <cstdint>

namespace Solvers::Tsa::ContactEvents {
// TSACore raw lifecycle bits, independent of the optional Windows-pad feature.
// Initial down debounce is not a confirmed contact. Once reported, Move and
// pending lift/hold stay down until the final lift flag or removal from output.
inline constexpr uint32_t kContact = 0x02u | 0x04u | 0x08u | 0x10u;
inline constexpr uint32_t kLift = 0x20u | 0x40u;
inline bool Down(uint32_t event) noexcept { return (event & kContact) != 0; }
inline bool Lift(uint32_t event) noexcept { return (event & kLift) != 0; }
}
