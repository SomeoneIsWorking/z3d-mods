// One game's options: the installed patch's switches (its z3d-options.json: the guest address of
// the patch's options block and one byte per switch, tools/z3dmods/options.py) and the app's own
// (layout, custom textures). The player's choices are kept per game and written into the guest.
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
  // An app switch on the pad (cstick_invert_x, cstick_invert_y); empty for the others.
  std::string pad;
  // A folder of replacement game files the app layers in while the switch is on; empty for the others.
  std::string layer;
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
  // Writes the patch's switches into the running game's options block and sets the pad's.
  void apply(Emulator &emulator, PadInput &pad) const;

private:
  void save() const;

  GameSetup setup_;
  std::filesystem::path stored_;
  uint32_t address_ = 0;
  // The manifest's switches first, in its order, then layout and textures.
  std::vector<Option> items_;
  // How many of the first are the patch's, a byte each of its block.
  size_t blockCount_ = 0;
};

} // namespace z3d::app
