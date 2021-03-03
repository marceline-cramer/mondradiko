// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "bundler/prefab/GltfConverter.h"

#include <memory>
#include <unordered_map>
#include <vector>

#include "bundler/Bundler.h"
#include "log/log.h"
#include "types/assets/PrefabAsset_generated.h"

namespace mondradiko {

GltfConverter::GltfConverter(Bundler *bundler) : _bundler(bundler) {}

GltfConverter::AssetOffset GltfConverter::_loadModel(AssetBuilder *fbb,
                                                     GltfModel model) const {
  std::vector<uint32_t> children;
  for (const auto &scene : model.scenes) {
    auto child_id = _loadScene(model, scene);
    children.push_back(static_cast<uint32_t>(child_id));
  }

  auto children_offset = fbb->CreateVector(children);

  assets::PrefabAssetBuilder prefab(*fbb);
  prefab.add_children(children_offset);
  auto prefab_offset = prefab.Finish();

  assets::SerializedAssetBuilder asset(*fbb);
  asset.add_type(assets::AssetType::PrefabAsset);
  asset.add_prefab(prefab_offset);
  auto asset_offset = asset.Finish();

  return asset_offset;
}

assets::AssetId GltfConverter::_loadScene(GltfModel model,
                                          GltfScene scene) const {
  log_inf("Loading scene");

  flatbuffers::FlatBufferBuilder fbb;

  std::vector<uint32_t> children;

  for (auto node_index : scene.nodes) {
    const tinygltf::Node &node = model.nodes[node_index];
    auto child_id = _loadNode(model, node, glm::vec3(1.0, 1.0, 1.0));
    children.push_back(static_cast<uint32_t>(child_id));
  }

  auto children_offset = fbb.CreateVector(children);

  assets::PrefabAssetBuilder prefab_asset(fbb);
  prefab_asset.add_children(children_offset);
  auto prefab_offset = prefab_asset.Finish();

  assets::SerializedAssetBuilder asset(fbb);
  asset.add_type(assets::AssetType::PrefabAsset);
  asset.add_prefab(prefab_offset);
  auto asset_offset = asset.Finish();

  assets::AssetId prefab_id = _bundler->addAsset(&fbb, asset_offset);
  return prefab_id;
}

assets::AssetId GltfConverter::_loadNode(GltfModel model, GltfNode node,
                                         glm::vec3 parent_scale) const {
  std::vector<uint32_t> children;

  glm::vec3 node_translation;
  glm::vec3 node_scale;
  glm::quat node_orientation;

  if (node.translation.size() == 3) {
    node_translation = glm::make_vec3(node.translation.data());
  } else {
    node_translation = glm::vec3(0.0, 0.0, 0.0);
  }

  if (node.scale.size() == 3) {
    node_scale = glm::make_vec3(node.scale.data());
  } else {
    node_scale = glm::vec3(1.0, 1.0, 1.0);
  }

  node_scale *= parent_scale;

  if (node.rotation.size() == 4) {
    node_orientation = glm::make_quat(node.rotation.data());
  } else {
    node_orientation = glm::quat(1.0, 0.0, 0.0, 0.0);
  }

  // Create mesh
  if (node.mesh != -1) {
    const auto &mesh = model.meshes[node.mesh];

    for (const auto &primitive : mesh.primitives) {
      assets::AssetId mesh_id = _loadPrimitive(model, primitive, node_scale);

      const tinygltf::Material &material = model.materials[primitive.material];
      assets::AssetId material_id = _loadMaterial(model, material);

      flatbuffers::FlatBufferBuilder fbb;

      assets::MeshRendererPrefab mesh_renderer;
      mesh_renderer.mutate_mesh(mesh_id);
      mesh_renderer.mutate_material(material_id);

      assets::TransformPrefab transform;
      transform.mutable_position().mutate_x(node_translation.x);
      transform.mutable_position().mutate_y(node_translation.y);
      transform.mutable_position().mutate_z(node_translation.z);

      transform.mutable_orientation().mutate_w(node_orientation.w);
      transform.mutable_orientation().mutate_x(node_orientation.x);
      transform.mutable_orientation().mutate_y(node_orientation.y);
      transform.mutable_orientation().mutate_z(node_orientation.z);

      assets::PrefabAssetBuilder prefab(fbb);
      prefab.add_mesh_renderer(&mesh_renderer);
      prefab.add_transform(&transform);
      auto prefab_offset = prefab.Finish();

      assets::SerializedAssetBuilder asset(fbb);
      asset.add_type(assets::AssetType::PrefabAsset);
      asset.add_prefab(prefab_offset);
      auto asset_offset = asset.Finish();

      assets::AssetId asset_id = _bundler->addAsset(&fbb, asset_offset);
      children.push_back(static_cast<uint32_t>(asset_id));
    }
  }

  {  // Load children
    for (auto child_index : node.children) {
      const auto &child_node = model.nodes[child_index];
      assets::AssetId child_id = _loadNode(model, child_node, node_scale);
      children.push_back(static_cast<uint32_t>(child_id));
    }
  }

  {  // Create prefab
    flatbuffers::FlatBufferBuilder fbb;

    auto children_offset = fbb.CreateVector(children);

    assets::PrefabAssetBuilder prefab_asset(fbb);
    prefab_asset.add_children(children_offset);
    auto prefab_offset = prefab_asset.Finish();

    assets::SerializedAssetBuilder asset(fbb);
    asset.add_type(assets::AssetType::PrefabAsset);
    asset.add_prefab(prefab_offset);
    auto asset_offset = asset.Finish();

    assets::AssetId prefab_id = _bundler->addAsset(&fbb, asset_offset);
    return prefab_id;
  }
}

/**
 * @brief Helper class for reading GLTF buffers.
 *
 */
class GltfAccessor {
 public:
  GltfAccessor(const tinygltf::Model &model, uint32_t accessor_index) {
    const tinygltf::Accessor &accessor = model.accessors[accessor_index];
    const tinygltf::BufferView &buffer_view =
        model.bufferViews[accessor.bufferView];
    buffer_data = &(model.buffers[buffer_view.buffer]
                        .data[accessor.byteOffset + buffer_view.byteOffset]);
    stride = accessor.ByteStride(buffer_view);

    if (stride == -1) {
      log_ftl("Invalid GLTF accessor");
    }

    count = accessor.count;
  }

