// z3d: plays a 3DS game on the Azahar libretro core in a window, or, with --hidden, runs it frame
// by frame from stdin lines for the tools.
//   z3d --core <so> --rom <3ds> --user <dir> [--title <text>] [--option key=value]... [--hidden]
#include "libretro_core.hpp"
#include "player.hpp"
#include "repl.hpp"
#include "window.hpp"

#include <lucent/log.h>

#include <exception>
#include <iostream>
#include <string_view>

namespace {

struct Arguments {
  z3d::app::CoreConfig core;
  std::string title = "z3d";
  std::string appId = "z3d";
  bool hidden = false;
};

Arguments parseArgs(int argc, char **argv) {
  Arguments args;
  for (int i = 1; i < argc; ++i) {
    const std::string_view flag = argv[i];
    if (flag == "--hidden") {
      args.hidden = true;
      continue;
    }
    if (i + 1 >= argc) {
      throw std::invalid_argument(std::string(flag) + " wants a value");
    }
    const std::string value = argv[++i];
    if (flag == "--core") {
      args.core.core = value;
    } else if (flag == "--rom") {
      args.core.rom = value;
    } else if (flag == "--user") {
      args.core.userDir = value;
    } else if (flag == "--title") {
      args.title = value;
    } else if (flag == "--app-id") {
      args.appId = value;
    } else if (flag == "--option") {
      const size_t equals = value.find('=');
      if (equals == std::string::npos) {
        throw std::invalid_argument("--option wants key=value, got " + value);
      }
      args.core.options[value.substr(0, equals)] = value.substr(equals + 1);
    } else {
      throw std::invalid_argument("unknown flag " + std::string(flag));
    }
  }
  if (args.core.core.empty() || args.core.rom.empty() || args.core.userDir.empty()) {
    throw std::invalid_argument("usage: z3d --core <so> --rom <3ds> --user <dir> [--title <text>] "
                                "[--app-id <id>] [--option k=v]... [--hidden]");
  }
  return args;
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Arguments args = parseArgs(argc, argv);
    z3d::app::Window window(args.title, args.appId, args.hidden);
    z3d::app::LibretroCore core(args.core, window);
    if (args.hidden) {
      z3d::app::Repl repl(core);
      std::cout << "ready\n" << std::flush;
      repl.serve(std::cin, std::cout);
      return 0;
    }
    z3d::app::Player player(core, window);
    player.run();
    return 0;
  } catch (const std::exception &error) {
    lucent::error("z3d", "{}", error.what());
    return 1;
  }
}
