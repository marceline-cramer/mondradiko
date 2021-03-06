// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "core/ui/UserInterface.h"

#include "core/components/internal/PointerComponent.h"
#include "core/components/internal/WorldTransform.h"
#include "core/cvars/CVarScope.h"
#include "core/cvars/FileCVar.h"
#include "core/cvars/StringCVar.h"
#include "core/displays/Viewport.h"
#include "core/gpu/GpuDescriptorPool.h"
#include "core/gpu/GpuDescriptorSet.h"
#include "core/gpu/GpuDescriptorSetLayout.h"
#include "core/gpu/GpuInstance.h"
#include "core/gpu/GpuPipeline.h"
#include "core/gpu/GpuShader.h"
#include "core/gpu/GpuVector.h"
#include "core/gpu/GraphicsState.h"
#include "core/renderer/DebugDraw.h"
#include "core/renderer/Renderer.h"
#include "core/scripting/environment/UiScriptEnvironment.h"
#include "core/scripting/instance/UiScript.h"
#include "core/shaders/panel.frag.h"
#include "core/shaders/panel.vert.h"
#include "core/shaders/ui_draw.frag.h"
#include "core/shaders/ui_draw.vert.h"
#include "core/ui/UiDrawList.h"
#include "core/ui/glyph/GlyphLoader.h"
#include "core/ui/glyph/GlyphStyle.h"
#include "core/ui/panels/UiPanel.h"
#include "core/world/World.h"
#include "log/log.h"

namespace mondradiko {
namespace core {

void UserInterface::initCVars(CVarScope* cvars) {
  CVarScope* ui = cvars->addChild("ui");

  ui->addValue<FileCVar>("script_path");
  ui->addValue<StringCVar>("panel_impl");
}

UserInterface::UserInterface(const CVarScope* _cvars, Filesystem* fs,
                             GlyphLoader* glyphs, Renderer* renderer,
                             World* world)
    : cvars(_cvars->getChild("ui")),
      fs(fs),
      glyphs(glyphs),
      gpu(renderer->getGpu()),
      renderer(renderer),
      world(world) {
  log_zone;

  {
    log_zone_named("Bind script API");

    scripts = new UiScriptEnvironment(this);
  }

  {  // Temp panel
    UiPanel* temp_panel = new UiPanel(glyphs, scripts);
    panels.push_back(temp_panel);
  }

  {
    log_zone_named("Load initial UI script");

    loadUiScript();
  }

  {
    log_zone_named("Create shaders");

    panel_vertex_shader =
        new GpuShader(gpu, VK_SHADER_STAGE_VERTEX_BIT, shaders_panel_vert,
                      sizeof(shaders_panel_vert));
    panel_fragment_shader =
        new GpuShader(gpu, VK_SHADER_STAGE_FRAGMENT_BIT, shaders_panel_frag,
                      sizeof(shaders_panel_frag));
    ui_vertex_shader =
        new GpuShader(gpu, VK_SHADER_STAGE_VERTEX_BIT, shaders_ui_draw_vert,
                      sizeof(shaders_ui_draw_vert));
    ui_fragment_shader =
        new GpuShader(gpu, VK_SHADER_STAGE_FRAGMENT_BIT, shaders_ui_draw_frag,
                      sizeof(shaders_ui_draw_frag));
  }

  {
    log_zone_named("Create set layouts");

    panel_layout = new GpuDescriptorSetLayout(gpu);
    panel_layout->addStorageBuffer(sizeof(PanelUniform));

    glyph_set_layout = new GpuDescriptorSetLayout(gpu);
    glyph_set_layout->addCombinedImageSampler(glyphs->getSampler());
    glyph_set_layout->addStorageBuffer(sizeof(GlyphStyleUniform));
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
        gpu, panel_pipeline_layout, renderer->getMainRenderPass(),
        renderer->getOverlaySubpass(), panel_vertex_shader,
        panel_fragment_shader, vertex_bindings, attribute_descriptions);
  }

