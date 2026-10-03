#pragma once

#include "Keire/Animation/AnimationSystem.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace KeireEditor
{
    struct RigChainInspection
    {
        std::vector<std::size_t> Bones;
        std::vector<float> SegmentLengths;
        float MaximumReach = 0.0F;
        std::string Error;
        [[nodiscard]] bool Valid() const noexcept { return Error.empty(); }
    };

    // SkeletonAsset guarantees unique names and parents preceding children. Include the entire
    // ancestor transform so a scaled model root does not silently under-report a limb's reach.
    [[nodiscard]] inline RigChainInspection InspectRigChain(const Keire::SkeletonAsset& skeleton,
                                                            const std::size_t root, const std::size_t tip)
    {
        RigChainInspection result;
        const auto bones = skeleton.Bones();
        if (root >= bones.size() || tip >= bones.size())
        {
            result.Error = "Choose a chain root and tip from this skeleton.";
            return result;
        }
        if (root == tip)
        {
            result.Error = "Choose a tip below the root. A chain needs at least two bones.";
            return result;
        }
        for (auto current = static_cast<std::int32_t>(tip); current >= 0; current = bones[current].Parent)
        {
            result.Bones.push_back(static_cast<std::size_t>(current));
            if (static_cast<std::size_t>(current) == root)
                break;
        }
        if (result.Bones.back() != root)
        {
            result.Bones.clear();
            result.Error = "The tip is not below this root. Select bones on the same limb, with root above tip.";
            return result;
        }
        std::reverse(result.Bones.begin(), result.Bones.end());
        std::vector<Keire::Matrix4> transforms(bones.size());
        for (std::size_t index = 0; index < bones.size(); ++index)
        {
            const auto& bone = bones[index];
            const auto local =
                Keire::Math::ComposeTransform(bone.BindPose.Translation, bone.BindPose.Rotation, bone.BindPose.Scale);
            transforms[index] = bone.Parent < 0 ? local : Keire::Math::Multiply(transforms[bone.Parent], local);
        }
        for (std::size_t index = 1; index < result.Bones.size(); ++index)
        {
            const auto a = Keire::Math::TransformPoint(transforms[result.Bones[index - 1]], {});
            const auto b = Keire::Math::TransformPoint(transforms[result.Bones[index]], {});
            const auto length = std::hypot(b.X - a.X, b.Y - a.Y, b.Z - a.Z);
            if (!std::isfinite(length) || length <= 0.000001F)
            {
                result.Error = "Bone '" + bones[result.Bones[index]].Name +
                               "' has a zero or invalid bind segment. Choose another endpoint or repair the skeleton.";
                return result;
            }
            result.SegmentLengths.push_back(length);
            result.MaximumReach += length;
        }
        if (!std::isfinite(result.MaximumReach))
            result.Error = "Chain reach overflows. Check the model's import scale and bone transforms.";
        return result;
    }

    [[nodiscard]] inline std::vector<Keire::Vector3>
    PublishedRigChainPoints(const Keire::SkeletonAsset& skeleton, const std::span<const Keire::Matrix4> palette,
                            const std::span<const std::size_t> chain, const Keire::Matrix4& entityWorld)
    {
        if (palette.size() != skeleton.Bones().size() || palette.empty())
            throw std::invalid_argument("No matching published pose. Preview an animation or run scene Play Mode.");
        std::vector<Keire::Vector3> points;
        points.reserve(chain.size());
        for (const auto index : chain)
        {
            if (index >= palette.size())
                throw std::invalid_argument(
                    "Chain selection no longer matches this skeleton. Select its endpoints again.");
            const auto pose =
                Keire::Math::Multiply(palette[index], Keire::Math::Inverse(skeleton.Bones()[index].InverseBindPose));
            const auto point = Keire::Math::TransformPoint(Keire::Math::Multiply(entityWorld, pose), {});
            if (!Keire::Math::IsFinite(point))
                throw std::invalid_argument(
                    "Published chain contains a non-finite joint position. Inspect the animation and IK targets.");
            points.push_back(point);
        }
        return points;
    }
} // namespace KeireEditor
