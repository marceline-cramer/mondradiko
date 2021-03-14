// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "core/scripting/ScriptEnvironment.h"

#include <vector>

#include "bindings/script_linker.h"
#include "core/assets/ScriptAsset.h"
#include "core/components/ScriptComponent.h"
#include "core/components/TransformComponent.h"
#include "core/scripting/ComponentScript.h"
#include "core/scripting/ScriptInstance.h"
#include "log/log.h"

namespace mondradiko {

static wasm_trap_t* interruptCallback(const wasmtime_caller_t* caller,
                                      void* env, const wasm_val_t args[],
                                      wasm_val_t results[]) {
  ScriptEnvironment* scripts = reinterpret_cast<ScriptEnvironment*>(env);
  log_err("Store interrupted");
  wasmtime_interrupt_handle_interrupt(scripts->getInterruptHandle());
  return nullptr;
}

static wasm_trap_t* abortCallback(const wasmtime_caller_t* caller, void* env,
                                  const wasm_val_t args[],
                                  wasm_val_t results[]) {
  // TODO(marceline-cramer) Parse arguments and print abort info
  ScriptEnvironment* scripts = reinterpret_cast<ScriptEnvironment*>(env);
  log_err("AssemblyScript called abort()");

  wasm_message_t message;
  wasm_name_new_from_string(&message, "AssemblyScript abort()");
  wasm_trap_t* trap = wasm_trap_new(scripts->getStore(), &message);
  wasm_name_delete(&message);

  return trap;
}

// Dummy finalizer needed for wasmtime_func_new_with_env()
static void interruptCallbackFinalizer(void*) {}

ScriptEnvironment::ScriptEnvironment() {
  log_zone;

  // Create a config to allow interrupts
  wasm_config_t* config = wasm_config_new();
  if (config == nullptr) {
    log_ftl("Failed to create Wasm config");
  }

  wasmtime_config_interruptable_set(config, true);

  // Create the engine
  // Frees the config
  engine = wasm_engine_new_with_config(config);
  if (engine == nullptr) {
    log_ftl("Failed to create Wasm engine");
  }

  // Create the store
  store = wasm_store_new(engine);
  if (store == nullptr) {
    log_ftl("Failed to create Wasm store");
  }

  // Create an interrupt handle
  interrupt_handle = wasmtime_interrupt_handle_new(store);
  if (interrupt_handle == nullptr) {
    log_ftl("Failed to create interrupt handler");
  }

  {  // Create a function to interrupt the store
    wasm_functype_t* interrupt_func_type = wasm_functype_new_0_0();
    interrupt_func = wasmtime_func_new_with_env(store, interrupt_func_type,
                                                interruptCallback, this,
                                                interruptCallbackFinalizer);
    wasm_functype_delete(interrupt_func_type);
  }

  linkAssemblyScriptEnv();
}

ScriptEnvironment::~ScriptEnvironment() {
  log_zone;

  for (auto iter : bindings) {
    wasm_func_delete(iter.second);
  }

  if (interrupt_func) wasm_func_delete(interrupt_func);
  if (interrupt_handle) wasmtime_interrupt_handle_delete(interrupt_handle);

  if (store) wasm_store_delete(store);
  if (engine) wasm_engine_delete(engine);
}

void ScriptEnvironment::initializeAssets(AssetPool* asset_pool) {
  log_zone;

  asset_pool->initializeAssetType<ScriptAsset>(this);
}

void ScriptEnvironment::linkUiApis(UserInterface* ui) {
  // TODO(marceline-cramer) UI API
}

// Helper function to link a component type's API
template <class ComponentType>
void linkComponentApi(ScriptEnvironment* scripts, World* world) {
  ComponentType::linkScriptApi(scripts, world);
}

void ScriptEnvironment::linkComponentApis(World* world) {
  linkComponentApi<TransformComponent>(this, world);
}

void ScriptEnvironment::linkAssemblyScriptEnv() {
  log_zone;

  // See: https://www.assemblyscript.org/exports-and-imports.html#imports-2

  {  // Link abort()
    wasm_valtype_t* ps[] = {wasm_valtype_new_i32(), wasm_valtype_new_i32(),
                            wasm_valtype_new_i32(), wasm_valtype_new_i32()};

    wasm_valtype_vec_t params, results;
    wasm_valtype_vec_new(&params, 4, ps);
    wasm_valtype_vec_new_empty(&results);

    wasm_functype_t* abort_func_type = wasm_functype_new(&params, &results);

    wasm_func_t* abort_func =
        wasmtime_func_new_with_env(store, abort_func_type, abortCallback, this,
                                   interruptCallbackFinalizer);
    wasm_functype_delete(abort_func_type);

    addBinding("abort", abort_func);
  }
}

void ScriptEnvironment::update(EntityRegistry* registry, AssetPool* asset_pool,
                               double dt) {
  auto script_view = registry->view<ScriptComponent>();

  for (auto& e : script_view) {
    auto& script = script_view.get(e);

    if (!script.getScriptAsset()) continue;

    script.script_instance->update(e, dt);
  }
}

void ScriptEnvironment::updateScript(EntityRegistry* registry,
                                     AssetPool* asset_pool, EntityId entity,
                                     AssetId script_id, const uint8_t* data,
                                     size_t data_size) {
  // Ensure the entity exists
  if (!registry->valid(entity)) {
    entity = registry->create(entity);
  }

  bool needs_initialization = false;

  const AssetHandle<ScriptAsset>& script_asset =
      asset_pool->load<ScriptAsset>(script_id);

  // Destroy the old ScriptInstance if necessary
  if (registry->has<ScriptComponent>(entity)) {
    ScriptComponent& old_script = registry->get<ScriptComponent>(entity);
    if (old_script.script_asset != script_asset) {
      delete old_script.script_instance;
      needs_initialization = true;
    }
  } else {
    needs_initialization = true;
  }

  ScriptComponent& script_component =
      registry->get_or_emplace<ScriptComponent>(entity);

  if (needs_initialization) {
    script_component.script_instance = script_asset->createInstance();
  }

  script_component.script_asset = script_asset;
  script_component.script_instance->updateData(data, data_size);
}

void ScriptEnvironment::addBinding(const char* symbol, wasm_func_t* func) {
  bindings.emplace(std::string(symbol), func);
}

wasm_func_t* ScriptEnvironment::getBinding(const std::string& symbol) {
  auto iter = bindings.find(symbol);
  if (iter == bindings.end()) return nullptr;
  return iter->second;
}

bool ScriptEnvironment::handleError(wasmtime_error_t* error,
                                    wasm_trap_t* trap) {
  wasm_byte_vec_t error_message;
  if (error != nullptr) {
    wasmtime_error_message(error, &error_message);
    wasmtime_error_delete(error);
  } else if (trap != nullptr) {
    wasm_trap_message(trap, &error_message);
    wasm_trap_delete(trap);
  } else {
    // Return false if no error was thrown
    return false;
  }

  std::string error_string(error_message.data, error_message.size);
  wasm_byte_vec_delete(&error_message);
  log_err_fmt("Wasmtime error thrown: %s", error_string.c_str());
  return true;
}

}  // namespace mondradiko
