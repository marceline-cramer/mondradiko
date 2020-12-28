/**
 * @file MeshAsset.h
 * @author Marceline Cramer (cramermarceline@gmail.com)
 * @brief Loads a mesh.
 * @date 2020-12-12
 *
 * @copyright Copyright (c) 2020 the Mondradiko contributors.
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 */

#pragma once

#include <array>
#include <cstddef>

#include "core/assets/AssetPool.h"
#include "core/common/vulkan_headers.h"
#include "core/common/api_headers.h"
#include "core/common/glm_headers.h"

namespace mondradiko {

// Forward declarations
class GpuInstance;
class GpuBuffer;

using MeshVertexAttributeDescriptions =
    std::array<VkVertexInputAttributeDescription, 4>;

struct MeshVertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec3 color;
  glm::vec2 tex_coord;

  static VkVertexInputBindingDescription getBindingDescription() {
    VkVertexInputBindingDescription description{
        .binding = 0,
        .stride = sizeof(MeshVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};

    return description;
  }

  static MeshVertexAttributeDescriptions getAttributeDescriptions() {
    MeshVertexAttributeDescriptions descriptions;

    descriptions[0] = {.location = 0,
                       .binding = 0,
                       .format = VK_FORMAT_R32G32B32_SFLOAT,
                       .offset = offsetof(MeshVertex, position)};

    descriptions[1] = {.location = 1,
                       .binding = 0,
                       .format = VK_FORMAT_R32G32B32_SFLOAT,
                       .offset = offsetof(MeshVertex, normal)};

    descriptions[2] = {.location = 2,
                       .binding = 0,
                       .format = VK_FORMAT_R32G32B32_SFLOAT,
                       .offset = offsetof(MeshVertex, color)};

    descriptions[3] = {.location = 3,
                       .binding = 0,
                       .format = VK_FORMAT_R32G32_SFLOAT,
                       .offset = offsetof(MeshVertex, tex_coord)};

    return descriptions;
  }
};

using MeshIndex = uint32_t;

class MeshAsset : public Asset {
 public:
  MeshAsset(assets::ImmutableAsset&, AssetPool*, GpuInstance*);
  ~MeshAsset();

  GpuBuffer* vertex_buffer = nullptr;
  GpuBuffer* index_buffer = nullptr;
  size_t index_count = 0;
};

}  // namespace mondradiko
