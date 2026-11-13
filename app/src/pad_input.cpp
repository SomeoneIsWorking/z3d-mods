#include "pad_input.hpp"

#include "common/param_package.h"
#include "core/frontend/input.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace z3d::app {
namespace {

constexpr const char *kEngine = "z3d";
using Button = Settings::NativeButton::Values;
using Stick = Settings::NativeAnalog::Values;

struct KeyBinding {
  SDL_Scancode key;
  Button button;
};

// The player's Azahar keyboard profile (qt-config.ini [Controls] profiles\1).
constexpr std::array kKeys{
    KeyBinding{SDL_SCANCODE_A, Settings::NativeButton::A},
    KeyBinding{SDL_SCANCODE_S, Settings::NativeButton::B},
    KeyBinding{SDL_SCANCODE_Z, Settings::NativeButton::X},
    KeyBinding{SDL_SCANCODE_X, Settings::NativeButton::Y},
    KeyBinding{SDL_SCANCODE_M, Settings::NativeButton::Start},
    KeyBinding{SDL_SCANCODE_N, Settings::NativeButton::Select},
    KeyBinding{SDL_SCANCODE_Q, Settings::NativeButton::L},
    KeyBinding{SDL_SCANCODE_W, Settings::NativeButton::R},
    KeyBinding{SDL_SCANCODE_1, Settings::NativeButton::ZL},
    KeyBinding{SDL_SCANCODE_2, Settings::NativeButton::ZR},
    KeyBinding{SDL_SCANCODE_3, Settings::NativeButton::Debug},
    KeyBinding{SDL_SCANCODE_T, Settings::NativeButton::Up},
    KeyBinding{SDL_SCANCODE_G, Settings::NativeButton::Down},
    KeyBinding{SDL_SCANCODE_F, Settings::NativeButton::Left},
    KeyBinding{SDL_SCANCODE_H, Settings::NativeButton::Right},
};

struct PadBinding {
  SDL_GamepadButton pad;
  Button button;
};

// By name on an Xbox pad, which is what the sets' HUDs draw: A is A (south), B is B (east), the
// triggers are L and R, the bumpers ZL and ZR.
constexpr std::array kPadButtons{
    PadBinding{SDL_GAMEPAD_BUTTON_SOUTH, Settings::NativeButton::A},
    PadBinding{SDL_GAMEPAD_BUTTON_EAST, Settings::NativeButton::B},
    PadBinding{SDL_GAMEPAD_BUTTON_WEST, Settings::NativeButton::X},
    PadBinding{SDL_GAMEPAD_BUTTON_NORTH, Settings::NativeButton::Y},
    PadBinding{SDL_GAMEPAD_BUTTON_START, Settings::NativeButton::Start},
    PadBinding{SDL_GAMEPAD_BUTTON_BACK, Settings::NativeButton::Select},
    PadBinding{SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, Settings::NativeButton::ZL},
    PadBinding{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, Settings::NativeButton::ZR},
    // No button of the console: our patches read it as the chord button.
    PadBinding{SDL_GAMEPAD_BUTTON_LEFT_STICK, Settings::NativeButton::Debug},
    PadBinding{SDL_GAMEPAD_BUTTON_DPAD_UP, Settings::NativeButton::Up},
    PadBinding{SDL_GAMEPAD_BUTTON_DPAD_DOWN, Settings::NativeButton::Down},
    PadBinding{SDL_GAMEPAD_BUTTON_DPAD_LEFT, Settings::NativeButton::Left},
    PadBinding{SDL_GAMEPAD_BUTTON_DPAD_RIGHT, Settings::NativeButton::Right},
};

constexpr Settings::NativeButton::Values swappedXY(Settings::NativeButton::Values button) {
  if (button == Settings::NativeButton::X) {
    return Settings::NativeButton::Y;
  }
  return button == Settings::NativeButton::Y ? Settings::NativeButton::X : button;
}

constexpr int16_t kTriggerPressed = 16384;
constexpr float kAxisFull = 32767.0F;

float axis(SDL_Gamepad *pad, SDL_GamepadAxis which) {
  return std::clamp(static_cast<float>(SDL_GetGamepadAxis(pad, which)) / kAxisFull, -1.0F, 1.0F);
}

// The larger of two deflections, so keyboard and pads add up without overflowing.
float stronger(float a, float b) {
  return std::abs(a) >= std::abs(b) ? a : b;
}

class ButtonDevice final : public Input::ButtonDevice {
public:
  ButtonDevice(std::shared_ptr<const PadState> state, size_t index)
      : state_(std::move(state)), index_(index) {
  }
  [[nodiscard]] bool GetStatus() const override {
    return state_->buttons[index_];
  }

private:
  std::shared_ptr<const PadState> state_;
  size_t index_;
};

class AnalogDevice final : public Input::AnalogDevice {
public:
  AnalogDevice(std::shared_ptr<const PadState> state, size_t index)
      : state_(std::move(state)), index_(index) {
  }
  [[nodiscard]] std::tuple<float, float> GetStatus() const override {
    const auto &[x, y] = state_->sticks[index_];
    if (index_ != Settings::NativeAnalog::CStick) {
      return {x, y};
    }
    return {state_->cStickInverted[0] ? -x : x, state_->cStickInverted[1] ? -y : y};
  }

private:
  std::shared_ptr<const PadState> state_;
  size_t index_;
};

template <typename Device, typename Base> class Factory final : public Input::Factory<Base> {
public:
  Factory(std::shared_ptr<const PadState> state, const char *key, size_t count)
      : state_(std::move(state)), key_(key), count_(count) {
  }
  std::unique_ptr<Base> Create(const Common::ParamPackage &params) override {
    const int index = params.Get(key_, -1);
    if (index < 0 || static_cast<size_t>(index) >= count_) {
      return std::make_unique<Base>();
    }
    return std::make_unique<Device>(state_, static_cast<size_t>(index));
  }

private:
  std::shared_ptr<const PadState> state_;
  const char *key_;
  size_t count_;
};

} // namespace

