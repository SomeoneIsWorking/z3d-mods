#include "window.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <stdexcept>

namespace z3d::app {
namespace {

std::runtime_error sdlError(const char *what) {
  return std::runtime_error(std::string(what) + ": " + SDL_GetError());
}

constexpr int kStartWidth = 800;
constexpr int kStartHeight = 960;
// The first window takes this much of the display, in the frame's shape.
constexpr float kDisplayShare = 0.85F;

} // namespace

Window::Window(const std::string &title, const std::string &appId, bool hidden) : hidden_(hidden) {
  SDL_SetHint(SDL_HINT_APP_ID, appId.c_str());
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    throw sdlError("SDL_Init");
  }
  SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
  if (hidden_) {
    flags |= SDL_WINDOW_HIDDEN;
  }
  window_ = SDL_CreateWindow(title.c_str(), kStartWidth, kStartHeight, flags);
  if (window_ == nullptr) {
    SDL_Quit();
    throw sdlError("SDL_CreateWindow");
  }
}

Window::~Window() {
  SDL_DestroyWindow(window_);
  SDL_Quit();
}

std::span<const char *const> Window::surfaceExtensions() const {
  Uint32 count = 0;
  const char *const *names = SDL_Vulkan_GetInstanceExtensions(&count);
  if (names == nullptr) {
    throw sdlError("SDL_Vulkan_GetInstanceExtensions");
  }
  return {names, count};
}

VkSurfaceKHR Window::createSurface(VkInstance instance) const {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!SDL_Vulkan_CreateSurface(window_, instance, nullptr, &surface)) {
    throw sdlError("SDL_Vulkan_CreateSurface");
  }
  return surface;
}

void Window::destroySurface(VkInstance instance, VkSurfaceKHR surface) {
  SDL_Vulkan_DestroySurface(instance, surface, nullptr);
}

VkExtent2D Window::pixels() const {
  int width = 0;
  int height = 0;
  SDL_GetWindowSizeInPixels(window_, &width, &height);
  return {static_cast<uint32_t>(std::max(width, 0)), static_cast<uint32_t>(std::max(height, 0))};
}

float Window::pixelDensity() const {
  return SDL_GetWindowPixelDensity(window_);
}

void Window::fit(VkExtent2D frame) {
  SDL_Rect usable{};
  const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
  if (!SDL_GetDisplayUsableBounds(display, &usable)) {
    usable = {0, 0, kStartWidth, kStartHeight};
  }
  const float scale =
      std::min(static_cast<float>(usable.w) * kDisplayShare / static_cast<float>(frame.width),
               static_cast<float>(usable.h) * kDisplayShare / static_cast<float>(frame.height));
  SDL_SetWindowSize(window_, static_cast<int>(static_cast<float>(frame.width) * scale),
                    static_cast<int>(static_cast<float>(frame.height) * scale));
  SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

} // namespace z3d::app
