// OoT3D's layout for the Start cutscene skip.
#include "cutscene_script.hpp"
#include "game.hpp"
#include "../common/cutscene_skip.hpp"
#include "../common/guest.hpp"

#include <stdint.h>

namespace z3dmods::oot3d {
namespace {

using InterpreterFn = void (*)(void *play, void *cutscene, const void *script);
using CloseTextboxFn = void (*)(void *play);
using MessageStateFn = int32_t (*)();

struct Game {
  static void interpret(void *play, void *cutscene, const void *script) {
    guest::function<InterpreterFn>(kCutsceneInterpreter)(play, cutscene, script);
  }

  static bool running(const void *cutscene) {
    return guest::read<uint8_t>(cutscene, kCsState) == kCsStateRunning;
  }

  static uint16_t frame(const void *cutscene) {
    return guest::read<uint16_t>(cutscene, kCsFrames);
  }

  static void setFrame(void *cutscene, uint16_t frame) {
    guest::write<uint16_t>(cutscene, kCsFrames, frame);
  }

  // The interpreter copies the script's end frame here on every call.
  static int32_t endFrame(const void *cutscene, const void * /*script*/) {
    return guest::read<int32_t>(cutscene, kCsEndFrame);
  }

  static bool transitionStarted(const void *play) {
    return guest::read<int8_t>(play, kPlayTransitionTrigger) == kTransitionStart;
  }

  static bool startPressed(const void *play, const void *cutscene) {
    const auto *save = reinterpret_cast<const void *>(kSaveContext);
    if (guest::read<int32_t>(save, kSaveGameMode) != kGameModeNormal) {
      return false;
    }
    if (guest::read<int32_t>(save, kSaveFileNum) == kFileNumDebug) {
      return false;
    }
    if ((guest::read<uint32_t>(play, kPlayInputPressed) & kButtonStart) == 0) {
      return false;
    }
    if (frame(cutscene) <= minimumFrames()) {
      return false;
    }
    return !transitionStarted(play);
  }

  static bool textIsChoice(const void *script, uint16_t frame) {
    return CutsceneScript(script).textIsChoiceAt(frame);
  }

  static bool messageIsChoice(const void * /*play*/) {
    return guest::function<MessageStateFn>(kMessageGetState)() == kMessageStateChoice;
  }

  static void closeMessage(void *play) {
    guest::function<CloseTextboxFn>(kMessageCloseTextbox)(play);
  }

  // Same minimum as the game's title-demo skip: one second of cutscene frames. That skip also
  // requires the in-cutscene flag (0x5043D4+0x18) clear, which blocks every gameplay cutscene.
  static uint32_t minimumFrames() {
    const auto timing = guest::read<uintptr_t>(kGameInfo);
    const auto divisor = guest::read<int16_t>(reinterpret_cast<void *>(timing), kUpdateRate);
    return static_cast<uint32_t>(60.0F / static_cast<float>(divisor) + 0.5F);
  }
};

constinit CutsceneSkip<Game> skip;

} // namespace

// Called instead of the interpreter from its two call sites (link.ld).
void stepCutscene(void *play, void *cutscene, const void *script) {
  skip.step(play, cutscene, script);
}

} // namespace z3dmods::oot3d
