#include "presenter.hpp"

#include <lucent/log.h>

#include <algorithm>
#include <cstring>

namespace z3d::app {
namespace {

constexpr VkImageSubresourceRange kColor{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

struct Transition {
  VkImageLayout from;
  VkImageLayout to;
  VkPipelineStageFlags sourceStage;
  VkAccessFlags sourceAccess;
  VkPipelineStageFlags destinationStage;
  VkAccessFlags destinationAccess;
};

void barrier(VkCommandBuffer commands, VkImage image, const Transition &transition) {
  VkImageMemoryBarrier info{};
  info.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  info.srcAccessMask = transition.sourceAccess;
  info.dstAccessMask = transition.destinationAccess;
  info.oldLayout = transition.from;
  info.newLayout = transition.to;
  info.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  info.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  info.image = image;
  info.subresourceRange = kColor;
  vkCmdPipelineBarrier(commands, transition.sourceStage, transition.destinationStage, 0, 0, nullptr,
                       0, nullptr, 1, &info);
}

// The core's image to a transfer source and back; its own work is done before set_image.
Transition toSource(const CoreImage &image) {
  return {image.layout,
          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
          VK_ACCESS_MEMORY_WRITE_BIT,
          VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_TRANSFER_READ_BIT};
}

Transition fromSource(const CoreImage &image) {
  return {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.layout,
          VK_PIPELINE_STAGE_TRANSFER_BIT,       0,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,   VK_ACCESS_SHADER_READ_BIT};
}

Placement fit(VkExtent2D frame, VkExtent2D window) {
  const float scale =
      std::min(static_cast<float>(window.width) / static_cast<float>(frame.width),
               static_cast<float>(window.height) / static_cast<float>(frame.height));
  const auto width = static_cast<uint32_t>(static_cast<float>(frame.width) * scale);
  const auto height = static_cast<uint32_t>(static_cast<float>(frame.height) * scale);
  return {static_cast<int32_t>((window.width - width) / 2),
          static_cast<int32_t>((window.height - height) / 2), width, height};
}

VkSurfaceFormatKHR chooseFormat(VkPhysicalDevice device, VkSurfaceKHR surface) {
  uint32_t count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, formats.data());
  if (formats.empty()) {
    throw VulkanError("the window offers no surface formats");
  }
  // UNORM: the core's image is already display-referred.
  for (const VkSurfaceFormatKHR &format : formats) {
    if (format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM) {
      return format;
    }
  }
  return formats.front();
}

} // namespace

Presenter::Presenter(VulkanDevice &device, VkSurfaceKHR surface)
    : device_(device), surface_(surface) {
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = device_.queueFamily();
  check(vkCreateCommandPool(device_.handle(), &poolInfo, nullptr, &pool_), "vkCreateCommandPool");
  VkCommandBufferAllocateInfo allocate{};
  allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocate.commandPool = pool_;
  allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocate.commandBufferCount = 1;
  check(vkAllocateCommandBuffers(device_.handle(), &allocate, &commands_),
        "vkAllocateCommandBuffers");
  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  check(vkCreateFence(device_.handle(), &fenceInfo, nullptr, &fence_), "vkCreateFence");
  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  check(vkCreateSemaphore(device_.handle(), &semaphoreInfo, nullptr, &acquired_),
        "vkCreateSemaphore");
}

Presenter::~Presenter() {
  device_.waitIdle();
  destroySwapchain();
  vkDestroySemaphore(device_.handle(), acquired_, nullptr);
  vkDestroyFence(device_.handle(), fence_, nullptr);
  vkDestroyCommandPool(device_.handle(), pool_, nullptr);
}

void Presenter::createSwapchain(VkExtent2D pixels) {
  VkSurfaceCapabilitiesKHR capabilities;
  check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device_.physical(), surface_, &capabilities),
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
  VkExtent2D extent = capabilities.currentExtent;
  if (extent.width == UINT32_MAX) {
    extent = {std::clamp(pixels.width, capabilities.minImageExtent.width,
                         capabilities.maxImageExtent.width),
              std::clamp(pixels.height, capabilities.minImageExtent.height,
                         capabilities.maxImageExtent.height)};
  }
  if (extent.width == 0 || extent.height == 0) {
    return;
  }
  uint32_t count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount != 0) {
    count = std::min(count, capabilities.maxImageCount);
  }
  const VkSurfaceFormatKHR format = chooseFormat(device_.physical(), surface_);
  const VkSwapchainCreateInfoKHR info{
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .pNext = nullptr,
      .flags = 0,
      .surface = surface_,
      .minImageCount = count,
      .imageFormat = format.format,
      .imageColorSpace = format.colorSpace,
      .imageExtent = extent,
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices = nullptr,
      .preTransform = capabilities.currentTransform,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      // FIFO everywhere: the frame loop is paced by the audio clock, not by presenting.
      .presentMode = VK_PRESENT_MODE_FIFO_KHR,
      .clipped = VK_TRUE,
      .oldSwapchain = VK_NULL_HANDLE,
  };
  check(vkCreateSwapchainKHR(device_.handle(), &info, nullptr, &swapchain_),
        "vkCreateSwapchainKHR");
  extent_ = extent;
  uint32_t images = 0;
  vkGetSwapchainImagesKHR(device_.handle(), swapchain_, &images, nullptr);
  images_.resize(images);
  vkGetSwapchainImagesKHR(device_.handle(), swapchain_, &images, images_.data());
  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  drawn_.resize(images);
  for (VkSemaphore &semaphore : drawn_) {
    check(vkCreateSemaphore(device_.handle(), &semaphoreInfo, nullptr, &semaphore),
          "vkCreateSemaphore");
  }
  lucent::info("present", "swapchain {}x{}, {} images", extent.width, extent.height, images);
}

