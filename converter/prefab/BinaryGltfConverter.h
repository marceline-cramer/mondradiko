// Copyright (c) 2020-2021 the Mondradiko contributors.
// SPDX-License-Identifier: LGPL-3.0-or-later

#pragma once

#include "converter/prefab/GltfConverter.h"

namespace mondradiko {
namespace converter {

class BinaryGltfConverter : public GltfConverter {
 public:
  explicit BinaryGltfConverter(BundlerInterface* bundler)
      : GltfConverter(bundler) {}

  // ConverterInterface implementation
  AssetOffset convert(AssetBuilder*, const toml::table&) const final;
};

}  // namespace converter
}  // namespace mondradiko
