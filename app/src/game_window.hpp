// The app's SDL3 window as the core's EmuWindow: the native surface the Vulkan renderer presents
// to, the event pump the renderer runs once per emulated frame, and touch from the mouse. Between
// games the title picker draws on it and pumps it with pollMenu().
#pragma once

#include "app_config.hpp"
#include "screen_overlay.hpp"

#include "core/frontend/emu_window.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct SDL_Window;
struct SDL_FPoint;

namespace z3d::app {

class PadInput;

// A pixel of the 320x240 touch screen.
struct TouchPoint {
  uint16_t x = 0;
  uint16_t y = 0;
};

class GameWindow final : public Frontend::EmuWindow {
public:
  // Hidden windows are never mapped; their size is the screenshot layout at the render scale.
  GameWindow(const AppConfig &config, PadInput &pad);
  ~GameWindow() override;
  GameWindow(const GameWindow &) = delete;
  GameWindow &operator=(const GameWindow &) = delete;
  GameWindow(GameWindow &&) = delete;
  GameWindow &operator=(GameWindow &&) = delete;

  // Called by the renderer at the end of every emulated frame.
  void PollEvents() override;
  // The picker's pump: no touch; left clicks come back in window pixels.
  [[nodiscard]] std::vector<SDL_FPoint> pollMenu();
  // A game's title on the window and the window in its layout's shape (core_settings.hpp).
  void boot(const std::string &title);

  // Where the overlay layout puts the touch screen from now on.
  void placeOverlay(OverlayPlacement placement);

  // A stylus held at a point of the 320x240 touch screen, wherever the layout draws it, or lifted.
  void touchScreen(std::optional<TouchPoint> at);

  [[nodiscard]] SDL_Window *sdlWindow() const {
    return window_;
  }
  // Escape or the gamepad's guide button since the last call.
  [[nodiscard]] bool takeMenuRequest() {
    return std::exchange(menuRequested_, false);
  }

  [[nodiscard]] uint64_t framesShown() const {
    return frames_;
  }
  [[nodiscard]] bool closeRequested() const {
    return closeRequested_;
  }

private:
  void pump(bool inGame);
  void fitToDisplay();
  void updateLayout();
  // Window coordinates to framebuffer pixels.
  void touch(SDL_FPoint at, bool pressed);

  PadInput &pad_;
  bool hidden_;
  SDL_Window *window_ = nullptr;
  uint64_t frames_ = 0;
  bool closeRequested_ = false;
  bool menuRequested_ = false;
  bool touching_ = false;
  OverlayPlacement overlay_ = OverlayPlacement::Split;
  std::vector<SDL_FPoint> clicks_;
};

} // namespace z3d::app
