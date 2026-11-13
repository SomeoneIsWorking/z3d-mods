#include "core_video.hpp"

#include "../window.hpp"

#include <lucent/log.h>

namespace z3d::app {
namespace {

CoreVideo &of(void *self) {
  return *static_cast<CoreVideo *>(self);
}

} // namespace

CoreVideo::~CoreVideo() {
  destroyContext();
  presenter_.reset();
  device_.reset();
  if (surface_ != VK_NULL_HANDLE) {
    Window::destroySurface(instance_->handle(), surface_);
  }
}

bool CoreVideo::setRender(const retro_hw_render_callback &callback) {
  if (callback.context_type != RETRO_HW_CONTEXT_VULKAN) {
    lucent::error("video", "the core asked for hardware context {}; the app draws with Vulkan only",
                  static_cast<int>(callback.context_type));
    return false;
  }
  callback_ = callback;
  requested_ = true;
  return true;
}

bool CoreVideo::setNegotiation(const retro_hw_render_context_negotiation_interface &negotiation) {
  if (negotiation.interface_type != RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN) {
    return false;
  }
  negotiation_ = reinterpret_cast<const Negotiation *>(&negotiation);
  return true;
}

bool CoreVideo::renderInterface(const retro_hw_render_interface **out) const {
  if (!contextUp_) {
    return false;
  }
  *out = reinterpret_cast<const retro_hw_render_interface *>(&interface_);
  return true;
}

void CoreVideo::start(const Window &window) {
  if (!requested_) {
    throw VulkanError("the core did not ask for a Vulkan context (set citra_graphics_api=Vulkan)");
  }
  instance_ = std::make_unique<VulkanInstance>(window.surfaceExtensions(), negotiation_);
  surface_ = window.createSurface(instance_->handle());
  device_ = std::make_unique<VulkanDevice>(*instance_, surface_, negotiation_);
  presenter_ = std::make_unique<Presenter>(*device_, window.hidden() ? VK_NULL_HANDLE : surface_);
  interface_ = retro_hw_render_interface_vulkan{
      .interface_type = RETRO_HW_RENDER_INTERFACE_VULKAN,
      .interface_version = RETRO_HW_RENDER_INTERFACE_VULKAN_VERSION,
      .handle = this,
      .instance = instance_->handle(),
      .gpu = device_->physical(),
      .device = device_->handle(),
      .get_device_proc_addr = vkGetDeviceProcAddr,
      .get_instance_proc_addr = vkGetInstanceProcAddr,
      .queue = device_->queue(),
      .queue_index = device_->queueFamily(),
      .set_image = setImage,
      .get_sync_index = syncIndex,
      .get_sync_index_mask = syncIndexMask,
      .set_command_buffers = setCommandBuffers,
      .wait_sync_index = waitSyncIndex,
      .lock_queue = lockQueue,
      .unlock_queue = unlockQueue,
      .set_signal_semaphore = setSignalSemaphore,
  };
  contextUp_ = true;
  if (callback_.context_reset != nullptr) {
    callback_.context_reset();
  }
}

void CoreVideo::destroyContext() {
  if (!contextUp_) {
    return;
  }
  device_->waitIdle();
  if (callback_.context_destroy != nullptr) {
    callback_.context_destroy();
  }
  contextUp_ = false;
  image_.reset();
}

void CoreVideo::frameReady(unsigned width, unsigned height) {
  frame_ = {width, height};
}

void CoreVideo::show(VkExtent2D pixels) {
  if (image_) {
    presenter_->show(*image_, frame_, pixels);
  }
}

std::vector<uint32_t> CoreVideo::read() const {
  if (!image_ || frame_.width == 0) {
    return {};
  }
  return presenter_->read(*image_, frame_);
}

const std::optional<Placement> &CoreVideo::placement() const {
  return presenter_->placement();
}

void CoreVideo::setImage(void *self, const retro_vulkan_image *image, uint32_t count,
                         const VkSemaphore * /*semaphores*/, uint32_t /*queueFamily*/) {
  CoreVideo &video = of(self);
  if (image == nullptr) {
    video.image_.reset();
    return;
  }
  if (count != 0) {
    lucent::warn("video", "the core passed {} semaphores with its image; they are not waited",
                 count);
  }
  video.image_ =
      CoreImage{image->create_info.image, image->create_info.format, image->image_layout};
}

// One frame in flight: the presenter waits for the last one before drawing the next.
uint32_t CoreVideo::syncIndex(void * /*self*/) {
  return 0;
}

uint32_t CoreVideo::syncIndexMask(void * /*self*/) {
  return 1;
}

void CoreVideo::setCommandBuffers(void * /*self*/, uint32_t count,
                                  const VkCommandBuffer * /*buffers*/) {
  lucent::warn("video", "the core handed over {} command buffers; this frontend does not run them",
               count);
}

void CoreVideo::waitSyncIndex(void *self) {
  of(self).presenter_->waitForFrame();
}

void CoreVideo::lockQueue(void *self) {
  of(self).device_->lockQueue();
}

void CoreVideo::unlockQueue(void *self) {
  of(self).device_->unlockQueue();
}

void CoreVideo::setSignalSemaphore(void * /*self*/, VkSemaphore /*semaphore*/) {
  lucent::warn("video", "the core asked for a signal semaphore; this frontend does not signal it");
}

} // namespace z3d::app
