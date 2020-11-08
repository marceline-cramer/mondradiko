/**
 * @file DisplayInterface.h
 * @author Marceline Cramer (cramermarceline@gmail.com)
 * @brief Base class for user-facing displays, VR or otherwise.
 * @date 2020-11-08
 *
 * @copyright Copyright (c) 2020 Marceline Cramer
 *
 * This file is part of Mondradiko.
 *
 * Mondradiko is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Mondradiko is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with Mondradiko.  If not, see <https: //www.gnu.org/licenses/>.
 *
 */

#ifndef SRC_DISPLAYS_DISPLAY_H_
#define SRC_DISPLAYS_DISPLAY_H_

#include <vector>

#include "displays/ViewportInterface.h"
#include "src/api_headers.h"

namespace mondradiko {

struct VulkanRequirements {
  uint32_t min_api_version;
  uint32_t max_api_version;

  std::vector<std::string> instance_extensions;
  VkPhysicalDevice physical_device;
  std::vector<std::string> device_extensions;

  std::vector<VkFormat> swapchain_formats;
};

struct DisplayPollEventsInfo {
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

  virtual bool beginSession() = 0;
  virtual bool getRequirements(VulkanRequirements*) = 0;
  virtual bool getVulkanDevice(VkInstance, VkPhysicalDevice*) = 0;

  virtual VkFormat getSwapchainFormat() = 0;

  virtual void pollEvents(DisplayPollEventsInfo*) = 0;
  virtual void beginFrame(DisplayBeginFrameInfo*) = 0;
  virtual void acquireViewports(std::vector<ViewportInterface*>*) = 0;
  virtual void endFrame() = 0;

 private:
};

}  // namespace mondradiko

#endif  // SRC_DISPLAYS_DISPLAY_H_
