// MM3D's layout for the Start cutscene skip.
#include "cutscene_script.hpp"
#include "game.hpp"
#include "options.hpp"
#include "../common/cutscene_skip.hpp"
#include "../common/guest.hpp"

#include <stdint.h>

namespace z3dmods::mm3d {
namespace {

using InterpreterFn = void (*)(void *play, void *cutscene, const void *script);
using CloseTextboxFn = void (*)();
using MessageStateFn = int32_t (*)(void *messageContext);

struct Game {
  static bool enabled() {
    return options().cutsceneSkip != 0;
  }

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

  // The interpreter reads the header through a byte copy; the script may be unaligned.
  static int32_t endFrame(const void * /*cutscene*/, const void *script) {
    return guest::read<uint8_t>(script, kScriptEndFrame) |
           (guest::read<uint8_t>(script, kScriptEndFrame + 1) << 8);
  }

  static bool transitionStarted(const void *play) {
    return guest::read<int8_t>(play, kPlayTransitionTrigger) == kTransitionStart;
  }

  static bool startPressed(const void *play, const void * /*cutscene*/) {
    const auto *common = reinterpret_cast<const void *>(kCommonData);
    if (guest::read<int32_t>(common, kGameMode) != kGameModeNormal) {
      return false;
    }
    if ((guest::read<uint32_t>(play, kPlayInputPressed) & kButtonStart) == 0) {
      return false;
    }
    return !transitionStarted(play);
  }

  static bool textIsChoice(const void *script, uint16_t frame) {
    return CutsceneScript(script).textIsChoiceAt(frame);
  }

  static bool messageIsChoice(void *play) {
    auto *messages = static_cast<uint8_t *>(play) + kPlayMessageContext;
    return guest::function<MessageStateFn>(kMessageGetState)(messages) == kMessageStateChoice;
  }

  static void closeMessage(void * /*play*/) {
    guest::function<CloseTextboxFn>(kMessageCloseTextbox)();
  }
};

constinit CutsceneSkip<Game> skip;

} // namespace

// Called instead of the interpreter from its three call sites (link.ld).
void stepCutscene(void *play, void *cutscene, const void *script) {
  skip.step(play, cutscene, script);
}

} // namespace z3dmods::mm3d
