// Puts the core's image in the window (letterboxed blit into a swapchain) and reads it back for
// screenshots. One frame in flight.
#pragma once

#include "vulkan_device.hpp"

#include <optional>

namespace z3d::app {

// The image the core last handed over (set_image).
struct CoreImage {
  VkImage image = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
};

// Where the frame lies in the window, in the swapchain's pixels.
struct Placement {
  int32_t x = 0;
  int32_t y = 0;
  uint32_t width = 0;
  uint32_t height = 0;
};

class Presenter {
public:
  // A null `surface` (the tools' hidden window) presents nothing.
  Presenter(VulkanDevice &device, VkSurfaceKHR surface);
  ~Presenter();
  Presenter(const Presenter &) = delete;
  Presenter &operator=(const Presenter &) = delete;
  Presenter(Presenter &&) = delete;
  Presenter &operator=(Presenter &&) = delete;

  // Draws `frame` pixels of `image` fitted into a window of `pixels`, and presents it.
  void show(const CoreImage &image, VkExtent2D frame, VkExtent2D pixels);
  // The frame as XRGB8888 rows, top row first.
  [[nodiscard]] std::vector<uint32_t> read(const CoreImage &image, VkExtent2D frame);
  // Blocks until the last submitted frame is done (wait_sync_index).
  void waitForFrame();

  [[nodiscard]] const std::optional<Placement> &placement() const {
    return placement_;
  }

private:
  void createSwapchain(VkExtent2D pixels);
  void destroySwapchain();
  void record(VkCommandBuffer commands, const CoreImage &image, VkExtent2D frame, VkImage target);
  [[nodiscard]] VkCommandBuffer begin();
  void submitAndWait(VkCommandBuffer commands);

  VulkanDevice &device_;
  VkSurfaceKHR surface_;
  VkCommandPool pool_ = VK_NULL_HANDLE;
  VkCommandBuffer commands_ = VK_NULL_HANDLE;
  VkFence fence_ = VK_NULL_HANDLE;
  VkSemaphore acquired_ = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkExtent2D extent_{};
  std::vector<VkImage> images_;
  // One per swapchain image: an image's semaphore is free again once that image is reacquired.
  std::vector<VkSemaphore> drawn_;
  std::optional<Placement> placement_;
};

} // namespace z3d::app
