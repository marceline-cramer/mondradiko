// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "core/renderer/MeshPass.h"

#include "core/assets/MeshAsset.h"
#include "core/components/internal/WorldTransform.h"
#include "core/components/scriptable/PointLightComponent.h"
#include "core/components/synchronized/MeshRendererComponent.h"
#include "core/cvars/CVarScope.h"
#include "core/gpu/GpuBuffer.h"
#include "core/gpu/GpuDescriptorPool.h"
#include "core/gpu/GpuDescriptorSet.h"
#include "core/gpu/GpuDescriptorSetLayout.h"
#include "core/gpu/GpuInstance.h"
#include "core/gpu/GpuPipeline.h"
#include "core/gpu/GpuShader.h"
#include "core/gpu/GpuVector.h"
#include "core/gpu/GraphicsState.h"
#include "core/renderer/Renderer.h"
#include "core/world/World.h"
#include "log/log.h"
#include "shaders/mesh_depth.frag.h"
#include "shaders/mesh_depth.vert.h"
#include "shaders/mesh_forward.frag.h"
#include "shaders/mesh_forward.vert.h"

namespace mondradiko {
namespace core {

void MeshPass::initCVars(CVarScope* cvars) {}

void MeshPass::initDummyAssets(AssetPool* asset_pool) {
  asset_pool->initializeAssetType<MaterialAsset>();
  asset_pool->initializeAssetType<MeshAsset>();
  asset_pool->initializeAssetType<TextureAsset>();
}

MeshPass::MeshPass(Renderer* renderer, World* world)
    : gpu(renderer->getGpu()), renderer(renderer), world(world) {
  log_zone;

  {
    log_zone_named("Create texture sampler");

    VkSamplerCreateInfo sampler_info{};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sampler_info.mipLodBias = 0.0f;
    // TODO(marceline-cramer) Anisotropy support
    sampler_info.anisotropyEnable = VK_FALSE;
    sampler_info.compareEnable = VK_FALSE;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 0.0f;
    sampler_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;

    if (vkCreateSampler(gpu->device, &sampler_info, nullptr,
                        &texture_sampler) != VK_SUCCESS) {
      log_ftl("Failed to create texture sampler.");
    }
  }

  {
    log_zone_named("Create set layouts");

    material_layout = new GpuDescriptorSetLayout(gpu);
    material_layout->addStorageBuffer(sizeof(MaterialUniform));

    mesh_layout = new GpuDescriptorSetLayout(gpu);
    mesh_layout->addStorageBuffer(sizeof(MeshUniform));
    mesh_layout->addStorageBuffer(sizeof(PointLightUniform));

    texture_layout = new GpuDescriptorSetLayout(gpu);
    texture_layout->addCombinedImageSampler(texture_sampler);
    texture_layout->addCombinedImageSampler(texture_sampler);
    texture_layout->addCombinedImageSampler(texture_sampler);
    texture_layout->addCombinedImageSampler(texture_sampler);
  }

  {
    log_zone_named("Create pipeline layout");

    types::vector<VkDescriptorSetLayout> set_layouts{
        renderer->getViewportLayout()->getSetLayout(),
        material_layout->getSetLayout(), mesh_layout->getSetLayout(),
        texture_layout->getSetLayout()};

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(set_layouts.size());
    layoutInfo.pSetLayouts = set_layouts.data();

    if (vkCreatePipelineLayout(gpu->device, &layoutInfo, nullptr,
                               &pipeline_layout) != VK_SUCCESS) {
      log_ftl("Failed to create pipeline layout.");
    }
  }

  {
    log_zone_named("Create shaders");

    depth_vertex_shader =
        new GpuShader(gpu, VK_SHADER_STAGE_VERTEX_BIT, shaders_mesh_depth_vert,
                      sizeof(shaders_mesh_depth_vert));
    depth_fragment_shader =
        new GpuShader(gpu, VK_SHADER_STAGE_FRAGMENT_BIT,
                      shaders_mesh_depth_frag, sizeof(shaders_mesh_depth_frag));

    forward_vertex_shader = new GpuShader(gpu, VK_SHADER_STAGE_VERTEX_BIT,
                                          shaders_mesh_forward_vert,
                                          sizeof(shaders_mesh_forward_vert));
    forward_fragment_shader = new GpuShader(gpu, VK_SHADER_STAGE_FRAGMENT_BIT,
                                            shaders_mesh_forward_frag,
                                            sizeof(shaders_mesh_forward_frag));
  }

  {
    log_zone_named("Create pipelines");

    auto vertex_bindings = MeshVertex::getVertexBindings();
    auto attribute_descriptions = MeshVertex::getAttributeDescriptions();

    depth_pipeline = new GpuPipeline(
        gpu, pipeline_layout, renderer->getViewportRenderPass(),
        renderer->getDepthSubpass(), depth_vertex_shader, depth_fragment_shader,
        vertex_bindings, attribute_descriptions);

    forward_pipeline = new GpuPipeline(
        gpu, pipeline_layout, renderer->getViewportRenderPass(),
        renderer->getForwardSubpass(), forward_vertex_shader,
        forward_fragment_shader, vertex_bindings, attribute_descriptions);

    transparent_pipeline = new GpuPipeline(
        gpu, pipeline_layout, renderer->getViewportRenderPass(),
        renderer->getTransparentSubpass(), forward_vertex_shader,
        forward_fragment_shader, vertex_bindings, attribute_descriptions);
  }

  {
    log_zone_named("Initialize asset types");

    AssetPool* asset_pool = world->getAssetPool();
    asset_pool->initializeAssetType<MaterialAsset>(asset_pool, renderer);
    asset_pool->initializeAssetType<MeshAsset>(this);
    asset_pool->initializeAssetType<TextureAsset>(this);
  }

  {
    log_zone_named("Create mesh data pools");

    size_t vertex_pool_size = 1024 * 1024 * sizeof(MeshVertex);
    size_t index_pool_size = 1024 * 1024 * sizeof(MeshIndex);

    vertex_pool = new GpuBuffer(
        gpu, vertex_pool_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    index_pool = new GpuBuffer(
        gpu, index_pool_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
  }
}

MeshPass::~MeshPass() {
  log_zone;

  if (texture_sampler != VK_NULL_HANDLE)
    vkDestroySampler(gpu->device, texture_sampler, nullptr);
  if (vertex_pool != nullptr) delete vertex_pool;
  if (index_pool != nullptr) delete index_pool;
  if (transparent_pipeline != nullptr) delete transparent_pipeline;
  if (forward_pipeline != nullptr) delete forward_pipeline;
  if (depth_pipeline != nullptr) delete depth_pipeline;
  if (depth_vertex_shader != nullptr) delete depth_vertex_shader;
  if (depth_fragment_shader != nullptr) delete depth_fragment_shader;
  if (forward_vertex_shader != nullptr) delete forward_vertex_shader;
  if (forward_fragment_shader != nullptr) delete forward_fragment_shader;
  if (pipeline_layout != VK_NULL_HANDLE)
    vkDestroyPipelineLayout(gpu->device, pipeline_layout, nullptr);
  if (material_layout != nullptr) delete material_layout;
  if (texture_layout != nullptr) delete texture_layout;
  if (mesh_layout != nullptr) delete mesh_layout;
}

size_t MeshPass::allocateVertices(size_t vertex_num) {
  // TODO(marceline-cramer) Proper pool allocation
  size_t vertex_offset = first_available_vertex;
  first_available_vertex += vertex_num;
  return vertex_offset;
}

size_t MeshPass::allocateIndices(size_t index_num) {
  // TODO(marceline-cramer) Proper pool allocation
  size_t index_offset = first_available_index;
  first_available_index += index_num;
  return index_offset;
}

void MeshPass::createFrameData(uint32_t frame_count) {
  log_zone;

  frame_data.resize(frame_count);

  for (auto& frame : frame_data) {
    frame.material_buffer = new GpuVector(gpu, sizeof(MaterialUniform),
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    frame.mesh_buffer = new GpuVector(gpu, sizeof(MeshUniform),
                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

    frame.point_lights = new GpuVector(gpu, sizeof(PointLightUniform),
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  }
}

void MeshPass::destroyFrameData() {
  log_zone;

  for (auto& frame : frame_data) {
    if (frame.material_buffer != nullptr) delete frame.material_buffer;
    if (frame.mesh_buffer != nullptr) delete frame.mesh_buffer;
    if (frame.point_lights != nullptr) delete frame.point_lights;
  }
}

void MeshPass::beginFrame(uint32_t frame_index, uint32_t viewport_count,
                          GpuDescriptorPool* descriptor_pool) {
  log_zone;

  renderer->addPassToPhase(RenderPhase::Depth, this);
  renderer->addPassToPhase(RenderPhase::Forward, this);
  renderer->addPassToPhase(RenderPhase::Transparent, this);

  current_frame = frame_index;
  auto& frame = frame_data[current_frame];

  types::vector<PointLightUniform> point_light_uniforms;

  {
    auto point_lights = world->registry.view<PointLightComponent>();

    for (auto e : point_lights) {
      auto& point_light = point_lights.get<PointLightComponent>(e);

      PointLightUniform uniform;
      point_light.getUniform(&uniform);

      if (world->registry.has<WorldTransform>(e)) {
        auto& transform = world->registry.get<WorldTransform>(e);
        uniform.position = transform.getTransform() * uniform.position;
      }

      point_light_uniforms.push_back(uniform);
    }
  }

  frame.point_lights->writeData(0, point_light_uniforms);

  types::unordered_map<AssetId, uint32_t> material_assets;
  types::vector<MaterialUniform> frame_materials;
  types::vector<GpuDescriptorSet*> frame_textures;

  types::vector<MeshUniform> frame_meshes;

  auto mesh_renderers =
      world->registry.group<MeshRendererComponent, WorldTransform>();

  frame.forward_commands.single_sided.clear();
  frame.forward_commands.double_sided.clear();
  frame.transparent_commands.single_sided.clear();
  frame.transparent_commands.double_sided.clear();

  for (auto e : mesh_renderers) {
    auto& mesh_renderer = mesh_renderers.get<MeshRendererComponent>(e);
    if (!mesh_renderer.isLoaded()) continue;

    MeshRenderCommand cmd;
    MeshRenderCommandList* target_commands;

    uint32_t material_idx;

    {  // Write material uniform
      const auto& material_asset = mesh_renderer.getMaterialAsset();

      MeshPassCommandList* pass_commands;
      if (material_asset->isTransparent()) {
        pass_commands = &frame.transparent_commands;
      } else {
        pass_commands = &frame.forward_commands;
      }

      if (material_asset->isDoubleSided()) {
        target_commands = &pass_commands->double_sided;
      } else {
        target_commands = &pass_commands->single_sided;
      }

      auto iter = material_assets.find(material_asset.getId());

      if (iter != material_assets.end()) {
        material_idx = iter->second;
        cmd.textures_descriptor = frame_textures[iter->second];
      } else {
        material_idx = frame_materials.size();
        material_assets.emplace(material_asset.getId(), material_idx);
        MaterialUniform uniform = material_asset->getUniform();
        frame_materials.push_back(uniform);

        cmd.textures_descriptor = descriptor_pool->allocate(texture_layout);
        material_asset->updateTextureDescriptor(cmd.textures_descriptor);
        frame_textures.push_back(cmd.textures_descriptor);
      }
    }

    {  // Write mesh uniform
      auto& transform = mesh_renderers.get<WorldTransform>(e);

      MeshUniform mesh_uniform;
      mesh_uniform.model = transform.getTransform();
      mesh_uniform.light_count = point_light_uniforms.size();
      mesh_uniform.material_idx = material_idx;

      cmd.mesh_idx = frame_meshes.size();
      frame_meshes.push_back(mesh_uniform);
    }

    {  // Write mesh asset
      const auto& mesh_asset = mesh_renderer.getMeshAsset();

      cmd.vertex_offset = mesh_asset->getVertexOffset();
      cmd.index_offset = mesh_asset->getIndexOffset();
      cmd.index_num = mesh_asset->getIndexNum();
    }

    target_commands->push_back(cmd);
  }

  frame.material_buffer->writeData(0, frame_materials);
  frame.material_descriptor = descriptor_pool->allocate(material_layout);
  frame.material_descriptor->updateStorageBuffer(0, frame.material_buffer);

  frame.mesh_buffer->writeData(0, frame_meshes);
  frame.mesh_descriptor = descriptor_pool->allocate(mesh_layout);
  frame.mesh_descriptor->updateStorageBuffer(0, frame.mesh_buffer);
  frame.mesh_descriptor->updateStorageBuffer(1, frame.point_lights);
}

void MeshPass::renderViewport(VkCommandBuffer command_buffer,
                              uint32_t viewport_index, RenderPhase phase,
                              const GpuDescriptorSet* viewport_descriptor) {
  log_zone;

  auto& frame = frame_data[current_frame];
  GraphicsState graphics_state;
  GpuPipeline* current_pipeline;

  MeshPassCommandList* pass_commands;
  if (phase == RenderPhase::Transparent) {
    pass_commands = &frame.transparent_commands;
  } else {
    pass_commands = &frame.forward_commands;
  }

  GraphicsState::InputAssemblyState input_assembly_state{};
  input_assembly_state.primitive_topology =
      GraphicsState::PrimitiveTopology::TriangleList;
  input_assembly_state.primitive_restart_enable =
      GraphicsState::BoolFlag::False;
  graphics_state.input_assembly_state = input_assembly_state;

  GraphicsState::RasterizatonState rasterization_state{};
  rasterization_state.polygon_mode = GraphicsState::PolygonMode::Fill;
  rasterization_state.cull_mode = GraphicsState::CullMode::Back;
  graphics_state.rasterization_state = rasterization_state;

  if (phase == RenderPhase::Depth) {
    current_pipeline = depth_pipeline;

    GraphicsState::DepthState depth_state{};
    depth_state.test_enable = GraphicsState::BoolFlag::True;
    depth_state.write_enable = GraphicsState::BoolFlag::True;
    depth_state.compare_op = GraphicsState::CompareOp::Less;
    graphics_state.depth_state = depth_state;

    GraphicsState::ColorBlendState color_blend_state{};
    graphics_state.color_blend_state = color_blend_state;
  } else if (phase == RenderPhase::Forward) {
    current_pipeline = forward_pipeline;

    GraphicsState::DepthState depth_state{};
    depth_state.test_enable = GraphicsState::BoolFlag::True;
    depth_state.write_enable = GraphicsState::BoolFlag::False;
    depth_state.compare_op = GraphicsState::CompareOp::Equal;
    graphics_state.depth_state = depth_state;

    GraphicsState::ColorBlendState color_blend_state{};
    graphics_state.color_blend_state = color_blend_state;
  } else {
    current_pipeline = transparent_pipeline;

    GraphicsState::DepthState depth_state{};
    depth_state.test_enable = GraphicsState::BoolFlag::True;
    depth_state.write_enable = GraphicsState::BoolFlag::False;
    depth_state.compare_op = GraphicsState::CompareOp::Less;
    graphics_state.depth_state = depth_state;

    GraphicsState::ColorBlendState color_blend_state{};
    color_blend_state.blend_mode = GraphicsState::BlendMode::AlphaBlend;
    graphics_state.color_blend_state = color_blend_state;
  }

  // TODO(marceline-cramer) GpuPipeline + GpuPipelineLayout
  viewport_descriptor->cmdBind(command_buffer, pipeline_layout, 0);
  frame.material_descriptor->cmdBind(command_buffer, pipeline_layout, 1);
  frame.mesh_descriptor->cmdBind(command_buffer, pipeline_layout, 2);

  VkBuffer vertex_buffers[] = {vertex_pool->getBuffer()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);
  vkCmdBindIndexBuffer(command_buffer, index_pool->getBuffer(), 0,
                       VK_INDEX_TYPE_UINT32);

  current_pipeline->cmdBind(command_buffer, graphics_state);
  executeMeshCommands(command_buffer, pass_commands->single_sided);

  graphics_state.rasterization_state.cull_mode = GraphicsState::CullMode::None;
  current_pipeline->cmdBind(command_buffer, graphics_state);
  executeMeshCommands(command_buffer, pass_commands->double_sided);
}

// Helper function to actually render meshes
void MeshPass::executeMeshCommands(VkCommandBuffer command_buffer,
                                   const MeshRenderCommandList& commands) {
  for (const auto& cmd : commands) {
    log_zone_named("Render mesh");

    cmd.textures_descriptor->cmdBind(command_buffer, pipeline_layout, 3);

    vkCmdDrawIndexed(command_buffer, cmd.index_num, 1, cmd.index_offset,
                     cmd.vertex_offset, cmd.mesh_idx);
  }
}

}  // namespace core
}  // namespace mondradiko
