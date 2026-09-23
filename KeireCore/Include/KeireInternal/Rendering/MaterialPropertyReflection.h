#pragma once

#include "Keire/Rendering/MaterialGraph.h"

#include <filesystem>

namespace Keire::Detail
{
    [[nodiscard]] ShaderAssetDefinition ReflectMaterialProperties(const ShaderGraphDefinition& graph,
                                                                  const std::filesystem::path& source);
    [[nodiscard]] ShaderAssetDefinition ReadMaterialPropertyReflection(const AssetImportContext& context,
                                                                       const MaterialShaderReference& reference);
    [[nodiscard]] MaterialAssetDefinition ResolveMaterialVariant(const MaterialAssetDefinition& parent,
                                                                 const MaterialInstanceDefinition& instance,
                                                                 const ShaderAssetDefinition& shader);
} // namespace Keire::Detail
