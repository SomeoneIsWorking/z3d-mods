// z3d_harness: an Azahar libretro core driven frame by frame from stdin.
//   z3d_harness --core <so> --rom <3ds> --user <dir> [--option key=value]...
#include "libretro_core.hpp"
#include "repl.hpp"

#include <lucent/log.h>

#include <exception>
#include <iostream>
#include <string_view>

namespace {

z3d::harness::CoreConfig parseArgs(int argc, char **argv) {
  z3d::harness::CoreConfig config;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string_view flag = argv[i];
    const std::string value = argv[i + 1];
    if (flag == "--core") {
      config.core = value;
    } else if (flag == "--rom") {
      config.rom = value;
    } else if (flag == "--user") {
      config.userDir = value;
    } else if (flag == "--option") {
      const size_t equals = value.find('=');
      if (equals == std::string::npos) {
        throw std::invalid_argument("--option wants key=value, got " + value);
      }
      config.options[value.substr(0, equals)] = value.substr(equals + 1);
    } else {
      throw std::invalid_argument("unknown flag " + std::string(flag));
    }
  }
  if (config.core.empty() || config.rom.empty() || config.userDir.empty()) {
    throw std::invalid_argument(
        "usage: z3d_harness --core <so> --rom <3ds> --user <dir> [--option k=v]...");
  }
  return config;
}

} // namespace

int main(int argc, char **argv) {
  try {
    z3d::harness::LibretroCore core(parseArgs(argc, argv));
    z3d::harness::Repl repl(core);
    std::cout << "ready\n" << std::flush;
    repl.serve(std::cin, std::cout);
    return 0;
  } catch (const std::exception &error) {
    lucent::error("harness", "{}", error.what());
    return 1;
  }
}
