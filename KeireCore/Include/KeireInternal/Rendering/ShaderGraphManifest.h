#pragma once

#include "Keire/Rendering/ShaderGraph.h"

#include <filesystem>
#include <span>
#include <string>

namespace Keire::Detail
{
    [[nodiscard]] std::string BuildShaderGraphManifest(const ShaderGraphDefinition& definition,
                                                       const std::filesystem::path& generatedSource,
                                                       std::span<const ShaderPropertyDefinition> properties,
                                                       std::span<const std::string> keywords,
                                                       bool usesVertexMaterialParameters);
} // namespace Keire::Detail
