/**
 * @file VulkanInstance.cc
 * @author Marceline Cramer (cramermarceline@gmail.com)
 * @brief Manages all low-level Vulkan objects such as device, debug messenger,
 * VMA allocator, etc.
 * @date 2020-10-24
 *
 * @copyright Copyright (c) 2020 Marceline Cramer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https: //www.gnu.org/licenses/>.
 *
 */

#include "graphics/VulkanInstance.h"

#include <cstring>
#include <set>

#include "./build_config.h"
#include "log/log.h"
#include "xr/XrDisplay.h"

static VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT messageType,
              const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
              void* pUserData) {
  Log::LogLevel severity;

  switch (messageSeverity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
      severity = Log::LOG_LEVEL_INFO;
      break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
      severity = Log::LOG_LEVEL_WARNING;
      break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
    default:
      severity = Log::LOG_LEVEL_ERROR;
      break;
  }

  // Add "../src/" so that it's clipped off in logging
  Log::log("../src/VulkanValidation", 0, severity, pCallbackData->pMessage);
  return VK_FALSE;
}

VulkanInstance::VulkanInstance(XrDisplay* display) {
  log_dbg("Initializing Vulkan.");

  VulkanRequirements requirements;

  display->getRequirements(&requirements);

  if (enableValidationLayers) {
    if (!checkValidationLayerSupport()) {
      log_wrn("Vulkan validation layers requested, but not available.");
      enableValidationLayers = false;
    }
  }

  createInstance(&requirements);

  if (enableValidationLayers) {
    setupDebugMessenger();
  }

  findPhysicalDevice(display);
  findQueueFamilies();
  createLogicalDevice(&requirements);
  createCommandPool();
  createAllocator();
  createDescriptorPool();
}

VulkanInstance::~VulkanInstance() {
  log_dbg("Cleaning up Vulkan.");

  if (descriptorPool != VK_NULL_HANDLE)
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);

  if (allocator != nullptr) vmaDestroyAllocator(allocator);

  if (commandPool != VK_NULL_HANDLE)
    vkDestroyCommandPool(device, commandPool, nullptr);

  if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);

  if (enableValidationLayers && debugMessenger != VK_NULL_HANDLE)
    ext_vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);

  if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
}

bool VulkanInstance::findFormatFromOptions(
    const std::vector<VkFormat>* options,
    const std::vector<VkFormat>* candidates, VkFormat* selected) {
  for (auto candidate : *candidates) {
    for (auto option : *options) {
      if (candidate == option) {
        *selected = option;
        return true;
      }
    }
  }

  return false;
}

bool VulkanInstance::checkValidationLayerSupport() {
  log_dbg("Checking for Vulkan validation layer support.");

  uint32_t layerCount;
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

  std::vector<VkLayerProperties> availableLayers(layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

  for (const char* layerName : validationLayers) {
    bool layerFound = false;

    for (const auto& layerProperties : availableLayers) {
      if (strcmp(layerName, layerProperties.layerName) == 0) {
        layerFound = true;
        break;
      }
    }

    if (!layerFound) {
      return false;
    }
  }

  return true;
}

void VulkanInstance::populateDebugMessengerCreateInfo(
    VkDebugUtilsMessengerCreateInfoEXT* createInfo) {
  *createInfo = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = debugCallback};
}

void VulkanInstance::createInstance(VulkanRequirements* requirements) {
  log_dbg("Creating Vulkan instance.");

  std::vector<const char*> extensionNames;
  for (uint32_t i = 0; i < requirements->instanceExtensions.size(); i++) {
    extensionNames.push_back(requirements->instanceExtensions[i].c_str());
  }

  extensionNames.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

  VkApplicationInfo appInfo{.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                            .pApplicationName = "Mondradiko",
                            .applicationVersion = VK_MAKE_VERSION(0, 0, 0),
                            .pEngineName = MONDRADIKO_NAME,
                            .engineVersion = MONDRADIKO_VULKAN_VERSION,
                            .apiVersion = requirements->minApiVersion};

  VkInstanceCreateInfo createInfo{
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &appInfo,
      .enabledExtensionCount = static_cast<uint32_t>(extensionNames.size()),
      .ppEnabledExtensionNames = extensionNames.data()};

  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo;
  if (enableValidationLayers) {
    createInfo.enabledLayerCount =
        static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();

    populateDebugMessengerCreateInfo(&debugCreateInfo);
    createInfo.pNext =
        reinterpret_cast<VkInstanceCreateInfo*>(&debugCreateInfo);
  }

  if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
    log_ftl("Failed to create Vulkan instance.");
  }

  ext_vkCreateDebugUtilsMessengerEXT =
      (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
          instance, "vkCreateDebugUtilsMessengerEXT");
  ext_vkDestroyDebugUtilsMessengerEXT =
      (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
          instance, "vkDestroyDebugUtilsMessengerEXT");
}

