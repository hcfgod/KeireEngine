#pragma once

#include "Keire/Rendering/PlanetEnvironmentView.h"

#include <array>
#include <cstdint>
#include <optional>

namespace Keire::RenderBackend
{
    // Identity and presentation geometry only: no project settings, clock, or frame-graph snapshot ownership.
    struct PlanetEnvironmentHistoryKey final
    {
        AssetId Planet;
        std::uint64_t World = 0;
        std::uint64_t PlanetFrame = 0;
        std::uint64_t ObserverFrame = 0;
        std::uint64_t TopologyRevision = 0;
        std::uint64_t DiscontinuityRevision = 0;
        double RadiusMetres = 0.0;
        std::array<double, 4> PlanetToObserverRotation{};
        std::array<double, 4> RootToObserverRotation{};

        bool operator==(const PlanetEnvironmentHistoryKey&) const noexcept = default;
    };

    static_assert(sizeof(PlanetEnvironmentHistoryKey) == 128);

    [[nodiscard]] inline std::optional<PlanetEnvironmentHistoryKey>
    MakePlanetEnvironmentHistoryKey(const std::optional<PlanetEnvironmentView>& sample) noexcept
    {
        if (!sample)
        {
            return std::nullopt;
        }
        // Main scene TAA has no independent moving-sky velocity. Conservatively reject rotation changes
        // until sky motion is reconstructed there, even though the cloud-only resolve supports rotation.
        return PlanetEnvironmentHistoryKey{sample->Planet,
                                           sample->World,
                                           sample->PlanetFrame,
                                           sample->ObserverFrame,
                                           sample->TopologyRevision,
                                           sample->DiscontinuityRevision,
                                           sample->RadiusMetres,
                                           sample->PlanetToObserverRotation,
                                           sample->RootToObserverRotation};
    }
} // namespace Keire::RenderBackend
