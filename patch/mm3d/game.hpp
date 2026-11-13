// MM3D guest layout of Project Restoration v110 code (the 1.0 layout), baked over USA Rev 1:
// addresses and offsets come from decomp/mm3d.
#pragma once

#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::mm3d {

// CommonData.setup: 1 on the title screen (whose demo is a play-state cutscene), 0 in play.
inline constexpr int32_t kGameModeNormal = 0;

inline constexpr uint32_t kButtonStart = 1U << 3;
// transitionTrigger: 20 asks for an outgoing scene transition (the interpreter's end check).
inline constexpr int8_t kTransitionStart = 20;

inline constexpr uint8_t kCsStateRunning = 2;

// Message_GetState() while a choice waits for the player
inline constexpr int32_t kMessageStateChoice = 4;

} // namespace z3dmods::mm3d
