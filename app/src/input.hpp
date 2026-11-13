// The player's keyboard, gamepads and mouse as one 3DS pad for the core.
#pragma once

#include "vulkan/presenter.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

union SDL_Event;
struct SDL_Gamepad;

namespace z3d::app {

// What the core reads through retro_input_state for port 0.
struct PadState {
  // RETRO_DEVICE_ID_JOYPAD bits.
  uint16_t buttons = 0;
  std::array<int16_t, 2> left{};
  std::array<int16_t, 2> right{};
  // RETRO_DEVICE_POINTER over the whole core frame, -0x7FFF..0x7FFF.
  std::array<int16_t, 2> pointer{};
  bool touching = false;
};

class Input {
public:
  Input() = default;
  ~Input();
  Input(const Input &) = delete;
  Input &operator=(const Input &) = delete;
  Input(Input &&) = delete;
  Input &operator=(Input &&) = delete;

  // Opens and closes gamepads as they come and go.
  void handle(const SDL_Event &event);
  // `placed` is where the core's `frame` lies in the window, in pixels.
  [[nodiscard]] PadState read(const std::optional<Placement> &placed, float pixelDensity) const;

private:
  std::vector<SDL_Gamepad *> pads_;
};

} // namespace z3d::app
