#include "app_config.hpp"

#include <array>
#include <stdexcept>
#include <utility>

namespace z3d::app {
namespace {

constexpr std::string_view kUsage =
    "usage: z3d --user <dir> (--rom <3ds> [--layout default|single_screen|large_screen|"
    "side_by_side|overlay] [--textures] [--options <json>] [--option <id>=<value>]... | "
    "--library <json> --font <ttf>) [--title <text>] "
    "[--app-id <id>] [--resolution <1..10>] [--hidden] [--dump-textures]";

constexpr std::array kLayouts{
    std::pair{std::string_view("default"), ScreenLayout::Default},
    std::pair{std::string_view("single_screen"), ScreenLayout::SingleScreen},
    std::pair{std::string_view("large_screen"), ScreenLayout::LargeScreen},
    std::pair{std::string_view("side_by_side"), ScreenLayout::SideBySide},
    std::pair{std::string_view("overlay"), ScreenLayout::Overlay},
};

constexpr uint32_t kMaxResolution = 10;

std::pair<std::string, uint8_t> parseOption(const std::string &text) {
  const size_t equals = text.find('=');
  if (equals == std::string::npos) {
    throw std::invalid_argument("--option wants <id>=<value>");
  }
  return {text.substr(0, equals), static_cast<uint8_t>(std::stoul(text.substr(equals + 1)))};
}

uint32_t parseResolution(const std::string &text) {
  const unsigned long value = std::stoul(text);
  if (value < 1 || value > kMaxResolution) {
    throw std::invalid_argument("--resolution wants 1.." + std::to_string(kMaxResolution));
  }
  return static_cast<uint32_t>(value);
}

} // namespace

ScreenLayout parseLayout(std::string_view name) {
  for (const auto &[text, layout] : kLayouts) {
    if (text == name) {
      return layout;
    }
  }
  throw std::invalid_argument("unknown layout " + std::string(name));
}

AppConfig AppConfig::parse(int argc, char **argv) {
  AppConfig config;
  GameSetup game;
  for (int i = 1; i < argc; ++i) {
    const std::string_view flag = argv[i];
    if (flag == "--hidden") {
      config.hidden = true;
      continue;
    }
    if (flag == "--dump-textures") {
      config.dumpTextures = true;
      continue;
    }
    if (flag == "--textures") {
      game.customTextures = true;
      continue;
    }
    if (i + 1 >= argc) {
      throw std::invalid_argument(std::string(flag) + " wants a value");
    }
    const std::string value = argv[++i];
    if (flag == "--rom") {
      game.rom = value;
    } else if (flag == "--options") {
      game.options = value;
    } else if (flag == "--option") {
      config.options.push_back(parseOption(value));
    } else if (flag == "--library") {
      config.library = value;
    } else if (flag == "--font") {
      config.font = value;
    } else if (flag == "--assets") {
      config.assets = value;
    } else if (flag == "--user") {
      config.userDir = value;
    } else if (flag == "--title") {
      config.title = value;
    } else if (flag == "--app-id") {
      config.appId = value;
    } else if (flag == "--layout") {
      game.layout = parseLayout(value);
    } else if (flag == "--resolution") {
      config.resolution = parseResolution(value);
    } else {
      throw std::invalid_argument("unknown flag " + std::string(flag));
    }
  }
  if (!game.rom.empty()) {
    config.game = game;
  }
  const bool picker = !config.library.empty() && !config.font.empty() && !config.hidden;
  if (config.userDir.empty() || config.game.has_value() == picker) {
    throw std::invalid_argument(std::string(kUsage));
  }
  return config;
}

} // namespace z3d::app
