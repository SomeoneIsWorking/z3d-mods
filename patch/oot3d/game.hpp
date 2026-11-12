// OoT3D USA Rev 1 guest layout (code.bin sha256 ef210566...).
#pragma once

#include <stdint.h>

namespace z3dmods::oot3d {

// gSaveContext
inline constexpr uintptr_t kSaveContext = 0x00587958;
inline constexpr uint32_t kSaveFileNum = 0x14DC;
inline constexpr uint32_t kSaveGameMode = 0x14E4;
inline constexpr int32_t kGameModeNormal = 0;
inline constexpr int32_t kFileNumDebug = 0xFEDC;

// PlayState
inline constexpr uint32_t kPlayInputPressed = 0x18;
inline constexpr uint32_t kPlayTransitionTrigger = 0x5C2D;
inline constexpr uint32_t kButtonStart = 1U << 3;

// CutsceneContext (play + 0x2298)
inline constexpr uint32_t kCsState = 0x08;
inline constexpr uint8_t kCsStateRunning = 2;
inline constexpr uint32_t kCsEndFrame = 0x18;
inline constexpr uint32_t kCsFrames = 0x20;

// Frame-rate divisor the destination handler's title-demo skip guard reads (s16 at +0x110).
inline constexpr uintptr_t kFrameRatePointer = 0x0051B2F4;
inline constexpr uint32_t kFrameRateDivisor = 0x110;

// Functions
inline constexpr uintptr_t kCutsceneInterpreter = 0x002C5BA0;

} // namespace z3dmods::oot3d
