// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "core/renderer/RenderPass.h"
#include "lib/include/wasm_headers.h"
#include "types/containers/string.h"
#include "types/containers/vector.h"

namespace mondradiko {
namespace core {

// Forward declarations
class CVarScope;
class DebugDrawList;
class Filesystem;
class GlyphLoader;
class GpuDescriptorSetLayout;
class GpuInstance;
class GpuPipeline;
class GpuShader;
class GpuVector;
class Renderer;
class UiDrawList;
class UiPanel;
class UiScript;
class UiScriptEnvironment;
class World;

class UserInterface : public RenderPass {
 public:
  static void initCVars(CVarScope*);

  UserInterface(const CVarScope*, Filesystem*, GlyphLoader*, Renderer*, World*);
  ~UserInterface();

  void loadUiScript();
  void displayMessage(const char*);
  bool update(double, DebugDrawList*);

  // RenderPass implementation
  void createFrameData(uint32_t) final;
  void destroyFrameData() final;
  void beginFrame(uint32_t, uint32_t, GpuDescriptorPool*) final;
  void render(RenderPhase, VkCommandBuffer) final {}
  void renderViewport(VkCommandBuffer, uint32_t, RenderPhase,
                      const GpuDescriptorSet*) final;
  void endFrame() final {}

 private:
  const CVarScope* cvars;
  Filesystem* fs;
  GlyphLoader* glyphs;
  GpuInstance* gpu;
  Renderer* renderer;
  World* world;

  UiScriptEnvironment* scripts = nullptr;
  wasm_module_t* script_module = nullptr;
  UiScript* ui_script = nullptr;

  types::vector<UiPanel*> panels;

  GpuShader* panel_vertex_shader = nullptr;
  GpuShader* panel_fragment_shader = nullptr;
  GpuDescriptorSetLayout* panel_layout = nullptr;
  VkPipelineLayout panel_pipeline_layout = VK_NULL_HANDLE;
  GpuPipeline* panel_pipeline = nullptr;

  GpuShader* ui_vertex_shader = nullptr;
  GpuShader* ui_fragment_shader = nullptr;
  GpuPipeline* ui_pipeline = nullptr;

  GpuDescriptorSetLayout* glyph_set_layout = nullptr;
  VkPipelineLayout glyph_pipeline_layout = VK_NULL_HANDLE;
  GpuPipeline* glyph_pipeline = nullptr;

  UiDrawList* current_draw = nullptr;

  struct FrameData {
    GpuVector* panels = nullptr;
    uint32_t panel_count;
    GpuVector* glyph_instances = nullptr;
    uint32_t glyph_count;

    GpuVector* styles = nullptr;

    GpuVector* ui_draw_vertices = nullptr;
    GpuVector* ui_draw_indices = nullptr;
    uint32_t ui_draw_count;

    GpuDescriptorSet* panels_descriptor = nullptr;
    GpuDescriptorSet* glyph_descriptor = nullptr;
  };

  types::vector<FrameData> frame_data;
  uint32_t current_frame;
};

}  // namespace core
}  // namespace mondradiko
