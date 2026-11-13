#include "vulkan_device.hpp"

#include <lucent/log.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>

namespace z3d::app {
namespace {

int preference(VkPhysicalDeviceType type) {
  switch (type) {
  case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
    return 4;
  case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
    return 3;
  case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
    return 2;
  case VK_PHYSICAL_DEVICE_TYPE_CPU:
    return 1;
  default:
    return 0;
  }
}

// A queue family that draws and presents to `surface`.
std::optional<uint32_t> drawingFamily(VkPhysicalDevice device, VkSurfaceKHR surface) {
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
  for (uint32_t family = 0; family < count; ++family) {
    if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
      continue;
    }
    VkBool32 presents = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, family, surface, &presents);
    if (presents == VK_TRUE) {
      return family;
    }
  }
  return std::nullopt;
}

struct Choice {
  VkPhysicalDevice device = VK_NULL_HANDLE;
  uint32_t family = 0;
};

Choice choose(VkInstance instance, VkSurfaceKHR surface) {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance, &count, nullptr);
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance, &count, devices.data());
  Choice best;
  int bestScore = -1;
  for (VkPhysicalDevice device : devices) {
    const auto family = drawingFamily(device, surface);
    if (!family) {
      continue;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    const int score = preference(properties.deviceType);
    if (score > bestScore) {
      best = {device, *family};
      bestScore = score;
    }
  }
  if (best.device == VK_NULL_HANDLE) {
    throw VulkanError("no Vulkan device draws and presents to the window");
  }
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(best.device, &properties);
  lucent::info("vulkan", "{}", properties.deviceName);
  return best;
}

} // namespace

void check(VkResult result, const char *what) {
  if (result != VK_SUCCESS) {
    throw VulkanError(std::string(what) + " failed: VkResult " +
                      std::to_string(static_cast<int>(result)));
  }
}

VulkanInstance::VulkanInstance(std::span<const char *const> extensions,
                               const Negotiation *negotiation) {
  VkApplicationInfo application{};
  application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application.pApplicationName = "z3d";
  application.apiVersion = VK_API_VERSION_1_1;
  if (negotiation != nullptr && negotiation->get_application_info != nullptr) {
    if (const VkApplicationInfo *wanted = negotiation->get_application_info()) {
      application = *wanted;
      application.apiVersion = std::max(application.apiVersion, VK_API_VERSION_1_1);
    }
  }
  VkInstanceCreateInfo create{};
  create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create.pApplicationInfo = &application;
  create.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  create.ppEnabledExtensionNames = extensions.data();
  check(vkCreateInstance(&create, nullptr, &instance_), "vkCreateInstance");
}

VulkanInstance::~VulkanInstance() {
  vkDestroyInstance(instance_, nullptr);
}

VulkanDevice::VulkanDevice(const VulkanInstance &instance, VkSurfaceKHR surface,
                           const Negotiation *negotiation)
    : extensions_{VK_KHR_SWAPCHAIN_EXTENSION_NAME}, negotiation_(negotiation) {
  const Choice choice = choose(instance.handle(), surface);
  physical_ = choice.device;
  queueFamily_ = choice.family;
  if (negotiation != nullptr && negotiate(instance, surface, *negotiation)) {
    negotiated_ = true;
    return;
  }
  createOwn();
}

bool VulkanDevice::negotiate(const VulkanInstance &instance, VkSurfaceKHR surface,
                             const Negotiation &negotiation) {
  if (negotiation.create_device == nullptr) {
    return false;
  }
  retro_vulkan_context context{};
  if (!negotiation.create_device(&context, instance.handle(), physical_, surface,
                                 vkGetInstanceProcAddr, extensions_.data(),
                                 static_cast<unsigned>(extensions_.size()), nullptr, 0, nullptr)) {
    lucent::warn("vulkan", "the core declined to create the device; the app makes it");
    return false;
  }
  if (context.gpu != physical_) {
    lucent::warn("vulkan", "the core made its device on another GPU");
    physical_ = context.gpu;
  }
  device_ = context.device;
  queue_ = context.queue;
  queueFamily_ = context.queue_family_index;
  lucent::info("vulkan", "the core created the device (queue family {})", queueFamily_);
  return true;
}

void VulkanDevice::createOwn() {
  const float priority = 1.0F;
  VkDeviceQueueCreateInfo queueInfo{};
  queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueInfo.queueFamilyIndex = queueFamily_;
  queueInfo.queueCount = 1;
  queueInfo.pQueuePriorities = &priority;
  VkDeviceCreateInfo create{};
  create.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  create.queueCreateInfoCount = 1;
  create.pQueueCreateInfos = &queueInfo;
  create.enabledExtensionCount = static_cast<uint32_t>(extensions_.size());
  create.ppEnabledExtensionNames = extensions_.data();
  check(vkCreateDevice(physical_, &create, nullptr, &device_), "vkCreateDevice");
  vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
}

VulkanDevice::~VulkanDevice() {
  if (device_ == VK_NULL_HANDLE) {
    return;
  }
  waitIdle();
  if (negotiated_ && negotiation_->destroy_device != nullptr) {
    negotiation_->destroy_device();
  }
  vkDestroyDevice(device_, nullptr);
}

VkResult VulkanDevice::submit(const VkSubmitInfo &info, VkFence fence) {
  const std::scoped_lock lock(queueLock_);
  return vkQueueSubmit(queue_, 1, &info, fence);
}

VkResult VulkanDevice::present(const VkPresentInfoKHR &info) {
  const std::scoped_lock lock(queueLock_);
  return vkQueuePresentKHR(queue_, &info);
}

void VulkanDevice::waitIdle() {
  const std::scoped_lock lock(queueLock_);
  vkDeviceWaitIdle(device_);
}

VkDeviceMemory VulkanDevice::allocate(VkMemoryRequirements needs,
                                      VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties memory{};
  vkGetPhysicalDeviceMemoryProperties(physical_, &memory);
  for (uint32_t type = 0; type < memory.memoryTypeCount; ++type) {
    if ((needs.memoryTypeBits & (1U << type)) == 0 ||
        (memory.memoryTypes[type].propertyFlags & properties) != properties) {
      continue;
    }
    VkMemoryAllocateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    info.allocationSize = needs.size;
    info.memoryTypeIndex = type;
    VkDeviceMemory allocated = VK_NULL_HANDLE;
    check(vkAllocateMemory(device_, &info, nullptr, &allocated), "vkAllocateMemory");
    return allocated;
  }
  throw VulkanError("no Vulkan memory type fits");
}

} // namespace z3d::app
