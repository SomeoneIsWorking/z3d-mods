// The target arrow's turn drawn at an in-between's place between two ticks
// (docs/plans/replay-interpolation.md): Target_Draw turns it by a step a tick.
#pragma once

#include "slot_link.hpp"

#include <stdint.h>

namespace z3dmods {

// The tick's angle plus the in-between's fraction of a step; slot_link's draw_fraction is 0 in
// every real draw.
inline float targetArrowTurn(uint32_t angle, uint32_t step) {
  return static_cast<float>(angle) + slotLink().draw_fraction * static_cast<float>(step);
}

} // namespace z3dmods
