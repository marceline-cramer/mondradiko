// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "core/assets/MaterialAsset.h"

#include "core/assets/Asset.h"
#include "core/assets/TextureAsset.h"
#include "core/gpu/GpuDescriptorSet.h"
#include "core/renderer/Renderer.h"
#include "types/assets/MaterialAsset_generated.h"

namespace mondradiko {
namespace core {

void MaterialAsset::updateTextureDescriptor(
    GpuDescriptorSet* descriptor) const {
  GpuImage* error_image = renderer->getErrorImage();

  if (uniform.has_albedo_texture) {
    descriptor->updateImage(0, albedo_texture->getImage());
  } else {
    descriptor->updateImage(0, error_image);
  }

  if (uniform.has_emissive_texture) {
    descriptor->updateImage(1, emissive_texture->getImage());
  } else {
    descriptor->updateImage(1, error_image);
  }

  if (uniform.normal_map_scale > 0.0) {
    descriptor->updateImage(2, normal_map_texture->getImage());
  } else {
    descriptor->updateImage(2, error_image);
  }

  if (uniform.has_metal_roughness_texture) {
    descriptor->updateImage(3, metal_roughness_texture->getImage());
  } else {
    descriptor->updateImage(3, error_image);
  }
}

bool MaterialAsset::_load(const assets::SerializedAsset* asset) {
  // Skip loading if we initialized as a dummy
  if (renderer == nullptr) return true;

  const assets::MaterialAsset* material = asset->material();

  if (material->albedo_texture() != NullAsset) {
    albedo_texture = asset_pool->load<TextureAsset>(material->albedo_texture());
    uniform.has_albedo_texture = 1;
  } else {
    uniform.has_albedo_texture = 0;
  }

  if (material->emissive_texture() != NullAsset) {
    emissive_texture =
        asset_pool->load<TextureAsset>(material->emissive_texture());
    uniform.has_emissive_texture = 1;
  } else {
    uniform.has_emissive_texture = 0;
  }

  if (material->normal_map_texture() != NullAsset) {
    normal_map_texture =
        asset_pool->load<TextureAsset>(material->normal_map_texture());
    uniform.normal_map_scale = material->normal_map_scale();
  } else {
    uniform.normal_map_scale = -1.0;
  }

  if (material->metal_roughness_texture() != NullAsset) {
    metal_roughness_texture =
        asset_pool->load<TextureAsset>(material->metal_roughness_texture());
    uniform.has_metal_roughness_texture = 1;
  } else {
    uniform.has_metal_roughness_texture = 0;
  }

  if (material->is_unlit()) {
    uniform.is_unlit = 1;
  } else {
    uniform.is_unlit = 0;
  }

  if (material->enable_blend()) {
    uniform.enable_blend = 1;
    transparent = true;
  } else {
    uniform.enable_blend = 0;
    transparent = false;
  }

  double_sided = material->is_double_sided();

  const assets::Vec3* emissive_factor = material->emissive_factor();
  uniform.emissive_factor = glm::make_vec4(emissive_factor->v()->data());

  const assets::Vec4* albedo_factor = material->albedo_factor();
  uniform.albedo_factor = glm::make_vec4(albedo_factor->v()->data());

  uniform.mask_threshold = material->mask_threshold();
  uniform.metallic_factor = material->metallic_factor();
  uniform.roughness_factor = material->roughness_factor();

  return true;
}

}  // namespace core
}  // namespace mondradiko