  template <typename ElementType>
  const ElementType *get(uint32_t element_index) {
    return reinterpret_cast<const ElementType *>(buffer_data +
                                                 stride * element_index);
  }

  uint32_t size() { return count; }

 private:
  const unsigned char *buffer_data;
  uint32_t count;
  int stride;
};

assets::AssetId GltfConverter::_loadPrimitive(GltfModel model,
                                              GltfPrimitive primitive,
                                              glm::vec3 scale) const {
  std::vector<assets::MeshVertex> vertices;
  std::vector<uint32_t> indices;

  if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
    log_ftl("GLTF primitive must be triangle list");
  }

  const auto &attributes = primitive.attributes;

  if (attributes.find("POSITION") == primitive.attributes.end()) {
    log_ftl("GLTF primitive must have position attributes");
  }

  if (attributes.find("NORMAL") == primitive.attributes.end()) {
    log_ftl("GLTF primitive must have normal attributes");
  }

  if (attributes.find("TEXCOORD_0") == primitive.attributes.end()) {
    log_ftl("GLTF primitive must have texture coordinates");
  }

  // TODO(marceline-cramer) Generate indices if they're not there
  if (primitive.indices <= -1) {
    log_ftl("GLTF primtive must have indices");
  }

  {  // Load vertices
    GltfAccessor pos_accessor(model, attributes.find("POSITION")->second);
    GltfAccessor norm_accessor(model, attributes.find("NORMAL")->second);
    GltfAccessor tex_accessor(model, attributes.find("TEXCOORD_0")->second);

    std::unique_ptr<GltfAccessor> tan_accessor;
    assets::Vec3 tangent_vec(0.0, 1.0, 0.0);
    if (attributes.find("TANGENT") != primitive.attributes.end()) {
      tan_accessor = std::make_unique<GltfAccessor>(
          model, attributes.find("TANGENT")->second);
    }

    for (size_t v = 0; v < pos_accessor.size(); v++) {
      const float *position_raw = pos_accessor.get<float>(v);
      const float *normal_raw = norm_accessor.get<float>(v);
      const float *tex_coord = tex_accessor.get<float>(v);

      // Scale the position
      glm::vec3 position =
          glm::vec3(position_raw[0], position_raw[1], position_raw[2]) * scale;

      // Normalize the normal
      glm::vec3 normal = glm::normalize(
          glm::vec3(normal_raw[0], normal_raw[1], normal_raw[2]));

      if (tan_accessor) {
        const float *tangent_raw = tan_accessor->get<float>(v);

        // Normalize the tangent
        glm::vec3 tangent = glm::normalize(
            glm::vec3(tangent_raw[0], tangent_raw[1], tangent_raw[2]));
        tangent_vec = assets::Vec3(tangent.x, tangent.y, tangent.z);
      }

      assets::Vec3 position_vec(position.x, position.y, position.z);
      assets::Vec3 normal_vec(normal.x, normal.y, normal.z);

      // TODO(marceline-cramer) Read mesh vertex colors
      assets::Vec3 color_vec(1.0, 1.0, 1.0);
      assets::Vec2 tex_coord_vec(tex_coord[0], tex_coord[1]);

      assets::MeshVertex vertex(position_vec, normal_vec, tangent_vec,
                                color_vec, tex_coord_vec);
      vertices.push_back(vertex);
    }
  }

