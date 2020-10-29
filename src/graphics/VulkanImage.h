/**
 * @file VulkanImage.h
 * @author Marceline Cramer (cramermarceline@gmail.com)
 * @brief Creates and manages a Vulkan image and its allocation.
 * @date 2020-10-24
 *
 * @copyright Copyright (c) 2020 Marceline Cramer
 *
 * This file is part of Mondradiko.
 *
 * Mondradiko is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Mondradiko is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Mondradiko.  If not, see <https: //www.gnu.org/licenses/>.
 *
 */

#ifndef SRC_GRAPHICS_VULKANIMAGE_H_
#define SRC_GRAPHICS_VULKANIMAGE_H_

#include "graphics/VulkanInstance.h"
#include "src/api_headers.h"

namespace mondradiko {

class VulkanImage {
 public:
  VulkanImage(VulkanInstance*, VkFormat, uint32_t, uint32_t, VkImageUsageFlags,
              VmaMemoryUsage);
  ~VulkanImage();

  void writeData(void*);
  void transitionLayout(VkImageLayout);
  void createView();

  VkFormat format;
  VkImageLayout layout;
  uint32_t width;
  uint32_t height;
  VmaAllocation allocation = nullptr;
  VmaAllocationInfo allocationInfo;
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;

 private:
  VulkanInstance* vulkanInstance;
};

}  // namespace mondradiko

#endif  // SRC_GRAPHICS_VULKANIMAGE_H_
