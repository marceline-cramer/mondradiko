// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "core/displays/Viewport.h"
#include "lib/include/sdl2_headers.h"
#include "types/containers/vector.h"

namespace mondradiko {
namespace core {

// Forward declarations
class GpuImage;
class GpuInstance;
class Renderer;
class SdlDisplay;

class SdlViewport : public Viewport {
 public:
  SdlViewport(GpuInstance*, SdlDisplay*, Renderer*);
  ~SdlViewport();

  // Viewport implementation
  void writeUniform(ViewportUniform*) final;
  bool isSignalRequired() final { return true; }

  void moveCamera(float, float, float, float, float);

 private:
  GpuInstance* gpu;
  SdlDisplay* display;
  Renderer* renderer;

  // Viewport implementation
  VkSemaphore _acquireImage(uint32_t*) final;
  void _releaseImage(uint32_t, VkSemaphore) final;

  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  uint32_t acquire_image_index = 0;
  types::vector<VkSemaphore> on_image_acquire;

  glm::vec3 camera_position;
  float camera_pan;
  float camera_tilt;
};

}  // namespace core
}  // namespace mondradiko
