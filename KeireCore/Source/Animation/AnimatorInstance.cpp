#include "Keire/Animation/AnimationSystem.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace Keire
{
    namespace
    {
        [[nodiscard]] BoneTransform Blend(const BoneTransform& first, const BoneTransform& second, const float alpha)
        {
            const auto vector = [alpha](const Vector3 left, const Vector3 right)
            {
                return Vector3{left.X + (right.X - left.X) * alpha, left.Y + (right.Y - left.Y) * alpha,
                               left.Z + (right.Z - left.Z) * alpha};
            };
            auto secondRotation = second.Rotation;
            const auto rotationDot = first.Rotation.X * secondRotation.X + first.Rotation.Y * secondRotation.Y +
                                     first.Rotation.Z * secondRotation.Z + first.Rotation.W * secondRotation.W;
            if (rotationDot < 0.0F)
                secondRotation = {-secondRotation.X, -secondRotation.Y, -secondRotation.Z, -secondRotation.W};
            auto rotation = Quaternion{first.Rotation.X + (secondRotation.X - first.Rotation.X) * alpha,
                                       first.Rotation.Y + (secondRotation.Y - first.Rotation.Y) * alpha,
                                       first.Rotation.Z + (secondRotation.Z - first.Rotation.Z) * alpha,
                                       first.Rotation.W + (secondRotation.W - first.Rotation.W) * alpha};
            return {vector(first.Translation, second.Translation), Math::Normalize(rotation),
                    vector(first.Scale, second.Scale)};
        }

        [[nodiscard]] Quaternion RotationDelta(const Quaternion previous, const Quaternion current)
        {
            const auto first = Math::Normalize(previous);
            const auto second = Math::Normalize(current);
            const Quaternion inverse{-first.X, -first.Y, -first.Z, first.W};
            auto result = Math::Normalize(
                {inverse.W * second.X + inverse.X * second.W + inverse.Y * second.Z - inverse.Z * second.Y,
                 inverse.W * second.Y - inverse.X * second.Z + inverse.Y * second.W + inverse.Z * second.X,
                 inverse.W * second.Z + inverse.X * second.Y - inverse.Y * second.X + inverse.Z * second.W,
                 inverse.W * second.W - inverse.X * second.X - inverse.Y * second.Y - inverse.Z * second.Z});
            if (result.W < 0.0F)
                result = {-result.X, -result.Y, -result.Z, -result.W};
            return result;
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
            const auto alpha = (time - first.Time) / (second.Time - first.Time);
            return Blend(first.Value, second.Value, alpha);
        }

        [[nodiscard]] std::vector<AnimatorPoseBoneDebugState>
        BuildPoseDebugState(const SkeletonAsset& skeleton, const std::span<const BoneTransform> localPose)
        {
            if (localPose.size() != skeleton.Bones().size())
                throw std::invalid_argument("Animator debug pose does not match its skeleton.");
            std::vector<Matrix4> world(localPose.size());
            std::vector<AnimatorPoseBoneDebugState> result;
            result.reserve(localPose.size());
            for (std::size_t index = 0; index < localPose.size(); ++index)
            {
                const auto& bone = skeleton.Bones()[index];
                const auto& transform = localPose[index];
                const auto local = Math::ComposeTransform(transform.Translation, transform.Rotation, transform.Scale);
                world[index] =
                    bone.Parent < 0 ? local : Math::Multiply(world[static_cast<std::size_t>(bone.Parent)], local);
                result.push_back({bone.Name, bone.Parent, transform, Math::TransformPoint(world[index], {})});
            }
            return result;
        }

        [[nodiscard]] bool Compare(const float current, const AnimationTransitionCondition& condition) noexcept
        {
            switch (condition.Comparison)
            {
            case AnimationConditionComparison::Greater:
                return current > condition.Value;
            case AnimationConditionComparison::Less:
                return current < condition.Value;
            case AnimationConditionComparison::Equal:
                return current == condition.Value;
            case AnimationConditionComparison::NotEqual:
                return current != condition.Value;
            }
            return false;
        }

        [[nodiscard]] const AnimationParameterDefinition* FindParameterById(const AnimationGraphDefinition& definition,
                                                                            const std::string_view id) noexcept
        {
            const auto found =
                std::ranges::find(definition.ParameterDefinitions, id, &AnimationParameterDefinition::Id);
            return found == definition.ParameterDefinitions.end() ? nullptr : std::addressof(*found);
        }

        [[nodiscard]] const AnimationLayerDefinition* FindLayer(const AnimationGraphDefinition& definition,
                                                                const std::string_view id) noexcept
        {
            const auto found = std::ranges::find(definition.Layers, id, &AnimationLayerDefinition::Id);
            return found == definition.Layers.end() ? nullptr : std::addressof(*found);
        }

        [[nodiscard]] const AnimationStateDefinition* FindState(const AnimationLayerDefinition& layer,
                                                                const std::string_view id) noexcept
        {
            const auto found = std::ranges::find(layer.States, id, &AnimationStateDefinition::Id);
            return found == layer.States.end() ? nullptr : std::addressof(*found);
        }

        struct WeightedClip
        {
            std::string Id;
            AssetId Asset;
            Ref<const AnimationClipAsset> Clip;
            float Weight = 0.0F;
            float Speed = 1.0F;
        };

        struct MotionEvaluation
        {
            std::vector<BoneTransform> Pose;
            std::vector<AnimatorBlendWeight> Weights;
            std::vector<AnimationEvent> Events;
            float Duration = 0.0F;
            bool RootMotion = false;
        };

        using WeightedBlendTreeChildren = std::vector<std::pair<const AnimationBlendTreeChild*, float>>;

        [[nodiscard]] double SquaredDistance(const Vector2 first, const Vector2 second) noexcept
        {
            const auto x = static_cast<double>(first.X) - static_cast<double>(second.X);
            const auto y = static_cast<double>(first.Y) - static_cast<double>(second.Y);
            return x * x + y * y;
        }

        [[nodiscard]] double OrientedArea(const Vector2 first, const Vector2 second, const Vector2 third) noexcept
        {
            const auto firstX = static_cast<double>(second.X) - static_cast<double>(first.X);
            const auto firstY = static_cast<double>(second.Y) - static_cast<double>(first.Y);
            const auto secondX = static_cast<double>(third.X) - static_cast<double>(first.X);
            const auto secondY = static_cast<double>(third.Y) - static_cast<double>(first.Y);
            return firstX * secondY - firstY * secondX;
        }

        [[nodiscard]] WeightedBlendTreeChildren
        ResolveBlendTree2DWeights(const std::span<const AnimationBlendTreeChild> children, const Vector2 value)
        {
            constexpr double ExactPositionToleranceSquared = 1.0e-12;
            constexpr double DegenerateAreaTolerance = 1.0e-12;
            constexpr double BarycentricTolerance = 1.0e-7;
            constexpr float MinimumWeight = 1.0e-6F;
            constexpr std::size_t MaximumLocalChildren = 12;

            std::vector<const AnimationBlendTreeChild*> sorted;
            sorted.reserve(children.size());
            for (const auto& child : children)
                sorted.push_back(std::addressof(child));
            std::ranges::sort(sorted, {}, &AnimationBlendTreeChild::Id);

            for (const auto* child : sorted)
                if (SquaredDistance(value, child->Position) <= ExactPositionToleranceSquared)
                    return {{child, 1.0F}};
            std::ranges::sort(sorted,
                              [value](const auto* first, const auto* second)
                              {
                                  return std::tuple{SquaredDistance(value, first->Position), first->Id} <
                                         std::tuple{SquaredDistance(value, second->Position), second->Id};
                              });
            // Runtime graphs allow hundreds of samples. Bound the simplex search to the nearest neighborhood so pose
            // evaluation remains predictable while distant and opposing motions cannot influence the result.
            if (sorted.size() > MaximumLocalChildren)
                sorted.resize(MaximumLocalChildren);

            struct TriangleCandidate
            {
                std::array<const AnimationBlendTreeChild*, 3> Children{};
                std::array<double, 3> Weights{};
                std::tuple<double, double, double> Score;
            };

            TriangleCandidate bestTriangle;
            bool foundTriangle = false;
            for (std::size_t first = 0; first + 2 < sorted.size(); ++first)
            {
                for (std::size_t second = first + 1; second + 1 < sorted.size(); ++second)
                {
                    for (std::size_t third = second + 1; third < sorted.size(); ++third)
                    {
                        const auto& firstPosition = sorted[first]->Position;
                        const auto& secondPosition = sorted[second]->Position;
                        const auto& thirdPosition = sorted[third]->Position;
                        const auto area = OrientedArea(firstPosition, secondPosition, thirdPosition);
                        if (std::abs(area) <= DegenerateAreaTolerance)
                            continue;

                        std::array<double, 3> weights{OrientedArea(value, secondPosition, thirdPosition) / area,
                                                      OrientedArea(value, thirdPosition, firstPosition) / area,
                                                      OrientedArea(value, firstPosition, secondPosition) / area};
                        if (std::ranges::any_of(weights,
                                                [](const double weight) { return weight < -BarycentricTolerance; }))
                            continue;

                        const std::array<double, 3> distances{SquaredDistance(value, firstPosition),
                                                              SquaredDistance(value, secondPosition),
                                                              SquaredDistance(value, thirdPosition)};
                        const auto score = std::tuple{*std::ranges::max_element(distances),
                                                      distances[0] + distances[1] + distances[2], std::abs(area)};
                        if (!foundTriangle || score < bestTriangle.Score)
                        {
                            bestTriangle = {{{sorted[first], sorted[second], sorted[third]}}, weights, score};
                            foundTriangle = true;
                        }
                    }
                }
            }

            WeightedBlendTreeChildren result;
            if (foundTriangle)
            {
                double total = 0.0;
                for (auto& weight : bestTriangle.Weights)
                {
                    weight = std::max(0.0, weight);
                    total += weight;
                }
                for (std::size_t index = 0; index < bestTriangle.Children.size(); ++index)
                {
                    const auto weight = static_cast<float>(bestTriangle.Weights[index] / total);
                    if (weight > MinimumWeight)
                        result.emplace_back(bestTriangle.Children[index], weight);
                }
                return result;
            }

            struct SegmentCandidate
            {
                const AnimationBlendTreeChild* First = nullptr;
                const AnimationBlendTreeChild* Second = nullptr;
                double Amount = 0.0;
                std::pair<double, double> Score;
            };

            SegmentCandidate bestSegment;
            bool foundSegment = false;
            for (std::size_t first = 0; first + 1 < sorted.size(); ++first)
            {
                for (std::size_t second = first + 1; second < sorted.size(); ++second)
                {
                    const auto segmentX = static_cast<double>(sorted[second]->Position.X) -
                                          static_cast<double>(sorted[first]->Position.X);
                    const auto segmentY = static_cast<double>(sorted[second]->Position.Y) -
                                          static_cast<double>(sorted[first]->Position.Y);
                    const auto lengthSquared = segmentX * segmentX + segmentY * segmentY;
                    if (lengthSquared <= ExactPositionToleranceSquared)
                        continue;
                    const auto valueX = static_cast<double>(value.X) - static_cast<double>(sorted[first]->Position.X);
                    const auto valueY = static_cast<double>(value.Y) - static_cast<double>(sorted[first]->Position.Y);
                    const auto amount = std::clamp((valueX * segmentX + valueY * segmentY) / lengthSquared, 0.0, 1.0);
                    const Vector2 projected{
                        static_cast<float>(static_cast<double>(sorted[first]->Position.X) + segmentX * amount),
                        static_cast<float>(static_cast<double>(sorted[first]->Position.Y) + segmentY * amount)};
                    const auto score = std::pair{SquaredDistance(value, projected), lengthSquared};
                    if (!foundSegment || score < bestSegment.Score)
                    {
                        bestSegment = {sorted[first], sorted[second], amount, score};
                        foundSegment = true;
                    }
                }
            }

            if (!foundSegment)
                return {{sorted.front(), 1.0F}};
            const auto firstWeight = static_cast<float>(1.0 - bestSegment.Amount);
            const auto secondWeight = static_cast<float>(bestSegment.Amount);
            if (firstWeight > MinimumWeight)
                result.emplace_back(bestSegment.First, firstWeight);
            if (secondWeight > MinimumWeight)
                result.emplace_back(bestSegment.Second, secondWeight);
            return result;
        }

        [[nodiscard]] std::vector<WeightedClip>
        ResolveMotion(const AnimationStateDefinition& state,
                      const std::function<float(std::string_view)>& floatParameter,
                      const AnimatorInstance::ClipResolver& resolver)
        {
            std::vector<std::pair<const AnimationBlendTreeChild*, float>> weightedChildren;
            if (state.Motion.Type == AnimationMotionType::BlendTree1D)
            {
                std::vector<const AnimationBlendTreeChild*> sorted;
                sorted.reserve(state.Motion.Children.size());
                for (const auto& child : state.Motion.Children)
                    sorted.push_back(std::addressof(child));
                std::ranges::sort(
                    sorted, [](const auto* left, const auto* right)
                    { return std::tie(left->Threshold, left->Id) < std::tie(right->Threshold, right->Id); });
                const auto value = floatParameter(state.Motion.ParameterX);
                if (value <= sorted.front()->Threshold)
                    weightedChildren.emplace_back(sorted.front(), 1.0F);
                else if (value >= sorted.back()->Threshold)
                    weightedChildren.emplace_back(sorted.back(), 1.0F);
                else
                {
                    const auto upper = std::ranges::upper_bound(
                        sorted, value, {}, [](const AnimationBlendTreeChild* child) { return child->Threshold; });
                    const auto* second = *upper;
                    const auto* first = *(upper - 1);
                    const auto alpha = (value - first->Threshold) / (second->Threshold - first->Threshold);
                    weightedChildren.emplace_back(first, 1.0F - alpha);
                    weightedChildren.emplace_back(second, alpha);
                }
            }
            else if (state.Motion.Type == AnimationMotionType::BlendTree2D)
            {
                const Vector2 value{floatParameter(state.Motion.ParameterX), floatParameter(state.Motion.ParameterY)};
                weightedChildren = ResolveBlendTree2DWeights(state.Motion.Children, value);
            }

            std::vector<WeightedClip> result;
            if (state.Motion.Type == AnimationMotionType::Clip)
            {
                auto clip = resolver(state.Motion.Clip);
                if (!clip)
                    throw std::runtime_error("Animator could not resolve an animation clip.");
                result.push_back({"clip", state.Motion.Clip, std::move(clip), 1.0F, 1.0F});
                return result;
            }
            for (const auto& [child, weight] : weightedChildren)
            {
                auto clip = resolver(child->Clip);
                if (!clip)
                    throw std::runtime_error("Animator could not resolve a blend-tree clip.");
                result.push_back({child->Id, child->Clip, std::move(clip), weight, child->Speed});
            }
            return result;
        }

        [[nodiscard]] float MotionDuration(const std::span<const WeightedClip> clips) noexcept
        {
            float result = 0.0F;
            for (const auto& clip : clips)
                result += clip.Weight * clip.Clip->Duration() / std::abs(clip.Speed);
            return result;
        }

        [[nodiscard]] float AdvanceStateTime(float time, const float delta, const float duration, const bool loop,
                                             bool& wrapped) noexcept
        {
            time += delta;
            wrapped = false;
            if (!loop)
                return std::clamp(time, 0.0F, duration);
            if (time >= duration || time < 0.0F)
            {
                time = std::fmod(time, duration);
                if (time < 0.0F)
                    time += duration;
                wrapped = true;
            }
            return time;
        }

        [[nodiscard]] std::vector<BoneTransform> SampleClipPose(const AnimationClipAsset& clip, const float time,
                                                                const std::span<const SkeletonBone> bones)
        {
            std::vector<BoneTransform> result;
            result.reserve(bones.size());
            for (const auto& bone : bones)
                result.push_back(bone.BindPose);
            for (const auto& track : clip.Tracks())
            {
                if (track.Bone >= result.size())
                    throw std::runtime_error("Animation clip track exceeds the animator skeleton.");
                result[track.Bone] = SampleTrack(track, time);
            }
            return result;
        }

        [[nodiscard]] MotionEvaluation EvaluateMotion(const std::span<const WeightedClip> clips, const float time,
                                                      const float previousTime, const bool wrapped,
                                                      const std::span<const SkeletonBone> bones)
        {
            MotionEvaluation result;
            result.Duration = MotionDuration(clips);
            result.Pose.resize(bones.size());
            std::vector<Vector4> rotations(bones.size());
            std::vector<Quaternion> rotationReferences(bones.size());
            std::vector<bool> hasRotationReference(bones.size());
            for (std::size_t bone = 0; bone < bones.size(); ++bone)
            {
                result.Pose[bone].Translation = {};
                result.Pose[bone].Scale = {};
            }
            const auto normalized = result.Duration > 0.0F ? time / result.Duration : 0.0F;
            const auto previousNormalized = result.Duration > 0.0F ? previousTime / result.Duration : 0.0F;
            for (const auto& weighted : clips)
            {
                result.RootMotion = result.RootMotion || (weighted.Weight > 0.0F && weighted.Clip->RootMotion());
                const auto clipTime =
                    std::clamp(normalized * weighted.Clip->Duration(), 0.0F, weighted.Clip->Duration());
                const auto previousClipTime =
                    std::clamp(previousNormalized * weighted.Clip->Duration(), 0.0F, weighted.Clip->Duration());
                const auto pose = SampleClipPose(*weighted.Clip, clipTime, bones);
                for (std::size_t bone = 0; bone < pose.size(); ++bone)
                {
                    result.Pose[bone].Translation.X += pose[bone].Translation.X * weighted.Weight;
                    result.Pose[bone].Translation.Y += pose[bone].Translation.Y * weighted.Weight;
                    result.Pose[bone].Translation.Z += pose[bone].Translation.Z * weighted.Weight;
                    result.Pose[bone].Scale.X += pose[bone].Scale.X * weighted.Weight;
                    result.Pose[bone].Scale.Y += pose[bone].Scale.Y * weighted.Weight;
                    result.Pose[bone].Scale.Z += pose[bone].Scale.Z * weighted.Weight;
                    auto rotation = pose[bone].Rotation;
                    if (weighted.Weight > 0.0F)
                    {
                        if (!hasRotationReference[bone])
                        {
                            rotationReferences[bone] = rotation;
                            hasRotationReference[bone] = true;
                        }
                        else
                        {
                            const auto& reference = rotationReferences[bone];
                            const auto dot = reference.X * rotation.X + reference.Y * rotation.Y +
                                             reference.Z * rotation.Z + reference.W * rotation.W;
                            if (dot < 0.0F)
                                rotation = {-rotation.X, -rotation.Y, -rotation.Z, -rotation.W};
                        }
                    }
                    rotations[bone].X += rotation.X * weighted.Weight;
                    rotations[bone].Y += rotation.Y * weighted.Weight;
                    rotations[bone].Z += rotation.Z * weighted.Weight;
                    rotations[bone].W += rotation.W * weighted.Weight;
                }
                result.Weights.push_back({weighted.Id, weighted.Asset, weighted.Weight});
                if (weighted.Weight > 0.0F)
                {
                    for (const auto& event : weighted.Clip->Events())
                    {
                        if ((!wrapped && event.Time > previousClipTime && event.Time <= clipTime) ||
                            (wrapped && (event.Time > previousClipTime || event.Time <= clipTime)))
                        {
                            if (result.Events.size() < 64)
                                result.Events.push_back(event);
                        }
                    }
                }
            }
            for (std::size_t bone = 0; bone < result.Pose.size(); ++bone)
                result.Pose[bone].Rotation =
                    Math::Normalize({rotations[bone].X, rotations[bone].Y, rotations[bone].Z, rotations[bone].W});
            return result;
        }

        [[nodiscard]] bool CompareInteger(const std::int32_t current,
                                          const AnimationTransitionCondition& condition) noexcept
        {
            switch (condition.Comparison)
            {
            case AnimationConditionComparison::Greater:
                return current > condition.IntegerValue;
            case AnimationConditionComparison::Less:
                return current < condition.IntegerValue;
            case AnimationConditionComparison::Equal:
                return current == condition.IntegerValue;
            case AnimationConditionComparison::NotEqual:
                return current != condition.IntegerValue;
            }
            return false;
        }

        [[nodiscard]] bool CompareBoolean(const bool current, const AnimationTransitionCondition& condition) noexcept
        {
            if (condition.Comparison == AnimationConditionComparison::Equal)
                return current == condition.BooleanValue;
            if (condition.Comparison == AnimationConditionComparison::NotEqual)
                return current != condition.BooleanValue;
            return false;
        }
    } // namespace

    AnimatorInstance::AnimatorInstance(Ref<const SkeletonAsset> skeleton, Ref<const AnimationGraphAsset> graph,
                                       ClipResolver resolver, AvatarMaskResolver maskResolver)
        : m_Skeleton(std::move(skeleton)), m_Graph(std::move(graph)), m_Resolver(std::move(resolver)),
          m_MaskResolver(std::move(maskResolver))
    {
        if (!m_Skeleton || !m_Graph || !m_Resolver)
            throw std::invalid_argument("Animator requires a skeleton, graph, and clip resolver.");
        ValidateAnimationGraph(m_Graph->Definition());
        Reset();
    }

    void AnimatorInstance::SetFloat(const std::string& parameter, const float value)
    {
        const auto found = m_Parameters.find(parameter);
        if (!std::isfinite(value) || found == m_Parameters.end() || found->second.Type != AnimationParameterType::Float)
            throw std::invalid_argument("Animator float parameter is unavailable or non-finite.");
        found->second.FloatValue = value;
        PublishDebugSnapshot();
    }

    float AnimatorInstance::Float(const std::string_view parameter) const
    {
        const auto found = m_Parameters.find(parameter);
        if (found == m_Parameters.end() || found->second.Type != AnimationParameterType::Float)
            throw std::invalid_argument("Animator float parameter is unavailable.");
        return found->second.FloatValue;
    }

    void AnimatorInstance::SetInteger(const std::string& parameter, const std::int32_t value)
    {
        const auto found = m_Parameters.find(parameter);
        if (found == m_Parameters.end() || found->second.Type != AnimationParameterType::Integer)
            throw std::invalid_argument("Animator integer parameter is unavailable.");
        found->second.IntegerValue = value;
        PublishDebugSnapshot();
    }

    std::int32_t AnimatorInstance::Integer(const std::string_view parameter) const
    {
        const auto found = m_Parameters.find(parameter);
        if (found == m_Parameters.end() || found->second.Type != AnimationParameterType::Integer)
            throw std::invalid_argument("Animator integer parameter is unavailable.");
        return found->second.IntegerValue;
    }

    void AnimatorInstance::SetBool(const std::string& parameter, const bool value)
    {
        const auto found = m_Parameters.find(parameter);
        if (found == m_Parameters.end() || found->second.Type != AnimationParameterType::Boolean)
            throw std::invalid_argument("Animator boolean parameter is unavailable.");
        found->second.BooleanValue = value;
        PublishDebugSnapshot();
    }

    bool AnimatorInstance::Bool(const std::string_view parameter) const
    {
        const auto found = m_Parameters.find(parameter);
        if (found == m_Parameters.end() || found->second.Type != AnimationParameterType::Boolean)
            throw std::invalid_argument("Animator boolean parameter is unavailable.");
        return found->second.BooleanValue;
    }

    void AnimatorInstance::SetTrigger(const std::string& parameter)
    {
        const auto found = m_Parameters.find(parameter);
        if (found == m_Parameters.end() || found->second.Type != AnimationParameterType::Trigger)
            throw std::invalid_argument("Animator trigger parameter is unavailable.");
        found->second.BooleanValue = true;
        PublishDebugSnapshot();
    }

    void AnimatorInstance::ResetTrigger(const std::string& parameter)
    {
        const auto found = m_Parameters.find(parameter);
        if (found == m_Parameters.end() || found->second.Type != AnimationParameterType::Trigger)
            throw std::invalid_argument("Animator trigger parameter is unavailable.");
        found->second.BooleanValue = false;
        PublishDebugSnapshot();
    }

    bool AnimatorInstance::Trigger(const std::string_view parameter) const
    {
        const auto found = m_Parameters.find(parameter);
        if (found == m_Parameters.end() || found->second.Type != AnimationParameterType::Trigger)
            throw std::invalid_argument("Animator trigger parameter is unavailable.");
        return found->second.BooleanValue;
    }

    void AnimatorInstance::SetLayerWeight(std::string layer, const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 1.0F)
            throw std::invalid_argument("Animator layer weight is invalid.");
        const auto& definition = m_Graph->Definition();
        const auto layerDefinition = std::ranges::find_if(definition.Layers, [&](const auto& item)
                                                          { return item.Id == layer || item.Name == layer; });
        if (layerDefinition == definition.Layers.end())
            throw std::invalid_argument("Animator layer is unavailable.");
        const auto runtime = std::ranges::find(m_Layers, layerDefinition->Id, &RuntimeLayer::Id);
        runtime->Weight = value;
        PublishDebugSnapshot();
    }

    float AnimatorInstance::LayerWeight(const std::string_view layer) const
    {
        const auto& definition = m_Graph->Definition();
        const auto layerDefinition = std::ranges::find_if(definition.Layers, [&](const auto& item)
                                                          { return item.Id == layer || item.Name == layer; });
        if (layerDefinition == definition.Layers.end())
            throw std::invalid_argument("Animator layer is unavailable.");
        return std::ranges::find(m_Layers, layerDefinition->Id, &RuntimeLayer::Id)->Weight;
    }

    void AnimatorInstance::Play(const std::string_view state, const std::string_view layer, const float normalizedTime)
    {
        if (state.empty() || !std::isfinite(normalizedTime) || normalizedTime < 0.0F || normalizedTime > 1.0F)
            throw std::invalid_argument("Animator play request is invalid.");
        const auto& definition = m_Graph->Definition();
        const auto* layerDefinition =
            layer.empty() ? (definition.Layers.empty() ? nullptr : std::addressof(definition.Layers.front()))
                          : [&]() -> const AnimationLayerDefinition*
        {
            const auto found = std::ranges::find_if(definition.Layers, [&](const auto& candidate)
                                                    { return candidate.Id == layer || candidate.Name == layer; });
            return found == definition.Layers.end() ? nullptr : std::addressof(*found);
        }();
        if (!layerDefinition)
            throw std::invalid_argument("Animator layer is unavailable.");
        const auto stateDefinition = std::ranges::find_if(layerDefinition->States, [&](const auto& candidate)
                                                          { return candidate.Id == state || candidate.Name == state; });
        if (stateDefinition == layerDefinition->States.end())
            throw std::invalid_argument("Animator state is unavailable.");
        const auto runtime = std::ranges::find(m_Layers, layerDefinition->Id, &RuntimeLayer::Id);
        if (runtime == m_Layers.end())
            throw std::logic_error("Animator runtime layer is unavailable.");
        const auto floatParameter = [&](const std::string_view id)
        {
            const auto* parameter = FindParameterById(definition, id);
            if (!parameter || parameter->Type != AnimationParameterType::Float)
                throw std::logic_error("Animation blend tree references an unavailable float parameter.");
            return m_Parameters.at(parameter->Name).FloatValue;
        };
        const auto clips = ResolveMotion(*stateDefinition, floatParameter, m_Resolver);
        runtime->StateId = stateDefinition->Id;
        runtime->Time = MotionDuration(clips) * normalizedTime;
        runtime->NormalizedTime = normalizedTime;
        runtime->BlendWeights.clear();
        runtime->Transition.reset();
        m_Playing = true;
        m_HasPreviousRootRotation = false;
        PublishDebugSnapshot();
    }

    void AnimatorInstance::CrossFade(const std::string_view state, const float duration, const std::string_view layer,
                                     const float normalizedTime)
    {
        if (!std::isfinite(duration) || duration < 0.0F || duration > 60.0F)
            throw std::invalid_argument("Animator cross-fade duration is invalid.");
        const auto& definition = m_Graph->Definition();
        const auto* layerDefinition =
            layer.empty() ? (definition.Layers.empty() ? nullptr : std::addressof(definition.Layers.front()))
                          : [&]() -> const AnimationLayerDefinition*
        {
            const auto found = std::ranges::find_if(definition.Layers, [&](const auto& candidate)
                                                    { return candidate.Id == layer || candidate.Name == layer; });
            return found == definition.Layers.end() ? nullptr : std::addressof(*found);
        }();
        if (!layerDefinition)
            throw std::invalid_argument("Animator layer is unavailable.");
        const auto destination = std::ranges::find_if(layerDefinition->States, [&](const auto& candidate)
                                                      { return candidate.Id == state || candidate.Name == state; });
        if (destination == layerDefinition->States.end())
            throw std::invalid_argument("Animator state is unavailable.");
        const auto runtime = std::ranges::find(m_Layers, layerDefinition->Id, &RuntimeLayer::Id);
        if (runtime == m_Layers.end())
            throw std::logic_error("Animator runtime layer is unavailable.");
        if (!m_Playing || duration == 0.0F)
        {
            Play(state, layer, normalizedTime);
            return;
        }
        const auto floatParameter = [&](const std::string_view id)
        {
            const auto* parameter = FindParameterById(definition, id);
            if (!parameter || parameter->Type != AnimationParameterType::Float)
                throw std::logic_error("Animation blend tree references an unavailable float parameter.");
            return m_Parameters.at(parameter->Name).FloatValue;
        };
        const auto destinationClips = ResolveMotion(*destination, floatParameter, m_Resolver);
        const auto destinationTime = MotionDuration(destinationClips) * normalizedTime;
        runtime->Transition =
            RuntimeTransition{runtime->StateId, destination->Id, runtime->Time, destinationTime, 0.0F, duration, {}};
        runtime->StateId = destination->Id;
        m_Playing = true;
        PublishDebugSnapshot();
    }

    void AnimatorInstance::Stop()
    {
        m_Playing = false;
        m_State.clear();
        m_Time = 0.0F;
        m_HasPreviousRootRotation = false;
        for (auto& layer : m_Layers)
            layer.Transition.reset();
        PublishDebugSnapshot();
    }

    AnimatorSample AnimatorInstance::Update(const float deltaSeconds)
    {
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F || deltaSeconds > 10.0F)
            throw std::invalid_argument("Animator delta time is invalid.");
        const auto evaluationStart = std::chrono::steady_clock::now();
        std::uint32_t layersEvaluated = 0;
        std::uint32_t transitionsTested = 0;
        std::uint32_t motionsEvaluated = 0;
        std::uint32_t clipsSampled = 0;
        const auto finishProfile = [&]
        {
            const auto elapsed =
                std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - evaluationStart).count();
            const auto previousCount = m_Profile.UpdateCount;
            m_Profile.LastEvaluationMicroseconds = elapsed;
            m_Profile.AverageEvaluationMicroseconds =
                (m_Profile.AverageEvaluationMicroseconds * static_cast<double>(previousCount) + elapsed) /
                static_cast<double>(previousCount + 1U);
            m_Profile.PeakEvaluationMicroseconds = std::max(m_Profile.PeakEvaluationMicroseconds, elapsed);
            ++m_Profile.UpdateCount;
            m_Profile.LayersEvaluated = layersEvaluated;
            m_Profile.TransitionsTested = transitionsTested;
            m_Profile.MotionsEvaluated = motionsEvaluated;
            m_Profile.ClipsSampled = clipsSampled;
        };
        const auto& definition = m_Graph->Definition();
        const auto floatParameter = [&](const std::string_view id)
        {
            const auto* parameter = FindParameterById(definition, id);
            if (!parameter || parameter->Type != AnimationParameterType::Float)
                throw std::logic_error("Animation blend tree references an unavailable float parameter.");
            return m_Parameters.at(parameter->Name).FloatValue;
        };
        const auto conditionMatches = [&](const AnimationTransitionCondition& condition)
        {
            const auto* parameter = FindParameterById(definition, condition.ParameterId);
            if (!parameter)
                throw std::logic_error("Animation transition references an unavailable parameter.");
            const auto& value = m_Parameters.at(parameter->Name);
            switch (parameter->Type)
            {
            case AnimationParameterType::Float:
                return Compare(value.FloatValue, condition);
            case AnimationParameterType::Integer:
                return CompareInteger(value.IntegerValue, condition);
            case AnimationParameterType::Boolean:
            case AnimationParameterType::Trigger:
                return CompareBoolean(value.BooleanValue, condition);
            }
            return false;
        };

        AnimatorSample result;
        result.LocalPose.reserve(m_Skeleton->Bones().size());
        for (const auto& bone : m_Skeleton->Bones())
            result.LocalPose.push_back(bone.BindPose);
        if (m_Layers.empty() || !m_Playing)
        {
            m_State.clear();
            m_Time = 0.0F;
            m_HasPreviousRootRotation = false;
            finishProfile();
            m_DebugPose = BuildPoseDebugState(*m_Skeleton, result.LocalPose);
            PublishDebugSnapshot();
            return result;
        }
        bool baseWrapped = false;
        bool baseRootMotion = false;
        std::vector<AnimationEvent> events;

        for (std::size_t layerIndex = 0; layerIndex < m_Layers.size(); ++layerIndex)
        {
            ++layersEvaluated;
            auto& runtime = m_Layers[layerIndex];
            const auto* layer = FindLayer(definition, runtime.Id);
            if (!layer)
                throw std::logic_error("Animator runtime layer is unavailable.");
            auto* state = FindState(*layer, runtime.StateId);
            if (!state)
                throw std::logic_error("Animator runtime state is unavailable.");

            if (!runtime.Transition)
            {
                for (const auto& transition : state->Transitions)
                {
                    ++transitionsTested;
                    if (transition.HasExitTime && runtime.NormalizedTime < transition.ExitTime)
                        continue;
                    if (!std::ranges::all_of(transition.Conditions, conditionMatches))
                        continue;
                    runtime.Transition = RuntimeTransition{state->Id, transition.DestinationId, runtime.Time, 0.0F,
                                                           0.0F,      transition.Duration,      transition.Id};
                    runtime.StateId = transition.DestinationId;
                    for (const auto& condition : transition.Conditions)
                    {
                        const auto* parameter = FindParameterById(definition, condition.ParameterId);
                        if (parameter && parameter->Type == AnimationParameterType::Trigger)
                            m_Parameters.at(parameter->Name).BooleanValue = false;
                    }
                    break;
                }
            }

            MotionEvaluation evaluated;
            if (runtime.Transition)
            {
                auto& transition = *runtime.Transition;
                const auto* source = FindState(*layer, transition.SourceStateId);
                const auto* destination = FindState(*layer, transition.DestinationStateId);
                if (!source || !destination)
                    throw std::logic_error("Animator transition state is unavailable.");
                const auto sourceClips = ResolveMotion(*source, floatParameter, m_Resolver);
                const auto destinationClips = ResolveMotion(*destination, floatParameter, m_Resolver);
                motionsEvaluated += 2;
                clipsSampled += static_cast<std::uint32_t>(sourceClips.size() + destinationClips.size());
                bool sourceWrapped = false;
                bool destinationWrapped = false;
                const auto previousSourceTime = transition.SourceTime;
                const auto previousDestinationTime = transition.DestinationTime;
                transition.SourceTime = AdvanceStateTime(transition.SourceTime, deltaSeconds * source->Speed,
                                                         MotionDuration(sourceClips), source->Loop, sourceWrapped);
                transition.DestinationTime =
                    AdvanceStateTime(transition.DestinationTime, deltaSeconds * destination->Speed,
                                     MotionDuration(destinationClips), destination->Loop, destinationWrapped);
                transition.Elapsed += deltaSeconds;
                const auto sourceEvaluation = EvaluateMotion(sourceClips, transition.SourceTime, previousSourceTime,
                                                             sourceWrapped, m_Skeleton->Bones());
                evaluated = EvaluateMotion(destinationClips, transition.DestinationTime, previousDestinationTime,
                                           destinationWrapped, m_Skeleton->Bones());
                const auto alpha = transition.Duration <= 0.0F
                                       ? 1.0F
                                       : std::clamp(transition.Elapsed / transition.Duration, 0.0F, 1.0F);
                for (std::size_t bone = 0; bone < evaluated.Pose.size(); ++bone)
                    evaluated.Pose[bone] = Blend(sourceEvaluation.Pose[bone], evaluated.Pose[bone], alpha);
                evaluated.RootMotion = evaluated.RootMotion || sourceEvaluation.RootMotion;
                runtime.Time = transition.DestinationTime;
                runtime.NormalizedTime = evaluated.Duration > 0.0F ? runtime.Time / evaluated.Duration : 0.0F;
                runtime.BlendWeights = evaluated.Weights;
                if (layerIndex == 0)
                {
                    baseWrapped = destinationWrapped;
                    baseRootMotion = evaluated.RootMotion;
                }
                if (alpha >= 1.0F)
                    runtime.Transition.reset();
            }
            else
            {
                const auto clips = ResolveMotion(*state, floatParameter, m_Resolver);
                ++motionsEvaluated;
                clipsSampled += static_cast<std::uint32_t>(clips.size());
                const auto duration = MotionDuration(clips);
                const auto previousTime = runtime.Time;
                bool wrapped = false;
                runtime.Time =
                    AdvanceStateTime(runtime.Time, deltaSeconds * state->Speed, duration, state->Loop, wrapped);
                evaluated = EvaluateMotion(clips, runtime.Time, previousTime, wrapped, m_Skeleton->Bones());
                runtime.NormalizedTime = duration > 0.0F ? runtime.Time / duration : 0.0F;
                runtime.BlendWeights = evaluated.Weights;
                if (layerIndex == 0)
                {
                    baseWrapped = wrapped;
                    baseRootMotion = evaluated.RootMotion;
                }
            }

            float layerWeight = runtime.Weight;
            Ref<const AvatarMaskAsset> mask;
            if (layer->AvatarMask)
            {
                if (!m_MaskResolver)
                    throw std::runtime_error("Animator could not resolve an avatar mask.");
                mask = m_MaskResolver(layer->AvatarMask);
                if (!mask)
                    throw std::runtime_error("Animator could not resolve an avatar mask.");
            }
            for (std::size_t bone = 0; bone < result.LocalPose.size(); ++bone)
            {
                const auto weight = layerWeight * (mask ? mask->Weight(m_Skeleton->Bones()[bone].Name) : 1.0F);
                if (layerIndex == 0 || layer->Mode == AnimationLayerMode::Override)
                {
                    result.LocalPose[bone] = Blend(result.LocalPose[bone], evaluated.Pose[bone], weight);
                }
                else
                {
                    const auto& bind = m_Skeleton->Bones()[bone].BindPose;
                    auto& target = result.LocalPose[bone];
                    target.Translation.X += (evaluated.Pose[bone].Translation.X - bind.Translation.X) * weight;
                    target.Translation.Y += (evaluated.Pose[bone].Translation.Y - bind.Translation.Y) * weight;
                    target.Translation.Z += (evaluated.Pose[bone].Translation.Z - bind.Translation.Z) * weight;
                    target.Scale.X += (evaluated.Pose[bone].Scale.X - bind.Scale.X) * weight;
                    target.Scale.Y += (evaluated.Pose[bone].Scale.Y - bind.Scale.Y) * weight;
                    target.Scale.Z += (evaluated.Pose[bone].Scale.Z - bind.Scale.Z) * weight;
                    target.Rotation = Math::Normalize(
                        {target.Rotation.X + (evaluated.Pose[bone].Rotation.X - bind.Rotation.X) * weight,
                         target.Rotation.Y + (evaluated.Pose[bone].Rotation.Y - bind.Rotation.Y) * weight,
                         target.Rotation.Z + (evaluated.Pose[bone].Rotation.Z - bind.Rotation.Z) * weight,
                         target.Rotation.W + (evaluated.Pose[bone].Rotation.W - bind.Rotation.W) * weight});
                }
            }
            events.insert(events.end(), evaluated.Events.begin(), evaluated.Events.end());
            if (events.size() > 64)
                events.resize(64);
        }

        const auto& baseRuntime = m_Layers.front();
        const auto& baseLayer = definition.Layers.front();
        const auto* baseState = FindState(baseLayer, baseRuntime.StateId);
        result.State = baseState ? baseState->Name : std::string{};
        result.NormalizedTime = baseRuntime.NormalizedTime;
        result.Events = events;
        m_State = result.State;
        m_Time = baseRuntime.Time;
        if (baseRootMotion && !result.LocalPose.empty())
        {
            const auto current = result.LocalPose.front();
            if (!baseWrapped)
            {
                result.RootMotion = {current.Translation.X - m_PreviousRoot.Translation.X,
                                     current.Translation.Y - m_PreviousRoot.Translation.Y,
                                     current.Translation.Z - m_PreviousRoot.Translation.Z};
                if (m_HasPreviousRootRotation)
                    result.RootRotation = RotationDelta(m_PreviousRoot.Rotation, current.Rotation);
            }
            m_PreviousRoot = current;
            m_HasPreviousRootRotation = true;
            result.LocalPose.front().Translation = {};
            result.LocalPose.front().Rotation = {};
        }
        else
        {
            m_HasPreviousRootRotation = false;
        }
        finishProfile();
        m_DebugPose = BuildPoseDebugState(*m_Skeleton, result.LocalPose);
        m_DebugTrajectoryTime += deltaSeconds;
        m_DebugTrajectoryPosition.X += result.RootMotion.X;
        m_DebugTrajectoryPosition.Y += result.RootMotion.Y;
        m_DebugTrajectoryPosition.Z += result.RootMotion.Z;
        if (deltaSeconds > 0.0F || m_DebugMotionTrajectory.empty())
        {
            m_DebugMotionTrajectory.push_back({m_DebugTrajectoryTime, m_DebugTrajectoryPosition});
            if (m_DebugMotionTrajectory.size() > 240)
                m_DebugMotionTrajectory.erase(m_DebugMotionTrajectory.begin());
        }
        PublishDebugSnapshot(result.RootMotion, result.RootRotation, result.Events);
        return result;
    }

    AnimatorCheckpoint AnimatorInstance::CaptureCheckpoint() const
    {
        AnimatorCheckpoint result;
        result.Playing = m_Playing;
        result.PreviousRoot = m_PreviousRoot;
        result.HasPreviousRootRotation = m_HasPreviousRootRotation;
        const auto& definition = m_Graph->Definition();
        result.Parameters.reserve(definition.ParameterDefinitions.size());
        for (const auto& parameter : definition.ParameterDefinitions)
        {
            const auto& runtime = m_Parameters.at(parameter.Name);
            result.Parameters.push_back(
                {runtime.Id, runtime.Type, runtime.FloatValue, runtime.IntegerValue, runtime.BooleanValue});
        }
        result.Layers.reserve(m_Layers.size());
        for (const auto& layer : m_Layers)
        {
            AnimatorCheckpointLayer captured{layer.Id, layer.StateId, layer.Time, layer.Weight, layer.NormalizedTime};
            if (layer.Transition)
            {
                const auto& transition = *layer.Transition;
                captured.Transition = AnimatorCheckpointTransition{
                    transition.Id,         transition.SourceStateId,   transition.DestinationStateId,
                    transition.SourceTime, transition.DestinationTime, transition.Elapsed,
                    transition.Duration};
            }
            result.Layers.push_back(std::move(captured));
        }
        return result;
    }

    void AnimatorInstance::RestoreCheckpoint(const AnimatorCheckpoint& checkpoint)
    {
        const auto& definition = m_Graph->Definition();
        if (checkpoint.Parameters.size() != definition.ParameterDefinitions.size() ||
            checkpoint.Layers.size() != definition.Layers.size() ||
            !Math::IsFinite(checkpoint.PreviousRoot.Translation) || !Math::IsFinite(checkpoint.PreviousRoot.Rotation) ||
            !Math::IsFinite(checkpoint.PreviousRoot.Scale))
        {
            throw std::invalid_argument("Animator checkpoint is incompatible or contains non-finite state.");
        }

        std::map<std::string, RuntimeParameter, std::less<>> parameters;
        std::set<std::string, std::less<>> parameterIds;
        for (const auto& captured : checkpoint.Parameters)
        {
            const auto* parameter = FindParameterById(definition, captured.Id);
            if (!parameter || parameter->Type != captured.Type || !parameterIds.insert(captured.Id).second ||
                !std::isfinite(captured.FloatValue))
            {
                throw std::invalid_argument("Animator checkpoint parameter state is incompatible.");
            }
            parameters.emplace(parameter->Name, RuntimeParameter{captured.Type, captured.Id, captured.FloatValue,
                                                                 captured.IntegerValue, captured.BooleanValue});
        }

        std::vector<RuntimeLayer> layers;
        layers.reserve(checkpoint.Layers.size());
        std::set<std::string, std::less<>> layerIds;
        for (const auto& captured : checkpoint.Layers)
        {
            const auto* layer = FindLayer(definition, captured.Id);
            if (!layer || !FindState(*layer, captured.StateId) || !layerIds.insert(captured.Id).second ||
                !std::isfinite(captured.Time) || captured.Time < 0.0F || !std::isfinite(captured.Weight) ||
                captured.Weight < 0.0F || captured.Weight > 1.0F || !std::isfinite(captured.NormalizedTime) ||
                captured.NormalizedTime < 0.0F || captured.NormalizedTime > 1.0F)
            {
                throw std::invalid_argument("Animator checkpoint layer state is incompatible.");
            }
            RuntimeLayer runtime{captured.Id, captured.StateId, captured.Time, captured.Weight,
                                 captured.NormalizedTime};
            if (captured.Transition)
            {
                const auto& transition = *captured.Transition;
                if (!FindState(*layer, transition.SourceStateId) || !FindState(*layer, transition.DestinationStateId) ||
                    !std::isfinite(transition.SourceTime) || transition.SourceTime < 0.0F ||
                    !std::isfinite(transition.DestinationTime) || transition.DestinationTime < 0.0F ||
                    !std::isfinite(transition.Elapsed) || transition.Elapsed < 0.0F ||
                    !std::isfinite(transition.Duration) || transition.Duration < 0.0F)
                {
                    throw std::invalid_argument("Animator checkpoint transition state is incompatible.");
                }
                runtime.Transition = RuntimeTransition{transition.SourceStateId,
                                                       transition.DestinationStateId,
                                                       transition.SourceTime,
                                                       transition.DestinationTime,
                                                       transition.Elapsed,
                                                       transition.Duration,
                                                       transition.Id};
            }
            layers.push_back(std::move(runtime));
        }

        m_Parameters = std::move(parameters);
        m_Layers = std::move(layers);
        m_Playing = checkpoint.Playing;
        m_PreviousRoot = checkpoint.PreviousRoot;
        m_PreviousRoot.Rotation = Math::Normalize(m_PreviousRoot.Rotation);
        m_HasPreviousRootRotation = checkpoint.HasPreviousRootRotation;
        if (m_Layers.empty())
        {
            m_State.clear();
            m_Time = 0.0F;
        }
        else
        {
            const auto* layer = FindLayer(definition, m_Layers.front().Id);
            const auto* state = layer ? FindState(*layer, m_Layers.front().StateId) : nullptr;
            m_State = state ? state->Name : std::string{};
            m_Time = m_Layers.front().Time;
        }
        PublishDebugSnapshot();
    }

    bool AnimatorInstance::Reload(Ref<const AnimationGraphAsset> graph)
    {
        if (!graph || graph->Definition().Layers.empty())
            return false;
        try
        {
            ValidateAnimationGraph(graph->Definition());
        }
        catch (const std::invalid_argument&)
        {
            return false;
        }

        const auto& replacement = graph->Definition();
        bool compatible = true;
        for (const auto& [name, parameter] : m_Parameters)
        {
            (void)name;
            const auto* candidate = FindParameterById(replacement, parameter.Id);
            if (!candidate || candidate->Type != parameter.Type)
            {
                compatible = false;
                break;
            }
        }
        if (compatible)
        {
            for (const auto& runtime : m_Layers)
            {
                const auto* layer = FindLayer(replacement, runtime.Id);
                if (!layer || !FindState(*layer, runtime.StateId))
                {
                    compatible = false;
                    break;
                }
                if (runtime.Transition)
                {
                    const auto* source = FindState(*layer, runtime.Transition->SourceStateId);
                    const auto* destination = FindState(*layer, runtime.Transition->DestinationStateId);
                    if (!source || !destination)
                    {
                        compatible = false;
                        break;
                    }
                    const auto transition =
                        std::ranges::find(source->Transitions, runtime.Transition->Id, &AnimationTransition::Id);
                    if (transition == source->Transitions.end() ||
                        transition->DestinationId != runtime.Transition->DestinationStateId)
                    {
                        compatible = false;
                        break;
                    }
                }
            }
        }

        if (!compatible)
        {
            m_Graph = std::move(graph);
            Reset();
            return false;
        }

        std::map<std::string, RuntimeParameter, std::less<>> parametersById;
        for (const auto& [name, parameter] : m_Parameters)
        {
            (void)name;
            parametersById.emplace(parameter.Id, parameter);
        }
        const auto previousLayers = std::move(m_Layers);
        m_Graph = std::move(graph);
        m_Parameters.clear();
        for (const auto& parameter : replacement.ParameterDefinitions)
        {
            const auto previous = parametersById.find(parameter.Id);
            if (previous != parametersById.end())
            {
                m_Parameters.emplace(parameter.Name, previous->second);
            }
            else
            {
                m_Parameters.emplace(parameter.Name,
                                     RuntimeParameter{parameter.Type, parameter.Id, parameter.FloatDefault,
                                                      parameter.IntegerDefault, parameter.BooleanDefault});
            }
        }
        m_Layers.clear();
        m_Layers.reserve(replacement.Layers.size());
        for (const auto& layer : replacement.Layers)
        {
            const auto previous = std::ranges::find(previousLayers, layer.Id, &RuntimeLayer::Id);
            if (previous != previousLayers.end())
            {
                auto runtime = *previous;
                runtime.BlendWeights.clear();
                m_Layers.push_back(std::move(runtime));
            }
            else
            {
                m_Layers.push_back({layer.Id, layer.EntryStateId, 0.0F, layer.DefaultWeight});
            }
        }
        const auto& base = m_Layers.front();
        const auto* baseState = FindState(replacement.Layers.front(), base.StateId);
        m_State = baseState ? baseState->Name : std::string{};
        m_Time = base.Time;
        m_HasPreviousRootRotation = false;
        PublishDebugSnapshot();
        return true;
    }

    void AnimatorInstance::PublishDebugSnapshot(const Vector3 rootMotion, const Quaternion rootRotation,
                                                const std::span<const AnimationEvent> events)
    {
        auto snapshot = std::make_shared<AnimatorDebugSnapshot>();
        snapshot->Revision = ++m_DebugRevision;
        snapshot->RootMotion = rootMotion;
        snapshot->RootRotation = rootRotation;
        m_RecentEvents.insert(m_RecentEvents.end(), events.begin(), events.end());
        if (m_RecentEvents.size() > 64)
            m_RecentEvents.erase(m_RecentEvents.begin(), m_RecentEvents.end() - 64);
        snapshot->RecentEvents = m_RecentEvents;
        snapshot->Pose = m_DebugPose;
        snapshot->MotionTrajectory = m_DebugMotionTrajectory;
        snapshot->Profile = m_Profile;
        const auto& definition = m_Graph->Definition();
        snapshot->Parameters.reserve(definition.ParameterDefinitions.size());
        for (const auto& parameter : definition.ParameterDefinitions)
        {
            const auto& value = m_Parameters.at(parameter.Name);
            snapshot->Parameters.push_back({parameter.Id, parameter.Name, parameter.Type, value.FloatValue,
                                            value.IntegerValue, value.BooleanValue});
        }
        snapshot->Layers.reserve(m_Layers.size());
        for (const auto& runtime : m_Layers)
        {
            const auto* layer = FindLayer(definition, runtime.Id);
            const auto* state = layer ? FindState(*layer, runtime.StateId) : nullptr;
            AnimatorLayerDebugState debug;
            debug.Id = runtime.Id;
            debug.Name = layer ? layer->Name : std::string{};
            debug.StateId = runtime.StateId;
            debug.State = state ? state->Name : std::string{};
            debug.NormalizedTime = runtime.NormalizedTime;
            debug.Weight = runtime.Weight;
            debug.BlendWeights = runtime.BlendWeights;
            if (runtime.Transition)
            {
                debug.InTransition = true;
                debug.SourceStateId = runtime.Transition->SourceStateId;
                debug.DestinationStateId = runtime.Transition->DestinationStateId;
                debug.TransitionProgress =
                    runtime.Transition->Duration <= 0.0F
                        ? 1.0F
                        : std::clamp(runtime.Transition->Elapsed / runtime.Transition->Duration, 0.0F, 1.0F);
            }
            snapshot->Layers.push_back(std::move(debug));
        }
        m_DebugSnapshot = std::move(snapshot);
    }

    void AnimatorInstance::Reset()
    {
        const auto& definition = m_Graph->Definition();
        m_Parameters.clear();
        for (const auto& parameter : definition.ParameterDefinitions)
            m_Parameters.emplace(parameter.Name, RuntimeParameter{parameter.Type, parameter.Id, parameter.FloatDefault,
                                                                  parameter.IntegerDefault, parameter.BooleanDefault});
        m_Layers.clear();
        m_Layers.reserve(definition.Layers.size());
        for (const auto& layer : definition.Layers)
            m_Layers.push_back({layer.Id, layer.EntryStateId, 0.0F, layer.DefaultWeight});
        if (definition.Layers.empty())
        {
            m_State.clear();
        }
        else
        {
            const auto* entry = FindState(definition.Layers.front(), definition.Layers.front().EntryStateId);
            m_State = entry ? entry->Name : std::string{};
        }
        m_Time = 0.0F;
        m_Playing = true;
        m_PreviousRoot = {};
        m_HasPreviousRootRotation = false;
        m_RecentEvents.clear();
        std::vector<BoneTransform> bindPose;
        bindPose.reserve(m_Skeleton->Bones().size());
        for (const auto& bone : m_Skeleton->Bones())
            bindPose.push_back(bone.BindPose);
        m_DebugPose = BuildPoseDebugState(*m_Skeleton, bindPose);
        m_DebugMotionTrajectory = {{0.0F, {}}};
        m_DebugTrajectoryPosition = {};
        m_DebugTrajectoryTime = 0.0F;
        m_Profile = {};
        PublishDebugSnapshot();
    }
} // namespace Keire