void Presenter::destroySwapchain() {
  for (VkSemaphore semaphore : drawn_) {
    vkDestroySemaphore(device_.handle(), semaphore, nullptr);
  }
  drawn_.clear();
  images_.clear();
  if (swapchain_ != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device_.handle(), swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
}

void Presenter::waitForFrame() {
  check(vkWaitForFences(device_.handle(), 1, &fence_, VK_TRUE, UINT64_MAX), "vkWaitForFences");
}

void Presenter::record(VkCommandBuffer commands, const CoreImage &image, VkExtent2D frame,
                       VkImage target) {
  barrier(commands, target,
          {VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
           VK_ACCESS_TRANSFER_WRITE_BIT});
  const VkClearColorValue black{};
  vkCmdClearColorImage(commands, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &kColor);
  barrier(commands, image.image, toSource(image));
  const Placement placed = fit(frame, extent_);
  placement_ = placed;
  VkImageBlit blit{};
  blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.srcOffsets[1] = {static_cast<int32_t>(frame.width), static_cast<int32_t>(frame.height), 1};
  blit.dstSubresource = blit.srcSubresource;
  blit.dstOffsets[0] = {placed.x, placed.y, 0};
  blit.dstOffsets[1] = {placed.x + static_cast<int32_t>(placed.width),
                        placed.y + static_cast<int32_t>(placed.height), 1};
  vkCmdBlitImage(commands, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
  barrier(commands, image.image, fromSource(image));
  barrier(commands, target,
          {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
           VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
           VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0});
}

void Presenter::show(const CoreImage &image, VkExtent2D frame, VkExtent2D pixels) {
  if (surface_ == VK_NULL_HANDLE || frame.width == 0 || frame.height == 0) {
    return;
  }
  waitForFrame();
  if (swapchain_ != VK_NULL_HANDLE &&
      (pixels.width != extent_.width || pixels.height != extent_.height)) {
    device_.waitIdle();
    destroySwapchain();
  }
  if (swapchain_ == VK_NULL_HANDLE) {
    createSwapchain(pixels);
    if (swapchain_ == VK_NULL_HANDLE) {
      return;
    }
  }
  uint32_t index = 0;
  const VkResult acquired = vkAcquireNextImageKHR(device_.handle(), swapchain_, UINT64_MAX,
                                                  acquired_, VK_NULL_HANDLE, &index);
  if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
    device_.waitIdle();
    destroySwapchain();
    return;
  }
  if (acquired != VK_SUBOPTIMAL_KHR) {
    check(acquired, "vkAcquireNextImageKHR");
  }
  check(vkResetFences(device_.handle(), 1, &fence_), "vkResetFences");
  check(vkResetCommandBuffer(commands_, 0), "vkResetCommandBuffer");
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  check(vkBeginCommandBuffer(commands_, &beginInfo), "vkBeginCommandBuffer");
  record(commands_, image, frame, images_[index]);
  check(vkEndCommandBuffer(commands_), "vkEndCommandBuffer");
  const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &acquired_;
  submit.pWaitDstStageMask = &waitStage;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &commands_;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &drawn_[index];
  check(device_.submit(submit, fence_), "vkQueueSubmit");
  VkPresentInfoKHR present{};
  present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &drawn_[index];
  present.swapchainCount = 1;
  present.pSwapchains = &swapchain_;
  present.pImageIndices = &index;
  const VkResult presented = device_.present(present);
  if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
    device_.waitIdle();
    destroySwapchain();
    return;
  }
  check(presented, "vkQueuePresentKHR");
}

