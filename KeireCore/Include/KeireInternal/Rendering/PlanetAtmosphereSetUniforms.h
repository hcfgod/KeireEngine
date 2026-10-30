#pragma once

#include "KeireInternal/Rendering/PlanetAtmosphereUniforms.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace Keire::RenderBackend
{
    inline constexpr std::size_t MaximumPlanetAtmospheres = 8;

    struct PlanetAtmosphereSetUniforms
    {
        std::array<std::uint32_t, 4> Controls{};
        std::array<AtmosphereUniforms, MaximumPlanetAtmospheres> Media{};
    };

    static_assert(sizeof(PlanetAtmosphereSetUniforms) == 912);

    [[nodiscard]] inline PlanetAtmosphereSetUniforms
    MakePlanetAtmosphereSetUniforms(const AtmosphereSettings& settings, const Vector3 lightDirection,
                                    const std::span<const Detail::ResolvedPlanetFrame> planets)
    {
        PlanetAtmosphereSetUniforms result;
        if (!settings.Enabled || planets.size() < 2)
        {
            return result;
        }
        if (planets.size() > MaximumPlanetAtmospheres)
        {
            throw std::length_error("Planet atmosphere rendering supports at most eight candidates per view.");
        }
        result.Controls[0] = static_cast<std::uint32_t>(planets.size());
        for (std::size_t index = 0; index < planets.size(); ++index)
        {
            auto& medium = result.Media[index];
            medium = MakePlanetAtmosphereUniforms(settings, lightDirection, planets[index]);
            if (!Math::IsFinite(medium.ObserverRadius))
            {
                throw std::out_of_range("Planet atmosphere placement exceeds GPU coordinate range.");
            }
        }
        return result;
    }
} // namespace Keire::RenderBackend
