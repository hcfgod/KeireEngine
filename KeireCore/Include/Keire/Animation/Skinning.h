#pragma once

#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Api.h"
#include "Keire/Assets/RenderingAssets.h"

#include <span>
#include <vector>

namespace Keire
{
    [[nodiscard]] KEIRE_API std::vector<SkinVertexInfluence8>
    ExpandSkinInfluences(std::span<const SkinVertexInfluence> influences);

    KEIRE_API void SkinMeshCpu(std::span<const MeshVertex> source, std::span<const SkinVertexInfluence8> influences,
                               std::span<const Matrix4> palette, SkinningMethod method,
                               std::span<MeshVertex> destination);

    [[nodiscard]] KEIRE_API std::vector<MeshVertex> SkinMeshCpu(std::span<const MeshVertex> source,
                                                                std::span<const SkinVertexInfluence> influences,
                                                                std::span<const Matrix4> palette,
                                                                SkinningMethod method = SkinningMethod::LinearBlend);
} // namespace Keire
