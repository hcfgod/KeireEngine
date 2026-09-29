#pragma once

#include "Keire/Animation/Skinning.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/ECS/Components/AnimatorComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Entity.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace KeireEditor
{
    [[nodiscard]] inline std::optional<Keire::MeshBounds>
    CalculateScenePoseBounds(const Keire::MeshAsset& mesh, const Keire::SkinnedMeshAsset& skin,
                             const std::span<const Keire::Matrix4> palette)
    {
        if (palette.empty() || skin.Influences8().size() != mesh.Vertices().size())
            return std::nullopt;
        for (const auto& matrix : palette)
            if (!Keire::Math::IsFinite(matrix))
                return std::nullopt;
        std::optional<Keire::MeshBounds> result;
        const auto include = [&result](const Keire::Vector3 point)
        {
            if (!result)
                result = Keire::MeshBounds{point, point};
            else
            {
                result->Minimum.X = std::min(result->Minimum.X, point.X);
                result->Minimum.Y = std::min(result->Minimum.Y, point.Y);
                result->Minimum.Z = std::min(result->Minimum.Z, point.Z);
                result->Maximum.X = std::max(result->Maximum.X, point.X);
                result->Maximum.Y = std::max(result->Maximum.Y, point.Y);
                result->Maximum.Z = std::max(result->Maximum.Z, point.Z);
            }
        };
        if (skin.Method() == Keire::SkinningMethod::LinearBlend && skin.HasCompleteInfluenceBounds())
        {
            for (const auto& influence : skin.InfluenceBounds())
                if (influence.Bone >= palette.size())
                    return std::nullopt;
            for (const auto& bounds : Keire::CalculateLinearBlendPoseBounds(
                     skin.InfluenceBounds(), skin.InfluenceBoundsSubmeshCount(), palette))
            {
                include(bounds.Minimum);
                include(bounds.Maximum);
            }
        }
        else
        {
            for (const auto& influence : skin.Influences8())
                for (std::size_t index = 0; index < influence.Count; ++index)
                    if (influence.Bones[index] >= palette.size())
                        return std::nullopt;
            // DQ bounds cannot use the linear union; evaluate the selected mesh with its actual skinning method.
            std::vector<Keire::MeshVertex> vertices(mesh.Vertices().size());
            Keire::SkinMeshCpu(mesh.Vertices(), skin.Influences8(), palette, skin.Method(), vertices);
            for (const auto& vertex : vertices)
            {
                if (!Keire::Math::IsFinite(vertex.Position))
                    return std::nullopt;
                include(vertex.Position);
            }
        }
        return result;
    }

    [[nodiscard]] inline std::optional<Keire::MeshBounds>
    ResolveScenePoseBounds(const Keire::Ref<Keire::AssetSystem>& assets, const Keire::Entity& entity)
    {
        if (!assets || !assets->IsOpen() || !entity)
            return std::nullopt;
        const auto animator = entity.GetComponent<Keire::AnimatorComponent>();
        const auto renderer = entity.GetComponent<Keire::MeshRendererComponent>();
        if (!animator || !animator->Enabled() || animator->SkinPalette().empty() || !renderer || !renderer->Enabled() ||
            !renderer->Visible() ||
            assets->TryGetType(animator->SkinnedMesh()) != Keire::SkinnedMeshAsset::StaticType() ||
            assets->TryGetType(renderer->Mesh()) != Keire::MeshAsset::StaticType())
            return std::nullopt;
        const auto skin = assets->Load<Keire::SkinnedMeshAsset>(animator->SkinnedMesh()).TryGetLoaded();
        const auto mesh = assets->Load<Keire::MeshAsset>(renderer->Mesh()).TryGetLoaded();
        if (!skin || !mesh || skin->Mesh() != renderer->Mesh() || skin->Skeleton() != animator->Skeleton())
            return std::nullopt;
        return CalculateScenePoseBounds(*mesh, *skin, animator->SkinPalette());
    }
} // namespace KeireEditor
