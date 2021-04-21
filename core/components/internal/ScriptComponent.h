// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "core/assets/ScriptAsset.h"
#include "core/components/InternalComponent.h"

namespace mondradiko {
namespace core {

// Forward declarations
class ComponentScript;

class ScriptComponent : public InternalComponent {
 public:
  const AssetHandle<ScriptAsset>& getScriptAsset() { return script_asset; }

 private:
  // Systems allowed to access private members directly
  friend class ComponentScriptEnvironment;

  AssetHandle<ScriptAsset> script_asset;
  ComponentScript* script_instance;
};

}  // namespace core
}  // namespace mondradiko
