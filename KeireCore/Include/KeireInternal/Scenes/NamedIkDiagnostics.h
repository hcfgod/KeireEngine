#pragma once

#include "Keire/Animation/AnimationSystem.h"
#include "Keire/ECS/Components/AnimatorComponent.h"
#include "Keire/ECS/Entity.h"
#include "Keire/Math/Math.h"

#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    struct NamedIkEndpointCapture final
    {
        std::string Name;
        std::string Bone;
        AnimatorIkSpace Space = AnimatorIkSpace::Model;
        Vector3 OriginalTarget;
        Vector3 ModelTarget;
        float Weight = 0;
        float Tolerance = 0;
        std::optional<std::uint32_t> Index;
    };

    [[nodiscard]] inline NamedIkEndpointCapture
    CaptureNamedIkEndpoint(const AnimatorIkGoal& goal, const std::map<std::string, std::uint32_t, std::less<>>& indices,
                           const std::optional<Matrix4>& worldInverse,
                           const std::optional<Matrix4>& presentationInverse)
    {
        NamedIkEndpointCapture result{goal.Name,      goal.Bones.empty() ? std::string{} : goal.Bones.back(),
                                      goal.Space,     goal.Target,
                                      goal.Target,    goal.Weight,
                                      goal.Tolerance, {}};
        if (goal.Bones.empty() || !Math::IsFinite(goal.Target) || !std::isfinite(goal.Weight) || goal.Weight < 0 ||
            goal.Weight > 1 || !std::isfinite(goal.Tolerance) || goal.Tolerance <= 0)
            return result;
        for (const auto& name : goal.Bones)
            if (!indices.contains(name))
                return result;
        if (goal.Space != AnimatorIkSpace::Model)
        {
            const auto* inverse = goal.Space == AnimatorIkSpace::World               ? &worldInverse
                                  : goal.Space == AnimatorIkSpace::PresentationWorld ? &presentationInverse
                                                                                     : nullptr;
            if (!inverse || !*inverse)
                return result;
            result.ModelTarget = Math::TransformPoint(**inverse, goal.Target);
        }
        if (Math::IsFinite(result.ModelTarget))
            result.Index = indices.at(result.Bone);
        return result;
    }

    struct NamedIkEndpointResidual final
    {
        Vector3 ModelEndpoint;
        Vector3 PresentedEndpoint;
        Vector3 PresentedTarget;
        float ModelDistance = 0;
        // Model solve error expressed through the final presentation transform.
        float PresentedDistance = 0;
        // Separately measures deviation from the originally submitted world-space target.
        // Model-space goals have no original world target.
        std::optional<float> OriginalWorldDistance;
    };

    [[nodiscard]] inline std::optional<NamedIkEndpointResidual>
    MeasureNamedIkEndpoint(const NamedIkEndpointCapture& capture, std::span<const AnimatorPoseBoneDebugState> pose,
                           const Matrix4& presentation)
    {
        if (!capture.Index || *capture.Index >= pose.size() || pose[*capture.Index].Name != capture.Bone)
            return std::nullopt;
        NamedIkEndpointResidual result;
        result.ModelEndpoint = pose[*capture.Index].WorldPosition; // Snapshot field is model space.
        result.PresentedEndpoint = Math::TransformPoint(presentation, result.ModelEndpoint);
        result.PresentedTarget = Math::TransformPoint(presentation, capture.ModelTarget);
        const auto distance = [](const Vector3 a, const Vector3 b)
        { return std::hypot(a.X - b.X, a.Y - b.Y, a.Z - b.Z); };
        result.ModelDistance = distance(result.ModelEndpoint, capture.ModelTarget);
        result.PresentedDistance = distance(result.PresentedEndpoint, result.PresentedTarget);
        if (capture.Space == AnimatorIkSpace::World || capture.Space == AnimatorIkSpace::PresentationWorld)
            result.OriginalWorldDistance = distance(result.PresentedEndpoint, capture.OriginalTarget);
        if (!Math::IsFinite(result.ModelEndpoint) || !Math::IsFinite(result.PresentedEndpoint) ||
            !Math::IsFinite(result.PresentedTarget) || !std::isfinite(result.ModelDistance) ||
            !std::isfinite(result.PresentedDistance) ||
            (result.OriginalWorldDistance && !std::isfinite(*result.OriginalWorldDistance)))
            return std::nullopt;
        return result;
    }

    [[nodiscard]] inline bool IsNamedIkPublicationCurrent(const std::shared_ptr<const AnimatorDebugSnapshot>& captured,
                                                          const std::uint64_t capturedGeneration,
                                                          const std::shared_ptr<const AnimatorDebugSnapshot>& current,
                                                          const std::uint64_t currentGeneration) noexcept
    {
        return captured && captured == current && capturedGeneration == currentGeneration;
    }

    // Session-owned optional inspection. No renderer/asset readback and no mesh-clearance claim.
    class NamedIkDiagnostics final
    {
      public:
        NamedIkDiagnostics();
        explicit NamedIkDiagnostics(std::string entityName) : m_EntityName(std::move(entityName)) {}
        [[nodiscard]] bool Enabled() const noexcept { return !m_EntityName.empty(); }
        void Capture(const Entity& entity, const AnimatorComponent& animator,
                     const std::map<std::string, std::uint32_t, std::less<>>& indices,
                     const std::optional<Matrix4>& worldInverse, const std::optional<Matrix4>& presentationInverse);
        void Published(const Entity& entity, const AnimatorComponent& animator);
        void Observe(float deltaSeconds);
        void Reset() noexcept;

      private:
        struct Pending final
        {
            Entity Owner;
            std::vector<NamedIkEndpointCapture> Goals;
            std::shared_ptr<const AnimatorDebugSnapshot> Snapshot;
            std::uint64_t Generation = 0;
        };
        void Summary(std::string_view kind);
        std::string m_EntityName;
        std::map<EntityId, Pending> m_Pending;
        std::uint64_t m_Frames = 0, m_Captured = 0, m_Observed = 0, m_Invalid = 0, m_Inactive = 0, m_Weighted = 0,
                      m_SolverDiagnostics = 0, m_Unpublished = 0, m_Unavailable = 0, m_OriginalWorldObservations = 0;
        std::uint32_t m_Lines = 0;
        float m_Elapsed = 0, m_Maximum = 0, m_MaximumFullWeight = 0, m_MaximumOriginalWorld = 0,
              m_MaximumFullWeightOriginalWorld = 0;
    };
} // namespace Keire::Detail
