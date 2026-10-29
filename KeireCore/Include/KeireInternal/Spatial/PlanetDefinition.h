#pragma once

#include "Keire/Assets/Asset.h"
#include "KeireInternal/Spatial/FrameSnapshot.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <stdexcept>

namespace Keire::Detail
{
    // Placement belongs to the parent frame; the planet-fixed frame has its center at zero.
    struct PlanetDefinition
    {
        AssetId Planet;
        std::uint64_t Frame = 0;
        SectorPosition ParentCenter{{1, 1}, {}, {}};
        FrameRotation Orientation;
        std::array<double, 3> AngularVelocity{};
        double EpochSeconds = 0.0;
        double RadiusMetres = 6'360'000.0;
        double GravitationalParameter = 0.0;

        bool operator==(const PlanetDefinition& other) const noexcept
        {
            return Planet == other.Planet && Frame == other.Frame && ParentCenter == other.ParentCenter &&
                   Orientation.Components() == other.Orientation.Components() &&
                   AngularVelocity == other.AngularVelocity && EpochSeconds == other.EpochSeconds &&
                   RadiusMetres == other.RadiusMetres && GravitationalParameter == other.GravitationalParameter;
        }
    };

    inline void ValidatePlanetDefinition(const PlanetDefinition& definition)
    {
        if (!definition.Planet || definition.Frame == 0 ||
            definition.Frame == definition.ParentCenter.Identity().Frame || !std::isfinite(definition.EpochSeconds) ||
            !std::isfinite(definition.RadiusMetres) || definition.RadiusMetres <= 0.0 ||
            !std::isfinite(definition.GravitationalParameter) || definition.GravitationalParameter < 0.0)
        {
            throw std::invalid_argument("Planet identity, frame, radius, gravity or epoch is invalid.");
        }
        for (const double value : definition.AngularVelocity)
        {
            if (!std::isfinite(value))
            {
                throw std::invalid_argument("Planet angular velocity must be finite.");
            }
        }
        if (!std::isfinite(std::hypot(definition.AngularVelocity[0], definition.AngularVelocity[1],
                                      definition.AngularVelocity[2])))
        {
            throw std::invalid_argument("Planet angular speed exceeds double range.");
        }
    }

    [[nodiscard]] inline FrameSnapshot EvaluatePlanetFrame(const PlanetDefinition& definition, const double time,
                                                           const std::uint64_t revision)
    {
        ValidatePlanetDefinition(definition);
        const double speed =
            std::hypot(definition.AngularVelocity[0], definition.AngularVelocity[1], definition.AngularVelocity[2]);
        auto rotation = definition.Orientation;
        const double elapsed = time - definition.EpochSeconds;
        if (!std::isfinite(time) || !std::isfinite(elapsed))
        {
            throw std::invalid_argument("Planet evaluation requires a finite local simulation epoch.");
        }
        if (speed > 0.0)
        {
            const double period = 2.0 * std::numbers::pi / speed;
            const double angle = std::isfinite(period) ? std::remainder(elapsed, period) * speed : elapsed * speed;
            const double halfAngle = angle * 0.5;
            const double sine = std::sin(halfAngle);
            const auto spin = FrameRotation::Normalize(
                definition.AngularVelocity[0] / speed * sine, definition.AngularVelocity[1] / speed * sine,
                definition.AngularVelocity[2] / speed * sine, std::cos(halfAngle));
            rotation = spin.Compose(rotation);
        }
        return FrameSnapshot({definition.ParentCenter.Identity().World, definition.Frame}, definition.ParentCenter,
                             rotation, {}, definition.AngularVelocity, time, revision);
    }
} // namespace Keire::Detail
