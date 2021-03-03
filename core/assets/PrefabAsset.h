// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include <vector>

#include "core/assets/AssetHandle.h"
#include "core/world/Entity.h"
#include "types/assets/PrefabAsset_generated.h"

namespace mondradiko {

// Forward declarations
class AssetPool;
class ScriptEnvironment;
class TransformComponent;

class PrefabAsset : public Asset {
 public:
  DECL_ASSET_TYPE(assets::AssetType::PrefabAsset);

  // Asset lifetime implementation
  explicit PrefabAsset(AssetPool* asset_pool) : asset_pool(asset_pool) {}
  void load(const assets::SerializedAsset*) final;
  ~PrefabAsset();

  EntityId instantiate(EntityRegistry*, ScriptEnvironment*) const;

 private:
  AssetPool* asset_pool;

  assets::PrefabAssetT* prefab;
  std::vector<AssetHandle<PrefabAsset>> children;
};

}  // namespace mondradiko
