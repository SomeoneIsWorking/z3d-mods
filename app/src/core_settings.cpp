#include "core_settings.hpp"

#include "pad_input.hpp"

#include "audio_core/sink_details.h"
#include "common/file_util.h"
#include "common/settings.h"
#include "core/hle/service/service.h"

#include <stdexcept>
#include <string>

namespace z3d::app {
namespace {

std::string userPath(const std::filesystem::path &userDir) {
  std::string path = (userDir / "Azahar").string();
  if (!path.ends_with('/')) {
    path += '/';
  }
  return path;
}

// Frames the interactive loop paces to, as a percentage of the 3DS's own rate.
constexpr double kRealTime = 100.0;

Settings::LayoutOption layoutOption(ScreenLayout layout) {
  switch (layout) {
  case ScreenLayout::Default:
    return Settings::LayoutOption::Default;
  case ScreenLayout::SingleScreen:
    return Settings::LayoutOption::SingleScreen;
  case ScreenLayout::LargeScreen:
    return Settings::LayoutOption::LargeScreen;
  case ScreenLayout::SideBySide:
    return Settings::LayoutOption::SideScreen;
  }
  throw std::invalid_argument("unknown screen layout");
}

} // namespace

void applyCoreSettings(const AppConfig &config) {
  FileUtil::SetUserPath(userPath(config.userDir));
  auto &values = Settings::values;
  values.graphics_api = Settings::GraphicsAPI::Vulkan;
  values.resolution_factor = config.resolution;
  values.dump_textures = config.dumpTextures;
  values.frame_limit = config.hidden ? 0.0 : kRealTime;
  values.use_vsync = !config.hidden;
  values.output_type = config.hidden ? AudioCore::SinkType::Null : AudioCore::SinkType::Auto;
  values.enable_audio_stretching = false;
  values.current_input_profile = PadInput::profile();
  // Every system module is HLE; the core looks each one up by name.
  for (const auto &module : Service::service_module_map) {
    values.lle_modules[module.name] = false;
  }
}

void applyGameSettings(const GameSetup &game) {
  Settings::values.layout_option = layoutOption(game.layout);
  Settings::values.custom_textures = game.customTextures;
}

} // namespace z3d::app
