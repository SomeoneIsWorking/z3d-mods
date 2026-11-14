#include "pad_input.hpp"

#include "common/param_package.h"
#include "core/frontend/input.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <ostream>
#include <string>

namespace z3d::app {
namespace {

constexpr const char *kEngine = "z3d";
using Button = Settings::NativeButton::Values;
using Stick = Settings::NativeAnalog::Values;

constexpr uint32_t bit(Button button) {
  return 1U << button;
}

constexpr int kNoSlot = -1;

// What a pad button does in play: a verb or a slot, and the slot or verb it is while the chord
// is held.
struct PlayButton {
  SDL_GamepadButton pad;
  std::optional<Verb> verb;
  int slot = kNoSlot;
  int chordSlot = kNoSlot;
  std::optional<Verb> chordVerb = std::nullopt;
};

// The play layout (docs/plans/pc-controls-hud.md). The triggers are axes: LT targets, RT shields.
constexpr std::array kPlayButtons{
    PlayButton{SDL_GAMEPAD_BUTTON_SOUTH, Verb::Action, kNoSlot, 7},
    PlayButton{SDL_GAMEPAD_BUTTON_EAST, std::nullopt, 1, 8},
    PlayButton{SDL_GAMEPAD_BUTTON_WEST, Verb::Sword, kNoSlot, 9},
    PlayButton{SDL_GAMEPAD_BUTTON_NORTH, std::nullopt, 0, 10},
    PlayButton{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, std::nullopt, 2, 2},
    PlayButton{SDL_GAMEPAD_BUTTON_DPAD_UP, std::nullopt, 3, 3},
    PlayButton{SDL_GAMEPAD_BUTTON_DPAD_RIGHT, std::nullopt, 4, 4},
    PlayButton{SDL_GAMEPAD_BUTTON_DPAD_DOWN, std::nullopt, 5, 5},
    PlayButton{SDL_GAMEPAD_BUTTON_DPAD_LEFT, std::nullopt, 6, 6},
    PlayButton{SDL_GAMEPAD_BUTTON_RIGHT_STICK, Verb::FirstPerson},
    // The games keep their items off the pause screen, so Menu is the item screen.
    PlayButton{SDL_GAMEPAD_BUTTON_START, Verb::ItemScreen, kNoSlot, kNoSlot, Verb::Pause},
    PlayButton{SDL_GAMEPAD_BUTTON_BACK, Verb::Map},
};

struct PlayKey {
  SDL_Scancode key;
  Verb verb;
};

// A verb's first key is the one its prompt shows.
constexpr std::array kPlayKeys{
    PlayKey{SDL_SCANCODE_E, Verb::Action},  PlayKey{SDL_SCANCODE_SPACE, Verb::Action},
    PlayKey{SDL_SCANCODE_Q, Verb::Target},  PlayKey{SDL_SCANCODE_F, Verb::FirstPerson},
    PlayKey{SDL_SCANCODE_R, Verb::Ocarina}, PlayKey{SDL_SCANCODE_TAB, Verb::ItemScreen},
    PlayKey{SDL_SCANCODE_M, Verb::Map},     PlayKey{SDL_SCANCODE_ESCAPE, Verb::Pause},
};

struct PlayMouse {
  uint8_t button;
  Verb verb;
};

constexpr std::array kPlayMouse{PlayMouse{SDL_BUTTON_LEFT, Verb::Sword},
                                PlayMouse{SDL_BUTTON_RIGHT, Verb::Shield},
                                PlayMouse{SDL_BUTTON_MIDDLE, Verb::Target}};

// 1 to 8 are slots 0 to 7; with Shift, 1 to 3 are slots 8 to 10 and the others stay.
constexpr std::array kSlotKeys{SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
                               SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8};
constexpr size_t kShiftSlots = kSlotCount - kSlotKeys.size();

struct KeyBinding {
  SDL_Scancode key;
  Button button;
};

// Outside play a key is a 3DS button: confirm, back, the menus' own buttons, and the ocarina's
// five notes on 1 to 5 from the lowest.
constexpr std::array kNameKeys{
    KeyBinding{SDL_SCANCODE_E, Settings::NativeButton::A},
    KeyBinding{SDL_SCANCODE_SPACE, Settings::NativeButton::A},
    KeyBinding{SDL_SCANCODE_RETURN, Settings::NativeButton::A},
    KeyBinding{SDL_SCANCODE_BACKSPACE, Settings::NativeButton::B},
    KeyBinding{SDL_SCANCODE_ESCAPE, Settings::NativeButton::Start},
    KeyBinding{SDL_SCANCODE_M, Settings::NativeButton::Select},
    KeyBinding{SDL_SCANCODE_Q, Settings::NativeButton::L},
    KeyBinding{SDL_SCANCODE_F, Settings::NativeButton::R},
    KeyBinding{SDL_SCANCODE_UP, Settings::NativeButton::Up},
    KeyBinding{SDL_SCANCODE_DOWN, Settings::NativeButton::Down},
    KeyBinding{SDL_SCANCODE_LEFT, Settings::NativeButton::Left},
    KeyBinding{SDL_SCANCODE_RIGHT, Settings::NativeButton::Right},
    KeyBinding{SDL_SCANCODE_1, Settings::NativeButton::L},
    KeyBinding{SDL_SCANCODE_2, Settings::NativeButton::R},
    KeyBinding{SDL_SCANCODE_3, Settings::NativeButton::Y},
    KeyBinding{SDL_SCANCODE_4, Settings::NativeButton::X},
    KeyBinding{SDL_SCANCODE_5, Settings::NativeButton::A},
};

struct PadBinding {
  SDL_GamepadButton pad;
  Button button;
};

// Outside play a pad button is the 3DS button of its name on an Xbox pad: A is A (south), B is
// B (east); the triggers are L and R, the bumpers ZL and ZR.
constexpr std::array kNameButtons{
    PadBinding{SDL_GAMEPAD_BUTTON_SOUTH, Settings::NativeButton::A},
    PadBinding{SDL_GAMEPAD_BUTTON_EAST, Settings::NativeButton::B},
    PadBinding{SDL_GAMEPAD_BUTTON_WEST, Settings::NativeButton::X},
    PadBinding{SDL_GAMEPAD_BUTTON_NORTH, Settings::NativeButton::Y},
    PadBinding{SDL_GAMEPAD_BUTTON_START, Settings::NativeButton::Start},
    PadBinding{SDL_GAMEPAD_BUTTON_BACK, Settings::NativeButton::Select},
    PadBinding{SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, Settings::NativeButton::ZL},
    PadBinding{SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, Settings::NativeButton::ZR},
    PadBinding{SDL_GAMEPAD_BUTTON_DPAD_UP, Settings::NativeButton::Up},
    PadBinding{SDL_GAMEPAD_BUTTON_DPAD_DOWN, Settings::NativeButton::Down},
    PadBinding{SDL_GAMEPAD_BUTTON_DPAD_LEFT, Settings::NativeButton::Left},
    PadBinding{SDL_GAMEPAD_BUTTON_DPAD_RIGHT, Settings::NativeButton::Right},
};

struct MenuKey {
  SDL_Scancode key;
  bool MenuPad::*held;
};

// The picker's keys: confirm and back as in a game's menus, X for a game's options. A button's
// first key is the one its hint shows.
constexpr std::array kMenuKeys{
    MenuKey{SDL_SCANCODE_E, &MenuPad::a},       MenuKey{SDL_SCANCODE_RETURN, &MenuPad::a},
    MenuKey{SDL_SCANCODE_SPACE, &MenuPad::a},   MenuKey{SDL_SCANCODE_BACKSPACE, &MenuPad::b},
    MenuKey{SDL_SCANCODE_X, &MenuPad::x},       MenuKey{SDL_SCANCODE_Y, &MenuPad::y},
    MenuKey{SDL_SCANCODE_W, &MenuPad::up},      MenuKey{SDL_SCANCODE_S, &MenuPad::down},
    MenuKey{SDL_SCANCODE_A, &MenuPad::left},    MenuKey{SDL_SCANCODE_D, &MenuPad::right},
    MenuKey{SDL_SCANCODE_UP, &MenuPad::up},     MenuKey{SDL_SCANCODE_DOWN, &MenuPad::down},
    MenuKey{SDL_SCANCODE_LEFT, &MenuPad::left}, MenuKey{SDL_SCANCODE_RIGHT, &MenuPad::right},
};

struct MenuButton {
  SDL_GamepadButton pad;
  bool MenuPad::*held;
};

constexpr std::array kMenuButtons{
    MenuButton{SDL_GAMEPAD_BUTTON_SOUTH, &MenuPad::a},
    MenuButton{SDL_GAMEPAD_BUTTON_EAST, &MenuPad::b},
    MenuButton{SDL_GAMEPAD_BUTTON_WEST, &MenuPad::x},
    MenuButton{SDL_GAMEPAD_BUTTON_NORTH, &MenuPad::y},
    MenuButton{SDL_GAMEPAD_BUTTON_START, &MenuPad::start},
    MenuButton{SDL_GAMEPAD_BUTTON_DPAD_UP, &MenuPad::up},
    MenuButton{SDL_GAMEPAD_BUTTON_DPAD_DOWN, &MenuPad::down},
    MenuButton{SDL_GAMEPAD_BUTTON_DPAD_LEFT, &MenuPad::left},
    MenuButton{SDL_GAMEPAD_BUTTON_DPAD_RIGHT, &MenuPad::right},
};

// MenuPad's buttons by name, for listKeys.
constexpr std::array kMenuNames{
    std::pair{"a", &MenuPad::a},       std::pair{"b", &MenuPad::b},
    std::pair{"x", &MenuPad::x},       std::pair{"y", &MenuPad::y},
    std::pair{"up", &MenuPad::up},     std::pair{"down", &MenuPad::down},
    std::pair{"left", &MenuPad::left}, std::pair{"right", &MenuPad::right},
};

struct StickKey {
  SDL_Scancode key;
  const char *name;
  // The circle pad's axis (0 x, 1 y) and the way the key pushes it.
  size_t axis;
  float push;
};

constexpr std::array kStickKeys{
    StickKey{SDL_SCANCODE_W, "up", 1, 1.0F},
    StickKey{SDL_SCANCODE_S, "down", 1, -1.0F},
    StickKey{SDL_SCANCODE_A, "left", 0, -1.0F},
    StickKey{SDL_SCANCODE_D, "right", 0, 1.0F},
};

constexpr std::array kChordKeys{SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT};
// What the core's names of the 3DS buttons (Settings::NativeButton::mapping) all start with.
constexpr std::string_view kButtonPrefix = "button_";

constexpr int16_t kTriggerPressed = 16384;
constexpr float kAxisFull = 32767.0F;
// A stick this far out counts as the pad being used.
constexpr float kStickMoved = 0.5F;

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

// The 3DS lying flat and turning as the pad's state says.
class MotionDevice final : public Input::MotionDevice {
public:
  explicit MotionDevice(std::shared_ptr<const PadState> state) : state_(std::move(state)) {
  }
  [[nodiscard]] std::tuple<Common::Vec3<float>, Common::Vec3<float>> GetStatus() const override {
    const auto &[x, y, z] = state_->gyro;
    return {Common::MakeVec(0.0F, -1.0F, 0.0F), Common::MakeVec(x, y, z)};
  }
  // Every sample is ours, so the core never stretches the rate by the host's frame time.
  std::optional<Common::Vec3<float>> TakeGyroscopeSample() override {
    Common::Vec3<float> rate = Common::MakeVec(state_->gyro[0], state_->gyro[1], state_->gyro[2]);
    if (state_->gyroSource != nullptr) {
      const auto [x, y, z] = state_->gyroSource->takeSample();
      rate += Common::MakeVec(x, y, z);
    }
    return rate;
  }

private:
  std::shared_ptr<const PadState> state_;
};

class MotionFactory final : public Input::Factory<Input::MotionDevice> {
public:
  explicit MotionFactory(std::shared_ptr<const PadState> state) : state_(std::move(state)) {
  }
  std::unique_ptr<Input::MotionDevice> Create(const Common::ParamPackage & /*params*/) override {
    return std::make_unique<MotionDevice>(state_);
  }

private:
  std::shared_ptr<const PadState> state_;
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
  Input::RegisterFactory<Input::MotionDevice>(kEngine, std::make_shared<MotionFactory>(state_));
}

PadInput::~PadInput() {
  Input::UnregisterFactory<Input::ButtonDevice>(kEngine);
  Input::UnregisterFactory<Input::AnalogDevice>(kEngine);
  Input::UnregisterFactory<Input::MotionDevice>(kEngine);
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
  profile.motion_device = std::string("engine:") + kEngine;
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
  state.gyro = state_->gyro;
  state.gyroSource = state_->gyroSource;
  const bool play = game_ != nullptr && mode_ != PadMode::ByName;
  uint32_t held = 0;
  uint16_t slots = 0;
  const auto press = [&](Verb verb) {
    // A screen that assigns slots closes on the input that opened it.
    const bool leaves = mode_ == PadMode::Assign && verb == Verb::ItemScreen;
    held |= leaves ? game_->leave : game_->buttons[static_cast<size_t>(verb)];
  };
  const bool *keys = SDL_GetKeyboardState(nullptr);
  const bool shift = std::ranges::any_of(kChordKeys, [keys](SDL_Scancode key) {
    return keys[key];
  });
  bool keyHeld = false;
  if (play) {
    for (const PlayKey &binding : kPlayKeys) {
      if (keys[binding.key]) {
        keyHeld = true;
        press(binding.verb);
      }
    }
    for (size_t i = 0; i < kSlotKeys.size(); ++i) {
      if (keys[kSlotKeys[i]]) {
        keyHeld = true;
        slots |= static_cast<uint16_t>(1U << (shift && i < kShiftSlots ? kSlotKeys.size() + i : i));
      }
    }
    const SDL_MouseButtonFlags mouse =
        mode_ == PadMode::Play ? SDL_GetMouseState(nullptr, nullptr) : 0;
    for (const PlayMouse &binding : kPlayMouse) {
      if ((mouse & SDL_BUTTON_MASK(binding.button)) != 0) {
        keyHeld = true;
        press(binding.verb);
      }
    }
  } else {
    for (const KeyBinding &binding : kNameKeys) {
      if (keys[binding.key]) {
        keyHeld = true;
        held |= bit(binding.button);
      }
    }
  }
  MenuPad menu;
  for (const MenuKey &binding : kMenuKeys) {
    menu.*binding.held = menu.*binding.held || keys[binding.key];
    keyHeld = keyHeld || keys[binding.key];
  }
  auto &circle = state.sticks[Settings::NativeAnalog::CirclePad];
  for (const StickKey &binding : kStickKeys) {
    if (keys[binding.key]) {
      keyHeld = true;
      circle[binding.axis] += binding.push;
    }
  }
  bool padHeld = false;
  bool chord = play && shift;
  auto &cStick = state.sticks[Settings::NativeAnalog::CStick];
  for (SDL_Gamepad *pad : pads_) {
    const bool padChord = play && SDL_GetGamepadButton(pad, kChord);
    chord = chord || padChord;
    padHeld = padHeld || padChord;
    if (play) {
      for (const PlayButton &binding : kPlayButtons) {
        if (!SDL_GetGamepadButton(pad, binding.pad)) {
          continue;
        }
        padHeld = true;
        const int slot = padChord ? binding.chordSlot : binding.slot;
        if (slot != kNoSlot) {
          slots |= static_cast<uint16_t>(1U << slot);
        } else if (padChord && binding.chordVerb) {
          press(*binding.chordVerb);
        } else if (binding.verb) {
          press(*binding.verb);
        }
      }
    } else {
      for (const PadBinding &binding : kNameButtons) {
        if (SDL_GetGamepadButton(pad, binding.pad)) {
          padHeld = true;
          held |= bit(binding.button);
        }
      }
    }
    if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kTriggerPressed) {
      padHeld = true;
      if (play) {
        press(Verb::Target);
      } else {
        held |= bit(Settings::NativeButton::L);
      }
    }
    if (SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > kTriggerPressed) {
      padHeld = true;
      if (play) {
        press(Verb::Shield);
      } else {
        held |= bit(Settings::NativeButton::R);
      }
    }
    // SDL's y grows downward; the 3DS's grows upward.
    circle[0] = stronger(circle[0], axis(pad, SDL_GAMEPAD_AXIS_LEFTX));
    circle[1] = stronger(circle[1], -axis(pad, SDL_GAMEPAD_AXIS_LEFTY));
    cStick[0] = stronger(cStick[0], axis(pad, SDL_GAMEPAD_AXIS_RIGHTX));
    cStick[1] = stronger(cStick[1], -axis(pad, SDL_GAMEPAD_AXIS_RIGHTY));
    padHeld = padHeld || std::abs(axis(pad, SDL_GAMEPAD_AXIS_LEFTX)) > kStickMoved ||
              std::abs(axis(pad, SDL_GAMEPAD_AXIS_LEFTY)) > kStickMoved;
    for (const MenuButton &binding : kMenuButtons) {
      menu.*binding.held = menu.*binding.held || SDL_GetGamepadButton(pad, binding.pad);
    }
    menu.left = menu.left || axis(pad, SDL_GAMEPAD_AXIS_LEFTX) < -kStickMoved;
    menu.right = menu.right || axis(pad, SDL_GAMEPAD_AXIS_LEFTX) > kStickMoved;
    menu.up = menu.up || axis(pad, SDL_GAMEPAD_AXIS_LEFTY) < -kStickMoved;
    menu.down = menu.down || axis(pad, SDL_GAMEPAD_AXIS_LEFTY) > kStickMoved;
  }
  for (size_t i = 0; i < state.buttons.size(); ++i) {
    state.buttons[i] = ((held >> i) & 1U) != 0;
  }
  menu_ = menu;
  slots_ = slots;
  chord_ = chord;
  // The keyboard wins a frame both are held in: it is the one in the player's hands last.
  if (keyHeld) {
    lastDevice_ = HostDevice::Keyboard;
  } else if (padHeld) {
    lastDevice_ = HostDevice::Pad;
  }
  *state_ = state;
}

Prompt PadInput::slotPrompt(size_t slot) {
  Prompt prompt;
  for (const PlayButton &binding : kPlayButtons) {
    if (binding.slot == static_cast<int>(slot)) {
      prompt.pad = binding.pad;
      break;
    }
    if (binding.chordSlot == static_cast<int>(slot)) {
      prompt.pad = binding.pad;
      prompt.chord = true;
    }
  }
  prompt.shift = slot >= kSlotKeys.size();
  prompt.key = kSlotKeys[prompt.shift ? slot - kSlotKeys.size() : slot];
  return prompt;
}

Prompt PadInput::verbPrompt(Verb verb) {
  Prompt prompt;
  if (const auto *pad = std::ranges::find(kPlayButtons, std::optional(verb), &PlayButton::verb);
      pad != kPlayButtons.end()) {
    prompt.pad = pad->pad;
  }
  if (const auto *key = std::ranges::find(kPlayKeys, verb, &PlayKey::verb);
      key != kPlayKeys.end()) {
    prompt.key = key->key;
  }
  if (const auto *mouse = std::ranges::find(kPlayMouse, verb, &PlayMouse::verb);
      mouse != kPlayMouse.end()) {
    prompt.mouse = mouse->button;
  }
  return prompt;
}

Prompt PadInput::menuPrompt(bool MenuPad::*held) {
  Prompt prompt;
  if (const auto *pad = std::ranges::find(kMenuButtons, held, &MenuButton::held);
      pad != kMenuButtons.end()) {
    prompt.pad = pad->pad;
  }
  if (const auto *key = std::ranges::find(kMenuKeys, held, &MenuKey::held);
      key != kMenuKeys.end()) {
    prompt.key = key->key;
  }
  return prompt;
}

void PadInput::listKeys(std::ostream &out) {
  const auto line = [&out](std::string_view table, std::string_view name, std::string_view key) {
    out << table << '\t' << name << '\t' << key << '\n';
  };
  for (const MenuKey &binding : kMenuKeys) {
    line("menu",
         std::ranges::find(kMenuNames, binding.held,
                           &std::pair<const char *, bool MenuPad::*>::second)
             ->first,
         SDL_GetScancodeName(binding.key));
  }
  for (const KeyBinding &binding : kNameKeys) {
    line("name",
         std::string_view(Settings::NativeButton::mapping[binding.button])
             .substr(kButtonPrefix.size()),
         SDL_GetScancodeName(binding.key));
  }
  const auto verbName = [](Verb verb) {
    return std::ranges::find(kVerbNames, verb, &std::pair<std::string_view, Verb>::second)->first;
  };
  for (const PlayKey &binding : kPlayKeys) {
    line("play", verbName(binding.verb), SDL_GetScancodeName(binding.key));
  }
  for (const PlayMouse &binding : kPlayMouse) {
    line("mouse", verbName(binding.verb), std::to_string(binding.button));
  }
  for (size_t slot = 0; slot < kSlotCount; ++slot) {
    // A slot behind the chord is "<chord key>+<key>".
    const Prompt by = slotPrompt(slot);
    line("slot", std::to_string(slot),
         (by.shift ? std::string(SDL_GetScancodeName(kChordKeys.front())) + "+" : std::string()) +
             SDL_GetScancodeName(*by.key));
  }
  for (const StickKey &binding : kStickKeys) {
    line("stick", binding.name, SDL_GetScancodeName(binding.key));
  }
  for (const SDL_Scancode key : kChordKeys) {
    line("chord", "chord", SDL_GetScancodeName(key));
  }
}

// The 3DS buttons each game's mod set reads for a verb, in Verb's order: Action, Sword, Shield,
// Target, FirstPerson, Pause, Map, ItemScreen, Ocarina.
const std::array<PadInput::GameVerbs, 2> PadInput::kGames{
    // Ocarina of Time 3D, The One Screen (its source/input/shortcuts.h): the D-pad's Left is
    // View, Up the item screen, Right the ocarina; Select the map.
    GameVerbs{"0004000000033500",
              {bit(Settings::NativeButton::A), bit(Settings::NativeButton::B),
               bit(Settings::NativeButton::R), bit(Settings::NativeButton::L),
               bit(Settings::NativeButton::Left), bit(Settings::NativeButton::Start),
               bit(Settings::NativeButton::Select), bit(Settings::NativeButton::Up),
               bit(Settings::NativeButton::Right)},
              bit(Settings::NativeButton::Start)},
    // Majora's Mask 3D, Project Restoration: ZL is View, Select the Items screen, and ZR with
    // Start the Gear screen, with Select the map, with A the ocarina.
    GameVerbs{"0004000000125500",
              {bit(Settings::NativeButton::A), bit(Settings::NativeButton::B),
               bit(Settings::NativeButton::R), bit(Settings::NativeButton::L),
               bit(Settings::NativeButton::ZL),
               bit(Settings::NativeButton::ZR) | bit(Settings::NativeButton::Start),
               bit(Settings::NativeButton::ZR) | bit(Settings::NativeButton::Select),
               bit(Settings::NativeButton::Select),
               bit(Settings::NativeButton::ZR) | bit(Settings::NativeButton::A)},
              bit(Settings::NativeButton::Select)},
};

void PadInput::bindGame(std::string_view titleId) {
  const auto *found = std::ranges::find(kGames, titleId, &GameVerbs::titleId);
  game_ = found != kGames.end() ? found : nullptr;
  mode_ = PadMode::ByName;
}

void PadInput::setButtons(uint32_t mask) {
  for (size_t i = 0; i < state_->buttons.size(); ++i) {
    state_->buttons[i] = ((mask >> i) & 1U) != 0;
  }
}

void PadInput::setStick(Stick stick, float x, float y) {
  state_->sticks[stick] = {std::clamp(x, -1.0F, 1.0F), std::clamp(y, -1.0F, 1.0F)};
}

void PadInput::setGyro(float x, float y, float z) {
  state_->gyro = {x, y, z};
}

} // namespace z3d::app
