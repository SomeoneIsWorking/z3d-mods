// The app's Vulkan instance and the one device and graphics queue the core and the presenter share.
#pragma once

#include <vulkan/vulkan.h>

#include <libretro_vulkan.h>

#include <cstdint>
#include <mutex>
#include <span>
#include <stdexcept>
#include <vector>

namespace z3d::app {

class VulkanError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Throws VulkanError naming `what` unless `result` is VK_SUCCESS.
void check(VkResult result, const char *what);

using Negotiation = retro_hw_render_context_negotiation_interface_vulkan;

class VulkanInstance {
public:
  // `extensions` are the window system's; `negotiation` (may be null) supplies the core's
  // application info.
  VulkanInstance(std::span<const char *const> extensions, const Negotiation *negotiation);
  ~VulkanInstance();
  VulkanInstance(const VulkanInstance &) = delete;
  VulkanInstance &operator=(const VulkanInstance &) = delete;
  VulkanInstance(VulkanInstance &&) = delete;
  VulkanInstance &operator=(VulkanInstance &&) = delete;

  [[nodiscard]] VkInstance handle() const {
    return instance_;
  }

private:
  VkInstance instance_ = VK_NULL_HANDLE;
};

class VulkanDevice {
public:
  // The best GPU with a graphics queue that presents to `surface`. The core creates the device
  // through `negotiation` when it has one, with the swapchain extension added; else the app does.
  VulkanDevice(const VulkanInstance &instance, VkSurfaceKHR surface,
               const Negotiation *negotiation);
  ~VulkanDevice();
  VulkanDevice(const VulkanDevice &) = delete;
  VulkanDevice &operator=(const VulkanDevice &) = delete;
  VulkanDevice(VulkanDevice &&) = delete;
  VulkanDevice &operator=(VulkanDevice &&) = delete;

  [[nodiscard]] VkPhysicalDevice physical() const {
    return physical_;
  }
  [[nodiscard]] VkDevice handle() const {
    return device_;
  }
  [[nodiscard]] VkQueue queue() const {
    return queue_;
  }
  [[nodiscard]] uint32_t queueFamily() const {
    return queueFamily_;
  }

  // The core takes the same lock when it submits (lock_queue).
  void lockQueue() {
    queueLock_.lock();
  }
  void unlockQueue() {
    queueLock_.unlock();
  }
  [[nodiscard]] VkResult submit(const VkSubmitInfo &info, VkFence fence);
  [[nodiscard]] VkResult present(const VkPresentInfoKHR &info);
  void waitIdle();

  // Memory meeting `needs` with all of `properties`; throws VulkanError if none.
  [[nodiscard]] VkDeviceMemory allocate(VkMemoryRequirements needs,
                                        VkMemoryPropertyFlags properties) const;

private:
  bool negotiate(const VulkanInstance &instance, VkSurfaceKHR surface,
                 const Negotiation &negotiation);
  void createOwn();

  std::vector<const char *> extensions_;
  bool negotiated_ = false;
  const Negotiation *negotiation_ = nullptr;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  uint32_t queueFamily_ = 0;
  std::mutex queueLock_;
};

} // namespace z3d::app
