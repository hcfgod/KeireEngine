#pragma once

#include "Keire/Rendering/MaterialGraph.h"

#include <filesystem>
#include <span>
#include <string>

namespace Keire::Detail
{
    [[nodiscard]] std::string BuildMaterialGraphManifest(const MaterialGraphDefinition& definition,
                                                         const std::filesystem::path& generatedSource,
                                                         std::span<const ShaderPropertyDefinition> properties,
                                                         std::span<const std::string> keywords,
                                                         bool usesVertexMaterialParameters);
} // namespace Keire::Detail
