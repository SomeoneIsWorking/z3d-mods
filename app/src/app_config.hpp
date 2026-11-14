// The app's typed command line; the one place flags are read.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace z3d::app {

// Overlay is the app's own single screen (screen_overlay.hpp).
enum class ScreenLayout : uint8_t { Default, SingleScreen, LargeScreen, SideBySide, Overlay };

[[nodiscard]] ScreenLayout parseLayout(std::string_view name);

// What one game boots with.
struct GameSetup {
  std::filesystem::path rom;
  ScreenLayout layout = ScreenLayout::Default;
  bool customTextures = false;
  // The installed patch's z3d-options.json (game_options.hpp), relative to the user folder.
  std::filesystem::path options;
  // Folders beside the game's mods romfs/ that are layered over it (the core's romfs_layers).
  std::vector<std::string> romfsLayers;
};

struct AppConfig {
  // A direct launch; without it the window opens the title picker over `library`.
  std::optional<GameSetup> game;
  // A direct launch's option choices, set before the game boots: --option <id>=<value>.
  std::vector<std::pair<std::string, uint8_t>> options;
  // The tools' list of games (game_library.hpp) and the picker's outline font.
  std::filesystem::path library;
  std::filesystem::path font;
  // The tools' installer for a game whose set is missing (set_install.hpp); the dump's path is
  // added after it. Everything after --installer on the command line.
  std::vector<std::string> installer;
  // A port-assets copy (sets/, fonts/), which the play HUD draws from; none draws no HUD.
  std::filesystem::path assets;
  // Holds Azahar/ in the core's user layout (nand, sdmc, sysdata, load).
  std::filesystem::path userDir;
  std::string title = "z3d";
  // The desktop entry the window takes its icon from.
  std::string appId = "z3d";
  // The tools' mode: unmapped window, no sound, unpaced, driven by stdin lines.
  bool hidden = false;
  // Internal render scale; 1 is the 3DS's own 400x240 top screen.
  uint32_t resolution = 1;
  // Every texture the game uploads, as PNG under <user>/Azahar/dump/textures/.
  bool dumpTextures = false;

  // z3d --user <dir> (--rom <3ds> [--layout l] [--textures] [--options <json>] [--option <id>=<value>]... | --library <json> --font <ttf>)
  //     [--title t] [--app-id id] [--resolution n] [--hidden] [--dump-textures]
  //     [--installer <program> [<argument>]...]
  [[nodiscard]] static AppConfig parse(int argc, char **argv);
};

} // namespace z3d::app
