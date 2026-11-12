#pragma once

#include "PenStatusChannel.h"

namespace PenStatus {

// Publish time tracks channel freshness, not a change to the rendered state.
// Compare fields rather than object bytes so padding cannot invalidate the result.
[[nodiscard]] inline bool SameUiState(const State& left, const State& right) noexcept {
    State leftUi = left;
    State rightUi = right;
    leftUi.updatedAtUnixMs = 0;
    rightUi.updatedAtUnixMs = 0;
    return leftUi == rightUi;
}

} // namespace PenStatus
