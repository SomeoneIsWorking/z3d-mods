// The target arrow's turn drawn at an in-between's place between two ticks
// (docs/plans/replay-interpolation.md, decomp/mm3d docs/actors.md "The target arrow").
#include "slot_link.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::mm3d {

float arrowTurn(uint32_t angle) {
  return static_cast<float>(angle) + slotLink().draw_fraction * static_cast<float>(kArrowTurnStep);
}

} // namespace z3dmods::mm3d
