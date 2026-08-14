#pragma once

#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/Math/Math.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire::Detail
{
    [[nodiscard]] inline bool IsBoneInSubtree(const SkeletonAsset& skeleton, const std::uint32_t candidate,
                                              const std::uint32_t root) noexcept
    {
        if (candidate >= skeleton.Bones().size() || root >= skeleton.Bones().size())
            return false;
        if (candidate == root)
            return true;
        auto parent = skeleton.Bones()[candidate].Parent;
        for (std::size_t depth = 0; parent >= 0 && depth < skeleton.Bones().size(); ++depth)
        {
            if (parent == static_cast<std::int32_t>(root))
                return true;
            if (static_cast<std::size_t>(parent) >= skeleton.Bones().size())
                return false;
            parent = skeleton.Bones()[static_cast<std::size_t>(parent)].Parent;
        }
        return false;
    }

    struct ModelFootGroundContact final
    {
        Vector3 Position;
        Vector3 Normal;
    };

    struct FootPlantSupportAnchor final
    {
        Vector3 LocalPosition;
        Vector3 LocalNormal{0.0F, 1.0F, 0.0F};
    };

    [[nodiscard]] inline std::optional<FootPlantSupportAnchor>
    CaptureFootPlantSupportAnchor(const Matrix4& supportToWorld, const Vector3 worldPosition,
                                  const Vector3 worldNormal) noexcept
    {
        Vector3 supportPosition;
        Vector3 supportScale;
        Quaternion supportRotation;
        const auto normalLength =
            std::sqrt(worldNormal.X * worldNormal.X + worldNormal.Y * worldNormal.Y + worldNormal.Z * worldNormal.Z);
        if (!Math::IsFinite(worldPosition) || !Math::IsFinite(worldNormal) || !std::isfinite(normalLength) ||
            normalLength <= 0.000001F ||
            !Math::DecomposeTransform(supportToWorld, supportPosition, supportRotation, supportScale))
        {
            return std::nullopt;
        }

        try
        {
            const auto worldToSupport = Math::Inverse(supportToWorld);
            const auto worldToSupportRotation =
                Math::Inverse(Math::ComposeTransform({}, supportRotation, {1.0F, 1.0F, 1.0F}));
            auto localNormal = Math::TransformDirection(
                worldToSupportRotation,
                {worldNormal.X / normalLength, worldNormal.Y / normalLength, worldNormal.Z / normalLength});
            const auto localNormalLength = std::sqrt(localNormal.X * localNormal.X + localNormal.Y * localNormal.Y +
                                                     localNormal.Z * localNormal.Z);
            if (!Math::IsFinite(localNormal) || !std::isfinite(localNormalLength) || localNormalLength <= 0.000001F)
                return std::nullopt;
            localNormal = {localNormal.X / localNormalLength, localNormal.Y / localNormalLength,
                           localNormal.Z / localNormalLength};
            return FootPlantSupportAnchor{Math::TransformPoint(worldToSupport, worldPosition), localNormal};
        }
        catch (const std::exception&)
        {
            return std::nullopt;
        }
    }

    [[nodiscard]] inline std::optional<ModelFootGroundContact>
    ResolveFootPlantSupportAnchor(const Matrix4& supportToWorld, const FootPlantSupportAnchor& anchor) noexcept
    {
        Vector3 supportPosition;
        Vector3 supportScale;
        Quaternion supportRotation;
        const auto localNormalLength =
            std::sqrt(anchor.LocalNormal.X * anchor.LocalNormal.X + anchor.LocalNormal.Y * anchor.LocalNormal.Y +
                      anchor.LocalNormal.Z * anchor.LocalNormal.Z);
        if (!Math::IsFinite(anchor.LocalPosition) || !Math::IsFinite(anchor.LocalNormal) ||
            !std::isfinite(localNormalLength) || localNormalLength <= 0.000001F ||
            !Math::DecomposeTransform(supportToWorld, supportPosition, supportRotation, supportScale))
        {
            return std::nullopt;
        }

        const auto supportRotationToWorld = Math::ComposeTransform({}, supportRotation, {1.0F, 1.0F, 1.0F});
        auto worldNormal = Math::TransformDirection(supportRotationToWorld, anchor.LocalNormal);
        const auto worldNormalLength =
            std::sqrt(worldNormal.X * worldNormal.X + worldNormal.Y * worldNormal.Y + worldNormal.Z * worldNormal.Z);
        if (!Math::IsFinite(worldNormal) || !std::isfinite(worldNormalLength) || worldNormalLength <= 0.000001F)
            return std::nullopt;
        worldNormal = {worldNormal.X / worldNormalLength, worldNormal.Y / worldNormalLength,
                       worldNormal.Z / worldNormalLength};
        const auto worldPosition = Math::TransformPoint(supportToWorld, anchor.LocalPosition);
        if (!Math::IsFinite(worldPosition))
            return std::nullopt;
        return ModelFootGroundContact{worldPosition, worldNormal};
    }

    [[nodiscard]] inline std::optional<ModelFootGroundContact>
    ToModelFootGroundContact(const Matrix4& worldToModel, const Vector3 worldPosition, const Vector3 worldNormal,
                             const float worldFootOffset) noexcept
    {
        const auto normalLength =
            std::sqrt(worldNormal.X * worldNormal.X + worldNormal.Y * worldNormal.Y + worldNormal.Z * worldNormal.Z);
        if (!std::isfinite(normalLength) || normalLength <= 0.000001F || !std::isfinite(worldFootOffset) ||
            worldFootOffset < 0.0F)
            return std::nullopt;
        const Vector3 normalizedNormal{worldNormal.X / normalLength, worldNormal.Y / normalLength,
                                       worldNormal.Z / normalLength};
        const Vector3 solePosition{worldPosition.X + normalizedNormal.X * worldFootOffset,
                                   worldPosition.Y + normalizedNormal.Y * worldFootOffset,
                                   worldPosition.Z + normalizedNormal.Z * worldFootOffset};
        return ModelFootGroundContact{Math::TransformPoint(worldToModel, solePosition),
                                      Math::TransformDirection(worldToModel, normalizedNormal)};
    }

    [[nodiscard]] inline float WorldVerticalDistanceToModel(const Matrix4& worldToModel,
                                                            const float worldDistance) noexcept
    {
        const auto modelDistance = Math::TransformDirection(worldToModel, {0.0F, worldDistance, 0.0F});
        return std::sqrt(modelDistance.X * modelDistance.X + modelDistance.Y * modelDistance.Y +
                         modelDistance.Z * modelDistance.Z);
    }

    [[nodiscard]] inline std::optional<float> WorldSurfaceDistanceToModel(const Matrix4& worldToModel,
                                                                          const Vector3 worldNormal,
                                                                          const float worldDistance) noexcept
    {
        const auto normalLength =
            std::sqrt(worldNormal.X * worldNormal.X + worldNormal.Y * worldNormal.Y + worldNormal.Z * worldNormal.Z);
        if (!std::isfinite(normalLength) || normalLength <= 0.000001F || !std::isfinite(worldDistance) ||
            worldDistance < 0.0F)
            return std::nullopt;
        const auto modelDistance = Math::TransformDirection(
            worldToModel, {worldNormal.X / normalLength * worldDistance, worldNormal.Y / normalLength * worldDistance,
                           worldNormal.Z / normalLength * worldDistance});
        const auto distance = std::sqrt(modelDistance.X * modelDistance.X + modelDistance.Y * modelDistance.Y +
                                        modelDistance.Z * modelDistance.Z);
        return std::isfinite(distance) ? std::optional(distance) : std::nullopt;
    }

    [[nodiscard]] inline std::optional<Vector3> FootTargetAboveSurface(const ModelFootGroundContact& contact,
                                                                       const float automaticClearance,
                                                                       const float minimumClearance) noexcept
    {
        const auto normalLength = std::sqrt(contact.Normal.X * contact.Normal.X + contact.Normal.Y * contact.Normal.Y +
                                            contact.Normal.Z * contact.Normal.Z);
        if (!Math::IsFinite(contact.Position) || !std::isfinite(normalLength) || normalLength <= 0.000001F ||
            !std::isfinite(automaticClearance) || automaticClearance < 0.0F || !std::isfinite(minimumClearance) ||
            minimumClearance < 0.0F)
            return std::nullopt;
        const auto clearance = std::max(automaticClearance, minimumClearance);
        return Vector3{contact.Position.X + contact.Normal.X / normalLength * clearance,
                       contact.Position.Y + contact.Normal.Y / normalLength * clearance,
                       contact.Position.Z + contact.Normal.Z / normalLength * clearance};
    }

    [[nodiscard]] inline std::optional<float> FootBoneSurfaceClearance(const SkeletonAsset& skeleton,
                                                                       const std::span<const Matrix4> modelBones,
                                                                       const std::uint32_t foot,
                                                                       const Vector3 modelNormal) noexcept
    {
        if (modelBones.size() != skeleton.Bones().size() || foot >= modelBones.size() || !Math::IsFinite(modelNormal))
            return std::nullopt;
        const auto normalLength =
            std::sqrt(modelNormal.X * modelNormal.X + modelNormal.Y * modelNormal.Y + modelNormal.Z * modelNormal.Z);
        if (!std::isfinite(normalLength) || normalLength <= 0.000001F)
            return std::nullopt;
        const Vector3 normal{modelNormal.X / normalLength, modelNormal.Y / normalLength, modelNormal.Z / normalLength};
        const auto footPosition = Math::TransformPoint(modelBones[foot], {});
        float clearance = 0.0F;
        for (std::uint32_t candidate = 0; candidate < skeleton.Bones().size(); ++candidate)
        {
            if (candidate == foot || !IsBoneInSubtree(skeleton, candidate, foot))
                continue;
            const auto position = Math::TransformPoint(modelBones[candidate], {});
            const auto projected = (footPosition.X - position.X) * normal.X + (footPosition.Y - position.Y) * normal.Y +
                                   (footPosition.Z - position.Z) * normal.Z;
            clearance = std::max(clearance, projected);
        }
        return clearance;
    }

    [[nodiscard]] inline std::optional<float> FootBoneBindSurfaceClearance(const SkeletonAsset& skeleton,
                                                                           const std::uint32_t foot)
    {
        if (foot >= skeleton.Bones().size())
            return std::nullopt;
        std::vector<Matrix4> modelBones(skeleton.Bones().size());
        for (std::size_t index = 0; index < skeleton.Bones().size(); ++index)
        {
            const auto& bone = skeleton.Bones()[index];
            modelBones[index] =
                Math::ComposeTransform(bone.BindPose.Translation, bone.BindPose.Rotation, bone.BindPose.Scale);
            if (bone.Parent >= 0)
            {
                modelBones[index] =
                    Math::Multiply(modelBones[static_cast<std::size_t>(bone.Parent)], modelBones[index]);
            }
        }
        return FootBoneSurfaceClearance(skeleton, modelBones, foot, {0.0F, 1.0F, 0.0F});
    }

    [[nodiscard]] inline std::optional<std::uint32_t>
    AutomaticFootToeBone(const SkeletonAsset& skeleton, const SkinnedMeshAsset* skin, const std::uint32_t foot)
    {
        if (foot >= skeleton.Bones().size())
            return std::nullopt;

        std::vector<Matrix4> bindModel(skeleton.Bones().size());
        for (std::size_t index = 0; index < skeleton.Bones().size(); ++index)
        {
            const auto& bone = skeleton.Bones()[index];
            bindModel[index] =
                Math::ComposeTransform(bone.BindPose.Translation, bone.BindPose.Rotation, bone.BindPose.Scale);
            if (bone.Parent >= 0)
                bindModel[index] = Math::Multiply(bindModel[static_cast<std::size_t>(bone.Parent)], bindModel[index]);
        }
        const auto footPosition = Math::TransformPoint(bindModel[foot], {});

        std::vector<bool> directlyDeforms(skeleton.Bones().size());
        if (skin)
        {
            for (const auto& influence : skin->Influences8())
            {
                if (influence.Count > influence.Bones.size())
                    return std::nullopt;
                for (std::size_t influenceIndex = 0; influenceIndex < influence.Count; ++influenceIndex)
                {
                    const auto bone = influence.Bones[influenceIndex];
                    if (bone >= directlyDeforms.size() || !std::isfinite(influence.Weights[influenceIndex]) ||
                        influence.Weights[influenceIndex] < 0.0F)
                    {
                        return std::nullopt;
                    }
                    if (influence.Weights[influenceIndex] >= 0.05F)
                        directlyDeforms[bone] = true;
                }
            }
        }

        const auto normalizedName = [](const std::string_view name)
        {
            std::string result;
            result.reserve(name.size());
            for (const auto character : name)
            {
                const auto value = static_cast<unsigned char>(character);
                if (std::isalnum(value) != 0)
                    result.push_back(static_cast<char>(std::tolower(value)));
            }
            return result;
        };
        const auto depthFromFoot = [&skeleton, foot](const std::uint32_t candidate)
        {
            std::size_t depth = 0;
            auto current = skeleton.Bones()[candidate].Parent;
            while (current >= 0 && static_cast<std::size_t>(current) < skeleton.Bones().size())
            {
                ++depth;
                if (current == static_cast<std::int32_t>(foot))
                    return depth;
                current = skeleton.Bones()[static_cast<std::size_t>(current)].Parent;
            }
            return skeleton.Bones().size();
        };

        std::optional<std::uint32_t> best;
        std::size_t bestPriority = std::numeric_limits<std::size_t>::max();
        std::size_t bestDepth = std::numeric_limits<std::size_t>::max();
        for (std::uint32_t candidate = 0; candidate < skeleton.Bones().size(); ++candidate)
        {
            if (!IsBoneInSubtree(skeleton, candidate, foot) || candidate == foot)
                continue;
            const auto position = Math::TransformPoint(bindModel[candidate], {});
            const auto deltaX = position.X - footPosition.X;
            const auto deltaY = position.Y - footPosition.Y;
            const auto deltaZ = position.Z - footPosition.Z;
            const auto planarDistance = std::sqrt(deltaX * deltaX + deltaZ * deltaZ);
            if (planarDistance <= 0.00001F || std::abs(deltaY) > planarDistance * 1.5F)
                continue;
            const auto name = normalizedName(skeleton.Bones()[candidate].Name);
            const auto namedToe = name.find("toe") != std::string::npos || name.find("ball") != std::string::npos ||
                                  name.find("metatars") != std::string::npos || name.find("digit") != std::string::npos;
            const auto priority = namedToe                     ? std::size_t{0}
                                  : directlyDeforms[candidate] ? std::size_t{1}
                                                               : std::size_t{2};
            if (skin && priority == 2)
                continue;
            const auto depth = depthFromFoot(candidate);
            if (priority < bestPriority || (priority == bestPriority && depth < bestDepth))
            {
                best = candidate;
                bestPriority = priority;
                bestDepth = depth;
            }
        }
        return best;
    }

    [[nodiscard]] inline std::optional<float> FootMeshBindSurfaceClearance(const SkeletonAsset& skeleton,
                                                                           const SkinnedMeshAsset& skin,
                                                                           const MeshAsset& mesh,
                                                                           const std::uint32_t foot) noexcept
    {
        constexpr float MinimumFootInfluence = 0.25F;
        const auto vertices = mesh.Vertices();
        const auto influences = skin.Influences8();
        if (foot >= skeleton.Bones().size() || vertices.empty() || influences.size() != vertices.size())
            return std::nullopt;

        std::vector<Matrix4> bindModel(skeleton.Bones().size());
        for (std::size_t index = 0; index < skeleton.Bones().size(); ++index)
        {
            const auto& bone = skeleton.Bones()[index];
            bindModel[index] =
                Math::ComposeTransform(bone.BindPose.Translation, bone.BindPose.Rotation, bone.BindPose.Scale);
            if (bone.Parent >= 0)
                bindModel[index] = Math::Multiply(bindModel[static_cast<std::size_t>(bone.Parent)], bindModel[index]);
        }
        const auto footPosition = Math::TransformPoint(bindModel[foot], {});
        std::vector<bool> footBones(skeleton.Bones().size());
        for (std::uint32_t bone = 0; bone < skeleton.Bones().size(); ++bone)
            footBones[bone] = IsBoneInSubtree(skeleton, bone, foot);
        float clearance = 0.0F;
        bool foundFootVertex = false;
        for (std::size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex)
        {
            const auto& influence = influences[vertexIndex];
            if (influence.Count == 0 || influence.Count > influence.Bones.size())
                return std::nullopt;
            float footWeight = 0.0F;
            for (std::size_t influenceIndex = 0; influenceIndex < influence.Count; ++influenceIndex)
            {
                const auto bone = influence.Bones[influenceIndex];
                const auto weight = influence.Weights[influenceIndex];
                if (bone >= skeleton.Bones().size() || !std::isfinite(weight) || weight < 0.0F)
                    return std::nullopt;
                if (footBones[bone])
                    footWeight += weight;
            }
            if (footWeight < MinimumFootInfluence)
                continue;
            const auto position = vertices[vertexIndex].Position;
            if (!Math::IsFinite(position))
                return std::nullopt;
            clearance = std::max(clearance, footPosition.Y - position.Y);
            foundFootVertex = true;
        }
        return foundFootVertex ? std::optional(clearance) : std::nullopt;
    }
} // namespace Keire::Detail
