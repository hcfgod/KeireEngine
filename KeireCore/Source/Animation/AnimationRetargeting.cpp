#include "Keire/Animation/RiggingSystem.h"

#include "KeireInternal/Animation/RiggingMath.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Keire
{
    namespace
    {
        constexpr std::string_view FbxHelperMarker = "_$AssimpFbx$_";

        struct TrackResolution final
        {
            std::uint32_t SourceLogicalBone = 0;
            std::optional<std::uint32_t> TargetLogicalBone;
            AnimationRetargetMatch Match = AnimationRetargetMatch::Unmapped;
        };

        struct RetargetEntry final
        {
            std::uint32_t Source = 0;
            std::uint32_t Target = 0;
            float TranslationScale = 1.0F;
        };

        [[nodiscard]] bool IsFbxHelper(const std::string_view name) noexcept
        {
            return name.find(FbxHelperMarker) != std::string_view::npos;
        }

        [[nodiscard]] std::string_view LogicalBoneName(const std::string_view name) noexcept
        {
            const auto marker = name.find(FbxHelperMarker);
            return marker == std::string_view::npos ? name : name.substr(0, marker);
        }

        [[nodiscard]] std::unordered_map<std::string_view, std::uint32_t>
        BoneIndicesByName(const SkeletonAsset& skeleton)
        {
            std::unordered_map<std::string_view, std::uint32_t> result;
            result.reserve(skeleton.Bones().size());
            for (std::uint32_t index = 0; index < skeleton.Bones().size(); ++index)
                result.emplace(skeleton.Bones()[index].Name, index);
            return result;
        }

        [[nodiscard]] std::uint32_t
        LogicalBone(const SkeletonAsset& skeleton,
                    const std::unordered_map<std::string_view, std::uint32_t>& bonesByName,
                    const std::uint32_t bone)
        {
            const auto& name = skeleton.Bones()[bone].Name;
            if (!IsFbxHelper(name))
                return bone;
            const auto found = bonesByName.find(LogicalBoneName(name));
            return found == bonesByName.end() ? bone : found->second;
        }

        [[nodiscard]] std::int32_t LogicalParent(const SkeletonAsset& skeleton, const std::uint32_t bone) noexcept
        {
            auto parent = skeleton.Bones()[bone].Parent;
            while (parent >= 0 && IsFbxHelper(skeleton.Bones()[static_cast<std::size_t>(parent)].Name))
                parent = skeleton.Bones()[static_cast<std::size_t>(parent)].Parent;
            return parent;
        }

        [[nodiscard]] std::vector<Matrix4> BindModelMatrices(const SkeletonAsset& skeleton)
        {
            std::vector<BoneTransform> bindPose;
            bindPose.reserve(skeleton.Bones().size());
            std::ranges::transform(skeleton.Bones(), std::back_inserter(bindPose), &SkeletonBone::BindPose);
            return RiggingDetail::WorldMatrices(skeleton, bindPose);
        }

        [[nodiscard]] Matrix4 RelativeTransform(const std::span<const Matrix4> models, const std::int32_t parent,
                                                const std::uint32_t child)
        {
            return parent < 0 ? models[child]
                              : Math::Multiply(Math::Inverse(models[static_cast<std::size_t>(parent)]), models[child]);
        }

        [[nodiscard]] float LogicalTranslationScale(const SkeletonAsset& sourceSkeleton,
                                                    const std::span<const Matrix4> sourceBindModels,
                                                    const std::uint32_t sourceBone,
                                                    const SkeletonAsset& targetSkeleton,
                                                    const std::span<const Matrix4> targetBindModels,
                                                    const std::uint32_t targetBone)
        {
            Vector3 sourceTranslation;
            Quaternion sourceRotation;
            Vector3 sourceScale;
            Vector3 targetTranslation;
            Quaternion targetRotation;
            Vector3 targetScale;
            const auto sourceRelative = RelativeTransform(sourceBindModels, LogicalParent(sourceSkeleton, sourceBone),
                                                          sourceBone);
            const auto targetRelative = RelativeTransform(targetBindModels, LogicalParent(targetSkeleton, targetBone),
                                                          targetBone);
            if (!Math::DecomposeTransform(sourceRelative, sourceTranslation, sourceRotation, sourceScale) ||
                !Math::DecomposeTransform(targetRelative, targetTranslation, targetRotation, targetScale))
            {
                return 1.0F;
            }
            const auto sourceLength = RiggingDetail::Length(sourceTranslation);
            const auto targetLength = RiggingDetail::Length(targetTranslation);
            return sourceLength > RiggingDetail::Epsilon ? targetLength / sourceLength : 1.0F;
        }

        [[nodiscard]] TrackResolution
        ResolveTrack(const SkeletonAsset& sourceSkeleton, const RigDefinition& sourceRig,
                     const std::unordered_map<std::string_view, std::uint32_t>& sourceByName,
                     const AnimationTrack& track, const SkeletonAsset& targetSkeleton,
                     const std::unordered_map<std::string_view, std::uint32_t>& targetByName,
                     const std::unordered_map<RigBoneSemantic, std::uint32_t>& targetBySemantic)
        {
            TrackResolution result;
            result.SourceLogicalBone = LogicalBone(sourceSkeleton, sourceByName, track.Bone);
            const auto& sourceName = sourceSkeleton.Bones()[track.Bone].Name;
            const auto logicalName = LogicalBoneName(sourceName);
            if (const auto exactLogical = targetByName.find(logicalName); exactLogical != targetByName.end())
            {
                result.TargetLogicalBone = exactLogical->second;
                result.Match = IsFbxHelper(sourceName) ? AnimationRetargetMatch::Hierarchy
                                                       : AnimationRetargetMatch::ExactName;
                return result;
            }
            if (const auto exact = targetByName.find(sourceName); exact != targetByName.end())
            {
                result.TargetLogicalBone = LogicalBone(targetSkeleton, targetByName, exact->second);
                result.Match = AnimationRetargetMatch::ExactName;
                return result;
            }

            const auto semantic = sourceRig.Bones[result.SourceLogicalBone].Semantic;
            if (semantic != RigBoneSemantic::None)
            {
                if (const auto target = targetBySemantic.find(semantic); target != targetBySemantic.end())
                {
                    result.TargetLogicalBone = LogicalBone(targetSkeleton, targetByName, target->second);
                    result.Match = AnimationRetargetMatch::Semantic;
                }
            }
            return result;
        }

        [[nodiscard]] int MatchPriority(const AnimationRetargetMatch match) noexcept
        {
            switch (match)
            {
            case AnimationRetargetMatch::ExactName:
                return 3;
            case AnimationRetargetMatch::Hierarchy:
                return 2;
            case AnimationRetargetMatch::Semantic:
                return 1;
            case AnimationRetargetMatch::Unmapped:
            case AnimationRetargetMatch::TargetConflict:
                return 0;
            }
            return 0;
        }

        [[nodiscard]] BoneTransform Blend(const BoneTransform& first, const BoneTransform& second, const float alpha)
        {
            const auto blendVector = [alpha](const Vector3 left, const Vector3 right)
            {
                return Vector3{left.X + (right.X - left.X) * alpha, left.Y + (right.Y - left.Y) * alpha,
                               left.Z + (right.Z - left.Z) * alpha};
            };
            auto secondRotation = second.Rotation;
            const auto dot = first.Rotation.X * secondRotation.X + first.Rotation.Y * secondRotation.Y +
                             first.Rotation.Z * secondRotation.Z + first.Rotation.W * secondRotation.W;
            if (dot < 0.0F)
                secondRotation = {-secondRotation.X, -secondRotation.Y, -secondRotation.Z, -secondRotation.W};
            const Quaternion rotation{first.Rotation.X + (secondRotation.X - first.Rotation.X) * alpha,
                                      first.Rotation.Y + (secondRotation.Y - first.Rotation.Y) * alpha,
                                      first.Rotation.Z + (secondRotation.Z - first.Rotation.Z) * alpha,
                                      first.Rotation.W + (secondRotation.W - first.Rotation.W) * alpha};
            return {blendVector(first.Translation, second.Translation), Math::Normalize(rotation),
                    blendVector(first.Scale, second.Scale)};
        }

        [[nodiscard]] BoneTransform SampleTrack(const AnimationTrack& track, const float time)
        {
            if (track.Keys.size() == 1 || time <= track.Keys.front().Time)
                return track.Keys.front().Value;
            if (time >= track.Keys.back().Time)
                return track.Keys.back().Value;
            const auto upper = std::ranges::upper_bound(track.Keys, time, {}, &AnimationKeyframe::Time);
            const auto& second = *upper;
            const auto& first = *(upper - 1);
            return Blend(first.Value, second.Value, (time - first.Time) / (second.Time - first.Time));
        }

        [[nodiscard]] Vector3 SafeRelativeScale(const Vector3 scale) noexcept
        {
            const auto axis = [](const float value) noexcept
            { return std::isfinite(value) && value >= 0.125F && value <= 8.0F ? value : 1.0F; };
            return {axis(scale.X), axis(scale.Y), axis(scale.Z)};
        }
    } // namespace

    AnimationRetargetDiagnostics DiagnoseAnimationRetargeting(const SkeletonAsset& sourceSkeleton,
                                                              const RigDefinition& sourceRig,
                                                              const AnimationClipAsset& sourceClip,
                                                              const SkeletonAsset& targetSkeleton,
                                                              const RigDefinition& targetRig)
    {
        ValidateRigDefinition(sourceRig);
        ValidateRigDefinition(targetRig);
        if (sourceSkeleton.Bones().size() != sourceRig.Bones.size() ||
            targetSkeleton.Bones().size() != targetRig.Bones.size())
        {
            throw std::invalid_argument("Retargeting requires rig definitions that match their skeletons.");
        }

        const auto sourceByName = BoneIndicesByName(sourceSkeleton);
        const auto targetByName = BoneIndicesByName(targetSkeleton);
        std::unordered_map<RigBoneSemantic, std::uint32_t> targetBySemantic;
        for (std::uint32_t index = 0; index < targetRig.Bones.size(); ++index)
            if (targetRig.Bones[index].Semantic != RigBoneSemantic::None)
                targetBySemantic.emplace(targetRig.Bones[index].Semantic, index);
        const auto sourceBindModels = BindModelMatrices(sourceSkeleton);
        const auto targetBindModels = BindModelMatrices(targetSkeleton);

        AnimationRetargetDiagnostics result;
        result.SourceTrackCount = sourceClip.Tracks().size();
        result.Mappings.reserve(sourceClip.Tracks().size());
        std::vector<std::uint32_t> sourceLogicalBones;
        sourceLogicalBones.reserve(sourceClip.Tracks().size());
        struct AcceptedTarget final
        {
            std::uint32_t Source = 0;
            int Priority = 0;
        };
        std::unordered_map<std::uint32_t, AcceptedTarget> acceptedTargets;
        bool usedHierarchyMapping = false;

        for (const auto& sourceTrack : sourceClip.Tracks())
        {
            AnimationRetargetBoneMapping mapping;
            mapping.SourceBone = sourceTrack.Bone;
            if (sourceTrack.Bone >= sourceRig.Bones.size())
            {
                result.Messages.push_back({RigDiagnosticSeverity::Error, "KEIRERETARGET0001",
                                           "The clip references a source bone outside its rig."});
                result.Mappings.push_back(std::move(mapping));
                sourceLogicalBones.push_back(sourceTrack.Bone);
                continue;
            }

            mapping.SourceName = sourceSkeleton.Bones()[sourceTrack.Bone].Name;
            const auto resolution = ResolveTrack(sourceSkeleton, sourceRig, sourceByName, sourceTrack, targetSkeleton,
                                                 targetByName, targetBySemantic);
            sourceLogicalBones.push_back(resolution.SourceLogicalBone);
            mapping.Semantic = sourceRig.Bones[resolution.SourceLogicalBone].Semantic;
            mapping.Match = resolution.Match;
            if (!resolution.TargetLogicalBone)
            {
                result.Messages.push_back({RigDiagnosticSeverity::Warning, "KEIRERETARGET0002",
                                           "No target bone matches source track '" + mapping.SourceName + "'.",
                                           mapping.Semantic});
                result.Mappings.push_back(std::move(mapping));
                continue;
            }

            mapping.TargetBone = resolution.TargetLogicalBone;
            mapping.TargetName = targetSkeleton.Bones()[*resolution.TargetLogicalBone].Name;
            mapping.TranslationScale = LogicalTranslationScale(
                sourceSkeleton, sourceBindModels, resolution.SourceLogicalBone, targetSkeleton, targetBindModels,
                *resolution.TargetLogicalBone);
            const auto& sourceBind = sourceSkeleton.Bones()[sourceTrack.Bone].BindPose;
            for (const auto& key : sourceTrack.Keys)
            {
                const auto fallback = [](const float animated, const float source)
                {
                    if (std::abs(source) <= RiggingDetail::Epsilon)
                        return true;
                    const auto relative = animated / source;
                    return !std::isfinite(relative) || relative < 0.125F || relative > 8.0F;
                };
                mapping.ScaleFallbackKeyCount += fallback(key.Value.Scale.X, sourceBind.Scale.X) ? 1U : 0U;
                mapping.ScaleFallbackKeyCount += fallback(key.Value.Scale.Y, sourceBind.Scale.Y) ? 1U : 0U;
                mapping.ScaleFallbackKeyCount += fallback(key.Value.Scale.Z, sourceBind.Scale.Z) ? 1U : 0U;
            }

            const auto priority = MatchPriority(mapping.Match);
            if (const auto accepted = acceptedTargets.find(*mapping.TargetBone); accepted != acceptedTargets.end() &&
                accepted->second.Source != resolution.SourceLogicalBone)
            {
                if (priority > accepted->second.Priority)
                {
                    for (std::size_t index = 0; index < result.Mappings.size(); ++index)
                    {
                        auto& previous = result.Mappings[index];
                        if (previous.TargetBone == mapping.TargetBone &&
                            sourceLogicalBones[index] == accepted->second.Source)
                        {
                            previous.TargetBone.reset();
                            previous.Match = AnimationRetargetMatch::TargetConflict;
                        }
                    }
                    accepted->second = {resolution.SourceLogicalBone, priority};
                }
                else
                {
                    mapping.TargetBone.reset();
                    mapping.Match = AnimationRetargetMatch::TargetConflict;
                }
                result.Messages.push_back({RigDiagnosticSeverity::Warning, "KEIRERETARGET0003",
                                           "Multiple source bone groups resolve to target bone '" +
                                               targetSkeleton.Bones()[*resolution.TargetLogicalBone].Name +
                                               "'; the strongest hierarchy match takes priority.",
                                           mapping.Semantic});
            }
            else
            {
                acceptedTargets.insert_or_assign(*mapping.TargetBone,
                                                 AcceptedTarget{resolution.SourceLogicalBone, priority});
            }
            usedHierarchyMapping = usedHierarchyMapping || mapping.Match == AnimationRetargetMatch::Hierarchy;
            result.Mappings.push_back(std::move(mapping));
        }

        for (const auto& mapping : result.Mappings)
        {
            if (!mapping.TargetBone)
                continue;
            ++result.MappedTrackCount;
            result.ExactNameMatchCount += mapping.Match == AnimationRetargetMatch::ExactName ? 1U : 0U;
            result.HierarchyMatchCount += mapping.Match == AnimationRetargetMatch::Hierarchy ? 1U : 0U;
            result.SemanticMatchCount += mapping.Match == AnimationRetargetMatch::Semantic ? 1U : 0U;
        }
        if (usedHierarchyMapping)
        {
            result.Messages.push_back(
                {RigDiagnosticSeverity::Information, "KEIRERETARGET0006",
                 "Importer helper tracks were collapsed into logical bones before reference-pose retargeting."});
        }
        if (sourceClip.RootMotion())
        {
            const auto rootMapping = std::ranges::find(result.Mappings, 0U, &AnimationRetargetBoneMapping::SourceBone);
            result.RootMotionMapped = rootMapping != result.Mappings.end() && rootMapping->TargetBone == 0U;
            if (!result.RootMotionMapped)
                result.Messages.push_back(
                    {RigDiagnosticSeverity::Warning, "KEIRERETARGET0004",
                     "Root motion is enabled but the source root does not map to the target root."});
        }
        if (!result.Compatible())
            result.Messages.push_back({RigDiagnosticSeverity::Error, "KEIRERETARGET0005",
                                       "Retargeting found no compatible source animation tracks."});
        return result;
    }

    AnimationRetargetResult
    RetargetAnimationClipWithDiagnostics(const SkeletonAsset& sourceSkeleton, const RigDefinition& sourceRig,
                                         const AnimationClipAsset& sourceClip, const AssetId targetSkeletonId,
                                         const SkeletonAsset& targetSkeleton, const RigDefinition& targetRig)
    {
        auto diagnostics =
            DiagnoseAnimationRetargeting(sourceSkeleton, sourceRig, sourceClip, targetSkeleton, targetRig);
        if (!diagnostics.Compatible())
            throw std::invalid_argument("Retargeting found no compatible semantic bone tracks.");

        const auto sourceByName = BoneIndicesByName(sourceSkeleton);
        std::vector<RetargetEntry> entries;
        entries.reserve(diagnostics.MappedTrackCount);
        for (const auto& mapping : diagnostics.Mappings)
        {
            if (!mapping.TargetBone)
                continue;
            const auto source = LogicalBone(sourceSkeleton, sourceByName, mapping.SourceBone);
            const auto duplicate = std::ranges::find(entries, *mapping.TargetBone, &RetargetEntry::Target);
            if (duplicate == entries.end())
                entries.push_back({source, *mapping.TargetBone, mapping.TranslationScale});
        }
        std::ranges::sort(entries, {}, &RetargetEntry::Target);

        std::vector<float> keyTimes;
        for (const auto& track : sourceClip.Tracks())
            for (const auto& key : track.Keys)
                keyTimes.push_back(key.Time);
        std::ranges::sort(keyTimes);
        keyTimes.erase(std::unique(keyTimes.begin(), keyTimes.end(), [](const float left, const float right)
                                   { return std::abs(left - right) <= 0.000001F; }),
                       keyTimes.end());

        std::vector<AnimationTrack> tracks;
        tracks.reserve(entries.size());
        for (const auto& entry : entries)
        {
            AnimationTrack track;
            track.Bone = entry.Target;
            track.Keys.reserve(keyTimes.size());
            tracks.push_back(std::move(track));
        }

        const auto sourceBindModels = BindModelMatrices(sourceSkeleton);
        const auto targetBindModels = BindModelMatrices(targetSkeleton);
        std::vector<std::int32_t> entryByTarget(targetSkeleton.Bones().size(), -1);
        for (std::size_t index = 0; index < entries.size(); ++index)
            entryByTarget[entries[index].Target] = static_cast<std::int32_t>(index);

        for (const auto time : keyTimes)
        {
            std::vector<BoneTransform> sourcePose;
            sourcePose.reserve(sourceSkeleton.Bones().size());
            std::ranges::transform(sourceSkeleton.Bones(), std::back_inserter(sourcePose), &SkeletonBone::BindPose);
            for (const auto& sourceTrack : sourceClip.Tracks())
                sourcePose[sourceTrack.Bone] = SampleTrack(sourceTrack, time);
            const auto sourceModels = RiggingDetail::WorldMatrices(sourceSkeleton, sourcePose);

            std::vector<BoneTransform> targetPose;
            targetPose.reserve(targetSkeleton.Bones().size());
            std::ranges::transform(targetSkeleton.Bones(), std::back_inserter(targetPose), &SkeletonBone::BindPose);
            std::vector<Matrix4> targetModels(targetSkeleton.Bones().size());
            for (std::uint32_t target = 0; target < targetSkeleton.Bones().size(); ++target)
            {
                const auto entryIndex = entryByTarget[target];
                if (entryIndex >= 0)
                {
                    const auto& entry = entries[static_cast<std::size_t>(entryIndex)];
                    const auto sourceParent = LogicalParent(sourceSkeleton, entry.Source);
                    const auto targetParent = LogicalParent(targetSkeleton, entry.Target);
                    const auto sourceBindRelative = RelativeTransform(sourceBindModels, sourceParent, entry.Source);
                    const auto sourceAnimatedRelative = RelativeTransform(sourceModels, sourceParent, entry.Source);
                    auto sourceDelta = Math::Multiply(Math::Inverse(sourceBindRelative), sourceAnimatedRelative);
                    Vector3 deltaTranslation;
                    Quaternion deltaRotation;
                    Vector3 deltaScale;
                    if (!Math::DecomposeTransform(sourceDelta, deltaTranslation, deltaRotation, deltaScale))
                        throw std::runtime_error("Animation retargeting produced a non-decomposable source delta.");
                    deltaTranslation = RiggingDetail::Multiply(deltaTranslation, entry.TranslationScale);
                    sourceDelta = Math::ComposeTransform(deltaTranslation, deltaRotation, SafeRelativeScale(deltaScale));

                    const auto targetBindRelative = RelativeTransform(targetBindModels, targetParent, entry.Target);
                    const auto desiredRelative = Math::Multiply(targetBindRelative, sourceDelta);
                    const auto desiredModel = targetParent < 0
                                                  ? desiredRelative
                                                  : Math::Multiply(targetModels[static_cast<std::size_t>(targetParent)],
                                                                   desiredRelative);
                    const auto immediateParent = targetSkeleton.Bones()[target].Parent;
                    const auto local = immediateParent < 0
                                           ? desiredModel
                                           : Math::Multiply(
                                                 Math::Inverse(targetModels[static_cast<std::size_t>(immediateParent)]),
                                                 desiredModel);
                    if (!Math::DecomposeTransform(local, targetPose[target].Translation, targetPose[target].Rotation,
                                                  targetPose[target].Scale))
                    {
                        throw std::runtime_error("Animation retargeting produced a non-decomposable target pose.");
                    }
                    tracks[static_cast<std::size_t>(entryIndex)].Keys.push_back({time, targetPose[target]});
                }

                const auto& pose = targetPose[target];
                const auto local = Math::ComposeTransform(pose.Translation, pose.Rotation, pose.Scale);
                const auto parent = targetSkeleton.Bones()[target].Parent;
                targetModels[target] = parent < 0
                                           ? local
                                           : Math::Multiply(targetModels[static_cast<std::size_t>(parent)], local);
            }
        }

        AnimationRetargetResult result;
        result.Clip = CreateRef<AnimationClipAsset>(
            targetSkeletonId, sourceClip.Duration(), std::move(tracks),
            std::vector<AnimationEvent>(sourceClip.Events().begin(), sourceClip.Events().end()),
            sourceClip.RootMotion() && diagnostics.RootMotionMapped);
        result.Diagnostics = std::move(diagnostics);
        return result;
    }

    Ref<AnimationClipAsset> RetargetAnimationClip(const SkeletonAsset& sourceSkeleton, const RigDefinition& sourceRig,
                                                  const AnimationClipAsset& sourceClip,
                                                  const AssetId targetSkeletonId,
                                                  const SkeletonAsset& targetSkeleton,
                                                  const RigDefinition& targetRig)
    {
        return RetargetAnimationClipWithDiagnostics(sourceSkeleton, sourceRig, sourceClip, targetSkeletonId,
                                                    targetSkeleton, targetRig)
            .Clip;
    }
} // namespace Keire