void VulkanInstance::setupDebugMessenger() {
  log_dbg("Setting up Vulkan debug messenger.");

  VkDebugUtilsMessengerCreateInfoEXT createInfo;
  populateDebugMessengerCreateInfo(&createInfo);

  if (ext_vkCreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr,
                                         &debugMessenger) != VK_SUCCESS) {
    log_ftl("Failed to create Vulkan debug messenger.");
  }
}

void VulkanInstance::findPhysicalDevice(XrDisplay* display) {
  log_dbg("Finding Vulkan physical device.");

  display->getVulkanDevice(instance, &physicalDevice);
}

void VulkanInstance::findQueueFamilies() {
  log_dbg("Finding Vulkan queue families.");

  uint32_t queueFamilyCount;
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                           nullptr);
  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount,
                                           queueFamilies.data());

  for (uint32_t i = 0; i < queueFamilyCount; i++) {
    if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      graphicsQueueFamily = i;
      break;
    }
  }
}

void VulkanInstance::createLogicalDevice(VulkanRequirements* requirements) {
  log_dbg("Creating Vulkan logical device.");

  std::vector<const char*> extensionNames;
  for (uint32_t i = 0; i < requirements->deviceExtensions.size(); i++) {
    extensionNames.push_back(requirements->deviceExtensions[i].c_str());
  }

  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  std::set<uint32_t> uniqueQueueFamilies = {graphicsQueueFamily};

  float queuePriority = 1.0f;
  for (uint32_t queueFamily : uniqueQueueFamilies) {
    queueCreateInfos.push_back(
        {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
         .queueFamilyIndex = queueFamily,
         .queueCount = 1,
         .pQueuePriorities = &queuePriority});
  }

  VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
      .runtimeDescriptorArray = VK_TRUE};

  VkPhysicalDeviceFeatures deviceFeatures{.multiViewport = VK_TRUE,
                                          .samplerAnisotropy = VK_TRUE};

  VkDeviceCreateInfo createInfo{
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &indexingFeatures,
      .queueCreateInfoCount = (uint32_t)queueCreateInfos.size(),
      .pQueueCreateInfos = queueCreateInfos.data(),
      .enabledExtensionCount = static_cast<uint32_t>(extensionNames.size()),
      .ppEnabledExtensionNames = extensionNames.data(),
      .pEnabledFeatures = &deviceFeatures};

  if (enableValidationLayers) {
    createInfo.enabledLayerCount =
        static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
  }

  if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) !=
      VK_SUCCESS) {
    log_ftl("Failed to create Vulkan logical device.");
  }

  vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
}

void VulkanInstance::createCommandPool() {
  VkCommandPoolCreateInfo createInfo{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
               VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = graphicsQueueFamily};

  if (vkCreateCommandPool(device, &createInfo, nullptr, &commandPool) !=
      VK_SUCCESS) {
    log_ftl("Failed to create Vulkan command pool.");
  }
}

void VulkanInstance::createAllocator() {
  VmaAllocatorCreateInfo createInfo{
      .physicalDevice = physicalDevice, .device = device, .instance = instance};

  if (vmaCreateAllocator(&createInfo, &allocator) != VK_SUCCESS) {
    log_ftl("Failed to create Vulkan memory allocator.");
  }
}

void VulkanInstance::createDescriptorPool() {
  // TODO(marceline-cramer) Make wrapper class for descriptor management
  std::vector<VkDescriptorPoolSize> poolSizes;

  poolSizes.push_back(
      {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, .descriptorCount = 1000});

  VkDescriptorPoolCreateInfo createInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1000,
      .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
      .pPoolSizes = poolSizes.data()};

  if (vkCreateDescriptorPool(device, &createInfo, nullptr, &descriptorPool) !=
      VK_SUCCESS) {
    log_ftl("Failed to create Vulkan descriptor pool.");
  }
}