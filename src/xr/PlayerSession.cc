/**
 * @file PlayerSession.cc
 * @author Marceline Cramer (cramermarceline@gmail.com)
 * @brief Handles the lifetime of an OpenXR session, receives events, manages
 * viewports, synchronizes frames, and creates inputs.
 * @date 2020-10-24
 *
 * @copyright Copyright (c) 2020 Marceline Cramer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https: //www.gnu.org/licenses/>.
 *
 */

#include "xr/PlayerSession.h"

#include "graphics/Renderer.h"
#include "log/log.h"

PlayerSession::PlayerSession(XrDisplay* _display,
                             VulkanInstance* _vulkanInstance) {
  log_dbg("Creating OpenXR session.");

  display = _display;
  vulkanInstance = _vulkanInstance;

  XrGraphicsBindingVulkanKHR vulkanBindings{
      .type = XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR,
      .instance = vulkanInstance->instance,
      .physicalDevice = vulkanInstance->physicalDevice,
      .device = vulkanInstance->device,
      .queueFamilyIndex = vulkanInstance->graphicsQueueFamily};

  XrSessionCreateInfo createInfo{.type = XR_TYPE_SESSION_CREATE_INFO,
                                 .next = &vulkanBindings,
                                 .systemId = display->systemId};

  if (xrCreateSession(display->instance, &createInfo, &session) != XR_SUCCESS) {
    log_ftl("Failed to create OpenXR session.");
  }

  XrReferenceSpaceCreateInfo stageSpaceCreateInfo{
      .type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
      .referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE,
      .poseInReferenceSpace = {.orientation = {0.0, 0.0, 0.0, 1.0},
                               .position = {0.0, 0.0, 0.0}}};

  if (xrCreateReferenceSpace(session, &stageSpaceCreateInfo, &stageSpace) !=
      XR_SUCCESS) {
    log_ftl("Failed to create OpenXR stage reference space.");
  }
}

PlayerSession::~PlayerSession() {
  log_dbg("Destroying OpenXR session.");

  if (stageSpace != XR_NULL_HANDLE) xrDestroySpace(stageSpace);
  if (session != XR_NULL_HANDLE) xrDestroySession(session);
}

void PlayerSession::pollEvents(bool* shouldRun, bool* shouldQuit) {
  XrEventDataBuffer event{.type = XR_TYPE_EVENT_DATA_BUFFER};

  while (xrPollEvent(display->instance, &event) == XR_SUCCESS) {
    switch (event.type) {
      // Handle session state change events
      // Quitting, app focus, ready, etc.
      case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
        XrEventDataSessionStateChanged* changed =
            reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
        sessionState = changed->state;

        switch (sessionState) {
          case XR_SESSION_STATE_READY: {
            log_dbg("OpenXR session ready; beginning session.");

            XrSessionBeginInfo beginInfo{
                .type = XR_TYPE_SESSION_BEGIN_INFO,
                .primaryViewConfigurationType =
                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};

            xrBeginSession(session, &beginInfo);
            *shouldRun = true;

            break;
          }

          case XR_SESSION_STATE_VISIBLE: {
            log_dbg("OpenXR session is visible.");
            break;
          }

          case XR_SESSION_STATE_FOCUSED: {
            log_dbg("OpenXR session is focused.");
            break;
          }

          case XR_SESSION_STATE_IDLE: {
            log_dbg("OpenXR session is idle.");
            break;
          }

          case XR_SESSION_STATE_STOPPING:
          case XR_SESSION_STATE_EXITING:
          case XR_SESSION_STATE_LOSS_PENDING: {
            log_dbg("Ending OpenXR session.");
            *shouldQuit = true;
            *shouldRun = false;
            xrEndSession(session);
            break;
          }

          default:
            break;
        }

        break;
      }
      // If the instance is about to be lost,
      // just exit.
      case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
        *shouldQuit = true;
        break;
      }

      default:
        break;
    }

    event = {.type = XR_TYPE_EVENT_DATA_BUFFER};
  }
}

void PlayerSession::beginFrame(double* dt, bool* shouldRender) {
  currentFrameState = {.type = XR_TYPE_FRAME_STATE};

  xrWaitFrame(session, nullptr, &currentFrameState);

  // Convert nanoseconds to seconds
  *dt = currentFrameState.predictedDisplayPeriod / 1000000000.0;

  if (currentFrameState.shouldRender == XR_TRUE) {
    *shouldRender = true;
  } else {
    *shouldRender = false;
  }

  xrBeginFrame(session, nullptr);
}

void PlayerSession::endFrame(Renderer* renderer, bool didRender) {
  XrCompositionLayerBaseHeader* layer = nullptr;
  XrCompositionLayerProjection projectionLayer{
      .type = XR_TYPE_COMPOSITION_LAYER_PROJECTION};

  std::vector<XrCompositionLayerProjectionView> projectionViews;
  if (didRender) {
    layer = reinterpret_cast<XrCompositionLayerBaseHeader*>(&projectionLayer);

    XrViewState viewState{.type = XR_TYPE_VIEW_STATE};

    XrViewLocateInfo locateInfo{
        .type = XR_TYPE_VIEW_LOCATE_INFO,
        .viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        .displayTime = currentFrameState.predictedDisplayTime,
        .space = stageSpace};

    uint32_t viewCount = views.size();
    xrLocateViews(session, &locateInfo, &viewState, viewCount, &viewCount,
                  views.data());
    projectionViews.resize(viewCount);

    renderer->finishRender(&views, &projectionViews);

    projectionLayer.space = stageSpace;
    projectionLayer.viewCount = projectionViews.size();
    projectionLayer.views = projectionViews.data();
  }

  XrFrameEndInfo endInfo{
      .type = XR_TYPE_FRAME_END_INFO,
      .displayTime = currentFrameState.predictedDisplayTime,
      .environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
      .layerCount = static_cast<uint32_t>((layer == nullptr) ? 0 : 1),
      .layers = &layer};

  xrEndFrame(session, &endInfo);
}

void PlayerSession::enumerateSwapchainFormats(std::vector<VkFormat>* formats) {
  uint32_t formatCount;
  xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr);
  formats->resize(formatCount);

  std::vector<int64_t> formatCodes(formatCount);
  xrEnumerateSwapchainFormats(session, formatCount, &formatCount,
                              formatCodes.data());

  for (uint32_t i = 0; i < formatCount; i++) {
    (*formats)[i] = (VkFormat)formatCodes[i];
  }
}

bool PlayerSession::createViewports(std::vector<Viewport*>* viewports,
                                    VkFormat format, VkRenderPass renderPass) {
  // TODO(marceline-cramer) findViewConfiguration()
  uint32_t viewportCount;
  xrEnumerateViewConfigurationViews(display->instance, display->systemId,
                                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                    0, &viewportCount, nullptr);
  std::vector<XrViewConfigurationView> viewConfigs(viewportCount);
  xrEnumerateViewConfigurationViews(display->instance, display->systemId,
                                    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                    viewportCount, &viewportCount,
                                    viewConfigs.data());

  viewports->resize(0);
  views.resize(viewportCount);

  for (uint32_t i = 0; i < viewportCount; i++) {
    Viewport* viewport =
        new Viewport(format, renderPass, &viewConfigs[i], this, vulkanInstance);
    viewports->push_back(viewport);
  }

  return true;
}
