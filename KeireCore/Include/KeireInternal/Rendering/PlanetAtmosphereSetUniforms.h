#pragma once

#include "KeireInternal/Rendering/PlanetAtmosphereUniforms.h"

#include <array>
#include <cmath>
#include <cstddef>
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
        std::array<Vector4, MaximumPlanetAtmospheres> Ground{};
    };

    static_assert(sizeof(PlanetAtmosphereSetUniforms) == 1040);
    static_assert(offsetof(PlanetAtmosphereSetUniforms, Media) == 16);
    static_assert(offsetof(PlanetAtmosphereSetUniforms, Ground) == 912);

    [[nodiscard]] inline PlanetAtmosphereSetUniforms
    MakePlanetAtmosphereSetUniforms(const AtmosphereSettings& settings, const Vector3 lightDirection,
                                    const std::span<const Detail::ResolvedPlanetFrame> planets,
                                    const std::span<const AtmosphereSettings> profiles = {})
    {
        PlanetAtmosphereSetUniforms result;
        if ((!profiles.empty() && profiles.size() != planets.size()))
        {
            throw std::invalid_argument("Atmosphere profile and planet candidate counts differ.");
        }
        if (planets.empty() || (profiles.empty() && (!settings.Enabled || planets.size() < 2)))
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
            const auto& profile = profiles.empty() ? settings : profiles[index];
            medium = MakePlanetAtmosphereUniforms(profile, lightDirection, planets[index]);
            result.Ground[index] = {
                static_cast<float>(profile.GroundReflectance[0]), static_cast<float>(profile.GroundReflectance[1]),
                static_cast<float>(profile.GroundReflectance[2]), static_cast<float>(profile.GroundIntensity)};
            if (!Math::IsFinite(medium.ObserverRadius))
            {
                throw std::out_of_range("Planet atmosphere placement exceeds GPU coordinate range.");
            }
        }
        return result;
    }
} // namespace Keire::RenderBackend
