// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "core/displays/Display.h"
#include "lib/include/sdl2_headers.h"
#include "types/containers/vector.h"

namespace mondradiko {
namespace core {

// Forward declarations
class CVarScope;
class GpuInstance;
class SdlViewport;
class SpectatorAvatar;

class SdlDisplay : public Display {
 public:
  static void initCVars(CVarScope*);

  explicit SdlDisplay(const CVarScope*);
  ~SdlDisplay();

  bool getVulkanRequirements(VulkanRequirements*) final;
  bool getVulkanDevice(VkInstance, VkPhysicalDevice*) final;
  bool createSession(GpuInstance*) final;
  void setUserInterface(UserInterface*) final;
  const Avatar* getAvatar(World*) final;
  void destroySession() final;

  VkFormat getSwapchainFormat() final { return swapchain_format; }
  VkImageLayout getFinalLayout() final {
    // SDL expects the final swapchain image layout to be
    // _PRESENT_SRC_KHR
    return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  }
  VkFormat getDepthFormat() final { return depth_format; }

  void pollEvents(PollEventsInfo*) final;
  void beginFrame(BeginFrameInfo*) final;
  void acquireViewports(types::vector<Viewport*>*) final;
  void endFrame(BeginFrameInfo*) final;

  SDL_Window* window = nullptr;

  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkSurfaceCapabilitiesKHR surface_capabilities;

  VkFormat swapchain_format;
  VkColorSpaceKHR swapchain_color_space;
  VkPresentModeKHR swapchain_present_mode;

  VkFormat depth_format;

  SdlViewport* main_viewport = nullptr;

 private:
  const CVarScope* cvars;

  UserInterface* ui = nullptr;

  // TODO(marceline-cramer) Move this into GpuInstance
  uint32_t present_queue_family;
  VkQueue present_queue = VK_NULL_HANDLE;

  SpectatorAvatar* avatar = nullptr;

  double mouse_x;
  double mouse_y;

  double last_frame_time = -1.0;

  const Uint8* key_state;
};

}  // namespace core
}  // namespace mondradiko
