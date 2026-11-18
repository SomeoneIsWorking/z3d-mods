// Target_Draw's arrow turn (decomp/oot3d docs/frame.md "The target arrow"), hooked in hooks.s.
#include "../common/target_arrow.hpp"
#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::oot3d {

float arrowTurn(uint32_t angle) {
  return targetArrowTurn(angle, kArrowTurnStep);
}

} // namespace z3dmods::oot3d
