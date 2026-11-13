#include "player.hpp"

#include "emulator.hpp"
#include "game_window.hpp"

namespace z3d::app {

void Player::run() {
  // The core paces itself (frame limit) and reads the pad through the window's event pump.
  while (!window_.closeRequested()) {
    emulator_.runFrames(1, false);
  }
}

} // namespace z3d::app