VkCommandBuffer Presenter::begin() {
  waitForFrame();
  check(vkResetFences(device_.handle(), 1, &fence_), "vkResetFences");
  check(vkResetCommandBuffer(commands_, 0), "vkResetCommandBuffer");
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  check(vkBeginCommandBuffer(commands_, &beginInfo), "vkBeginCommandBuffer");
  return commands_;
}

void Presenter::submitAndWait(VkCommandBuffer commands) {
  check(vkEndCommandBuffer(commands), "vkEndCommandBuffer");
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &commands;
  check(device_.submit(submit, fence_), "vkQueueSubmit");
  waitForFrame();
}

std::vector<uint32_t> Presenter::read(const CoreImage &image, VkExtent2D frame) {
  const VkDeviceSize size = static_cast<VkDeviceSize>(frame.width) * frame.height * 4;
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  VkBuffer buffer = VK_NULL_HANDLE;
  check(vkCreateBuffer(device_.handle(), &bufferInfo, nullptr, &buffer), "vkCreateBuffer");
  VkMemoryRequirements needs{};
  vkGetBufferMemoryRequirements(device_.handle(), buffer, &needs);
  VkDeviceMemory memory = device_.allocate(needs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  check(vkBindBufferMemory(device_.handle(), buffer, memory, 0), "vkBindBufferMemory");

  VkCommandBuffer commands = begin();
  barrier(commands, image.image, toSource(image));
  VkBufferImageCopy copy{};
  copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.imageExtent = {frame.width, frame.height, 1};
  vkCmdCopyImageToBuffer(commands, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1,
                         &copy);
  barrier(commands, image.image, fromSource(image));
  submitAndWait(commands);

  std::vector<uint32_t> pixels(static_cast<size_t>(frame.width) * frame.height);
  void *mapped = nullptr;
  check(vkMapMemory(device_.handle(), memory, 0, size, 0, &mapped), "vkMapMemory");
  std::memcpy(pixels.data(), mapped, static_cast<size_t>(size));
  vkUnmapMemory(device_.handle(), memory);
  vkDestroyBuffer(device_.handle(), buffer, nullptr);
  vkFreeMemory(device_.handle(), memory, nullptr);
  const bool rgba =
      image.format == VK_FORMAT_R8G8B8A8_UNORM || image.format == VK_FORMAT_R8G8B8A8_SRGB;
  if (rgba) {
    for (uint32_t &pixel : pixels) {
      pixel = (pixel & 0xFF00FF00U) | ((pixel & 0xFFU) << 16) | ((pixel >> 16) & 0xFFU);
    }
  }
  return pixels;
}

} // namespace z3d::app
