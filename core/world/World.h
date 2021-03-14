// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <unordered_map>

#include "core/assets/AssetPool.h"
#include "core/physics/Physics.h"
#include "core/scripting/ScriptEnvironment.h"
#include "core/world/Entity.h"
#include "lib/include/flatbuffers_headers.h"

namespace mondradiko {

// Forward declarations
class Filesystem;

namespace protocol {
struct WorldEvent;

struct SpawnEntity;
struct UpdateComponents;
struct UpdateScripts;
}  // namespace protocol

class World {
 public:
  World(AssetPool*, Filesystem*, ScriptEnvironment*);
  ~World();

  void initializePrefabs();

  AssetPool* getAssetPool() { return asset_pool; }

  //
  // World event callbacks
  //
  void onSpawnEntity(const protocol::SpawnEntity*);
  void onUpdateComponents(const protocol::UpdateComponents*);
  void onUpdateScripts(const protocol::UpdateScripts*);

  //
  // Helper methods
  //
  bool update(double);
  void processEvent(const protocol::WorldEvent*);

  template <class ComponentType, class ProtocolComponentType>
  void updateComponents(
      const flatbuffers::Vector<EntityId>*,
      const flatbuffers::Vector<const ProtocolComponentType*>*);

  // private:
  AssetPool* asset_pool;
  Filesystem* fs;
  ScriptEnvironment* scripts;

  EntityRegistry registry;
  Physics physics;
};

}  // namespace mondradiko
