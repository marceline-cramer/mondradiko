// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "bundler/AssetBundleBuilder.h"

#include <cstring>
#include <fstream>

#include "log/log.h"
#include "lz4frame.h"  // NOLINT
#include "types/assets/Registry_generated.h"
#include "xxhash.h"  // NOLINT

namespace mondradiko {
namespace assets {

AssetBundleBuilder::AssetBundleBuilder(const std::filesystem::path& bundle_root)
    : bundle_root(bundle_root) {
  log_msg_fmt("Building asset bundle at %s", bundle_root.c_str());
}

AssetBundleBuilder::~AssetBundleBuilder() {
  log_dbg_fmt("Cleaning up asset bundle %s", bundle_root.c_str());

  for (auto& lump : lumps) {
    delete[] lump.data;
  }
}

AssetResult AssetBundleBuilder::addAsset(
    AssetId* id, flatbuffers::FlatBufferBuilder* fbb,
    flatbuffers::Offset<SerializedAsset> asset_offset) {
  fbb->Finish(asset_offset);
  size_t asset_size = fbb->GetSize();

  if (asset_size > ASSET_LUMP_MAX_SIZE) {
    log_err("Asset size exceeds max asset lump size");
    return AssetResult::BadSize;
  }

  // Generate ID by hashing asset data
  *id = static_cast<AssetId>(XXH3_64bits(fbb->GetBufferPointer(), asset_size));

  if (used_ids.find(*id) != used_ids.end()) {
    log_wrn_fmt("Attempted to build asset with duplicated ID 0x%0x", *id);
    return AssetResult::DuplicateAsset;
  }

  if (lumps.size() == 0) {
    lumps.resize(1);
    allocateLump(&lumps[0]);
  }

  uint32_t lump_index = lumps.size() - 1;

  if (lumps[lump_index].total_size + asset_size > ASSET_LUMP_MAX_SIZE) {
    LumpToSave new_lump;
    allocateLump(&new_lump);
    lumps.push_back(new_lump);
    lump_index++;
  }

  if (lumps[lump_index].compression_method != LumpCompressionMethod::None) {
    log_err_fmt("Lump %d has already been compressed", lump_index);
    return AssetResult::BadContents;
  }

  AssetToSave new_asset;
  new_asset.id = *id;
  new_asset.size = asset_size;
  lumps[lump_index].assets.push_back(new_asset);

  memcpy(lumps[lump_index].data + lumps[lump_index].total_size,
         fbb->GetBufferPointer(), asset_size);

  lumps[lump_index].total_size += asset_size;
  used_ids.emplace(*id);

  return AssetResult::Success;
}

AssetResult AssetBundleBuilder::addInitialPrefab(AssetId prefab) {
  initial_prefabs.push_back(prefab);
  return AssetResult::Success;
}

AssetResult AssetBundleBuilder::buildBundle(const char* registry_name) {
  for (uint32_t lump_index = 0; lump_index < lumps.size(); lump_index++) {
    auto& lump = lumps[lump_index];
    auto lump_name = generateLumpName(lump_index);
    auto lump_path = bundle_root / lump_name;
    std::ofstream lump_file(lump_path.c_str(), std::ofstream::binary);

    // TODO(marceline-cramer) Lump compression configuration
    compressLump(&lump);

    lump_file.write(reinterpret_cast<char*>(lump.data), lump.total_size);

    lump_file.close();
  }

  flatbuffers::FlatBufferBuilder fbb;

  std::vector<flatbuffers::Offset<LumpEntry>> lump_offsets;

  for (uint32_t lump_index = 0; lump_index < lumps.size(); lump_index++) {
    auto& lump = lumps[lump_index];

    log_dbg_fmt("Writing lump %d", lump_index);
    log_dbg_fmt("Lump size: %lu", lump.total_size);

    AssetEntry* asset_entries;
    auto assets_offset = fbb.CreateUninitializedVectorOfStructs(
        lump.assets.size(), &asset_entries);

    for (uint32_t i = 0; i < lump.assets.size(); i++) {
      auto& asset = lump.assets[i];
      asset_entries[i].mutate_id(asset.id);
      asset_entries[i].mutate_size(asset.size);
    }

    LumpEntryBuilder lump_entry(fbb);

    {
      LumpHash checksum =
          static_cast<LumpHash>(XXH3_64bits(lump.data, lump.total_size));
      log_dbg_fmt("Lump has checksum 0x%0lx", checksum);

      lump_entry.add_file_size(lump.total_size);
      lump_entry.add_checksum(checksum);
      lump_entry.add_hash_method(LumpHashMethod::xxHash);
      lump_entry.add_compression_method(lump.compression_method);
      lump_entry.add_assets(assets_offset);

      lump_offsets.push_back(lump_entry.Finish());
    }
  }

  auto initial_prefabs_offset = fbb.CreateVector(
      reinterpret_cast<const uint32_t*>(initial_prefabs.data()),
      initial_prefabs.size());
  auto lumps_offset = fbb.CreateVector(lump_offsets);

  RegistryBuilder registry_builder(fbb);
  registry_builder.add_version(MONDRADIKO_ASSET_VERSION);
  registry_builder.add_initial_prefabs(initial_prefabs_offset);
  registry_builder.add_lumps(lumps_offset);

  fbb.Finish(registry_builder.Finish());

  auto registry_path = bundle_root / registry_name;
  std::ofstream registry_file(registry_path.c_str(), std::ofstream::binary);
  registry_file.write(reinterpret_cast<char*>(fbb.GetBufferPointer()),
                      fbb.GetSize());
  registry_file.close();

  return AssetResult::Success;
}

void AssetBundleBuilder::allocateLump(LumpToSave* new_lump) {
  new_lump->compression_method = LumpCompressionMethod::None;
  new_lump->total_size = 0;
  new_lump->data = new char[ASSET_LUMP_MAX_SIZE];
  new_lump->assets.resize(0);
}

void AssetBundleBuilder::compressLump(LumpToSave* lump) {
  if (lump->compression_method != LumpCompressionMethod::None) {
    log_err("Can't compress lump; lump is already compressed");
    return;
  }

  // TODO(marceline-cramer) More lump compression types?
  log_dbg("Compressing lump with LZ4");

  LZ4F_preferences_t preferences;
  memset(&preferences, 0, sizeof(preferences));
  preferences.frameInfo.contentSize = lump->total_size;
  preferences.compressionLevel = LZ4F_compressionLevel_max();
  preferences.autoFlush = 1;
  preferences.favorDecSpeed = 1;

  size_t compressed_size =
      LZ4F_compressFrameBound(lump->total_size, &preferences);
  char* compressed_data = new char[compressed_size];

  size_t out_size =
      LZ4F_compressFrame(compressed_data, compressed_size, lump->data,
                         lump->total_size, &preferences);

  if (LZ4F_isError(out_size)) {
    log_err_fmt("LZ4HC compression failed: %s", LZ4F_getErrorName(out_size));
    return;
  }

  // TODO(marceline-cramer) Stream out to a file
  delete[] lump->data;
  lump->compression_method = LumpCompressionMethod::LZ4;
  lump->total_size = out_size;
  lump->data = compressed_data;
}

}  // namespace assets
}  // namespace mondradiko
