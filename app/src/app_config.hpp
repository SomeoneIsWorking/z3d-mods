// The app's typed command line; the one place flags are read.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace z3d::app {

enum class ScreenLayout : uint8_t { Default, SingleScreen, LargeScreen, SideBySide };

struct AppConfig {
  std::filesystem::path rom;
  // Holds Azahar/ in the core's user layout (nand, sdmc, sysdata, load).
  std::filesystem::path userDir;
  std::string title = "z3d";
  // The desktop entry the window takes its icon from.
  std::string appId = "z3d";
  // The tools' mode: unmapped window, no sound, unpaced, driven by stdin lines.
  bool hidden = false;
  ScreenLayout layout = ScreenLayout::Default;
  bool customTextures = false;
  // Internal render scale; 1 is the 3DS's own 400x240 top screen.
  uint32_t resolution = 1;

  // z3d --rom <3ds> --user <dir> [--title t] [--app-id id] [--layout l] [--textures]
  //     [--resolution n] [--hidden]
  [[nodiscard]] static AppConfig parse(int argc, char **argv);
};

} // namespace z3d::app
