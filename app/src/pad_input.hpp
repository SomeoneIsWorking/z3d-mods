// The 3DS pad the core reads: an input engine ("z3d") whose state the player's keyboard and
// gamepads or the tools' commands set.
#pragma once

#include "common/settings.h"

#include <SDL3/SDL_gamepad.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

union SDL_Event;

namespace z3d::app {

struct PadState {
  // Indexed by Settings::NativeButton.
  std::array<bool, Settings::NativeButton::NumButtons> buttons{};
  // Indexed by Settings::NativeAnalog: x right, y up, each -1..1.
  std::array<std::array<float, 2>, Settings::NativeAnalog::NumAnalogs> sticks{};
  // The C-stick as the game gets it: each axis turned around or not.
  std::array<bool, 2> cStickInverted{};
};

// An item button of a game's second page (plans/pad-features.md), by the name its HUD gives it.
enum class ItemButton : uint8_t { X, Y, I, II };

// What one face button does while the chord is held.
struct ChordSlot {
  SDL_GamepadButton face;
  ItemButton item;
  // The 3DS buttons that press that item button in this game's mod set: NativeButton bits.
  uint32_t buttons;
};

// A game's chord: while `chord` is held, A, B, X and Y press the second page's four item
// buttons and nothing else; released, they are the game's own buttons.
struct ChordLayout {
  std::string_view titleId;
  SDL_GamepadButton chord;
  std::array<ChordSlot, 4> slots;
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
  // The chord layout of the game with this title ID from here on; none for a game without one.
  void bindGame(std::string_view titleId);
  // The bound game's chord, for whoever draws it; null without one. It is held while
  // state().buttons[NativeButton::Debug] is set.
  [[nodiscard]] const ChordLayout *chord() const {
    return chord_;
  }
  // A game option (game_options.hpp); the picker and menus read the stick as held.
  void invertCStick(bool x, bool y) {
    state_->cStickInverted = {x, y};
  }

  [[nodiscard]] const PadState &state() const {
    return *state_;
  }

private:
  std::shared_ptr<PadState> state_;
  std::vector<SDL_Gamepad *> pads_;
  const ChordLayout *chord_ = nullptr;
};

} // namespace z3d::app