PadInput::PadInput() : state_(std::make_shared<PadState>()) {
  Input::RegisterFactory<Input::ButtonDevice>(
      kEngine, std::make_shared<Factory<ButtonDevice, Input::ButtonDevice>>(
                   state_, "button", Settings::NativeButton::NumButtons));
  Input::RegisterFactory<Input::AnalogDevice>(
      kEngine, std::make_shared<Factory<AnalogDevice, Input::AnalogDevice>>(
                   state_, "stick", Settings::NativeAnalog::NumAnalogs));
}

PadInput::~PadInput() {
  Input::UnregisterFactory<Input::ButtonDevice>(kEngine);
  Input::UnregisterFactory<Input::AnalogDevice>(kEngine);
  for (SDL_Gamepad *pad : pads_) {
    SDL_CloseGamepad(pad);
  }
}

Settings::InputProfile PadInput::profile() {
  Settings::InputProfile profile{};
  profile.name = kEngine;
  for (size_t i = 0; i < profile.buttons.size(); ++i) {
    profile.buttons[i] = std::string("engine:") + kEngine + ",button:" + std::to_string(i);
  }
  for (size_t i = 0; i < profile.analogs.size(); ++i) {
    profile.analogs[i] = std::string("engine:") + kEngine + ",stick:" + std::to_string(i);
  }
  profile.touch_device = "engine:emu_window";
  return profile;
}

void PadInput::handle(const SDL_Event &event) {
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

void PadInput::readHost() {
  PadState state;
  state.cStickInverted = state_->cStickInverted;
  state.swapXY = state_->swapXY;
  const bool *keys = SDL_GetKeyboardState(nullptr);
  for (const KeyBinding &binding : kKeys) {
    state.buttons[binding.button] = keys[binding.key];
  }
  auto &circle = state.sticks[Settings::NativeAnalog::CirclePad];
  circle[0] = (keys[SDL_SCANCODE_RIGHT] ? 1.0F : 0.0F) - (keys[SDL_SCANCODE_LEFT] ? 1.0F : 0.0F);
  circle[1] = (keys[SDL_SCANCODE_UP] ? 1.0F : 0.0F) - (keys[SDL_SCANCODE_DOWN] ? 1.0F : 0.0F);
  auto &cStick = state.sticks[Settings::NativeAnalog::CStick];
  for (SDL_Gamepad *pad : pads_) {
    for (const PadBinding &binding : kPadButtons) {
      if (SDL_GetGamepadButton(pad, binding.pad)) {
        state.buttons[state.swapXY ? swappedXY(binding.button) : binding.button] = true;
      }
    }
    if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kTriggerPressed) {
      state.buttons[Settings::NativeButton::L] = true;
    }
    if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > kTriggerPressed) {
      state.buttons[Settings::NativeButton::R] = true;
    }
    // SDL's y grows downward; the 3DS's grows upward.
    circle[0] = stronger(circle[0], axis(pad, SDL_GAMEPAD_AXIS_LEFTX));
    circle[1] = stronger(circle[1], -axis(pad, SDL_GAMEPAD_AXIS_LEFTY));
    cStick[0] = stronger(cStick[0], axis(pad, SDL_GAMEPAD_AXIS_RIGHTX));
    cStick[1] = stronger(cStick[1], -axis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
  }
  *state_ = state;
}

void PadInput::setButtons(uint32_t mask) {
  for (size_t i = 0; i < state_->buttons.size(); ++i) {
    state_->buttons[i] = ((mask >> i) & 1U) != 0;
  }
}

void PadInput::setStick(Stick stick, float x, float y) {
  state_->sticks[stick] = {std::clamp(x, -1.0F, 1.0F), std::clamp(y, -1.0F, 1.0F)};
}

} // namespace z3d::app
