// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "core/displays/SdlDisplay.h"

#include <chrono>

#include "core/avatars/SpectatorAvatar.h"
#include "core/cvars/CVarScope.h"
#include "core/cvars/FloatCVar.h"
#include "core/displays/SdlViewport.h"
#include "core/gpu/GpuInstance.h"
#include "core/ui/UserInterface.h"
#include "log/log.h"
#include "types/build_config.h"

namespace mondradiko {
namespace core {

void SdlDisplay::initCVars(CVarScope* cvars) {
  CVarScope* sdl = cvars->addChild("sdl");

  sdl->addValue<FloatCVar>("camera_speed", 0.0, 1000.0);
}

SdlDisplay::SdlDisplay(const CVarScope* cvars) : cvars(cvars->getChild("sdl")) {
  log_zone;

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    log_ftl_fmt("Failed to initialize SDL: %s", SDL_GetError());
  }

  window = SDL_CreateWindow(
      MONDRADIKO_NAME, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 800,
      600, SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

  if (!window) {
    log_ftl_fmt("Failed to create SDL window: %s", SDL_GetError());
  }

  key_state = SDL_GetKeyboardState(nullptr);
}

SdlDisplay::~SdlDisplay() {
  log_zone;

  if (window != nullptr) SDL_DestroyWindow(window);
  SDL_Quit();
}

bool SdlDisplay::getVulkanRequirements(VulkanRequirements* requirements) {
  log_zone;

  requirements->min_api_version = VK_MAKE_VERSION(1, 0, 0);
  requirements->max_api_version = VK_MAKE_VERSION(1, 2, 0);
  requirements->instance_extensions.resize(0);
  requirements->device_extensions.resize(0);

  uint32_t instance_extension_count;
  SDL_Vulkan_GetInstanceExtensions(window, &instance_extension_count, nullptr);
  types::vector<const char*> instance_extension_names(instance_extension_count);
  SDL_Vulkan_GetInstanceExtensions(window, &instance_extension_count,
                                   instance_extension_names.data());

  for (auto& instance_extension : instance_extension_names) {
    requirements->instance_extensions.push_back(instance_extension);
  }

  requirements->device_extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

  return true;
}

bool SdlDisplay::getVulkanDevice(VkInstance instance,
                                 VkPhysicalDevice* physical_device) {
  log_zone;

  if (SDL_Vulkan_CreateSurface(window, instance, &surface) != SDL_TRUE) {
    log_err_fmt("Failed to create SDL window surface: %s", SDL_GetError());
    return false;
  }

  uint32_t device_count = 0;
  vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
  types::vector<VkPhysicalDevice> devices(device_count);
  vkEnumeratePhysicalDevices(instance, &device_count, devices.data());

  // TODO(marceline-cramer) Move physical device selection/queue creation to
  // VulkanInstance
  // TODO(marceline-cramer) Pick best device
  if (device_count > 0) {
    *physical_device = devices[0];

    uint32_t queue_family_count;
    vkGetPhysicalDeviceQueueFamilyProperties(*physical_device,
                                             &queue_family_count, nullptr);
    types::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(
        *physical_device, &queue_family_count, queue_families.data());

    bool any_present_support = false;
    for (uint32_t i = 0; i < queue_family_count; i++) {
      VkBool32 present_support = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(*physical_device, i, surface,
                                           &present_support);

      if (present_support) any_present_support = true;
    }

    if (!any_present_support) {
      log_err("No Vulkan physical device supports a present queue.");
      return false;
    }
  } else {
    log_err("Could not find suitable Vulkan physical device.");
    return false;
  }

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(*physical_device, surface,
                                            &surface_capabilities);

  uint32_t format_count;
  vkGetPhysicalDeviceSurfaceFormatsKHR(*physical_device, surface, &format_count,
                                       nullptr);
  types::vector<VkSurfaceFormatKHR> surface_formats(format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(*physical_device, surface, &format_count,
                                       surface_formats.data());

  swapchain_format = VK_FORMAT_MAX_ENUM;
  for (const auto& surface_format : surface_formats) {
    if (surface_format.format == VK_FORMAT_B8G8R8A8_SRGB &&
        surface_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      swapchain_format = surface_format.format;
      swapchain_color_space = surface_format.colorSpace;
    }
  }

  if (swapchain_format == VK_FORMAT_MAX_ENUM) {
    log_err("Failed to find suitable surface format.");
    return false;
  }

  uint32_t present_mode_count;
  vkGetPhysicalDeviceSurfacePresentModesKHR(*physical_device, surface,
                                            &present_mode_count, nullptr);
  types::vector<VkPresentModeKHR> present_modes(present_mode_count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(
      *physical_device, surface, &present_mode_count, present_modes.data());

  swapchain_present_mode = VK_PRESENT_MODE_MAX_ENUM_KHR;
  for (const auto& surface_present_mode : present_modes) {
    if (surface_present_mode == VK_PRESENT_MODE_MAILBOX_KHR ||
        surface_present_mode == VK_PRESENT_MODE_FIFO_KHR) {
      swapchain_present_mode = surface_present_mode;
    }
  }

  if (swapchain_present_mode == VK_PRESENT_MODE_MAX_ENUM_KHR) {
    log_err("Failed to find suitable swapchain present mode.");
    return false;
  }

  return true;
}

bool SdlDisplay::createSession(GpuInstance* _gpu) {
  log_zone;

  gpu = _gpu;

  types::vector<VkFormat> depth_format_options = {VK_FORMAT_D32_SFLOAT,
                                                  VK_FORMAT_D32_SFLOAT_S8_UINT,
                                                  VK_FORMAT_D24_UNORM_S8_UINT};

  if (!gpu->findSupportedFormat(&depth_format_options, VK_IMAGE_TILING_OPTIMAL,
                                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                &depth_format)) {
    log_err("Failed to find supported depth format.");
    return false;
  }

  return true;
}

void SdlDisplay::setUserInterface(UserInterface* new_ui) { ui = new_ui; }

const Avatar* SdlDisplay::getAvatar(World* world) {
  avatar = new SpectatorAvatar(world);
  return avatar;
}

void SdlDisplay::destroySession() {
  log_zone;

  vkDeviceWaitIdle(gpu->device);

  if (avatar != nullptr) delete avatar;
  if (main_viewport != nullptr) delete main_viewport;
  if (surface != VK_NULL_HANDLE)
    vkDestroySurfaceKHR(gpu->instance, surface, nullptr);

  avatar = nullptr;
  main_viewport = nullptr;
  surface = VK_NULL_HANDLE;
}

void SdlDisplay::pollEvents(DisplayPollEventsInfo* poll_info) {
  log_zone;

  if (main_viewport == nullptr) {
    main_viewport = new SdlViewport(gpu, this, poll_info->renderer, avatar);
  }

  poll_info->should_quit = false;
  poll_info->should_run = true;

  mouse_x = 0.0;
  mouse_y = 0.0;

  SDL_Event e;
  while (SDL_PollEvent(&e) != 0) {
    switch (e.type) {
      case SDL_QUIT: {
        poll_info->should_quit = true;
        poll_info->should_run = false;
        break;
      }

      case SDL_MOUSEMOTION: {
        mouse_x += e.motion.xrel;
        mouse_y += e.motion.yrel;
        break;
      }

      case SDL_MOUSEBUTTONDOWN: {
        if (e.button.button == SDL_BUTTON_LEFT) {
          if (SDL_GetRelativeMouseMode() == SDL_FALSE) {
            SDL_SetRelativeMouseMode(SDL_TRUE);
          } else if (avatar != nullptr) {
            avatar->onClick();
          }
        }

        break;
      }

      case SDL_KEYDOWN: {
        switch (e.key.keysym.scancode) {
          case SDL_SCANCODE_ESCAPE: {
            SDL_SetRelativeMouseMode(SDL_FALSE);
            break;
          }

          case SDL_SCANCODE_F5: {
            if (ui != nullptr) {
              ui->loadUiScript();
            }
            break;
          }

          default:
            break;
        }  // switch (e.key)
        break;
      }

      case SDL_WINDOWEVENT: {
        switch (e.window.event) {
          case SDL_WINDOWEVENT_SIZE_CHANGED: {
            if (main_viewport) {
              delete main_viewport;
              main_viewport = nullptr;
            }

            break;
          }

          case SDL_WINDOWEVENT_CLOSE: {
            poll_info->should_quit = true;
            poll_info->should_run = false;
            break;
          }

          default:
            break;
        }  // switch (e.window.event)

        break;
      }

      default:
        break;
    }  // switch (e.type)
  }
}

void SdlDisplay::beginFrame(DisplayBeginFrameInfo* frame_info) {
  log_zone;

  {
    log_zone_named("Calculate delta time");

    // TODO(marceline-cramer) Find a better way to do this
    using clock = std::chrono::high_resolution_clock;
    static const auto start_time = clock::now();
    auto elapsed_time = clock::now();

    double current_time =
        std::chrono::duration<double, std::chrono::seconds::period>(
            elapsed_time - start_time)
            .count();

    if (last_frame_time < -1.0) {
      frame_info->dt = 0.0;
    } else {
      frame_info->dt = current_time - last_frame_time;
    }

    last_frame_time = current_time;
  }

  if (main_viewport != nullptr) {
    if (avatar != nullptr && SDL_GetRelativeMouseMode() == SDL_TRUE) {
      float camera_speed = cvars->get<FloatCVar>("camera_speed");
      camera_speed *= frame_info->dt;

      if (key_state[SDL_SCANCODE_LSHIFT]) {
        camera_speed *= 2.0;
      }

      float truck = 0.0;

      if (key_state[SDL_SCANCODE_W]) {
        truck = camera_speed;
      } else if (key_state[SDL_SCANCODE_S]) {
        truck = -camera_speed;
      }

      float dolly = 0.0;

      if (key_state[SDL_SCANCODE_D]) {
        dolly = -camera_speed;
      } else if (key_state[SDL_SCANCODE_A]) {
        dolly = camera_speed;
      }

      float boom = 0.0;

      if (key_state[SDL_SCANCODE_Q]) {
        boom = -camera_speed;
      } else if (key_state[SDL_SCANCODE_E]) {
        boom = camera_speed;
      }

      avatar->moveCamera(mouse_x * 0.003, mouse_y * 0.003, truck, dolly, boom);
    }

    frame_info->should_render = true;
  } else {
    frame_info->should_render = false;
  }
}

void SdlDisplay::acquireViewports(types::vector<Viewport*>* viewports) {
  log_zone;

  viewports->resize(1);
  viewports->at(0) = main_viewport;
}

void SdlDisplay::endFrame(DisplayBeginFrameInfo* frame_info) { log_zone; }

}  // namespace core
}  // namespace mondradiko
