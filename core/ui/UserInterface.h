// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "core/renderer/RenderPass.h"
#include "types/containers/string.h"
#include "types/containers/vector.h"

namespace mondradiko {
namespace core {

// Forward declarations
class GlyphLoader;
class GpuDescriptorSetLayout;
class GpuInstance;
class GpuPipeline;
class GpuShader;
class GpuVector;
class Renderer;
class ScriptEnvironment;
class UiPanel;

class UserInterface : public RenderPass {
 public:
  UserInterface(GlyphLoader*, Renderer*);
  ~UserInterface();

  void displayMessage(const char*);

  // RenderPass implementation
  void createFrameData(uint32_t) final;
  void destroyFrameData() final;
  void beginFrame(uint32_t, GpuDescriptorPool*) final;
  void render(RenderPhase, VkCommandBuffer) final {}
  void renderViewport(RenderPhase, VkCommandBuffer,
                      const GpuDescriptorSet*) final;
  void endFrame() final {}

 private:
  GlyphLoader* glyphs;
  GpuInstance* gpu;
  Renderer* renderer;

  ScriptEnvironment* scripts = nullptr;

  types::vector<UiPanel*> panels;

  types::string messages;

  GpuShader* panel_vertex_shader = nullptr;
  GpuShader* panel_fragment_shader = nullptr;
  GpuDescriptorSetLayout* panel_layout = nullptr;
  VkPipelineLayout panel_pipeline_layout = VK_NULL_HANDLE;
  GpuPipeline* panel_pipeline = nullptr;

  GpuDescriptorSetLayout* glyph_set_layout = nullptr;
  VkPipelineLayout glyph_pipeline_layout = VK_NULL_HANDLE;
  GpuPipeline* glyph_pipeline = nullptr;

  struct FrameData {
    GpuVector* panels = nullptr;
    uint32_t panel_count;
    GpuVector* glyph_instances = nullptr;
    uint32_t glyph_count;

    GpuVector* styles = nullptr;

    GpuDescriptorSet* panels_descriptor = nullptr;
    GpuDescriptorSet* glyph_descriptor = nullptr;
  };

  types::vector<FrameData> frame_data;
  uint32_t current_frame;
};

}  // namespace core
}  // namespace mondradiko
