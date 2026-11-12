// Start ends a cutscene at once: the remaining script frames run inside the press frame.
#include "game.hpp"
#include "../common/guest.hpp"

#include <stdint.h>

namespace z3dmods::oot3d {
namespace {

using InterpreterFn = void (*)(void *play, void *cutscene, const void *script);

// The script holds at most one frame per u16 value.
inline constexpr uint32_t kMaxSteps = 0x10000;

class CutsceneSkip {
public:
  void step(void *play, void *cutscene, const void *script) {
    interpret(play, cutscene, script);
    if (guest::read<uint8_t>(cutscene, kCsState) != kCsStateRunning) {
      latched_ = false;
      return;
    }
    if (!latched_ && startPressed(play, cutscene)) {
      latched_ = true;
    }
    if (latched_) {
      runToEnd(play, cutscene, script);
    }
  }

private:
  static void interpret(void *play, void *cutscene, const void *script) {
    guest::function<InterpreterFn>(kCutsceneInterpreter)(play, cutscene, script);
  }

  // Same minimum as the game's title-demo skip: one second of cutscene frames. That skip also
  // requires the in-cutscene flag (0x5043D4+0x18) clear, which blocks every gameplay cutscene.
  static uint32_t minimumFrames() {
    const auto timing = guest::read<uintptr_t>(kFrameRatePointer);
    const auto divisor = guest::read<int16_t>(reinterpret_cast<void *>(timing), kFrameRateDivisor);
    return static_cast<uint32_t>(60.0F / static_cast<float>(divisor) + 0.5F);
  }

  static bool startPressed(void *play, void *cutscene) {
    const auto *save = reinterpret_cast<void *>(kSaveContext);
    if (guest::read<int32_t>(save, kSaveGameMode) != kGameModeNormal) {
      return false;
    }
    if (guest::read<int32_t>(save, kSaveFileNum) == kFileNumDebug) {
      return false;
    }
    if ((guest::read<uint32_t>(play, kPlayInputPressed) & kButtonStart) == 0) {
      return false;
    }
    if (guest::read<uint16_t>(cutscene, kCsFrames) <= minimumFrames()) {
      return false;
    }
    return guest::read<int8_t>(play, kPlayTransitionTrigger) == 0;
  }

  // The caller's own catch-up loop (0x322028) steps frames the same way.
  static void runToEnd(void *play, void *cutscene, const void *script) {
    for (uint32_t steps = 0; steps < kMaxSteps; ++steps) {
      if (guest::read<uint8_t>(cutscene, kCsState) != kCsStateRunning ||
          guest::read<int8_t>(play, kPlayTransitionTrigger) != 0) {
        return;
      }
      const auto frame = static_cast<uint16_t>(guest::read<uint16_t>(cutscene, kCsFrames) + 1);
      if (static_cast<int32_t>(frame) > guest::read<int32_t>(cutscene, kCsEndFrame) + 1) {
        return;
      }
      guest::write<uint16_t>(cutscene, kCsFrames, frame);
      interpret(play, cutscene, script);
      if (guest::read<uint16_t>(cutscene, kCsFrames) != frame) {
        return; // a command holds the frame (a message waiting for the player)
      }
    }
  }

  bool latched_ = false;
};

constinit CutsceneSkip skip;

} // namespace

// Called instead of the interpreter from its two call sites (0x322040, 0x322070).
void stepCutscene(void *play, void *cutscene, const void *script) {
  skip.step(play, cutscene, script);
}

} // namespace z3dmods::oot3d
