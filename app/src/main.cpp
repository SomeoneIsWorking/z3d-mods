// z3d: plays a 3DS game on Azahar's core in a window, or, with --hidden, runs it frame by frame
// from stdin lines for the tools (repl.hpp). Flags: app_config.hpp.
#include "app_config.hpp"
#include "core_settings.hpp"
#include "emulator.hpp"
#include "game_window.hpp"
#include "pad_input.hpp"
#include "player.hpp"
#include "repl.hpp"

#include <lucent/log.h>

#include <exception>
#include <iostream>

int main(int argc, char **argv) {
  try {
    const z3d::app::AppConfig config = z3d::app::AppConfig::parse(argc, argv);
    z3d::app::applyCoreSettings(config);
    z3d::app::PadInput pad;
    z3d::app::GameWindow window(config, pad);
    z3d::app::Emulator emulator(config, window);
    if (config.hidden) {
      z3d::app::Repl repl(emulator, pad);
      std::cout << "ready\n" << std::flush;
      repl.serve(std::cin, std::cout);
      return 0;
    }
    z3d::app::Player(emulator, window).run();
    return 0;
  } catch (const std::exception &error) {
    lucent::error("z3d", "{}", error.what());
    return 1;
  }
}
