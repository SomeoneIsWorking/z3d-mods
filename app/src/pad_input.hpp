// The 3DS pad the core reads: an input engine ("z3d") whose state the player's keyboard, mouse
// and gamepads or the tools' commands set. In play the host's inputs follow one layout for every
// game (docs/plans/pc-controls-hud.md): verbs on fixed buttons and eleven item slots, which the
// game's patch takes from the slot link (slot_link.hpp).
#pragma once

#include "common/settings.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_scancode.h>

#include <array>
#include <cstdint>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

union SDL_Event;

namespace z3d::app {

// Something that turns the 3DS by its own account (mouse_aim.hpp).
class GyroSource {
public:
  GyroSource() = default;
  virtual ~GyroSource() = default;
  GyroSource(const GyroSource &) = delete;
  GyroSource &operator=(const GyroSource &) = delete;
  GyroSource(GyroSource &&) = delete;
  GyroSource &operator=(GyroSource &&) = delete;
  // The rate for one gyroscope sample of the HID service, degrees a second on PadState::gyro's
  // axes; called once per sample.
  virtual std::array<float, 3> takeSample() = 0;
};

struct PadState {
  // Indexed by Settings::NativeButton.
  std::array<bool, Settings::NativeButton::NumButtons> buttons{};
  // Indexed by Settings::NativeAnalog: x right, y up, each -1..1.
  std::array<std::array<float, 2>, Settings::NativeAnalog::NumAnalogs> sticks{};
  // The C-stick as the game gets it: each axis turned around or not.
  std::array<bool, 2> cStickInverted{};
  // The 3DS turning, in degrees a second of the game's time, on the core's axes: x points left
  // along the D-pad, y out of the touch screen, z up along the D-pad; right-handed.
  std::array<float, 3> gyro{};
  // Turns the 3DS on top of that, sample by sample; null without one.
  GyroSource *gyroSource = nullptr;
};

// The item slots: Y, B and RB, the D-pad's Up, Right, Down and Left, then A, B, X and Y with the
// chord held.
inline constexpr size_t kSlotCount = 11;

// Which table the host's inputs go through, as the game's patch asks (tools/z3dmods/slot_link.py):
// by name outside play (the 3DS button of the same name: menus, the ocarina), the play layout,
// or the play layout with the mouse left as the stylus (a screen that assigns slots).
enum class PadMode : uint8_t { ByName, Play, Assign };

// What a host input does in play, the same in every game.
enum class Verb : uint8_t { Action, Sword, Shield, Target, FirstPerson, Pause, Map, ItemScreen, Ocarina };
inline constexpr size_t kVerbCount = 9;
// The verbs' names in a HUD's description and in listKeys.
inline constexpr std::array kVerbNames{
    std::pair{std::string_view("action"), Verb::Action},
    std::pair{std::string_view("sword"), Verb::Sword},
    std::pair{std::string_view("shield"), Verb::Shield},
    std::pair{std::string_view("target"), Verb::Target},
    std::pair{std::string_view("first_person"), Verb::FirstPerson},
    std::pair{std::string_view("pause"), Verb::Pause},
    std::pair{std::string_view("map"), Verb::Map},
    std::pair{std::string_view("item_screen"), Verb::ItemScreen},
    std::pair{std::string_view("ocarina"), Verb::Ocarina}};

// What the picker and menus read: the host's buttons by their names on an Xbox pad, and the
// keys that stand for them, before any game's layout.
struct MenuPad {
  // The D-pad, the left stick or the arrow keys.
  bool up = false;
  bool down = false;
  bool left = false;
  bool right = false;
  bool a = false;
  bool b = false;
  bool x = false;
  bool y = false;
  bool start = false;
};

// What the player last pressed something on; the mouse counts as the keyboard.
enum class HostDevice : uint8_t { Pad, Keyboard };

// The host inputs that press something, for whoever draws a prompt: a pad button (with the
// chord held or not), and a key (with Shift or not) or a mouse button (SDL_BUTTON_*).
struct Prompt {
  std::optional<SDL_GamepadButton> pad;
  bool chord = false;
  std::optional<SDL_Scancode> key;
  bool shift = false;
  std::optional<uint8_t> mouse;
};

class PadInput {
public:
  // The pad button that is the chord, and the keys that are.
  static constexpr SDL_GamepadButton kChord = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;

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
  // The player's keyboard, mouse buttons and gamepads, as they are held now.
  void readHost();
  // NativeButton bits.
  void setButtons(uint32_t mask);
  void setStick(Settings::NativeAnalog::Values stick, float x, float y);
  // PadState::gyro, held until set again.
  void setGyro(float x, float y, float z);
  // PadState::gyroSource, which outlives its stay here; null takes it out.
  void setGyroSource(GyroSource *source) {
    state_->gyroSource = source;
  }
  // The play layout of the game with this title ID from here on; a game without one is by name.
  void bindGame(std::string_view titleId);
  // The table from the next readHost on.
  void setMode(PadMode mode) {
    mode_ = mode;
  }
  [[nodiscard]] PadMode mode() const {
    return mode_;
  }
  // The slots held now, bit n for slot n.
  [[nodiscard]] uint16_t slotsHeld() const {
    return slots_;
  }
  // Whether the chord is held: the pad's or, on the keyboard, Shift.
  [[nodiscard]] bool chordHeld() const {
    return chord_;
  }
  // The tools hold slots and the chord themselves.
  void setSlots(uint16_t slots, bool chord) {
    slots_ = slots;
    chord_ = chord;
  }
  [[nodiscard]] HostDevice lastDevice() const {
    return lastDevice_;
  }
  // The tools say which device their input stands for.
  void setDevice(HostDevice device) {
    lastDevice_ = device;
  }
  // What presses a slot and a verb in play.
  [[nodiscard]] static Prompt slotPrompt(size_t slot);
  [[nodiscard]] static Prompt verbPrompt(Verb verb);
  // What presses a button of the menus: a MenuPad member.
  [[nodiscard]] static Prompt menuPrompt(bool MenuPad::*held);
  // Every key and mouse binding, a "<table>\t<name>\t<key>" line each, for the tools
  // (tools/z3dmods/app_keys.py): menu, name (a 3DS button outside play), play (a verb), mouse
  // (a verb's mouse button), slot, stick and chord.
  static void listKeys(std::ostream &out);
  // A game option (game_options.hpp).
  void invertCStick(bool x, bool y) {
    state_->cStickInverted = {x, y};
  }

  [[nodiscard]] const PadState &state() const {
    return *state_;
  }
  // As readHost last found the host.
  [[nodiscard]] const MenuPad &menu() const {
    return menu_;
  }

private:
  // The 3DS buttons a game's mod set reads for each verb: NativeButton bits by Verb.
  struct GameVerbs {
    std::string_view titleId;
    std::array<uint32_t, kVerbCount> buttons;
    // What leaves a screen that assigns slots.
    uint32_t leave;
  };
  static const std::array<GameVerbs, 2> kGames;

  std::shared_ptr<PadState> state_;
  MenuPad menu_;
  std::vector<SDL_Gamepad *> pads_;
  const GameVerbs *game_ = nullptr;
  PadMode mode_ = PadMode::ByName;
  uint16_t slots_ = 0;
  bool chord_ = false;
  HostDevice lastDevice_ = HostDevice::Pad;
};

} // namespace z3d::app
