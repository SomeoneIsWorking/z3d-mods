// Whether the player's window is fullscreen, kept in <user>/window.json for every game and the
// picker. Nothing chosen means fullscreen.
#pragma once

#include <filesystem>

namespace z3d::app {

class WindowMode {
public:
  // An empty path keeps the default and saves nothing (hidden runs).
  explicit WindowMode(std::filesystem::path stored);

  [[nodiscard]] bool fullscreen() const {
    return fullscreen_;
  }
  void setFullscreen(bool fullscreen);

private:
  std::filesystem::path stored_;
  bool fullscreen_ = true;
};

} // namespace z3d::app
