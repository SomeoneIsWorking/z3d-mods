#include "game_window.hpp"

#include "pad_input.hpp"
#include "screen_overlay.hpp"

#include "common/settings.h"
#include "core/3ds.h"
#include "core/frontend/framebuffer_layout.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>

namespace z3d::app {
namespace {

std::runtime_error sdlError(const char *what) {
  return std::runtime_error(std::string(what) + ": " + SDL_GetError());
}

// The first window takes this much of the display, in the frame's shape.
constexpr float kDisplayShare = 0.85F;

Frontend::EmuWindow::WindowSystemInfo nativeSurface(SDL_Window *window) {
  Frontend::EmuWindow::WindowSystemInfo info;
  const SDL_PropertiesID props = SDL_GetWindowProperties(window);
  const std::string_view driver = SDL_GetCurrentVideoDriver();
  if (driver == "wayland") {
    info.type = Frontend::WindowSystemType::Wayland;
    info.display_connection =
        SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
    info.render_surface =
        SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
  } else if (driver == "x11") {
    info.type = Frontend::WindowSystemType::X11;
    info.display_connection =
        SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    // The core takes an X11 window id in its surface pointer.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    info.render_surface = reinterpret_cast<void *>(
        static_cast<uintptr_t>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)));
  } else {
    throw std::runtime_error("no Vulkan surface for SDL video driver " + std::string(driver));
  }
  if (info.display_connection == nullptr || info.render_surface == nullptr) {
    throw std::runtime_error("SDL window has no native surface");
  }
  info.render_surface_scale = SDL_GetWindowPixelDensity(window);
  return info;
}

} // namespace

GameWindow::GameWindow(const AppConfig &config, PadInput &pad) : pad_(pad), hidden_(config.hidden) {
  SDL_SetHint(SDL_HINT_APP_ID, config.appId.c_str());
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    throw sdlError("SDL_Init");
  }
  const Layout::FramebufferLayout shape =
      Layout::FrameLayoutFromResolutionScale(hidden_ ? config.resolution : 1);
  SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  flags |= hidden_ ? SDL_WINDOW_HIDDEN : SDL_WINDOW_RESIZABLE;
  window_ = SDL_CreateWindow(config.title.c_str(), static_cast<int>(shape.width),
                             static_cast<int>(shape.height), flags);
  if (window_ == nullptr) {
    SDL_Quit();
    throw sdlError("SDL_CreateWindow");
  }
  if (!hidden_) {
    fitToDisplay();
  }
  window_info = nativeSurface(window_);
  updateLayout();
}

GameWindow::~GameWindow() {
  SDL_DestroyWindow(window_);
  SDL_Quit();
}

void GameWindow::PollEvents() {
  ++frames_;
  pump(true);
}

std::vector<SDL_FPoint> GameWindow::pollMenu() {
  clicks_.clear();
  pump(false);
  return std::exchange(clicks_, {});
}

void GameWindow::placeOverlay(OverlayPlacement placement) {
  overlay_ = placement;
  updateLayout();
}

void GameWindow::boot(const std::string &title) {
  overlay_ = OverlayPlacement::Split;
  SDL_SetWindowTitle(window_, title.c_str());
  if (!hidden_) {
    fitToDisplay();
  }
  updateLayout();
}

void GameWindow::pump(bool inGame) {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      closeRequested_ = true;
      break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      updateLayout();
      break;
    case SDL_EVENT_KEY_DOWN:
      if (event.key.scancode == SDL_SCANCODE_ESCAPE && !event.key.repeat) {
        menuRequested_ = true;
      }
      break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      if (event.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE) {
        menuRequested_ = true;
      }
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      if (event.button.button == SDL_BUTTON_LEFT) {
        if (inGame) {
          touch({event.button.x, event.button.y}, true);
        } else {
          const float density = SDL_GetWindowPixelDensity(window_);
          clicks_.push_back({event.button.x * density, event.button.y * density});
        }
      }
      break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (event.button.button == SDL_BUTTON_LEFT && touching_) {
        touch({event.button.x, event.button.y}, false);
      }
      break;
    case SDL_EVENT_MOUSE_MOTION:
      if (touching_) {
        touch({event.motion.x, event.motion.y}, true);
      }
      break;
    default:
      pad_.handle(event);
      break;
    }
  }
  if (!hidden_) {
    pad_.readHost();
  }
}

void GameWindow::fitToDisplay() {
  const Layout::FramebufferLayout shape = Layout::FrameLayoutFromResolutionScale(1);
  SDL_Rect usable{};
  if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window_), &usable)) {
    return;
  }
  const float scale =
      std::min(static_cast<float>(usable.w) * kDisplayShare / static_cast<float>(shape.width),
               static_cast<float>(usable.h) * kDisplayShare / static_cast<float>(shape.height));
  SDL_SetWindowSize(window_, static_cast<int>(static_cast<float>(shape.width) * scale),
                    static_cast<int>(static_cast<float>(shape.height) * scale));
  SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

void GameWindow::updateLayout() {
  int width = 0;
  int height = 0;
  SDL_GetWindowSizeInPixels(window_, &width, &height);
  if (Settings::values.layout_option.GetValue() == Settings::LayoutOption::CustomLayout) {
    app::placeOverlay(static_cast<unsigned>(std::max(width, 1)), static_cast<unsigned>(std::max(height, 1)),
                      overlay_);
  }
  UpdateCurrentFramebufferLayout(static_cast<unsigned>(std::max(width, 1)),
                                 static_cast<unsigned>(std::max(height, 1)));
}

void GameWindow::touchScreen(std::optional<TouchPoint> at) {
  if (!at) {
    TouchReleased();
    touching_ = false;
    return;
  }
  const auto &screen = GetFramebufferLayout().bottom_screen;
  // The middle of the touched pixel.
  const auto px = screen.left + (static_cast<unsigned>(at->x) * 2 + 1) * screen.GetWidth() / (Core::kScreenBottomWidth * 2);
  const auto py = screen.top + (static_cast<unsigned>(at->y) * 2 + 1) * screen.GetHeight() / (Core::kScreenBottomHeight * 2);
  if (touching_) {
    TouchMoved(px, py);
  } else {
    touching_ = TouchPressed(px, py);
  }
}

void GameWindow::touch(SDL_FPoint at, bool pressed) {
  const float density = SDL_GetWindowPixelDensity(window_);
  const auto px = static_cast<unsigned>(std::max(at.x * density, 0.0F));
  const auto py = static_cast<unsigned>(std::max(at.y * density, 0.0F));
  if (!pressed) {
    TouchReleased();
    touching_ = false;
  } else if (touching_) {
    TouchMoved(px, py);
  } else {
    touching_ = TouchPressed(px, py);
  }
}

} // namespace z3d::app
