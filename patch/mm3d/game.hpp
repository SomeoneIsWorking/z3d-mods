// MM3D guest layout of Project Restoration v110 code (the v1.0 layout), baked over USA Rev 1.
#pragma once

#include <stdint.h>

namespace z3dmods::mm3d {

// CommonData (Project Restoration source/game/common_data.h)
inline constexpr uintptr_t kCommonData = 0x007751D8;
// 1 on the title screen (whose demo is a play-state cutscene), 0 in play; PR names it "setup".
inline constexpr uint32_t kGameMode = 0x1361C;
inline constexpr int32_t kGameModeNormal = 0;

// GlobalContext (Project Restoration source/game/context.h)
inline constexpr uint32_t kPlayInputPressed = 0x28 + 0x0C; // pad_state.input.new_buttons
inline constexpr uint32_t kButtonStart = 1U << 3;
inline constexpr uint32_t kPlayScript = 0x2688;
inline constexpr uint32_t kPlayMessageContext = 0x8020;
inline constexpr uint32_t kPlayTransitionTrigger = 0xC529;
// 20 asks for an outgoing scene transition (the interpreter's end check).
inline constexpr int8_t kTransitionStart = 20;

// CutsceneContext (play + 0x2428)
inline constexpr uint32_t kCsFrames = 0x20;
inline constexpr uint32_t kCsState = 0x264;
inline constexpr uint8_t kCsStateRunning = 2;

// Cutscene script header
inline constexpr uint32_t kScriptEndFrame = 0x34;

// Message_GetState() while a choice waits for the player
inline constexpr int32_t kMessageStateChoice = 4;

// Functions
inline constexpr uintptr_t kCutsceneInterpreter = 0x001846B4;
inline constexpr uintptr_t kMessageGetState = 0x001C5018;
inline constexpr uintptr_t kMessageCloseTextbox = 0x001EF228;

} // namespace z3dmods::mm3d
