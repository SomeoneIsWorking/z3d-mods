// The core's Vulkan side of libretro: its render callback and negotiation, the device it draws on,
// the render interface it calls (set_image, sync, queue lock), and the image it hands over.
#pragma once

#include "presenter.hpp"
#include "vulkan_device.hpp"

#include <libretro.h>

#include <memory>

namespace z3d::app {

class Window;

class CoreVideo {
public:
  CoreVideo() = default;
  ~CoreVideo();
  CoreVideo(const CoreVideo &) = delete;
  CoreVideo &operator=(const CoreVideo &) = delete;
  CoreVideo(CoreVideo &&) = delete;
  CoreVideo &operator=(CoreVideo &&) = delete;

  // RETRO_ENVIRONMENT_SET_HW_RENDER; false for anything but Vulkan.
  bool setRender(const retro_hw_render_callback &callback);
  // RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE.
  bool setNegotiation(const retro_hw_render_context_negotiation_interface &negotiation);
  // RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE; false until started.
  bool renderInterface(const retro_hw_render_interface **out) const;

  // After retro_load_game: the device on `window`'s surface, then the core's context_reset.
  void start(const Window &window);
  // Before retro_unload_game; the device stays up for the unload.
  void destroyContext();

  // video_refresh with RETRO_HW_FRAME_BUFFER_VALID.
  void frameReady(unsigned width, unsigned height);
  void show(VkExtent2D pixels);
  // The last frame as XRGB8888 rows, top row first; empty before the first frame.
  [[nodiscard]] std::vector<uint32_t> read() const;
  [[nodiscard]] VkExtent2D frame() const {
    return frame_;
  }
  [[nodiscard]] const std::optional<Placement> &placement() const;

private:
  static void setImage(void *self, const retro_vulkan_image *image, uint32_t count,
                       const VkSemaphore *semaphores, uint32_t queueFamily);
  static uint32_t syncIndex(void *self);
  static uint32_t syncIndexMask(void *self);
  static void setCommandBuffers(void *self, uint32_t count, const VkCommandBuffer *buffers);
  static void waitSyncIndex(void *self);
  static void lockQueue(void *self);
  static void unlockQueue(void *self);
  static void setSignalSemaphore(void *self, VkSemaphore semaphore);

  retro_hw_render_callback callback_{};
  bool requested_ = false;
  const Negotiation *negotiation_ = nullptr;
  std::unique_ptr<VulkanInstance> instance_;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  std::unique_ptr<VulkanDevice> device_;
  std::unique_ptr<Presenter> presenter_;
  retro_hw_render_interface_vulkan interface_{};
  bool contextUp_ = false;
  std::optional<CoreImage> image_;
  VkExtent2D frame_{};
};

} // namespace z3d::app
