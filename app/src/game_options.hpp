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

struct Option {
  std::string id;
  std::string label;
  // What the value picks from; a switch has "Off" and "On".
  std::vector<std::string> choices;
  uint8_t value = 0;
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

  // The setup to boot with: the set's, with the app's own options applied.
  [[nodiscard]] GameSetup setup() const;
  // Writes the patch's switches into the running game's options block.
  void apply(Emulator &emulator) const;

private:
  void save() const;

  GameSetup setup_;
  std::filesystem::path stored_;
  uint32_t address_ = 0;
  // The patch's switches first, in block order, then the app's own.
  std::vector<Option> items_;
  size_t patchCount_ = 0;
};

} // namespace z3d::app
