// Start ends a cutscene at once: the remaining script frames run inside the press frame.
#pragma once

#include <stdint.h>

namespace z3dmods {

// Game is the title's layout: interpret, running, frame, setFrame, endFrame, startPressed,
// transitionStarted, messageIsChoice and closeMessage, all static.
template <typename Game> class CutsceneSkip {
public:
  void step(void *play, void *cutscene, const void *script) {
    Game::interpret(play, cutscene, script);
    if (!Game::running(cutscene)) {
      latched_ = false;
      closedFrame_ = 0;
      return;
    }
    if (!latched_ && Game::startPressed(play, cutscene)) {
      latched_ = true;
    }
    if (latched_) {
      runToEnd(play, cutscene, script);
    }
  }

private:
  // The script holds at most one frame per u16 value.
  static constexpr uint32_t kMaxSteps = 0x10000;

  // Steps frames as the game's own update does. A text command holds its end frame until the
  // message has closed; the skip closes it once and resumes when the game's message update has
  // finished closing. A choice is left open for the player.
  void runToEnd(void *play, void *cutscene, const void *script) {
    for (uint32_t steps = 0; steps < kMaxSteps; ++steps) {
      if (!Game::running(cutscene) || Game::transitionStarted(play)) {
        return;
      }
      const auto frame = static_cast<uint16_t>(Game::frame(cutscene) + 1);
      if (static_cast<int32_t>(frame) > Game::endFrame(cutscene, script) + 1) {
        return;
      }
      Game::setFrame(cutscene, frame);
      Game::interpret(play, cutscene, script);
      if (Game::frame(cutscene) == frame) {
        continue;
      }
      if (closedFrame_ != frame && !Game::messageIsChoice(play)) {
        Game::closeMessage(play);
        closedFrame_ = frame;
      }
      return;
    }
  }

  bool latched_ = false;
  uint16_t closedFrame_ = 0;
};

} // namespace z3dmods
