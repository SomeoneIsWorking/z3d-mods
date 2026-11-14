#include "folder_setup.hpp"

#include "folder_browser.hpp"
#include "game_window.hpp"
#include "menu_canvas.hpp"
#include "tool_process.hpp"

#include <SDL3/SDL.h>
#include <lucent/log.h>
#include <setup_ui/setup_ui.h>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace z3d::app {
namespace {

struct Ask {
  // The tools' flag that takes the folder (tools/z3dmods/packed.py `folders`).
  const char *flag;
  const char *message;
  const char *label;
  const char *hint;
  const char *browserHeading;
};

constexpr const char *kTitle = "z3d";
constexpr const char *kBrowse = "Choose the folder";
constexpr const char *kAgain = "Choose another folder";
constexpr const char *kFooter = "A or Enter: choose the folder";
constexpr std::array kAsks{
    Ask{"--roms", "Where are your 3DS games?", "The folder with your game dumps (.3ds or .cci)",
        "z3d plays Ocarina of Time 3D and Majora's Mask 3D from your own dumps.",
        "Your games folder"},
    Ask{"--mods", "Where are your mod downloads?", "The folder with your mod downloads",
        "Setting a game up reads its mods from here and names any that is missing. "
        "A .zip or .tar left there is unpacked for you.",
        "Your mods folder"},
};

// Where the browser opens.
std::filesystem::path home() {
  const char *folder = SDL_GetUserFolder(SDL_FOLDER_HOME);
  if (folder == nullptr) {
    return "/";
  }
  // SDL's folder ends in a separator.
  const std::filesystem::path path = std::filesystem::path(folder).lexically_normal();
  return path.has_filename() ? path : path.parent_path();
}

// False when the window closes before the tools take a folder.
bool ask(const Ask &what, GameWindow &window, const PadInput &pad, const AppConfig &config) {
  const MenuCanvas canvas(window, config.font);
  setup_ui::Config screen;
  screen.title = kTitle;
  screen.message = what.message;
  screen.hint = what.hint;
  screen.footer = kFooter;
  screen.files = {setup_ui::FileSpec{what.flag, "", what.label}};
  screen.placement = setup_ui::Placement::Adopt;
  screen.browse_label = kBrowse;
  screen.again_label = kAgain;
  setup_ui::Session session(
      screen, setup_ui::SessionOptions{}, [&](const std::vector<setup_ui::StagedFile> &chosen) {
        std::vector<std::string> command = config.tools;
        command.insert(command.end(), {"folders", "--user", config.userDir.string(), what.flag,
                                       chosen.front().path.string()});
        ToolProcess tool;
        tool.start(command);
        tool.wait();
        if (tool.exitCode() == 0) {
          return std::string();
        }
        return tool.failure().empty() ? "The tools ended with " + std::to_string(tool.exitCode())
                                      : tool.failure();
      });
  setup_ui::ViewOptions options;
  options.font_path = config.font.string();
  options.host_renderer = canvas.renderer();
  setup_ui::View view(session, options);
  if (!view.open()) {
    throw std::runtime_error("the setup screen: " + view.last_error());
  }
  lucent::info("menu", "folders: asking {}", what.flag);
  while (!window.closeRequested()) {
    (void)window.pollMenu([&view](const SDL_Event &event) {
      view.handle_event(event);
    });
    for (const setup_ui::Request &request : view.poll()) {
      if (request.kind != setup_ui::RequestKind::Browse) {
        continue;
      }
      const auto folder = FolderBrowser(canvas, window, pad, what.browserHeading, home()).run();
      if (folder) {
        std::string refused;
        session.add_selected({*folder}, refused);
      }
      // What was pressed in the browser is not for the screen.
      (void)view.poll();
      break;
    }
    const setup_ui::Status before = session.status();
    session.validate_if_ready();
    if (session.status() == setup_ui::Status::Rejected && before != setup_ui::Status::Rejected) {
      lucent::info("menu", "folders: refused {}", what.flag);
    }
    view.frame();
    if (session.status() == setup_ui::Status::Accepted) {
      lucent::info("menu", "folders: took {}", what.flag);
      return true;
    }
  }
  return false;
}

} // namespace

bool FolderSetup::run() {
  return std::ranges::all_of(kAsks, [this](const Ask &what) {
    return ask(what, window_, pad_, config_);
  });
}

} // namespace z3d::app
