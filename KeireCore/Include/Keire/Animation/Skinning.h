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

    /// Builds the compact bind-space bounds required for conservative current-pose LBS bounds.
    [[nodiscard]] KEIRE_API std::vector<SkinInfluenceBounds>
    CalculateBindSpaceSkinInfluenceBounds(std::span<const MeshVertex> vertices, std::span<const std::uint32_t> indices,
                                          std::span<const MeshSubmesh> submeshes,
                                          std::span<const SkinVertexInfluence8> influences);

    /// Transforms per-bone bind-space bounds and unions them into one conservative bound per submesh.
    [[nodiscard]] KEIRE_API std::vector<MeshBounds>
    CalculateLinearBlendPoseBounds(std::span<const SkinInfluenceBounds> influenceBounds, std::uint32_t submeshCount,
                                   std::span<const Matrix4> palette);

    KEIRE_API void SkinMeshCpu(std::span<const MeshVertex> source, std::span<const SkinVertexInfluence8> influences,
                               std::span<const Matrix4> palette, SkinningMethod method,
                               std::span<MeshVertex> destination);

    [[nodiscard]] KEIRE_API std::vector<MeshVertex> SkinMeshCpu(std::span<const MeshVertex> source,
                                                                std::span<const SkinVertexInfluence> influences,
                                                                std::span<const Matrix4> palette,
                                                                SkinningMethod method = SkinningMethod::LinearBlend);
} // namespace Keire
