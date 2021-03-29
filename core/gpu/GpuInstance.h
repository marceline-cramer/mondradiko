// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "lib/include/vulkan_headers.h"
#include "types/containers/vector.h"

namespace mondradiko {
namespace core {

// Forward declarations
class DisplayInterface;
struct VulkanRequirements;

class GpuInstance {
 public:
  explicit GpuInstance(DisplayInterface*);
  ~GpuInstance();

  bool findFormatFromOptions(const types::vector<VkFormat>*,
                             const types::vector<VkFormat>*, VkFormat*);
  bool findSupportedFormat(const types::vector<VkFormat>*, VkImageTiling,
                           VkFormatFeatureFlags, VkFormat*);
  VkCommandBuffer beginSingleTimeCommands();
  void endSingleTimeCommands(VkCommandBuffer);

  bool enableValidationLayers = true;

  DisplayInterface* display;

  VkInstance instance = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;

  VkPhysicalDeviceProperties physical_device_properties;

  uint32_t graphics_queue_family;
  VkQueue graphics_queue;

  VkCommandPool command_pool = VK_NULL_HANDLE;

  VmaAllocator allocator = nullptr;

 private:
  const types::vector<const char*> validationLayers = {
      "VK_LAYER_KHRONOS_validation"};

  bool checkValidationLayerSupport();
  void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT*);
  void createInstance(VulkanRequirements*);
  void setupDebugMessenger();
  void findPhysicalDevice(DisplayInterface*);
  void findQueueFamilies();
  void createLogicalDevice(VulkanRequirements*);
  void createCommandPool();
  void createAllocator();

  PFN_vkCreateDebugUtilsMessengerEXT ext_vkCreateDebugUtilsMessengerEXT =
      nullptr;
  PFN_vkDestroyDebugUtilsMessengerEXT ext_vkDestroyDebugUtilsMessengerEXT =
      nullptr;
};

}  // namespace core
}  // namespace mondradiko
