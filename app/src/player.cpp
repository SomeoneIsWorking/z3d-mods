#include "player.hpp"

#include "emulator.hpp"
#include "game_window.hpp"

namespace z3d::app {

void Player::run() {
  (void)window_.takeMenuRequest();
  // The core paces itself (frame limit) and reads the pad through the window's event pump.
  while (!window_.closeRequested() && !window_.takeMenuRequest()) {
    emulator_.runFrames(1, false);
  }
}

} // namespace z3d::app
