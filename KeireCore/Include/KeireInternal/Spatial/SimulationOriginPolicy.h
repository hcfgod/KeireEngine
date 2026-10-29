#pragma once

#include "Keire/Math/Vector3d.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace Keire::Detail
{
    struct SimulationOriginPolicy final
    {
        double RebaseDistanceMetres = 8192.0;
        double GridSpacingMetres = 1024.0;
    };

    [[nodiscard]] inline std::optional<Vector3d> SelectSimulationOrigin(const Vector3d current, const Vector3d focus,
                                                                        const SimulationOriginPolicy policy = {})
    {
        if (!Math::IsFinite(current) || !Math::IsFinite(focus) || !std::isfinite(policy.RebaseDistanceMetres) ||
            policy.RebaseDistanceMetres <= 0.0 || !std::isfinite(policy.GridSpacingMetres) ||
            policy.GridSpacingMetres <= 0.0 || policy.GridSpacingMetres > policy.RebaseDistanceMetres)
        {
            throw std::invalid_argument("Simulation origin policy requires finite coordinates and a valid grid.");
        }
        const Vector3d relative{focus.X - current.X, focus.Y - current.Y, focus.Z - current.Z};
        if (!Math::IsFinite(relative))
        {
            throw std::out_of_range("Simulation focus exceeds the coordinate range.");
        }
        if (std::max({std::abs(relative.X), std::abs(relative.Y), std::abs(relative.Z)}) <= policy.RebaseDistanceMetres)
        {
            return {};
        }
        const auto snap = [&](const double coordinate)
        {
            // Subtract the remainder rather than divide/multiply enormous coordinates by the grid spacing.
            const double result = coordinate - std::remainder(coordinate, policy.GridSpacingMetres);
            if (!std::isfinite(result))
            {
                throw std::out_of_range("Simulation origin grid exceeds the coordinate range.");
            }
            return result;
        };
        const Vector3d selected{snap(focus.X), snap(focus.Y), snap(focus.Z)};
        if (selected == current)
        {
            return {};
        }
        return selected;
    }
} // namespace Keire::Detail
