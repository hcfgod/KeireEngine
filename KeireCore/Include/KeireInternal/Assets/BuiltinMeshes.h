#pragma once

#include "Keire/Assets/RenderingAssets.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    [[nodiscard]] std::pair<std::vector<MeshVertex>, std::vector<std::uint32_t>>
    CreateBuiltinMeshGeometry(BuiltinMesh mesh);
} // namespace Keire::Detail
