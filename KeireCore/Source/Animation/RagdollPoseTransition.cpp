#include "Keire/Animation/RiggingSystem.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Keire
{
    namespace
    {
        [[nodiscard]] Quaternion BlendQuaternion(const Quaternion left, const Quaternion right,
                                                 const float amount) noexcept
        {
            const auto t = std::clamp(amount, 0.0F, 1.0F);
            const auto dot = left.X * right.X + left.Y * right.Y + left.Z * right.Z + left.W * right.W;
            const auto sign = dot < 0.0F ? -1.0F : 1.0F;
            return Math::Normalize({left.X + (right.X * sign - left.X) * t, left.Y + (right.Y * sign - left.Y) * t,
                                    left.Z + (right.Z * sign - left.Z) * t, left.W + (right.W * sign - left.W) * t});
        }
    } // namespace

    void RagdollPoseTransition::SetRagdoll(const bool enabled, const float duration)
    {
        if (!std::isfinite(duration) || duration < 0.0F || duration > 60.0F)
            throw std::invalid_argument("Ragdoll transition duration must be finite and in the range 0..60.");
        m_StartWeight = m_Weight;
        m_TargetWeight = enabled ? 1.0F : 0.0F;
        m_Duration = duration;
        m_Elapsed = 0.0F;
        if (duration == 0.0F || m_StartWeight == m_TargetWeight)
        {
            m_Weight = m_TargetWeight;
            m_Mode = enabled ? RagdollPoseMode::Ragdoll : RagdollPoseMode::Animated;
            return;
        }
        m_Mode = enabled ? RagdollPoseMode::TransitionToRagdoll : RagdollPoseMode::TransitionToAnimation;
    }

    std::vector<BoneTransform> RagdollPoseTransition::Update(const float deltaSeconds,
                                                             const std::span<const BoneTransform> animationPose,
                                                             const std::span<const BoneTransform> ragdollPose)
    {
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F || deltaSeconds > 10.0F || animationPose.empty() ||
            animationPose.size() != ragdollPose.size() || animationPose.size() > 4096)
            throw std::invalid_argument("Ragdoll pose transition sample is invalid.");
        for (std::size_t index = 0; index < animationPose.size(); ++index)
        {
            if (!Math::IsFinite(animationPose[index].Translation) || !Math::IsFinite(animationPose[index].Rotation) ||
                !Math::IsFinite(animationPose[index].Scale) || !Math::IsFinite(ragdollPose[index].Translation) ||
                !Math::IsFinite(ragdollPose[index].Rotation) || !Math::IsFinite(ragdollPose[index].Scale))
                throw std::invalid_argument("Ragdoll pose transition contains a non-finite transform.");
        }

        auto nextElapsed = m_Elapsed;
        auto nextWeight = m_Weight;
        auto nextMode = m_Mode;
        if (m_Mode == RagdollPoseMode::TransitionToRagdoll || m_Mode == RagdollPoseMode::TransitionToAnimation)
        {
            nextElapsed = std::min(m_Elapsed + deltaSeconds, m_Duration);
            const auto alpha = m_Duration > 0.0F ? nextElapsed / m_Duration : 1.0F;
            nextWeight = m_StartWeight + (m_TargetWeight - m_StartWeight) * alpha;
            if (nextElapsed >= m_Duration)
                nextMode = m_TargetWeight > 0.0F ? RagdollPoseMode::Ragdoll : RagdollPoseMode::Animated;
        }

        std::vector<BoneTransform> result;
        result.reserve(animationPose.size());
        for (std::size_t index = 0; index < animationPose.size(); ++index)
        {
            const auto& animation = animationPose[index];
            const auto& ragdoll = ragdollPose[index];
            const auto blendVector = [nextWeight](const Vector3 first, const Vector3 second)
            {
                return Vector3{first.X + (second.X - first.X) * nextWeight, first.Y + (second.Y - first.Y) * nextWeight,
                               first.Z + (second.Z - first.Z) * nextWeight};
            };
            result.push_back({blendVector(animation.Translation, ragdoll.Translation),
                              BlendQuaternion(animation.Rotation, ragdoll.Rotation, nextWeight),
                              blendVector(animation.Scale, ragdoll.Scale)});
        }
        m_Elapsed = nextElapsed;
        m_Weight = nextWeight;
        m_Mode = nextMode;
        return result;
    }

    void RagdollPoseTransition::Reset() noexcept
    {
        m_Mode = RagdollPoseMode::Animated;
        m_Weight = 0.0F;
        m_StartWeight = 0.0F;
        m_TargetWeight = 0.0F;
        m_Duration = 0.0F;
        m_Elapsed = 0.0F;
    }
} // namespace Keire
