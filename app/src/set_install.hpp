// A game's mod set made from the player's own files: whether it is in the user folder, and the
// tools' installer run on the player's dump when it is not (docs/plans/packed-app.md).
#pragma once

#include "app_config.hpp"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

struct SDL_Process;

namespace z3d::app {

class SetInstall {
public:
  // `command` is the installer's program and arguments; the dump's path is added after them.
  // Empty installs nothing.
  SetInstall(std::vector<std::string> command, std::filesystem::path userDir);
  // Ends an installer that is still running.
  ~SetInstall();
  SetInstall(const SetInstall &) = delete;
  SetInstall &operator=(const SetInstall &) = delete;
  SetInstall(SetInstall &&) = delete;
  SetInstall &operator=(SetInstall &&) = delete;

  // The game's set is not in the user folder and there is an installer to make it.
  [[nodiscard]] bool wanted(const GameSetup &game) const;
  void start(const GameSetup &game);
  // Takes in what the installer has printed; false once it has ended.
  [[nodiscard]] bool running();
  // The last line the installer printed.
  [[nodiscard]] const std::string &lastLine() const {
    return lastLine_;
  }
  // Why the installer refused the player's files, in its own lines; its last line when it
  // ended some other way.
  [[nodiscard]] const std::string &failure() const {
    return refusal_.empty() ? lastLine_ : refusal_;
  }
  // The ended installer left the set in place.
  [[nodiscard]] bool succeeded() const {
    return succeeded_;
  }
  // Whether a set has been made since this was last asked.
  [[nodiscard]] bool takeFresh() {
    return std::exchange(fresh_, false);
  }

private:
  void read();

  std::vector<std::string> command_;
  std::filesystem::path userDir_;
  SDL_Process *process_ = nullptr;
  std::filesystem::path manifest_;
  std::string pending_;
  std::string lastLine_;
  std::string refusal_;
  bool succeeded_ = false;
  bool fresh_ = false;
};

} // namespace z3d::app
