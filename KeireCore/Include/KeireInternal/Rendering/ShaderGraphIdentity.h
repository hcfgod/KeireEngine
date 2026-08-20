#pragma once

#include "Keire/Rendering/ShaderGraph.h"

#include <string_view>

namespace Keire::Detail
{
    [[nodiscard]] AssetId StableMigratedShaderPinId(AssetId node, std::string_view name,
                                                    ShaderGraphPinDirection direction) noexcept;
    [[nodiscard]] AssetId DerivedShaderFunctionElementId(AssetId call, AssetId source, std::string_view role) noexcept;
} // namespace Keire::Detail
