// The 3DS pad the core reads: an input engine ("z3d") whose state the player's keyboard and
// gamepads or the tools' commands set.
#pragma once

#include "common/settings.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

union SDL_Event;
struct SDL_Gamepad;

namespace z3d::app {

struct PadState {
  // Indexed by Settings::NativeButton.
  std::array<bool, Settings::NativeButton::NumButtons> buttons{};
  // Indexed by Settings::NativeAnalog: x right, y up, each -1..1.
  std::array<std::array<float, 2>, Settings::NativeAnalog::NumAnalogs> sticks{};
  // The C-stick as the game gets it: each axis turned around or not.
  std::array<bool, 2> cStickInverted{};
  // A gamepad's X and Y trade places (a game whose HUD draws them by position).
  bool swapXY = false;
};

class PadInput {
public:
  // Registers the engine; the core's devices made from profile() read this pad until destruction.
  PadInput();
  ~PadInput();
  PadInput(const PadInput &) = delete;
  PadInput &operator=(const PadInput &) = delete;
  PadInput(PadInput &&) = delete;
  PadInput &operator=(PadInput &&) = delete;

  // Every button and stick on this engine, touch on the window.
  [[nodiscard]] static Settings::InputProfile profile();

  // Opens and closes gamepads as they come and go.
  void handle(const SDL_Event &event);
  // The player's keyboard and gamepads, as they are held now.
  void readHost();
  // NativeButton bits.
  void setButtons(uint32_t mask);
  void setStick(Settings::NativeAnalog::Values stick, float x, float y);
  // A game option (game_options.hpp); the picker and menus read the stick as held.
  void invertCStick(bool x, bool y) {
    state_->cStickInverted = {x, y};
  }
  void swapXY(bool swap) {
    state_->swapXY = swap;
  }

  [[nodiscard]] const PadState &state() const {
    return *state_;
  }

private:
  std::shared_ptr<PadState> state_;
  std::vector<SDL_Gamepad *> pads_;
};

} // namespace z3d::app
