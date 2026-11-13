// OoT3D USA guest layout (code.bin md5 20c7ec62...): addresses and offsets come from decomp/oot3d.
#pragma once

#include "symbols.hpp"

#include <stdint.h>

namespace z3dmods::oot3d {

inline constexpr int32_t kGameModeNormal = 0;
inline constexpr int32_t kFileNumDebug = 0xFEDC;

// sceneLoadFlag: 20 asks for an outgoing scene transition; -20 is the incoming fade, 0 none.
inline constexpr int8_t kTransitionStart = 20;
inline constexpr uint32_t kButtonStart = 1U << 3;

inline constexpr uint8_t kCsStateRunning = 2;

// R_UPDATE_RATE is SREG(30): the frame-rate divisor.
inline constexpr uint32_t kUpdateRate = kGameInfoData + sizeof(int16_t) * (kRegsPerGroup + 30);

// TEXT_STATE_CHOICE
inline constexpr int32_t kMessageStateChoice = 4;

} // namespace z3dmods::oot3d
