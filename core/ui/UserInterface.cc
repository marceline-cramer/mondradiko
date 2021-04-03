// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "core/ui/UserInterface.h"

#include "core/gpu/GpuDescriptorPool.h"
#include "core/gpu/GpuDescriptorSet.h"
#include "core/gpu/GpuDescriptorSetLayout.h"
#include "core/gpu/GpuInstance.h"
#include "core/gpu/GpuPipeline.h"
#include "core/gpu/GpuShader.h"
#include "core/gpu/GpuVector.h"
#include "core/gpu/GraphicsState.h"
#include "core/renderer/Renderer.h"
#include "core/scripting/ScriptEnvironment.h"
#include "core/shaders/panel.frag.h"
#include "core/shaders/panel.vert.h"
#include "core/ui/GlyphLoader.h"
#include "core/ui/UiPanel.h"
#include "log/log.h"

namespace mondradiko {
namespace core {

UserInterface::UserInterface(GlyphLoader* glyphs, Renderer* renderer)
    : glyphs(glyphs), gpu(renderer->getGpu()), renderer(renderer) {
  log_zone;

  {
    log_zone_named("Bind script API");

    scripts = new ScriptEnvironment;
    scripts->linkUiApis(this);
  }

  {  // Temp panel
    UiPanel* temp_panel = new UiPanel(glyphs, scripts);
    panels.push_back(temp_panel);
  }

  {
    log_zone_named("Create shaders");

    panel_vertex_shader =
        new GpuShader(gpu, VK_SHADER_STAGE_VERTEX_BIT, shaders_panel_vert,
                      sizeof(shaders_panel_vert));
    panel_fragment_shader =
        new GpuShader(gpu, VK_SHADER_STAGE_FRAGMENT_BIT, shaders_panel_frag,
                      sizeof(shaders_panel_frag));
  }

  {
    log_zone_named("Create set layouts");

    panel_layout = new GpuDescriptorSetLayout(gpu);
    panel_layout->addStorageBuffer(sizeof(PanelUniform));

    glyph_set_layout = new GpuDescriptorSetLayout(gpu);
    glyph_set_layout->addCombinedImageSampler(glyphs->getSampler());
    glyph_set_layout->addStorageBuffer(sizeof(GlyphUniform));
  }

  {
    log_zone_named("Create panel pipeline layout");

    types::vector<VkDescriptorSetLayout> set_layouts{
        renderer->getViewportLayout()->getSetLayout(),
        panel_layout->getSetLayout()};

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = static_cast<uint32_t>(set_layouts.size());
    layout_info.pSetLayouts = set_layouts.data();

    if (vkCreatePipelineLayout(gpu->device, &layout_info, nullptr,
                               &panel_pipeline_layout) != VK_SUCCESS) {
      log_ftl("Failed to create pipeline layout");
    }
  }

  {
    log_zone_named("Create glyph pipeline layout");

    types::vector<VkDescriptorSetLayout> set_layouts{
        renderer->getViewportLayout()->getSetLayout(),
        glyph_set_layout->getSetLayout()};

    VkPipelineLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = static_cast<uint32_t>(set_layouts.size());
    layout_info.pSetLayouts = set_layouts.data();

    if (vkCreatePipelineLayout(gpu->device, &layout_info, nullptr,
                               &glyph_pipeline_layout) != VK_SUCCESS) {
      log_ftl("Failed to create pipeline layout.");
    }
  }

  {
    log_zone_named("Create panel pipeline");

    GpuPipeline::VertexBindings vertex_bindings;
    GpuPipeline::AttributeDescriptions attribute_descriptions;

    panel_pipeline = new GpuPipeline(
        gpu, panel_pipeline_layout, renderer->getViewportRenderPass(),
        renderer->getTransparentSubpass(), panel_vertex_shader,
        panel_fragment_shader, vertex_bindings, attribute_descriptions);
  }

  {
    log_zone_named("Create glyph pipeline");

    auto vertex_bindings = GlyphInstance::getVertexBindings();
    auto attribute_descriptions = GlyphInstance::getAttributeDescriptions();

    glyph_pipeline = new GpuPipeline(
        gpu, glyph_pipeline_layout, renderer->getViewportRenderPass(),
        renderer->getTransparentSubpass(), glyphs->getVertexShader(),
        glyphs->getFragmentShader(), vertex_bindings, attribute_descriptions);
  }
}

UserInterface::~UserInterface() {
  log_zone;

  if (glyph_pipeline != nullptr) delete glyph_pipeline;
  if (glyph_pipeline_layout != VK_NULL_HANDLE)
    vkDestroyPipelineLayout(gpu->device, glyph_pipeline_layout, nullptr);
  if (glyph_set_layout != nullptr) delete glyph_set_layout;

  if (panel_pipeline != nullptr) delete panel_pipeline;
  if (panel_pipeline_layout != VK_NULL_HANDLE)
    vkDestroyPipelineLayout(gpu->device, panel_pipeline_layout, nullptr);
  if (panel_vertex_shader != nullptr) delete panel_vertex_shader;
  if (panel_fragment_shader != nullptr) delete panel_fragment_shader;
  if (scripts != nullptr) delete scripts;

  for (auto panel : panels) {
    if (panel != nullptr) delete panel;
  }
}

void UserInterface::createFrameData(uint32_t frame_count) {
  log_zone;

  frame_data.resize(frame_count);

  for (auto& frame : frame_data) {
    frame.panels = new GpuVector(gpu, sizeof(PanelUniform),
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    frame.glyph_instances = new GpuVector(gpu, sizeof(GlyphInstance),
                                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  }
}

void UserInterface::destroyFrameData() {
  log_zone;

  for (auto& frame : frame_data) {
    if (frame.panels != nullptr) delete frame.panels;
    if (frame.glyph_instances != nullptr) delete frame.glyph_instances;
  }
}

void UserInterface::beginFrame(uint32_t frame_index,
                               GpuDescriptorPool* descriptor_pool) {
  log_zone;

  renderer->addPassToPhase(RenderPhase::Transparent, this);

  current_frame = frame_index;
  auto& frame = frame_data[current_frame];

  frame.panel_count = 0;

  for (auto panel : panels) {
    PanelUniform panel_uniform{};
    panel->writeUniform(&panel_uniform);
    frame.panels->writeElement(frame.panel_count, panel_uniform);
    frame.panel_count++;
  }

  frame.panels_descriptor = descriptor_pool->allocate(panel_layout);
  frame.panels_descriptor->updateStorageBuffer(0, frame.panels);

  frame.glyph_descriptor = descriptor_pool->allocate(glyph_set_layout);
  frame.glyph_descriptor->updateImage(0, glyphs->getAtlas());
  frame.glyph_descriptor->updateStorageBuffer(1, glyphs->getGlyphs());

  frame.glyph_count = 0;

  GlyphString test_string;
  glyphs->drawString(&test_string,
                     "The quick brown fox jumps over the lazy dog.");

  for (uint32_t i = 0; i < test_string.size(); i++) {
    frame.glyph_instances->writeElement(frame.glyph_count, test_string[i]);
    frame.glyph_count++;
  }
}

void UserInterface::renderViewport(
    RenderPhase phase, VkCommandBuffer command_buffer,
    const GpuDescriptorSet* viewport_descriptor) {
  log_zone;

  auto& frame = frame_data[current_frame];

  {
    log_zone_named("Render panels");

    {
      GraphicsState graphics_state;

      GraphicsState::InputAssemblyState input_assembly_state{};
      input_assembly_state.primitive_topology =
          GraphicsState::PrimitiveTopology::TriangleStrip;
      input_assembly_state.primitive_restart_enable =
          GraphicsState::BoolFlag::False;
      graphics_state.input_assembly_state = input_assembly_state;

      GraphicsState::RasterizatonState rasterization_state{};
      rasterization_state.polygon_mode = GraphicsState::PolygonMode::Fill;
      rasterization_state.cull_mode = GraphicsState::CullMode::None;
      graphics_state.rasterization_state = rasterization_state;

      GraphicsState::DepthState depth_state{};
      depth_state.test_enable = GraphicsState::BoolFlag::True;
      depth_state.write_enable = GraphicsState::BoolFlag::False;
      depth_state.compare_op = GraphicsState::CompareOp::Less;
      graphics_state.depth_state = depth_state;

      GraphicsState::ColorBlendState color_blend_state{};
      color_blend_state.blend_mode = GraphicsState::BlendMode::AlphaBlend;
      graphics_state.color_blend_state = color_blend_state;

      panel_pipeline->cmdBind(command_buffer, graphics_state);
    }

    viewport_descriptor->cmdBind(command_buffer, panel_pipeline_layout, 0);
    frame.panels_descriptor->cmdBind(command_buffer, panel_pipeline_layout, 1);
    vkCmdDraw(command_buffer, 4, frame.panel_count, 0, 0);
  }

  {
    log_zone_named("Render glyphs");

    {
      GraphicsState graphics_state;

      GraphicsState::InputAssemblyState input_assembly_state{};
      input_assembly_state.primitive_topology =
          GraphicsState::PrimitiveTopology::TriangleStrip;
      input_assembly_state.primitive_restart_enable =
          GraphicsState::BoolFlag::False;
      graphics_state.input_assembly_state = input_assembly_state;

      GraphicsState::RasterizatonState rasterization_state{};
      rasterization_state.polygon_mode = GraphicsState::PolygonMode::Fill;
      rasterization_state.cull_mode = GraphicsState::CullMode::None;
      graphics_state.rasterization_state = rasterization_state;

      GraphicsState::DepthState depth_state{};
      depth_state.test_enable = GraphicsState::BoolFlag::False;
      depth_state.write_enable = GraphicsState::BoolFlag::False;
      depth_state.compare_op = GraphicsState::CompareOp::Always;
      graphics_state.depth_state = depth_state;

      glyph_pipeline->cmdBind(command_buffer, graphics_state);
    }

    viewport_descriptor->cmdBind(command_buffer, glyph_pipeline_layout, 0);
    frame.glyph_descriptor->cmdBind(command_buffer, glyph_pipeline_layout, 1);

    VkBuffer vertex_buffers[] = {frame.glyph_instances->getBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);
    vkCmdDraw(command_buffer, 4, frame.glyph_count, 0, 0);
  }
}

}  // namespace core
}  // namespace mondradiko
