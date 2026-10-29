#pragma once

#include "KeireInternal/Environment/EnvironmentClock.h"
#include "KeireInternal/Spatial/FrameConversion.h"
#include "KeireInternal/Spatial/PlanetDefinition.h"

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace Keire::Detail
{
    struct ResolvedPlanetFrame
    {
        AssetId Planet;
        SpatialIdentity PlanetFrame;
        SpatialIdentity ObserverFrame;
        std::uint64_t TopologyRevision = 0;
        std::uint64_t DiscontinuityRevision = 0;
        double SnapshotTime = 0.0;
        EnvironmentClockState Clock;
        std::array<double, 3> ObserverFromCenterPlanetMetres{};
        FrameRotation PlanetToObserver;
        double RadiusMetres = 0.0;
        FrameRotation RootToObserver;
    };

    [[nodiscard]] inline ResolvedPlanetFrame
    ResolvePlanetFrame(const FrameRegistry& registry, const LiveFrame& planetFrame, const LiveFrame& observerFrame,
                       const SectorPosition& observerPosition, const std::span<const BoundFrameSnapshot> snapshots,
                       const double snapshotTime, const EnvironmentClockState& clock, const AssetId planet,
                       const double radiusMetres, const std::uint64_t discontinuityRevision)
    {
        (void)EnvironmentClock(clock);
        if (!planet || !std::isfinite(radiusMetres) || radiusMetres <= 0.0)
        {
            throw std::invalid_argument("Planet resolution requires an identity and positive finite radius.");
        }
        const auto position = ConvertFrameMotion(registry, observerFrame, planetFrame,
                                                 SpatialMotion{observerPosition, {}, {}}, snapshots, snapshotTime);
        const SectorPosition center(registry.Identity(planetFrame), {}, {});
        auto root = observerFrame;
        while (const auto parent = registry.Parent(root))
        {
            root = *parent;
        }
        return {planet,
                registry.Identity(planetFrame),
                registry.Identity(observerFrame),
                registry.Revision(),
                discontinuityRevision,
                snapshotTime,
                clock,
                position.Position.RelativeTo(center),
                ConvertFrameRotation(registry, planetFrame, observerFrame, snapshots, snapshotTime),
                radiusMetres,
                ConvertFrameRotation(registry, root, observerFrame, snapshots, snapshotTime)};
    }
} // namespace Keire::Detail