  {  // Load indices
    const tinygltf::Accessor &accessor =
        model.accessors[primitive.indices > -1 ? primitive.indices : 0];
    const tinygltf::BufferView &bufferView =
        model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer &buffer = model.buffers[bufferView.buffer];

    const void *dataPtr =
        &(buffer.data[accessor.byteOffset + bufferView.byteOffset]);

    switch (accessor.componentType) {
      case TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT: {
        const uint32_t *buf = static_cast<const uint32_t *>(dataPtr);
        for (size_t index = 0; index < accessor.count; index++) {
          indices.push_back(buf[index]);
        }
        break;
      }
      case TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT: {
        const uint16_t *buf = static_cast<const uint16_t *>(dataPtr);
        for (size_t index = 0; index < accessor.count; index++) {
          indices.push_back(buf[index]);
        }
        break;
      }
      case TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE: {
        const uint8_t *buf = static_cast<const uint8_t *>(dataPtr);
        for (size_t index = 0; index < accessor.count; index++) {
          indices.push_back(buf[index]);
        }
        break;
      }
      default: {
        log_ftl_fmt("Index component type %d", accessor.componentType);
        break;
      }
    }
  }

  {  // Write primitive data
    flatbuffers::FlatBufferBuilder fbb;

    auto vertices_offset = fbb.CreateVectorOfStructs(vertices);
    auto indices_offset = fbb.CreateVector(indices);

    assets::MeshAssetBuilder mesh_asset(fbb);
    mesh_asset.add_vertices(vertices_offset);
    mesh_asset.add_indices(indices_offset);
    auto mesh_offset = mesh_asset.Finish();

    assets::SerializedAssetBuilder asset(fbb);
    asset.add_type(assets::AssetType::MeshAsset);
    asset.add_mesh(mesh_offset);
    auto asset_offset = asset.Finish();

    assets::AssetId mesh_id = _bundler->addAsset(&fbb, asset_offset);
    return mesh_id;
  }
}

// Helper functions to load vectors
// TODO(marceline-cramer) Move this into a helper file
void loadVector(assets::Vec3 *dst, const std::vector<double> &src) {
  if (src.size() >= 3) {
    dst->mutate_x(src[0]);
    dst->mutate_y(src[1]);
    dst->mutate_z(src[2]);
  }
}

void loadVector(assets::Vec4 *dst, const std::vector<double> &src) {
  if (src.size() >= 4) {
    dst->mutate_x(src[0]);
    dst->mutate_y(src[1]);
    dst->mutate_z(src[2]);
    dst->mutate_w(src[3]);
  }
}

