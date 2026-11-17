// One game's options: the installed patch's switches (its z3d-options.json: the guest address of
// the patch's options block and one byte per switch, tools/z3dmods/options.py) and the app's own
// (layout, custom textures, the HUD's size and opacity). The player's choices are kept per game and written into the guest.
#pragma once

#include "app_config.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace z3d::app {

class Emulator;
class PadInput;

struct Option {
  std::string id;
  std::string label;
  // What the value picks from; a switch has "Off" and "On".
  std::vector<std::string> choices;
  uint8_t value = 0;
  // An app option on the pad (cstick_invert_x, cstick_invert_y, mouse_scale); empty for the others.
  std::string pad;
  // What each choice is, for an option that picks a scale (mouse_scale); empty for a switch.
  std::vector<double> scales;
  // Folders of replacement game files: choice n + 1 layers in the nth, choice 0 none; empty for
  // the others.
  std::vector<std::string> layers;
};

// How our play HUD is drawn: its elements' size about their nearest corner of the top screen, and
// their opacity.
struct HudLook {
  float size = 1.0F;
  float opacity = 1.0F;
};

class GameOptions {
public:
  // `manifest` may not exist (a game without our patch). `stored` holds the player's choices; an
  // empty path keeps the defaults and saves nothing (the tools).
  GameOptions(const GameSetup &setup, const std::filesystem::path &manifest,
              std::filesystem::path stored);

  [[nodiscard]] const std::vector<Option> &items() const {
    return items_;
  }
  // Steps item `index` to its next (or previous) choice and saves.
  void step(size_t index, int direction);
  // False for an unknown id or a value outside its choices.
  [[nodiscard]] bool set(std::string_view id, uint8_t value);

  // The guest address of the patch's options block, where its own pages start; 0 without a patch.
  [[nodiscard]] uint32_t blockAddress() const {
    return address_;
  }
  // The setup to boot with: the set's, with the app's own options applied.
  [[nodiscard]] GameSetup setup() const;
  [[nodiscard]] HudLook hudLook() const;
  // Writes the patch's switches into the running game's options block and sets the pad's.
  void apply(Emulator &emulator, PadInput &pad) const;

private:
  void save() const;

  GameSetup setup_;
  std::filesystem::path stored_;
  uint32_t address_ = 0;
  // The manifest's switches first, in its order, then layout, the HUD's look and textures.
  std::vector<Option> items_;
  // How many of the first are the patch's, a byte each of its block.
  size_t blockCount_ = 0;
};

} // namespace z3d::app
