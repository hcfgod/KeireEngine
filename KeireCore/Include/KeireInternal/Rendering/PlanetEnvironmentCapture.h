#pragma once

#include "Keire/Rendering/PlanetEnvironmentView.h"
#include "KeireInternal/Spatial/PlanetFrameResolution.h"

#include <cmath>
#include <stdexcept>

namespace Keire::Detail
{
    [[nodiscard]] inline ResolvedPlanetFrame CapturePlanetEnvironment(const PlanetEnvironmentView& view)
    {
        if (!view.Planet || view.World == 0 || view.PlanetFrame == 0 || view.ObserverFrame == 0 ||
            view.TopologyRevision == 0 || !std::isfinite(view.SnapshotTime) || !std::isfinite(view.RadiusMetres) ||
            view.RadiusMetres <= 0.0)
        {
            throw std::invalid_argument("Planet environment requires valid frame identities, time and radius.");
        }
        for (const double coordinate : view.ObserverFromCenterPlanetMetres)
        {
            if (!std::isfinite(coordinate))
            {
                throw std::invalid_argument("Planet environment observer must be finite.");
            }
        }
        const EnvironmentClockState clock{view.Clock.Day, view.Clock.DayFraction, view.Clock.SecondsPerDay};
        (void)EnvironmentClock(clock);
        const auto& rotation = view.PlanetToObserverRotation;
        const double rotationLength =
            std::hypot(std::hypot(rotation[0], rotation[1]), std::hypot(rotation[2], rotation[3]));
        if (!std::isfinite(rotationLength) || std::abs(rotationLength - 1.0) > 1.0e-10)
        {
            throw std::invalid_argument("Planet environment orientation must be a unit quaternion.");
        }
        const auto& rootRotation = view.RootToObserverRotation;
        const double rootLength =
            std::hypot(std::hypot(rootRotation[0], rootRotation[1]), std::hypot(rootRotation[2], rootRotation[3]));
        if (!std::isfinite(rootLength) || std::abs(rootLength - 1.0) > 1.0e-10)
        {
            throw std::invalid_argument("Celestial reference orientation must be a unit quaternion.");
        }
        return {view.Planet,
                {view.World, view.PlanetFrame},
                {view.World, view.ObserverFrame},
                view.TopologyRevision,
                view.DiscontinuityRevision,
                view.SnapshotTime,
                clock,
                view.ObserverFromCenterPlanetMetres,
                FrameRotation::Normalize(rotation[0], rotation[1], rotation[2], rotation[3]),
                view.RadiusMetres,
                FrameRotation::Normalize(rootRotation[0], rootRotation[1], rootRotation[2], rootRotation[3])};
    }

    [[nodiscard]] inline PlanetEnvironmentView ToPlanetEnvironmentView(const ResolvedPlanetFrame& frame)
    {
        if (frame.PlanetFrame.World != frame.ObserverFrame.World)
        {
            throw std::invalid_argument("Planet and observer must belong to the same world.");
        }
        PlanetEnvironmentView result{frame.Planet,
                                     frame.PlanetFrame.World,
                                     frame.PlanetFrame.Frame,
                                     frame.ObserverFrame.Frame,
                                     frame.TopologyRevision,
                                     frame.DiscontinuityRevision,
                                     frame.SnapshotTime,
                                     {frame.Clock.Day, frame.Clock.DayFraction, frame.Clock.SecondsPerDay},
                                     frame.ObserverFromCenterPlanetMetres,
                                     frame.PlanetToObserver.Components(),
                                     frame.RadiusMetres,
                                     frame.RootToObserver.Components()};
        (void)CapturePlanetEnvironment(result);
        return result;
    }
} // namespace Keire::Detail