assets::AssetId GltfConverter::_loadMaterial(GltfModel model,
                                             GltfMaterial material) const {
  flatbuffers::FlatBufferBuilder fbb;

  assets::MaterialAssetBuilder material_builder(fbb);

  {  // Load base material attributes
    const auto &base = material;

    material_builder.add_is_double_sided(base.doubleSided);

    if (base.alphaMode == "MASK") {
      material_builder.add_mask_threshold(base.alphaCutoff);
      material_builder.add_enable_blend(false);
    } else if (base.alphaMode == "BLEND") {
      material_builder.add_mask_threshold(-1.0);
      material_builder.add_enable_blend(true);
    } else if (base.alphaMode == "OPAQUE") {
      material_builder.add_mask_threshold(-1.0);
      material_builder.add_enable_blend(false);
    } else {
      log_ftl_fmt("Unsupported alpha mode %s", base.alphaMode.c_str());
    }

    if (material.extensions.find("KHR_materials_unlit") !=
        material.extensions.end()) {
      material_builder.add_is_unlit(true);
    } else {
      material_builder.add_is_unlit(false);
    }

    assets::Vec3 emissive_factor(0.0, 0.0, 0.0);
    loadVector(&emissive_factor, base.emissiveFactor);
    material_builder.add_emissive_factor(&emissive_factor);

    assets::AssetId emissive_texture =
        _loadTexture(model, base.emissiveTexture, false);
    material_builder.add_emissive_texture(emissive_texture);

    material_builder.add_normal_map_scale(base.normalTexture.scale);

    if (base.normalTexture.index != -1) {
      assets::AssetId normal_map_texture =
          _loadImage(model.images[base.normalTexture.index], false);
      material_builder.add_normal_map_texture(normal_map_texture);
    } else {
      material_builder.add_normal_map_texture(assets::AssetId::NullAsset);
    }
  }

  {  // Load PBR
    const auto &pbr = material.pbrMetallicRoughness;

    assets::Vec4 albedo_factor(1.0, 1.0, 1.0, 1.0);
    loadVector(&albedo_factor, pbr.baseColorFactor);
    material_builder.add_albedo_factor(&albedo_factor);

    assets::AssetId albedo_texture =
        _loadTexture(model, pbr.baseColorTexture, true);
    material_builder.add_albedo_texture(albedo_texture);

    material_builder.add_metallic_factor(pbr.metallicFactor);
    material_builder.add_roughness_factor(pbr.roughnessFactor);

    assets::AssetId metal_roughness_texture =
        _loadTexture(model, pbr.metallicRoughnessTexture, false);
    material_builder.add_metal_roughness_texture(metal_roughness_texture);
  }

  auto material_offset = material_builder.Finish();

  assets::SerializedAssetBuilder asset_builder(fbb);
  asset_builder.add_type(assets::AssetType::MaterialAsset);
  asset_builder.add_material(material_offset);
  auto asset_offset = asset_builder.Finish();

  assets::AssetId asset_id = _bundler->addAsset(&fbb, asset_offset);
  return asset_id;
}

assets::AssetId GltfConverter::_loadTexture(GltfModel model,
                                            GltfTextureInfo texture_info,
                                            bool srgb) const {
  if (texture_info.index == -1) {
    log_err("Attempting to load null texture info");
    return assets::AssetId::NullAsset;
  }

  const auto &texture = model.textures[texture_info.index];

  if (texture.source == -1) {
    log_err("Attempting to load null texture source");
    return assets::AssetId::NullAsset;
  }

  const auto &image = model.images[texture.source];

  // TODO(marceline-cramer) Add sampler support

  return _loadImage(image, srgb);
}

assets::AssetId GltfConverter::_loadImage(GltfImage image, bool srgb) const {
  flatbuffers::FlatBufferBuilder fbb;

  log_inf("Loading GLTF image");
  log_inf_fmt("Name:\t\t\"%s\"", image.name.c_str());
  log_inf_fmt("Component#:\t%d", image.component);
  log_inf_fmt("Bits/channel:\t%d", image.bits);
  log_inf_fmt("Size:\t\t%dx%d", image.width, image.height);

  auto data_offset =
      fbb.CreateVector(reinterpret_cast<const uint8_t *>(image.image.data()),
                       image.image.size());

  assets::TextureAssetBuilder texture(fbb);

  texture.add_components(image.component);
  texture.add_bit_depth(image.bits);

  {
    using Components = assets::TextureComponentType;
    std::unordered_map<int, assets::TextureComponentType> types;
    types.emplace(TINYGLTF_COMPONENT_TYPE_BYTE, Components::Byte);
    types.emplace(TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE, Components::UByte);
    types.emplace(TINYGLTF_COMPONENT_TYPE_SHORT, Components::Short);
    types.emplace(TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT, Components::UShort);
    types.emplace(TINYGLTF_COMPONENT_TYPE_INT, Components::Int);
    types.emplace(TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT, Components::UInt);
    types.emplace(TINYGLTF_COMPONENT_TYPE_FLOAT, Components::Float);
    types.emplace(TINYGLTF_COMPONENT_TYPE_DOUBLE, Components::Double);

    auto iter = types.find(image.pixel_type);

    if (iter == types.end()) {
      log_ftl_fmt("Unrecognized GLTF image component type %d", image.component);
    }

    texture.add_component_type(iter->second);
  }

  texture.add_width(image.width);
  texture.add_height(image.height);
  texture.add_srgb(srgb);
  texture.add_data(data_offset);
  auto texture_offset = texture.Finish();

  assets::SerializedAssetBuilder asset(fbb);
  asset.add_type(assets::AssetType::TextureAsset);
  asset.add_texture(texture_offset);
  auto asset_offset = asset.Finish();

  assets::AssetId asset_id = _bundler->addAsset(&fbb, asset_offset);
  log_dbg_fmt("Added GLTF image: 0x%0dx", asset_id);
  return asset_id;
}

}  // namespace mondradiko
