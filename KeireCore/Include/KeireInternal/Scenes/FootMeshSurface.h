#pragma once

#include "Keire/Animation/Skinning.h"
#include "KeireInternal/Scenes/FootGroundingSpace.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace Keire::Detail
{
    struct FootMeshSurface
    {
        std::vector<MeshVertex> Vertices;
        std::vector<SkinVertexInfluence8> Influences;
        std::vector<MeshVertex> Deformed;
        SkinningMethod Method = SkinningMethod::LinearBlend;
    };

    [[nodiscard]] inline FootMeshSurface BuildFootMeshSurface(const SkeletonAsset& skeleton,
                                                              const SkinnedMeshAsset& skin, const MeshAsset& mesh,
                                                              const std::uint32_t foot)
    {
        FootMeshSurface result;
        result.Method = skin.Method();
        if (skin.Influences8().size() != mesh.Vertices().size() || foot >= skeleton.Bones().size())
            return result;
        for (std::size_t vertex = 0; vertex < mesh.Vertices().size(); ++vertex)
        {
            const auto& influence = skin.Influences8()[vertex];
            float weight = 0;
            if (influence.Count > influence.Bones.size())
                return {};
            for (std::size_t index = 0; index < influence.Count; ++index)
                if (IsBoneInSubtree(skeleton, influence.Bones[index], foot))
                    weight += influence.Weights[index];
            if (weight < 0.25F)
                continue;
            result.Vertices.push_back(mesh.Vertices()[vertex]);
            result.Influences.push_back(influence);
        }
        result.Deformed.resize(result.Vertices.size());
        return result;
    }

    [[nodiscard]] inline float FootMeshSurfacePenetration(FootMeshSurface& surface,
                                                          const std::span<const Matrix4> palette,
                                                          const Matrix4& modelToWorld, const Vector3 point,
                                                          const Vector3 normal, const float clearance = 0.0F,
                                                          const float softContactDistance = 0.0F)
    {
        if (surface.Vertices.empty())
            return 0;
        SkinMeshCpu(surface.Vertices, surface.Influences, palette, surface.Method, surface.Deformed);
        float penetration = -softContactDistance;
        for (const auto& vertex : surface.Deformed)
        {
            const auto position = Math::TransformPoint(modelToWorld, vertex.Position);
            penetration =
                std::max(penetration, clearance + (point.X - position.X) * normal.X +
                                          (point.Y - position.Y) * normal.Y + (point.Z - position.Z) * normal.Z);
        }
        if (softContactDistance > 0.0F && penetration < softContactDistance)
        {
            // Smooth the onset before contact; this upper bound on max(0, penetration)
            // keeps clearance conservative without a hard velocity kink at the support plane.
            const auto approach = penetration + softContactDistance;
            return approach * approach / (4.0F * softContactDistance);
        }
        return std::max(penetration, 0.0F);
    }
} // namespace Keire::Detail
