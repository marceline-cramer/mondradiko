// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "core/physics/Physics.h"

#include "core/assets/AssetPool.h"
#include "core/assets/ShapeAsset.h"
#include "core/components/internal/WorldTransform.h"
#include "core/components/synchronized/RigidBodyComponent.h"
#include "core/components/synchronized/ShapeComponent.h"
#include "core/world/World.h"
#include "log/log.h"

namespace mondradiko {
namespace core {

Physics::Physics(World* world) : world(world) {
  log_zone;

  broadphase = new btDbvtBroadphase();
  collision_configuration = new btDefaultCollisionConfiguration();
  dispatcher = new btCollisionDispatcher(collision_configuration);
  solver = new btSequentialImpulseConstraintSolver;

  dynamics_world = new btDiscreteDynamicsWorld(dispatcher, broadphase, solver,
                                               collision_configuration);
  dynamics_world->setGravity(btVector3(0, -1.0, 0));

  log_inf_fmt("Bullet Physics: %.2f %s precision", 0.01f * btGetVersion(),
              btIsDoublePrecision() ? "double" : "single");
}

Physics::~Physics() {
  log_zone;

  if (dynamics_world != nullptr) delete dynamics_world;
  if (solver != nullptr) delete solver;
  if (broadphase != nullptr) delete broadphase;
  if (dispatcher != nullptr) delete dispatcher;
  if (collision_configuration != nullptr) delete collision_configuration;
}

void Physics::initializeAssets(AssetPool* asset_pool) {
  asset_pool->initializeAssetType<ShapeAsset>();
}

void Physics::update(double dt) {
  log_zone;

  dynamics_world->stepSimulation(dt);

  auto& registry = world->registry;

  {
    log_zone_named("Refresh dead RigidBodys");

    auto rigid_bodies = registry.group<RigidBodyComponent, ShapeComponent>();

    for (auto e : rigid_bodies) {
      auto& rigid_body = rigid_bodies.get<RigidBodyComponent>(e);
      auto& shape = rigid_bodies.get<ShapeComponent>(e);

      if (!shape.isLoaded()) continue;

      if (rigid_body._rigid_body == nullptr) {
        float body_mass = rigid_body._data.mass();

        btCollisionShape* collision_shape =
            shape.getShape()->getCollisionShape();

        // TODO(marceline-cramer) RigidBodyComponent sets its own transform
        btTransform body_transform;
        body_transform.setIdentity();
        body_transform.setOrigin(btVector3(0.0, 10.0, 0.0));
        btMotionState* motion_state = new btDefaultMotionState(body_transform);

        btVector3 body_inertia;
        collision_shape->calculateLocalInertia(body_mass, body_inertia);

        btRigidBody::btRigidBodyConstructionInfo ci(
            body_mass, motion_state, collision_shape, body_inertia);
        btRigidBody* new_body = new btRigidBody(ci);

        rigid_body._rigid_body = new_body;
        rigid_body._motion_state = motion_state;

        dynamics_world->addRigidBody(new_body);
      }
    }
  }

  {
    log_zone_named("Update Transform from RigidBody");

    auto rigid_body_view = registry.view<RigidBodyComponent>();

    for (auto e : rigid_body_view) {
      auto& rigid_body = rigid_body_view.get(e);

      if (rigid_body._rigid_body == nullptr) continue;

      auto new_transform = rigid_body.makeWorldTransform();
      registry.emplace_or_replace<WorldTransform>(e, new_transform);
    }
  }
}

}  // namespace core
}  // namespace mondradiko
