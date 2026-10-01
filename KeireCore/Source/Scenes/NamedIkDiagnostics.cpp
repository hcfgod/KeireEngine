#include "KeireInternal/Scenes/NamedIkDiagnostics.h"

#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <string_view>

namespace Keire::Detail
{
    NamedIkDiagnostics::NamedIkDiagnostics()
    {
#if defined(_MSC_VER)
        char value[257]{};
        std::size_t required = 0;
        if (getenv_s(&required, value, sizeof(value), "KEIRE_NAMED_IK_DIAGNOSTICS") == 0 && required > 0)
            m_EntityName = value;
#else
        if (const auto* value = std::getenv("KEIRE_NAMED_IK_DIAGNOSTICS"))
        {
            const std::string_view name(value);
            if (name.size() <= 256)
                m_EntityName = name;
        }
#endif
    }

    void NamedIkDiagnostics::Capture(const Entity& entity, const AnimatorComponent& animator,
                                     const std::map<std::string, std::uint32_t, std::less<>>& indices,
                                     const std::optional<Matrix4>& worldInverse,
                                     const std::optional<Matrix4>& presentationInverse)
    {
        if (!Enabled() || entity.Name() != m_EntityName)
            return;
        auto& pending = m_Pending[entity.Id()];
        m_Unpublished += pending.Goals.size();
        pending.Owner = entity;
        pending.Snapshot.reset();
        pending.Goals.clear();
        for (const auto& goal : animator.IkGoals())
        {
            pending.Goals.push_back(CaptureNamedIkEndpoint(goal, indices, worldInverse, presentationInverse));
            ++m_Captured;
        }
    }

    void NamedIkDiagnostics::Published(const Entity& entity, const AnimatorComponent& animator)
    {
        const auto found = m_Pending.find(entity.Id());
        if (found == m_Pending.end())
            return;
        found->second.Snapshot = animator.RuntimeDebugSnapshot();
        found->second.Generation = animator.PoseGeneration();
    }

