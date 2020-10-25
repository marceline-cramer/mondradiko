/**
 * @file log.cc
 * @author Marceline Cramer (cramermarceline@gmail.com)
 * @brief Provides global logging with formatting.
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

#include "log/log.h"

#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

const char* Log::getLogPrefix(LogLevel level) {
  switch (level) {
    case Log::LOG_LEVEL_INFO:
      return "\e[36m[INF ";
    case Log::LOG_LEVEL_DEBUG:
      return "\e[32m[DBG ";
    case Log::LOG_LEVEL_WARNING:
      return "\e[33m[WRN ";
    case Log::LOG_LEVEL_ERROR:
      return "\e[33m[ERR ";
    default:
      return "\e[31m[FTL ";
  }
}

void Log::log(const char* filePath, int line, LogLevel level,
              const char* message) {
  const char* prefix = getLogPrefix(level);
  std::ostringstream header;
  std::string fileName(filePath);

  // Remove "../src/" from file path
  header << prefix << fileName.substr(7) << ":" << line << "]";

  std::cerr << std::left << std::setw(55) << header.str();
  std::cerr << message << "\e[0m" << std::endl;

  if (level == Log::LOG_LEVEL_FATAL) {
    throw std::runtime_error(message);
  }
}