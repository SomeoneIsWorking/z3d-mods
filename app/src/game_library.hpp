// The games the app can boot: the tools' library file, with each title and icon read from the
// player's own dump.
#pragma once

#include "app_config.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace z3d::app {

struct Game {
  // What booting it needs.
  GameSetup setup;
  // From the dump's SMDH: the English short title, and the large icon as RGBA8888 rows.
  std::string title;
  std::vector<uint32_t> icon;
  int iconSize = 0;
};

class GameLibrary {
public:
  // `path` is a JSON array of {"rom", "layout", "textures", "options"} objects (tools/z3dmods/play.py).
  static GameLibrary load(const std::filesystem::path &path);

  [[nodiscard]] const std::vector<Game> &games() const {
    return games_;
  }

private:
  std::vector<Game> games_;
};

} // namespace z3d::app
