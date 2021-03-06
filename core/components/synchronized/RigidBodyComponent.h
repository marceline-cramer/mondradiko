// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "core/components/SynchronizedComponent.h"
#include "lib/include/bullet_headers.h"
#include "types/assets/PrefabAsset_generated.h"
#include "types/protocol/RigidBodyComponent_generated.h"

namespace mondradiko {
namespace core {

// Forward declarations
class WorldTransform;

class RigidBodyComponent
    : public SynchronizedComponent<protocol::RigidBodyComponent> {
 public:
  explicit RigidBodyComponent(const assets::RigidBodyPrefab*);

  // TODO(marceline-cramer) Rigid body network sync

  WorldTransform makeWorldTransform();

 private:
  // Systems allowed to access private members directly
  friend class Physics;

  btRigidBody* _rigid_body = nullptr;
  btMotionState* _motion_state = nullptr;
};

}  // namespace core
}  // namespace mondradiko