  {
    log_zone_named("Create UI pipeline");

    auto vertex_bindings = UiDrawList::Vertex::getVertexBindings();
    auto attribute_descriptions =
        UiDrawList::Vertex::getAttributeDescriptions();

    ui_pipeline = new GpuPipeline(
        gpu, panel_pipeline_layout, renderer->getMainRenderPass(),
        renderer->getOverlaySubpass(), ui_vertex_shader, ui_fragment_shader,
        vertex_bindings, attribute_descriptions);
  }

  {
    log_zone_named("Create glyph pipeline");

    auto vertex_bindings = GlyphInstance::getVertexBindings();
    auto attribute_descriptions = GlyphInstance::getAttributeDescriptions();

    glyph_pipeline = new GpuPipeline(
        gpu, glyph_pipeline_layout, renderer->getMainRenderPass(),
        renderer->getOverlaySubpass(), glyphs->getVertexShader(),
        glyphs->getFragmentShader(), vertex_bindings, attribute_descriptions);
  }

  {
    log_zone_named("Create UI draw list");

    current_draw = new UiDrawList;
  }
}

UserInterface::~UserInterface() {
  log_zone;

  if (current_draw != nullptr) delete current_draw;

  if (glyph_pipeline != nullptr) delete glyph_pipeline;
  if (glyph_pipeline_layout != VK_NULL_HANDLE)
    vkDestroyPipelineLayout(gpu->device, glyph_pipeline_layout, nullptr);
  if (glyph_set_layout != nullptr) delete glyph_set_layout;

  if (ui_pipeline != nullptr) delete ui_pipeline;

  if (panel_pipeline != nullptr) delete panel_pipeline;
  if (panel_pipeline_layout != VK_NULL_HANDLE)
    vkDestroyPipelineLayout(gpu->device, panel_pipeline_layout, nullptr);
  if (panel_layout != nullptr) delete panel_layout;
  if (ui_vertex_shader != nullptr) delete ui_vertex_shader;
  if (ui_fragment_shader != nullptr) delete ui_fragment_shader;
  if (panel_vertex_shader != nullptr) delete panel_vertex_shader;
  if (panel_fragment_shader != nullptr) delete panel_fragment_shader;

  for (auto panel : panels) {
    if (panel != nullptr) delete panel;
  }

  if (ui_script != nullptr) delete ui_script;
  if (script_module != nullptr) wasm_module_delete(script_module);
  if (scripts != nullptr) delete scripts;
}

void UserInterface::loadUiScript() {
  log_zone;

  UiScript* old_script = ui_script;

  {
    log_zone_named("Load UI script module");

    auto script_path = cvars->get<FileCVar>("script_path").getPath();

    log_msg_fmt("Loading UI script from: %s", script_path.c_str());

    types::vector<char> script_data;
    if (!fs->loadBinaryFile(script_path, &script_data)) {
      log_ftl("Failed to load UI script file");
    }

    script_module = scripts->loadBinaryModule(script_data);
    if (script_module == nullptr) {
      log_ftl("Failed to load UI script module");
    }
  }

  {
    log_zone_named("Instantiate UI script");

    ui_script = new UiScript(scripts, script_module);
  }

  {
    log_zone_named("Bind to panels");

    auto panel_impl = cvars->get<StringCVar>("panel_impl").str();

    for (auto panel : panels) {
      panel->bindUiScript(ui_script, panel_impl);
    }
  }

  {
    log_zone_named("Destroy old UI script");

    if (old_script != nullptr) delete old_script;
  }
}

void UserInterface::displayMessage(const char* message) {
  ui_script->handleMessage(message);
}

bool UserInterface::update(double dt, DebugDrawList* debug_draw) {
  log_zone;

  auto pointers_view = world->registry.view<PointerComponent>();
  for (auto e : pointers_view) {
    auto& pointer = pointers_view.get(e);

    PointerComponent::State pointer_state = pointer.getState();
    pointer._dirty = false;

    glm::mat4 world_transform(1.0);
    if (world->registry.has<WorldTransform>(e)) {
      world_transform = world->registry.get<WorldTransform>(e).getTransform();
    }

    auto position = world_transform * glm::vec4(pointer.getPosition(), 1.0);
    auto direction = world_transform * glm::vec4(pointer.getDirection(), 0.0);

    double nearest_factor = -1.0;
    UiPanel* nearest = nullptr;

    // Find nearest panel
    for (auto panel : panels) {
      if (panel == nullptr) continue;

      // Test collision
      double d = panel->getRayIntersectFactor(position, direction);
      if (d > 0.0) {
        if (d < nearest_factor || nearest_factor < 0.0) {
          nearest_factor = d;
          nearest = panel;
        }
      }
    }

    if (nearest != nullptr) {
      auto coords = nearest->getRayIntersectCoords(position, direction);

      glm::vec3 color;  // Used for debug draw
      switch (pointer_state) {
        case PointerComponent::State::Hover: {
          nearest->onHover(coords);
          color = glm::vec3(0.5, 0.5, 1.0);
          break;
        }

        case PointerComponent::State::Select: {
          nearest->onSelect(coords);
          color = glm::vec3(0.0, 1.0, 0.0);
          break;
        }

        case PointerComponent::State::Drag: {
          nearest->onDrag(coords);
          color = glm::vec3(0.0, 0.0, 1.0);
          break;
        }

        case PointerComponent::State::Deselect: {
          nearest->onDeselect(coords);
          color = glm::vec3(1.0, 0.0, 0.0);
          break;
        }
      }

      // Draw X indicator at the collision point
      if (debug_draw != nullptr) {
        auto plane_transform = nearest->getPlaneTransform();
        glm::vec2 x_size(0.01, 0.01);
        glm::vec3 x_offset = nearest->getNormal() * glm::vec3(0.01);

        // Generate the vertices of the X
        glm::vec3 tr = plane_transform * glm::vec4(coords + x_size, 0.0, 1.0);
        glm::vec3 bl = plane_transform * glm::vec4(coords - x_size, 0.0, 1.0);

        x_size.x = -x_size.x;

        glm::vec3 tl = plane_transform * glm::vec4(coords + x_size, 0.0, 1.0);
        glm::vec3 br = plane_transform * glm::vec4(coords - x_size, 0.0, 1.0);

        // Draw the X
        debug_draw->drawLine(tr + x_offset, bl + x_offset, color);
        debug_draw->drawLine(tl + x_offset, br + x_offset, color);
      }
    }
  }

  current_draw->clear();

  for (auto panel : panels) {
    if (panel != nullptr) {
      panel->update(dt, current_draw);
    }
  }

  return true;
}

void UserInterface::createFrameData(uint32_t frame_count) {
  log_zone;

  frame_data.resize(frame_count);

  for (auto& frame : frame_data) {
    frame.panels = new GpuVector(gpu, sizeof(PanelUniform),
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    frame.glyph_instances = new GpuVector(gpu, sizeof(GlyphInstance),
                                          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    frame.styles = new GpuVector(gpu, sizeof(GlyphStyleUniform),
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    frame.ui_draw_vertices = new GpuVector(gpu, sizeof(UiDrawList::Vertex),
                                           VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    frame.ui_draw_indices = new GpuVector(gpu, sizeof(UiDrawList::Index),
                                          VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
  }
}

void UserInterface::destroyFrameData() {
  log_zone;

  for (auto& frame : frame_data) {
    if (frame.panels != nullptr) delete frame.panels;
    if (frame.glyph_instances != nullptr) delete frame.glyph_instances;
    if (frame.styles != nullptr) delete frame.styles;
    if (frame.ui_draw_vertices != nullptr) delete frame.ui_draw_vertices;
    if (frame.ui_draw_indices != nullptr) delete frame.ui_draw_indices;
  }
}

void UserInterface::beginFrame(uint32_t frame_index, uint32_t viewport_count,
                               GpuDescriptorPool* descriptor_pool) {
  log_zone;

  renderer->addPassToPhase(RenderPhase::Overlay, this);

  current_frame = frame_index;
  auto& frame = frame_data[current_frame];

  frame.panel_count = 0;
  GlyphStyleList styles;
  types::unordered_map<GlyphStyle*, uint32_t> style_indices;
  GlyphString test_string;
  types::vector<PanelUniform> panel_uniforms;

  for (auto panel : panels) {
    PanelUniform panel_uniform{};
    panel->writeUniform(&panel_uniform);
    panel_uniforms.emplace_back(panel_uniform);

    frame.panel_count++;

    auto panel_styles = panel->getStyles();

    for (auto panel_style : panel_styles) {
      uint32_t style_index;
      auto iter = style_indices.find(panel_style);
      if (iter != style_indices.end()) {
        style_index = iter->second;
      } else {
        style_index = styles.size();
        styles.push_back(panel_style);
        style_indices.emplace(panel_style, style_index);
      }

      panel_style->drawString(&test_string, style_index);
    }
  }

  frame.panels->writeData(0, panel_uniforms);

  frame.ui_draw_count =
      current_draw->writeData(frame.ui_draw_vertices, frame.ui_draw_indices);

  frame.glyph_count = test_string.size();
  frame.glyph_instances->writeData(0, test_string);

  types::vector<GlyphStyleUniform> style_uniforms(styles.size());

  for (uint32_t i = 0; i < styles.size(); i++) {
    style_uniforms[i] = styles[i]->getUniform();
  }

  frame.styles->writeData(0, style_uniforms);

  frame.panels_descriptor = descriptor_pool->allocate(panel_layout);
  frame.panels_descriptor->updateStorageBuffer(0, frame.panels);

  frame.glyph_descriptor = descriptor_pool->allocate(glyph_set_layout);
  frame.glyph_descriptor->updateImage(0, glyphs->getAtlas());
  frame.glyph_descriptor->updateStorageBuffer(1, frame.styles);
  frame.glyph_descriptor->updateStorageBuffer(2, glyphs->getGlyphs());
}

void UserInterface::renderViewport(
    VkCommandBuffer command_buffer, uint32_t viewport_index, RenderPhase phase,
    const GpuDescriptorSet* viewport_descriptor) {
  log_zone;

  auto& frame = frame_data[current_frame];

  {
    log_zone_named("Render panels and UI draw");

    GraphicsState gs;

    {
      gs = GraphicsState::CreateGenericOpaque();
      gs.input_assembly_state.primitive_topology =
          GraphicsState::PrimitiveTopology::TriangleStrip;
      gs.rasterization_state.cull_mode = GraphicsState::CullMode::None;
      gs.multisample_state.rasterization_samples =
          renderer->getCurrentViewport(viewport_index)->getSampleCount();
      gs.depth_state.write_enable = GraphicsState::BoolFlag::False;
      gs.color_blend_state.blend_mode =
          GraphicsState::BlendMode::AlphaPremultiplied;
      panel_pipeline->cmdBind(command_buffer, gs);
    }

    viewport_descriptor->cmdBind(command_buffer, panel_pipeline_layout, 0);
    frame.panels_descriptor->cmdBind(command_buffer, panel_pipeline_layout, 1);
    vkCmdDraw(command_buffer, 4, frame.panel_count, 0, 0);

    {
      gs.input_assembly_state.primitive_topology =
          GraphicsState::PrimitiveTopology::TriangleList;
      ui_pipeline->cmdBind(command_buffer, gs);
    }

    VkBuffer vertex_buffers[] = {frame.ui_draw_vertices->getBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);
    vkCmdBindIndexBuffer(command_buffer, frame.ui_draw_indices->getBuffer(), 0,
                         VK_INDEX_TYPE_UINT32);
    viewport_descriptor->cmdBind(command_buffer, panel_pipeline_layout, 0);
    frame.panels_descriptor->cmdBind(command_buffer, panel_pipeline_layout, 1);
    vkCmdDrawIndexed(command_buffer, frame.ui_draw_count, 1, 0, 0, 0);
  }

  {
    log_zone_named("Render glyphs");

    {
      auto gs = GraphicsState::CreateGenericOpaque();
      gs.input_assembly_state.primitive_topology =
          GraphicsState::PrimitiveTopology::TriangleStrip;
      gs.rasterization_state.cull_mode = GraphicsState::CullMode::None;
      gs.multisample_state.rasterization_samples =
          renderer->getCurrentViewport(viewport_index)->getSampleCount();
      gs.depth_state.write_enable = GraphicsState::BoolFlag::False;
      gs.color_blend_state.blend_mode =
          GraphicsState::BlendMode::AlphaPremultiplied;
      glyph_pipeline->cmdBind(command_buffer, gs);
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
