// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "core/displays/Viewport.h"
#include "lib/include/openxr_headers.h"

namespace mondradiko {
namespace core {

// Forward declaration
class GpuImage;
class GpuInstance;
class OpenXrDisplay;
class Renderer;

class OpenXrViewport : public Viewport {
 public:
  OpenXrViewport(GpuInstance*, OpenXrDisplay*, Renderer*,
                 XrViewConfigurationView*);
  ~OpenXrViewport();

  // Viewport implementation
  void writeUniform(ViewportUniform*) final;
  bool isSignalRequired() final { return false; }

  void updateView(const XrView&);
  void writeCompositionLayers(XrCompositionLayerProjectionView*);

 private:
  GpuInstance* gpu;
  OpenXrDisplay* display;
  Renderer* renderer;

  // Viewport implementation
  VkSemaphore _acquireImage(uint32_t*) final;
  void _releaseImage(uint32_t, VkSemaphore) final;

  XrSwapchain swapchain = XR_NULL_HANDLE;
  XrView view;
};

}  // namespace core
}  // namespace mondradiko
