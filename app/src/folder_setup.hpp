// The first run's questions: the folder of the player's dumps, then the folder of their mod
// downloads, each asked on the shared setup screen (setup-ui) in the game window, chosen in the
// folder browser and taken by the tools, which keep them in <user>/folders.json and write the
// library (docs/plans/packed-app.md).
#pragma once

#include "app_config.hpp"

namespace z3d::app {

class GameWindow;
class PadInput;

class FolderSetup {
public:
  FolderSetup(GameWindow &window, const PadInput &pad, const AppConfig &config)
      : window_(window), pad_(pad), config_(config) {
  }

  // False when the window closes before both folders are taken.
  [[nodiscard]] bool run();

private:
  GameWindow &window_;
  const PadInput &pad_;
  const AppConfig &config_;
};

} // namespace z3d::app
