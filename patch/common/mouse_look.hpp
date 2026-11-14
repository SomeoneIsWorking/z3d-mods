// The mouse's turn of the free camera that the app leaves in the slot link (slot_link.hpp).
#pragma once

#include "slot_link.hpp"

#include <stdint.h>

namespace z3dmods {

// Binary angles: right turns the view right, down turns it down.
struct LookTurn {
  int32_t right = 0;
  int32_t down = 0;
};

// Whether the app has left a turn to take.
inline bool lookPending() {
  volatile SlotLink &link = slotLink();
  return link.look[0] != 0 || link.look[1] != 0;
}

// The turn the app has added since the last call; the block keeps none of it.
inline LookTurn takeLook() {
  volatile SlotLink &link = slotLink();
  const LookTurn turn{link.look[0], link.look[1]};
  link.look[0] = 0;
  link.look[1] = 0;
  return turn;
}

} // namespace z3dmods
