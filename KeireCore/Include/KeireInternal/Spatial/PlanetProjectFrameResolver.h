#pragma once

#include "KeireInternal/Spatial/PlanetFrameResolution.h"
#include "KeireInternal/Spatial/PlanetProjectSettings.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    // Editor-owned cache. Rebuild topology only on authored changes, and publish rotations only when time changes.
    class PlanetProjectFrameResolver final
    {
      public:
        void Configure(const PlanetProjectSettings& settings)
        {
            if (m_State && m_State->Settings == settings)
            {
                return;
            }
            RequireRevisionCapacity();
            auto candidate = std::make_unique<State>(settings);
            m_State.swap(candidate);
            ++m_DiscontinuityRevision;
        }

        [[nodiscard]] std::optional<ResolvedPlanetFrame> Resolve(const SectorPosition& observerPosition,
                                                                 const double snapshotTime,
                                                                 const EnvironmentClockState& clock,
                                                                 const std::uint64_t clockDiscontinuityRevision = 0)
        {
            if (!m_State || m_State->Settings.Geometry == PlanetGeometryMode::Flat)
            {
                return std::nullopt;
            }
            if (clockDiscontinuityRevision != m_ClockRevision)
            {
                RequireRevisionCapacity();
                m_ClockRevision = clockDiscontinuityRevision;
                ++m_DiscontinuityRevision;
            }
            auto& state = *m_State;
            if (!state.SnapshotTime || *state.SnapshotTime != snapshotTime)
            {
                std::vector<FrameSnapshot> evaluations;
                evaluations.reserve(state.Settings.Planets.size());
                for (const auto& planet : state.Settings.Planets)
                {
                    evaluations.push_back(EvaluatePlanetFrame(planet, snapshotTime, state.Registry.Revision()));
                }
                state.Snapshots = state.Registry.Publish(evaluations, snapshotTime);
                state.SnapshotTime = snapshotTime;
            }
            const auto selected =
                std::ranges::find(state.Settings.Planets, state.Settings.SelectedPlanet, &PlanetDefinition::Planet);
            return ResolvePlanetFrame(state.Registry, state.Registry.Find(selected->Frame), state.Registry.Find(1),
                                      observerPosition, state.Snapshots, snapshotTime, clock, selected->Planet,
                                      selected->RadiusMetres, m_DiscontinuityRevision);
        }

      private:
        struct State
        {
            explicit State(const PlanetProjectSettings& settings) : Settings(settings)
            {
                ValidatePlanetProjectSettings(settings);
                std::set<std::uint64_t> created{1};
                while (created.size() != Settings.Planets.size() + 1)
                {
                    for (const auto& planet : Settings.Planets)
                    {
                        const auto parent = planet.ParentCenter.Identity().Frame;
                        if (!created.contains(planet.Frame) && created.contains(parent))
                        {
                            (void)Registry.Create(planet.Frame, Registry.Find(parent));
                            created.insert(planet.Frame);
                        }
                    }
                }
            }

            PlanetProjectSettings Settings;
            FrameRegistry Registry{1, 1};
            std::optional<double> SnapshotTime;
            std::vector<BoundFrameSnapshot> Snapshots;
        };

        void RequireRevisionCapacity() const
        {
            if (m_DiscontinuityRevision == std::numeric_limits<std::uint64_t>::max())
            {
                throw std::overflow_error("Planet preview discontinuity counter exhausted.");
            }
        }

        std::unique_ptr<State> m_State;
        std::uint64_t m_DiscontinuityRevision = 0;
        std::uint64_t m_ClockRevision = 0;
    };
} // namespace Keire::Detail
