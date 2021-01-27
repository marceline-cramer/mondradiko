/**
 * @file GpuDescriptorPool.h
 * @author Marceline Cramer (cramermarceline@gmail.com)
 * @brief Allocates descriptors from a dynamic pool.
 * @date 2020-11-30
 *
 * @copyright Copyright (c) 2020 the Mondradiko contributors.
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 */

#pragma once

#include <unordered_set>

#include "lib/include/vulkan_headers.h"

namespace mondradiko {

// Forward declarations
class GpuDescriptorSet;
class GpuDescriptorSetLayout;
class GpuInstance;

class GpuDescriptorPool {
 public:
  explicit GpuDescriptorPool(GpuInstance*);
  ~GpuDescriptorPool();

  GpuDescriptorSet* allocate(GpuDescriptorSetLayout*);
  void reset();

 private:
  GpuInstance* gpu;

  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;

  std::unordered_set<GpuDescriptorSet*> descriptor_sets;
};

}  // namespace mondradiko
