// The app's SDL3 window: Vulkan surface, size in pixels, and the event pump.
#pragma once

#include <vulkan/vulkan.h>

#include <span>
#include <string>

struct SDL_Window;

namespace z3d::app {

class Window {
public:
  // `hidden` is the tools' mode: the window is never mapped and nothing is presented.
  // `appId` names the desktop entry the desktop takes the window's icon from.
  Window(const std::string &title, const std::string &appId, bool hidden);
  ~Window();
  Window(const Window &) = delete;
  Window &operator=(const Window &) = delete;
  Window(Window &&) = delete;
  Window &operator=(Window &&) = delete;

  [[nodiscard]] bool hidden() const {
    return hidden_;
  }
  [[nodiscard]] std::span<const char *const> surfaceExtensions() const;
  [[nodiscard]] VkSurfaceKHR createSurface(VkInstance instance) const;
  static void destroySurface(VkInstance instance, VkSurfaceKHR surface);
  [[nodiscard]] VkExtent2D pixels() const;
  // Window coordinates to swapchain pixels (HiDPI).
  [[nodiscard]] float pixelDensity() const;
  // Sizes the window to `frame`'s shape, as large as the display comfortably allows.
  void fit(VkExtent2D frame);
  [[nodiscard]] SDL_Window *handle() const {
    return window_;
  }

private:
  bool hidden_;
  SDL_Window *window_ = nullptr;
};

} // namespace z3d::app
