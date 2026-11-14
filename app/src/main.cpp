// z3d: plays 3DS games on Azahar's core in a window, from its title picker or a direct launch,
// or, with --hidden, runs one frame by frame from stdin lines for the tools (repl.hpp).
// Flags: app_config.hpp.
#include "app_config.hpp"
#include "core_log.hpp"
#include "core_settings.hpp"
#include "emulator.hpp"
#include "game_library.hpp"
#include "game_options.hpp"
#include "game_window.hpp"
#include "hud.hpp"
#include "mod_swap.hpp"
#include "pad_input.hpp"
#include "player.hpp"
#include "repl.hpp"
#include "screen_overlay.hpp"
#include "set_install.hpp"
#include "title_picker.hpp"

#include <lucent/log.h>

#include <exception>
#include <iostream>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <vector>

namespace z3d::app {
namespace {

// The player's choices for a game, kept in the user folder under the dump's name.
std::filesystem::path storedOptions(const AppConfig &config, const GameSetup &game) {
  return config.userDir / "options" / game.rom.stem().concat(".json");
}

GameOptions loadOptions(const AppConfig &config, const GameSetup &game) {
  // The tools run every game on its defaults.
  return {game, config.userDir / game.options,
          config.hidden ? std::filesystem::path() : storedOptions(config, game)};
}

int direct(const AppConfig &config, const GameSetup &game, PadInput &pad) {
  GameOptions options = loadOptions(config, game);
  for (const auto &[id, value] : config.options) {
    if (!options.set(id, value)) {
      throw std::invalid_argument("--option " + id + ": no such option or value");
    }
  }
  applyGameSettings(options.setup());
  GameWindow window(config, pad);
  Emulator emulator(config, game.rom, window);
  options.apply(emulator, pad);
  const ScreenOverlay overlay(window, emulator, options.setup(), config.userDir / game.options);
  const Hud hud(window, emulator, pad, overlay, config.assets, config.userDir / game.options);
  ModSwap mods(emulator, (config.userDir / game.options).parent_path(), options.blockAddress());
  if (config.hidden) {
    Repl repl(emulator, window, pad, options, mods);
    std::cout << "ready\n" << std::flush;
    repl.serve(std::cin, std::cout);
    return 0;
  }
  Player(emulator, window, mods, options, pad).run();
  return 0;
}

int picker(const AppConfig &config, PadInput &pad) {
  const GameLibrary library = GameLibrary::load(config.library);
  std::vector<GameOptions> options;
  for (const Game &game : library.games()) {
    options.push_back(loadOptions(config, game.setup));
  }
  GameWindow window(config, pad);
  SetInstall install(config.installer, config.userDir);
  while (true) {
    std::optional<size_t> chosen;
    {
      TitlePicker picker(window, pad, library, options, install, config.font);
      chosen = picker.run();
    }
    if (!chosen) {
      return 0;
    }
    const Game &game = library.games()[*chosen];
    if (install.takeFresh()) {
      // The picker has just set the game up: its options exist now.
      options[*chosen] = loadOptions(config, game.setup);
    }
    lucent::info("z3d", "booting {}", game.title);
    applyGameSettings(options[*chosen].setup());
    window.boot(game.title);
    {
      Emulator emulator(config, game.setup.rom, window);
      options[*chosen].apply(emulator, pad);
      const ScreenOverlay overlay(window, emulator, options[*chosen].setup(),
                                  config.userDir / game.setup.options);
      const Hud hud(window, emulator, pad, overlay, config.assets, config.userDir / game.setup.options);
      ModSwap mods(emulator, (config.userDir / game.setup.options).parent_path(),
                   options[*chosen].blockAddress());
      Player(emulator, window, mods, options[*chosen], pad).run();
    }
    if (window.closeRequested()) {
      return 0;
    }
    // The picker's window has the shape of no game: both screens.
    applyGameSettings(GameSetup{});
    window.boot(config.title);
  }
}

} // namespace
} // namespace z3d::app

int main(int argc, char **argv) {
  try {
    const z3d::app::AppConfig config = z3d::app::AppConfig::parse(argc, argv);
    z3d::app::applyCoreSettings(config);
    const z3d::app::CoreLog log;
    z3d::app::PadInput pad;
    if (config.game) {
      return z3d::app::direct(config, *config.game, pad);
    }
    return z3d::app::picker(config, pad);
  } catch (const std::exception &error) {
    lucent::error("z3d", "{}", error.what());
    return 1;
  }
}
