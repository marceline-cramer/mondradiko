// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "lib/include/vulkan_headers.h"
#include "types/containers/string.h"
#include "types/containers/vector.h"

namespace mondradiko {

// Forward declarations
class GpuInstance;
class Renderer;
class Viewport;

struct VulkanRequirements {
  uint32_t min_api_version;
  uint32_t max_api_version;

  types::vector<types::string> instance_extensions;
  types::vector<types::string> device_extensions;
};

struct DisplayPollEventsInfo {
  Renderer* renderer;
  bool should_run;
  bool should_quit;
};

struct DisplayBeginFrameInfo {
  double dt;
  bool should_render;
};

class DisplayInterface {
 public:
  virtual ~DisplayInterface() {}

  virtual bool getVulkanRequirements(VulkanRequirements*) = 0;
  virtual bool getVulkanDevice(VkInstance, VkPhysicalDevice*) = 0;
  virtual bool createSession(GpuInstance*) = 0;
  virtual void destroySession() = 0;

  virtual VkFormat getSwapchainFormat() = 0;
  virtual VkImageLayout getFinalLayout() = 0;
  virtual VkFormat getDepthFormat() = 0;

  virtual void pollEvents(DisplayPollEventsInfo*) = 0;
  virtual void beginFrame(DisplayBeginFrameInfo*) = 0;
  virtual void acquireViewports(types::vector<Viewport*>*) = 0;
  virtual void endFrame(DisplayBeginFrameInfo*) = 0;

 private:
};

}  // namespace mondradiko
