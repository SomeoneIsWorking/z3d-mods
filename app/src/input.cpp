#include "input.hpp"

#include <SDL3/SDL.h>

#include <libretro.h>

#include <algorithm>
#include <cmath>

namespace z3d::app {
namespace {

struct KeyBinding {
  SDL_Scancode key;
  unsigned button;
};

// The player's Azahar keyboard profile (qt-config.ini [Controls] profiles\1).
constexpr std::array kKeys{
    KeyBinding{SDL_SCANCODE_A, RETRO_DEVICE_ID_JOYPAD_A},
    KeyBinding{SDL_SCANCODE_S, RETRO_DEVICE_ID_JOYPAD_B},
    KeyBinding{SDL_SCANCODE_Z, RETRO_DEVICE_ID_JOYPAD_X},
    KeyBinding{SDL_SCANCODE_X, RETRO_DEVICE_ID_JOYPAD_Y},
    KeyBinding{SDL_SCANCODE_M, RETRO_DEVICE_ID_JOYPAD_START},
    KeyBinding{SDL_SCANCODE_N, RETRO_DEVICE_ID_JOYPAD_SELECT},
    KeyBinding{SDL_SCANCODE_Q, RETRO_DEVICE_ID_JOYPAD_L},
    KeyBinding{SDL_SCANCODE_W, RETRO_DEVICE_ID_JOYPAD_R},
    KeyBinding{SDL_SCANCODE_1, RETRO_DEVICE_ID_JOYPAD_L2},
    KeyBinding{SDL_SCANCODE_2, RETRO_DEVICE_ID_JOYPAD_R2},
    KeyBinding{SDL_SCANCODE_T, RETRO_DEVICE_ID_JOYPAD_UP},
    KeyBinding{SDL_SCANCODE_G, RETRO_DEVICE_ID_JOYPAD_DOWN},
    KeyBinding{SDL_SCANCODE_F, RETRO_DEVICE_ID_JOYPAD_LEFT},
    KeyBinding{SDL_SCANCODE_H, RETRO_DEVICE_ID_JOYPAD_RIGHT},
};

struct PadBinding {
  SDL_GamepadButton pad;
  unsigned button;
};

// By position, as the core maps them: east is the 3DS A, south the 3DS B.
constexpr std::array kPadButtons{
    PadBinding{SDL_GAMEPAD_BUTTON_EAST, RETRO_DEVICE_ID_JOYPAD_A},
    PadBinding{SDL_GAMEPAD_BUTTON_SOUTH, RETRO_DEVICE_ID_JOYPAD_B},
    PadBinding{SDL_GAMEPAD_BUTTON_NORTH, RETRO_DEVICE_ID_JOYPAD_X},
    PadBinding{SDL_GAMEPAD_BUTTON_WEST, RETRO_DEVICE_ID_JOYPAD_Y},
    PadBinding{SDL_GAMEPAD_BUTTON_START, RETRO_DEVICE_ID_JOYPAD_START},
    PadBinding{SDL_GAMEPAD_BUTTON_BACK, RETRO_DEVICE_ID_JOYPAD_SELECT},
    PadBinding{SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, RETRO_DEVICE_ID_JOYPAD_L},
    PadBinding{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, RETRO_DEVICE_ID_JOYPAD_R},
    PadBinding{SDL_GAMEPAD_BUTTON_DPAD_UP, RETRO_DEVICE_ID_JOYPAD_UP},
    PadBinding{SDL_GAMEPAD_BUTTON_DPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_DOWN},
    PadBinding{SDL_GAMEPAD_BUTTON_DPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_LEFT},
    PadBinding{SDL_GAMEPAD_BUTTON_DPAD_RIGHT, RETRO_DEVICE_ID_JOYPAD_RIGHT},
};

constexpr int16_t kTriggerPressed = 16384;
constexpr int16_t kFull = 0x7FFF;

uint16_t bit(unsigned button) {
  return static_cast<uint16_t>(1U << button);
}

// The larger of two deflections, so keyboard and pad add up without overflowing.
int16_t stronger(int16_t a, int16_t b) {
  return std::abs(static_cast<int>(a)) >= std::abs(static_cast<int>(b)) ? a : b;
}

int16_t toPointer(float offset, uint32_t length) {
  const float unit = std::clamp(offset / static_cast<float>(length), 0.0F, 1.0F) * 2.0F - 1.0F;
  return static_cast<int16_t>(std::lround(unit * kFull));
}

} // namespace

Input::~Input() {
  for (SDL_Gamepad *pad : pads_) {
    SDL_CloseGamepad(pad);
  }
}

void Input::handle(const SDL_Event &event) {
  if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
    if (SDL_Gamepad *pad = SDL_OpenGamepad(event.gdevice.which)) {
      pads_.push_back(pad);
    }
  } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
    SDL_Gamepad *gone = SDL_GetGamepadFromID(event.gdevice.which);
    std::erase(pads_, gone);
    SDL_CloseGamepad(gone);
  }
}

PadState Input::read(const std::optional<Placement> &placed, float pixelDensity) const {
  PadState state;
  const bool *keys = SDL_GetKeyboardState(nullptr);
  for (const KeyBinding &binding : kKeys) {
    if (keys[binding.key]) {
      state.buttons |= bit(binding.button);
    }
  }
  state.left[0] = static_cast<int16_t>((keys[SDL_SCANCODE_RIGHT] ? kFull : 0) -
                                       (keys[SDL_SCANCODE_LEFT] ? kFull : 0));
  state.left[1] = static_cast<int16_t>((keys[SDL_SCANCODE_DOWN] ? kFull : 0) -
                                       (keys[SDL_SCANCODE_UP] ? kFull : 0));
  for (SDL_Gamepad *pad : pads_) {
    for (const PadBinding &binding : kPadButtons) {
      if (SDL_GetGamepadButton(pad, binding.pad)) {
        state.buttons |= bit(binding.button);
      }
    }
    if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kTriggerPressed) {
      state.buttons |= bit(RETRO_DEVICE_ID_JOYPAD_L2);
    }
    if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > kTriggerPressed) {
      state.buttons |= bit(RETRO_DEVICE_ID_JOYPAD_R2);
    }
    state.left[0] = stronger(state.left[0], SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX));
    state.left[1] = stronger(state.left[1], SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY));
    state.right[0] = stronger(state.right[0], SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX));
    state.right[1] = stronger(state.right[1], SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
  }
  if (placed && placed->width != 0 && placed->height != 0) {
    float x = 0;
    float y = 0;
    const SDL_MouseButtonFlags mouse = SDL_GetMouseState(&x, &y);
    const float px = x * pixelDensity - static_cast<float>(placed->x);
    const float py = y * pixelDensity - static_cast<float>(placed->y);
    state.pointer = {toPointer(px, placed->width), toPointer(py, placed->height)};
    const bool inside = px >= 0 && py >= 0 && px < static_cast<float>(placed->width) &&
                        py < static_cast<float>(placed->height);
    state.touching = inside && (mouse & SDL_BUTTON_LMASK) != 0;
  }
  return state;
}

} // namespace z3d::app
