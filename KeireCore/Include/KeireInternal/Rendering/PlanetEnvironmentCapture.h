#pragma once

#include "Keire/Rendering/PlanetEnvironmentView.h"
#include "KeireInternal/Environment/CloudEvaluation.h"
#include "KeireInternal/Environment/FogDensity.h"
#include "KeireInternal/Spatial/PlanetFrameResolution.h"
#include "KeireInternal/Spatial/PlanetProjectSettings.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

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
        if (view.AtmosphereOverride)
        {
            ValidateAtmosphere(*view.AtmosphereOverride, view.RadiusMetres);
        }
        if (view.CloudOverride)
        {
            ValidateCloudSettings(*view.CloudOverride);
        }
        if (view.FogOverride)
        {
            ValidateFogDensity(*view.FogOverride);
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

    // Resolve the selected planet before any phase, packet, cache or temporal-history capture.
    // An absent profile inherits; an explicitly disabled profile still replaces the inherited settings.
    inline void ApplyPlanetEnvironmentOverrides(const PlanetEnvironmentView& view, AtmosphereSettings& atmosphere,
                                                CloudSettings& clouds, FogSettings& fog)
    {
        (void)CapturePlanetEnvironment(view);
        if (view.AtmosphereOverride)
        {
            atmosphere = *view.AtmosphereOverride;
            atmosphere.PlanetRadius = view.RadiusMetres;
        }
        if (view.CloudOverride)
        {
            clouds = *view.CloudOverride;
        }
        if (view.FogOverride)
        {
            fog = *view.FogOverride;
        }
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
    [[nodiscard]] inline PlanetEnvironmentView ToPlanetEnvironmentView(const ResolvedPlanetFrame& frame,
                                                                       const PlanetProjectSettings& settings)
    {
        auto view = ToPlanetEnvironmentView(frame);
        for (const auto& profile : settings.AtmosphereOverrides)
        {
            if (profile.Planet == frame.Planet)
            {
                view.AtmosphereOverride = profile.Atmosphere;
                break;
            }
        }
        for (const auto& profile : settings.CloudOverrides)
        {
            if (profile.Planet == frame.Planet)
            {
                view.CloudOverride = profile.Clouds;
                break;
            }
        }
        for (const auto& profile : settings.FogOverrides)
        {
            if (profile.Planet == frame.Planet)
            {
                view.FogOverride = profile.Fog;
                break;
            }
        }
        (void)CapturePlanetEnvironment(view);
        return view;
    }

    struct CapturedPlanetAtmospheres
    {
        std::vector<ResolvedPlanetFrame> Frames;
        std::vector<AtmosphereSettings> Profiles;
    };

    // Capture all candidates before applying selected-planet overrides to the inherited environment. A view
    // request is one immutable observation: mixing epochs or observer axes corrupts transport and history.
    [[nodiscard]] inline CapturedPlanetAtmospheres
    CapturePlanetAtmospheres(const std::span<const PlanetEnvironmentView> candidates,
                             const std::optional<PlanetEnvironmentView>& selected, const AtmosphereSettings& inherited)
    {
        const auto sameRotation = [](const FrameRotation& left, const FrameRotation& right)
        {
            constexpr double rotationTolerance = 1.0e-10;
            const auto leftComponents = left.Components();
            const auto rightComponents = right.Components();
            double dot = 0.0;
            for (std::size_t axis = 0; axis < leftComponents.size(); ++axis)
            {
                dot += leftComponents[axis] * rightComponents[axis];
            }
            return std::abs(dot) >= 1.0 - rotationTolerance;
        };
        const auto selectedFrame = selected ? std::optional{CapturePlanetEnvironment(*selected)} : std::nullopt;
        CapturedPlanetAtmospheres result;
        result.Frames.reserve(candidates.size());
        const bool hasOverrides = (selected && selected->AtmosphereOverride) ||
                                  std::ranges::any_of(candidates, [](const auto& candidate)
                                                      { return candidate.AtmosphereOverride.has_value(); });
        if (!inherited.Enabled && !hasOverrides)
        {
            return result;
        }
        if (hasOverrides)
        {
            result.Profiles.reserve(candidates.size());
        }
        for (const auto& candidate : candidates)
        {
            const auto frame = CapturePlanetEnvironment(candidate);
            const auto* reference =
                selectedFrame ? &*selectedFrame : (result.Frames.empty() ? nullptr : &result.Frames.front());
            if (reference)
            {
                if (frame.ObserverFrame != reference->ObserverFrame ||
                    frame.TopologyRevision != reference->TopologyRevision ||
                    frame.SnapshotTime != reference->SnapshotTime || frame.Clock != reference->Clock ||
                    !sameRotation(frame.RootToObserver, reference->RootToObserver))
                {
                    throw std::invalid_argument(
                        "Planet atmosphere candidates require one observer frame, clock, axes and evaluation epoch.");
                }
            }
            if (std::ranges::any_of(
                    result.Frames, [&](const auto& captured)
                    { return captured.Planet == frame.Planet || captured.PlanetFrame == frame.PlanetFrame; }))
            {
                throw std::invalid_argument("Planet atmosphere candidates contain a duplicate planet or frame.");
            }
            if (selectedFrame && frame.Planet == selectedFrame->Planet &&
                (frame.PlanetFrame != selectedFrame->PlanetFrame || frame.RadiusMetres != selectedFrame->RadiusMetres ||
                 frame.DiscontinuityRevision != selectedFrame->DiscontinuityRevision ||
                 frame.ObserverFromCenterPlanetMetres != selectedFrame->ObserverFromCenterPlanetMetres ||
                 !sameRotation(frame.PlanetToObserver, selectedFrame->PlanetToObserver) ||
                 candidate.AtmosphereOverride != selected->AtmosphereOverride))
            {
                throw std::invalid_argument("Selected planet atmosphere differs from its composition candidate.");
            }
            result.Frames.push_back(frame);
        }
        // Keep profiles paired while sorting independently of request/authoring order. Equal-distance opaque
        // hits use this stable identity order in the shader as well as the CPU transport reference.
        std::ranges::sort(result.Frames, {}, &ResolvedPlanetFrame::Planet);
        if (hasOverrides)
        {
            std::vector<const PlanetEnvironmentView*> ordered;
            ordered.reserve(candidates.size());
            for (const auto& candidate : candidates)
            {
                ordered.push_back(&candidate);
            }
            std::ranges::sort(ordered,
                              [](const auto* left, const auto* right) { return left->Planet < right->Planet; });
            for (const auto* candidate : ordered)
            {
                result.Profiles.push_back(candidate->AtmosphereOverride.value_or(inherited));
            }
        }
        return result;
    }
} // namespace Keire::Detail
