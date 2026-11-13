// The app's SDL3 window as the core's EmuWindow: the native surface the Vulkan renderer presents
// to, the event pump the renderer runs once per emulated frame, and touch from the mouse.
#pragma once

#include "app_config.hpp"

#include "core/frontend/emu_window.h"

#include <cstdint>

struct SDL_Window;
struct SDL_FPoint;

namespace z3d::app {

class PadInput;

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

  [[nodiscard]] uint64_t framesShown() const {
    return frames_;
  }
  [[nodiscard]] bool closeRequested() const {
    return closeRequested_;
  }

private:
  void fitToDisplay();
  void updateLayout();
  // Window coordinates to framebuffer pixels.
  void touch(SDL_FPoint at, bool pressed);

  PadInput &pad_;
  bool hidden_;
  SDL_Window *window_ = nullptr;
  uint64_t frames_ = 0;
  bool closeRequested_ = false;
  bool touching_ = false;
};

} // namespace z3d::app
