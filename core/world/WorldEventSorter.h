// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <deque>
#include <memory>

#include "lib/include/flatbuffers_headers.h"

namespace mondradiko {

// Forward declarations
namespace protocol {
struct WorldEvent;
struct WorldEventT;
}  // namespace protocol

namespace core {

// Forward declarations
struct World;

class WorldEventSorter {
 public:
  explicit WorldEventSorter(World*);
  ~WorldEventSorter();

  void processEvent(std::unique_ptr<protocol::WorldEventT>&);

  using WorldUpdate = flatbuffers::Vector<
      flatbuffers::Offset<mondradiko::protocol::WorldEvent>>;
  using WorldUpdateOffset = flatbuffers::Offset<WorldUpdate>;

  WorldUpdateOffset broadcastGlobalEvents(
      flatbuffers::FlatBufferBuilder&) const;

  bool isOutOfDate();
  void clearQueue();
  World* getWorld() { return world; }

 private:
  World* world;

  using WorldEventQueue = std::deque<std::shared_ptr<protocol::WorldEventT>>;

  WorldEventQueue global_events;
};

}  // namespace core
}  // namespace mondradiko
