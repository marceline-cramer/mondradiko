// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "core/world/ScriptEntity.h"

#include "core/components/scriptable/PointLightComponent.h"
#include "core/components/scriptable/TransformComponent.h"
#include "core/scripting/environment/ComponentScriptEnvironment.h"
#include "core/world/World.h"
#include "types/containers/string.h"

namespace mondradiko {
namespace core {

wasm_functype_t* methodType_Entity() {
  return wasm_functype_new_1_1(wasm_valtype_new_i32(), wasm_valtype_new_i32());
}

template <class ComponentType>
static wasm_trap_t* Entity_hasComponent(World* world, const wasm_val_t args[],
                                        wasm_val_t results[]) {
  EntityId self_id = args[0].of.i32;
  if (!world->registry.valid(self_id)) {
    return world->scripts.createTrap("Invalid entity ID");
  }

  results[0].kind = WASM_I32;

  if (world->registry.has<ComponentType>(self_id)) {
    results[0].of.i32 = 1;
  } else {
    results[0].of.i32 = 0;
  }

  return nullptr;
}

template <class ComponentType>
static wasm_trap_t* Entity_addComponent(World* world, const wasm_val_t args[],
                                        wasm_val_t results[]) {
  EntityId self_id = args[0].of.i32;
  if (!world->registry.valid(self_id)) {
    return world->scripts.createTrap("Invalid entity ID");
  }

  results[0].kind = WASM_I32;
  results[0].of.i32 = self_id;

  if (!world->registry.has<ComponentType>(self_id)) {
    world->registry.emplace<ComponentType>(self_id);
  }

  return nullptr;
}

template <class ComponentType>
static wasm_trap_t* Entity_getComponent(World* world, const wasm_val_t args[],
                                        wasm_val_t results[]) {
  EntityId self_id = args[0].of.i32;
  if (!world->registry.valid(self_id)) {
    return world->scripts.createTrap("Invalid entity ID");
  }

  if (!world->registry.has<ComponentType>(self_id)) {
    world->registry.emplace<ComponentType>(self_id);
  }

  results[0].kind = WASM_I32;
  results[0].of.i32 = self_id;
  return nullptr;
}

using BoundEntityMethod = wasm_trap_t* (*)(World*, const wasm_val_t[],
                                           wasm_val_t[]);

using EntityMethodTypeCallback = wasm_functype_t* (*)();

static void finalizer(void*) {}

template <BoundEntityMethod method>
wasm_trap_t* entityMethodWrapper(const wasmtime_caller_t* caller, void* env,
                                 const wasm_val_t args[],
                                 wasm_val_t results[]) {
  World* world = reinterpret_cast<World*>(env);

  if (!world->registry.valid(args[0].of.i32)) {
    return world->scripts.createTrap("Invalid entity ID");
  }

  return (*method)(world, args, results);
}

template <BoundEntityMethod method>
void linkEntityMethod(ComponentScriptEnvironment* scripts, World* world,
                      const types::string& symbol,
                      EntityMethodTypeCallback type_callback) {
  wasm_store_t* store = scripts->getStore();

  wasm_functype_t* func_type = (*type_callback)();

  wasmtime_func_callback_with_env_t callback = entityMethodWrapper<method>;

  void* env = static_cast<void*>(world);

  wasm_func_t* func =
      wasmtime_func_new_with_env(store, func_type, callback, env, finalizer);

  scripts->addBinding(symbol.c_str(), func);
  wasm_functype_delete(func_type);
}

// Helper function to link a component API
template <class ComponentType>
void linkComponentApi(World* world, const char* symbol) {
  ComponentScriptEnvironment* scripts = &world->scripts;
  linkEntityMethod<Entity_getComponent<ComponentType>>(
      scripts, world, std::string("Entity_get") + symbol, methodType_Entity);
  linkEntityMethod<Entity_hasComponent<ComponentType>>(
      scripts, world, std::string("Entity_has") + symbol, methodType_Entity);
  linkEntityMethod<Entity_addComponent<ComponentType>>(
      scripts, world, std::string("Entity_add") + symbol, methodType_Entity);
}

void ScriptEntity::linkScriptApi(World* world) {
  ComponentScriptEnvironment* scripts = &world->scripts;

  linkComponentApi<PointLightComponent>(world, "PointLight");
  linkComponentApi<TransformComponent>(world, "Transform");
}

}  // namespace core
}  // namespace mondradiko