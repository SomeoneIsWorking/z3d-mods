// A game's mod set made from the player's own files: whether it is in the user folder, and the
// tools' setup run on the player's dump when it is not (docs/plans/packed-app.md).
#pragma once

#include "app_config.hpp"
#include "tool_process.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace z3d::app {

class SetInstall {
public:
  // `tools` is the tools' program and arguments; empty installs nothing.
  SetInstall(std::vector<std::string> tools, std::filesystem::path userDir);

  // Whether the game's set in the user folder is the one the tools make now.
  enum class State : uint8_t {
    Ready,
    // No set, and there are tools to make it.
    Missing,
    // A set made from other inputs than the tools make it from now (GameSetup::inputs).
    Outdated,
  };
  [[nodiscard]] State state(const GameSetup &game) const;
  [[nodiscard]] bool wanted(const GameSetup &game) const {
    return state(game) != State::Ready;
  }
  void start(const GameSetup &game);
  // Takes in what the setup has printed; false once it has ended.
  [[nodiscard]] bool running();
  // The last line the setup printed.
  [[nodiscard]] const std::string &lastLine() const {
    return tool_.lastLine();
  }
  // Why the setup refused the player's files.
  [[nodiscard]] const std::string &failure() const {
    return tool_.failure();
  }
  // The ended setup left the set in place.
  [[nodiscard]] bool succeeded() const {
    return succeeded_;
  }
  // Whether a set has been made since this was last asked.
  [[nodiscard]] bool takeFresh() {
    return std::exchange(fresh_, false);
  }

private:
  std::vector<std::string> tools_;
  std::filesystem::path userDir_;
  ToolProcess tool_;
  GameSetup game_;
  bool active_ = false;
  bool succeeded_ = false;
  bool fresh_ = false;
};

} // namespace z3d::app
