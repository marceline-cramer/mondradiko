// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "core/components/RigidBodyComponent.h"

#include "core/components/TransformComponent.h"

namespace mondradiko {
namespace core {

RigidBodyComponent::RigidBodyComponent(const assets::RigidBodyPrefab* prefab) {
  _data.mutate_mass(prefab->mass());
}

RigidBodyComponent::~RigidBodyComponent() { _destroy(nullptr); }

TransformComponent RigidBodyComponent::makeTransform() {
  auto transform = _rigid_body->getCenterOfMassTransform();
  auto body_position = transform.getOrigin();
  auto body_orientation = transform.getRotation();

  // TODO(marceline-cramer) Make helpers for these
  glm::vec3 position(body_position.x(), body_position.y(), body_position.z());
  glm::quat orientation(body_orientation.w(), body_orientation.x(),
                        body_orientation.y(), body_orientation.z());
  return TransformComponent(position, orientation);
}

void RigidBodyComponent::_destroy(btDynamicsWorld* dynamics_world) {
  if (_rigid_body != nullptr) {
    if (dynamics_world != nullptr) {
      dynamics_world->removeRigidBody(_rigid_body);
    }

    delete _rigid_body;
    _rigid_body = nullptr;
  }

  if (_motion_state != nullptr) {
    delete _motion_state;
    _motion_state = nullptr;
  }
}

}  // namespace core
}  // namespace mondradiko
