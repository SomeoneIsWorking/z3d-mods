#include "app_config.hpp"

#include <array>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace z3d::app {
namespace {

constexpr std::string_view kUsage =
    "usage: z3d --rom <3ds> --user <dir> [--title <text>] [--app-id <id>] "
    "[--layout default|single_screen|large_screen|side_by_side] [--textures] "
    "[--resolution <1..10>] [--hidden]";

constexpr std::array kLayouts{
    std::pair{std::string_view("default"), ScreenLayout::Default},
    std::pair{std::string_view("single_screen"), ScreenLayout::SingleScreen},
    std::pair{std::string_view("large_screen"), ScreenLayout::LargeScreen},
    std::pair{std::string_view("side_by_side"), ScreenLayout::SideBySide},
};

constexpr uint32_t kMaxResolution = 10;

ScreenLayout parseLayout(std::string_view name) {
  for (const auto &[text, layout] : kLayouts) {
    if (text == name) {
      return layout;
    }
  }
  throw std::invalid_argument("unknown layout " + std::string(name));
}

uint32_t parseResolution(const std::string &text) {
  const unsigned long value = std::stoul(text);
  if (value < 1 || value > kMaxResolution) {
    throw std::invalid_argument("--resolution wants 1.." + std::to_string(kMaxResolution));
  }
  return static_cast<uint32_t>(value);
}

} // namespace

AppConfig AppConfig::parse(int argc, char **argv) {
  AppConfig config;
  for (int i = 1; i < argc; ++i) {
    const std::string_view flag = argv[i];
    if (flag == "--hidden") {
      config.hidden = true;
      continue;
    }
    if (flag == "--textures") {
      config.customTextures = true;
      continue;
    }
    if (i + 1 >= argc) {
      throw std::invalid_argument(std::string(flag) + " wants a value");
    }
    const std::string value = argv[++i];
    if (flag == "--rom") {
      config.rom = value;
    } else if (flag == "--user") {
      config.userDir = value;
    } else if (flag == "--title") {
      config.title = value;
    } else if (flag == "--app-id") {
      config.appId = value;
    } else if (flag == "--layout") {
      config.layout = parseLayout(value);
    } else if (flag == "--resolution") {
      config.resolution = parseResolution(value);
    } else {
      throw std::invalid_argument("unknown flag " + std::string(flag));
    }
  }
  if (config.rom.empty() || config.userDir.empty()) {
    throw std::invalid_argument(std::string(kUsage));
  }
  return config;
}

} // namespace z3d::app
