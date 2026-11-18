#include "player.hpp"

#include "emulator.hpp"
#include "game_options.hpp"
#include "game_window.hpp"
#include "mod_swap.hpp"
#include "frame_interpolation.hpp"

#include <lucent/log.h>

namespace z3d::app {

void Player::run() {
  (void)window_.takeMenuRequest();
  const bool interpolate = options_.frameInterpolation();
  const bool interpolating = interpolation_.setMode(interpolate ? FrameInterpolation::Mode::On
                                                                : FrameInterpolation::Mode::Off);
  lucent::info("z3d", "frame interpolation {}", interpolating && interpolate ? "on" : "off");
  (void)emulator_.takePacing();
  auto report = std::chrono::steady_clock::now() + kPacingReport;
  // The core paces itself (frame limit) and reads the pad through the window's event pump.
  while (!window_.closeRequested() && !window_.takeMenuRequest()) {
    emulator_.runFrames(1, Emulator::Capture::None);
    swapMods();
    if (std::chrono::steady_clock::now() >= report) {
      lucent::info("z3d", "pacing {}", emulator_.takePacing().text());
      report += kPacingReport;
    }
  }
  interpolation_.setMode(FrameInterpolation::Mode::Off);
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