    void NamedIkDiagnostics::Observe(const float deltaSeconds)
    {
        if (!Enabled())
            return;
        ++m_Frames;
        if (std::isfinite(deltaSeconds) && deltaSeconds > 0)
            m_Elapsed += deltaSeconds;
        const bool emit = m_Frames == 1 || m_Elapsed >= .2F;
        if (emit)
            m_Elapsed = 0;
        for (auto& [id, pending] : m_Pending)
        {
            const auto animator =
                pending.Owner ? pending.Owner.GetComponent<AnimatorComponent>() : Ref<AnimatorComponent>{};
            const auto transform =
                pending.Owner ? pending.Owner.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
            const bool renderActive = animator && animator->Enabled() && pending.Owner.ActiveInHierarchy();
            const bool paired =
                animator && transform &&
                IsNamedIkPublicationCurrent(pending.Snapshot, pending.Generation, animator->RuntimeDebugSnapshot(),
                                            animator->PoseGeneration());
            std::optional<Matrix4> presentation;
            if (paired && renderActive)
            {
                try
                {
                    presentation = transform->PresentationWorldMatrix();
                    (void)Math::Inverse(*presentation); // Singular hierarchy must not masquerade as zero residual.
                }
                catch (const std::exception&)
                {
                    presentation.reset();
                }
                if (!animator->RuntimeDiagnostic().empty())
                    ++m_SolverDiagnostics;
            }
            for (const auto& goal : pending.Goals)
            {
                ++m_Observed;
                const auto residual =
                    presentation ? MeasureNamedIkEndpoint(goal, pending.Snapshot->Pose, *presentation) : std::nullopt;
                const bool newMaximum = residual && goal.Weight > 0 && residual->PresentedDistance > m_Maximum;
                if (!renderActive)
                    ++m_Unavailable;
                else if (!residual)
                    ++m_Invalid;
                else if (goal.Weight == 0)
                    ++m_Inactive;
                else
                {
                    if (goal.Weight < 1)
                        ++m_Weighted;
                    m_Maximum = std::max(m_Maximum, residual->PresentedDistance);
                    if (residual->OriginalWorldDistance)
                    {
                        ++m_OriginalWorldObservations;
                        m_MaximumOriginalWorld = std::max(m_MaximumOriginalWorld, *residual->OriginalWorldDistance);
                        if (goal.Weight == 1)
                            m_MaximumFullWeightOriginalWorld =
                                std::max(m_MaximumFullWeightOriginalWorld, *residual->OriginalWorldDistance);
                    }
                    if (goal.Weight == 1)
                        m_MaximumFullWeight = std::max(m_MaximumFullWeight, residual->PresentedDistance);
                }
                if ((emit || !residual || newMaximum) && m_Lines < 3000)
                {
                    ++m_Lines;
                    Log::GetCoreLogger().Write(
                        LogLevel::Info,
                        LogMessage(
                            "[NamedIkEndpoint] frame={} entity={} goal={} bone={} space={} weight={} tolerance={} "
                            "generation={} paired={} renderActive={} valid={} newMaximum={} modelResidual={} "
                            "presentedResidual={} "
                            "originalWorldAvailable={} originalWorldResidual={} "
                            "target=({},{},{}) endpoint=({},{},{}) originalTarget=({},{},{})",
                            m_Frames, id, goal.Name, goal.Bone, goal.Space, goal.Weight, goal.Tolerance,
                            pending.Generation, paired, renderActive, residual.has_value(), newMaximum,
                            residual ? residual->ModelDistance : -1, residual ? residual->PresentedDistance : -1,
                            residual && residual->OriginalWorldDistance.has_value(),
                            residual && residual->OriginalWorldDistance ? *residual->OriginalWorldDistance : -1,
                            residual ? residual->PresentedTarget.X : 0, residual ? residual->PresentedTarget.Y : 0,
                            residual ? residual->PresentedTarget.Z : 0, residual ? residual->PresentedEndpoint.X : 0,
                            residual ? residual->PresentedEndpoint.Y : 0, residual ? residual->PresentedEndpoint.Z : 0,
                            goal.OriginalTarget.X, goal.OriginalTarget.Y, goal.OriginalTarget.Z));
                }
            }
            pending.Goals.clear();
            pending.Snapshot.reset();
        }
        if (emit && m_Lines < 3000)
        {
            ++m_Lines;
            Summary("sample");
        }
    }

    void NamedIkDiagnostics::Summary(const std::string_view kind)
    {
        Log::GetCoreLogger().Write(
            LogLevel::Info,
            LogMessage("[NamedIkEndpointSummary] kind={} entityName={} frames={} captured={} observed={} invalid={} "
                       "inactive={} weighted={} solverDiagnosticFrames={} unpublished={} maxPresentedResidual={} "
                       "maxFullWeightResidual={} detailLines={} unavailable={} originalWorldObservations={} "
                       "maxOriginalWorldResidual={} maxFullWeightOriginalWorldResidual={} "
                       "scope=graph-named-published-bone-endpoints",
                       kind, m_EntityName, m_Frames, m_Captured, m_Observed, m_Invalid, m_Inactive, m_Weighted,
                       m_SolverDiagnostics, m_Unpublished, m_Maximum, m_MaximumFullWeight, m_Lines, m_Unavailable,
                       m_OriginalWorldObservations, m_MaximumOriginalWorld, m_MaximumFullWeightOriginalWorld));
    }

    void NamedIkDiagnostics::Reset() noexcept
    {
        for (const auto& [id, pending] : m_Pending)
            m_Unpublished += pending.Goals.size();
        if (Enabled() && (m_Frames || m_Captured))
        {
            try
            {
                Summary("final");
            }
            catch (...)
            {
            } // Inspection must not interrupt noexcept scene cleanup.
        }
        m_Pending.clear();
        m_Frames = m_Captured = m_Observed = m_Invalid = m_Inactive = m_Weighted = m_SolverDiagnostics = m_Unpublished =
            0;
        m_Unavailable = m_OriginalWorldObservations = 0;
        m_MaximumOriginalWorld = m_MaximumFullWeightOriginalWorld = 0;
        m_Lines = 0;
        m_Elapsed = m_Maximum = m_MaximumFullWeight = 0;
    }
} // namespace Keire::Detail
