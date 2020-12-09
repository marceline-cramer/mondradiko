/**
 * @file Renderer.h
 * @author Marceline Cramer (cramermarceline@gmail.com)
 * @brief Contains all Pipelines and performs frame rendering operations.
 * @date 2020-10-24
 *
 * @copyright Copyright (c) 2020 Marceline Cramer
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 */

#pragma once

#include <string>
#include <vector>

#include "core/common/api_headers.h"
#include "core/displays/DisplayInterface.h"
#include "core/gpu/GpuDescriptorPool.h"
#include "core/gpu/GpuInstance.h"
#include "core/renderer/MeshPipeline.h"

namespace mondradiko {

struct PipelinedFrameData {
  // TODO(marceline-cramer) Use command pool per frame, per thread
  VkCommandBuffer command_buffer;
  VkFence is_in_use;
  bool submitted;

  GpuDescriptorPool* descriptor_pool;
  GpuBuffer* viewports;
};

struct FramePushConstant {
  uint32_t view_index;
  uint32_t material_index;
};

class Renderer {
 public:
  Renderer(DisplayInterface*, GpuInstance*);
  ~Renderer();

  void renderFrame(entt::registry&);

  AssetHandle<MaterialAsset> loadMaterial(std::string, const aiScene*,
                                          uint32_t);
  AssetHandle<MeshAsset> loadMesh(std::string, const aiScene*, uint32_t);
  AssetHandle<TextureAsset> loadTexture(std::string, const aiScene*, aiString);

  VkRenderPass composite_pass = VK_NULL_HANDLE;

  GpuDescriptorSetLayout* viewport_layout = nullptr;

  MeshPipeline* mesh_pipeline = nullptr;

 private:
  DisplayInterface* display;
  GpuInstance* gpu;

  AssetPool<MaterialAsset> material_pool;
  AssetPool<MeshAsset> mesh_pool;
  AssetPool<TextureAsset> texture_pool;

  uint32_t current_frame = 0;
  std::vector<PipelinedFrameData> frames_in_flight;
};

}  // namespace mondradiko