#include "player.hpp"

#include "emulator.hpp"
#include "game_options.hpp"
#include "game_window.hpp"
#include "mod_swap.hpp"

namespace z3d::app {

void Player::run() {
  (void)window_.takeMenuRequest();
  // The core paces itself (frame limit) and reads the pad through the window's event pump.
  while (!window_.closeRequested() && !window_.takeMenuRequest()) {
    emulator_.runFrames(1, false);
    swapMods();
  }
}

void Player::swapMods() {
  const std::optional<ModKey> key = window_.takeModKey();
  if (!key) {
    return;
  }
  if (*key == ModKey::ToggleTextures) {
    mods_.setTextures(!mods_.texturesOn());
    return;
  }
  // A failed swap leaves the game as it was, options included.
  if (mods_.setPatch(*key == ModKey::ReloadPatch || !mods_.patchOn())) {
    options_.apply(emulator_, pad_);
  }
}

} // namespace z3d::app
