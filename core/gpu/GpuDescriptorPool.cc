// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "core/gpu/GpuDescriptorPool.h"

#include "core/gpu/GpuDescriptorSet.h"
#include "core/gpu/GpuDescriptorSetLayout.h"
#include "core/gpu/GpuInstance.h"
#include "log/log.h"

namespace mondradiko {

GpuDescriptorPool::GpuDescriptorPool(GpuInstance* gpu) : gpu(gpu) {
  // TODO(marceline-cramer) Dynamic pool recreation using set layouts
  types::vector<VkDescriptorPoolSize> pool_sizes;

  pool_sizes.push_back({VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000});
  pool_sizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000});
  pool_sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000});
  pool_sizes.push_back({VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000});

  VkDescriptorPoolCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
  create_info.maxSets = 1000,
  create_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size()),
  create_info.pPoolSizes = pool_sizes.data();

  if (vkCreateDescriptorPool(gpu->device, &create_info, nullptr,
                             &descriptor_pool) != VK_SUCCESS) {
    log_ftl("Failed to create descriptor pool.");
  }
}

GpuDescriptorPool::~GpuDescriptorPool() {
  if (descriptor_pool != VK_NULL_HANDLE) {
    // Destroy all allocated sets
    reset();
    vkDestroyDescriptorPool(gpu->device, descriptor_pool, nullptr);
  }
}

GpuDescriptorSet* GpuDescriptorPool::allocate(GpuDescriptorSetLayout* layout) {
  VkDescriptorSetLayout vk_set_layout = layout->getSetLayout();
  VkDescriptorSet vk_set;

  VkDescriptorSetAllocateInfo alloc_info{};
  alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
  alloc_info.descriptorPool = descriptor_pool,
  alloc_info.descriptorSetCount = 1;
  alloc_info.pSetLayouts = &vk_set_layout;

  if (vkAllocateDescriptorSets(gpu->device, &alloc_info, &vk_set) !=
      VK_SUCCESS) {
    log_ftl("Failed to allocate descriptor set");
  }

  // TODO(marceline-cramer) Dynamic pool resizing

  GpuDescriptorSet* new_set = new GpuDescriptorSet(gpu, layout, vk_set);
  descriptor_sets.emplace(new_set);
  return new_set;
}

void GpuDescriptorPool::reset() {
  vkResetDescriptorPool(gpu->device, descriptor_pool, 0);

  for (const auto& set : descriptor_sets) {
    delete set;
  }

  descriptor_sets.clear();
}

}  // namespace mondradiko
