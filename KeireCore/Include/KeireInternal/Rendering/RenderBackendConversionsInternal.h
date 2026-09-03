#pragma once

#include "Keire/Rendering/RenderSystem.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace Keire::RenderBackend
{
    [[nodiscard]] inline std::string LastSdlError()
    {
        const char* error = SDL_GetError();
        return error && *error ? std::string(error) : std::string("SDL did not provide a diagnostic");
    }

    [[nodiscard]] inline bool ValidColor(const Color color) noexcept
    {
        const auto valid = [](const float value) { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; };
        return valid(color.Red) && valid(color.Green) && valid(color.Blue) && valid(color.Alpha);
    }

    [[nodiscard]] inline Vector3 Normalize(const Vector3 value) noexcept
    {
        const float length = std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
        return length > 0.000001F ? Vector3{value.X / length, value.Y / length, value.Z / length}
                                  : Vector3{0.0F, -1.0F, 0.0F};
    }

    [[nodiscard]] inline std::uint64_t HashDependencyStamp(std::uint64_t seed, const std::uint64_t value) noexcept
    {
        return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
    }

    [[nodiscard]] inline std::uint64_t HashDependencyStamp(std::uint64_t seed, const AssetId value) noexcept
    {
        seed = HashDependencyStamp(seed, value.High());
        return HashDependencyStamp(seed, value.Low());
    }

    [[nodiscard]] inline Color TemperatureColor(const float kelvin) noexcept
    {
        const float temperature = std::clamp(kelvin, 1000.0F, 20000.0F) / 100.0F;
        const float red = temperature <= 66.0F
                              ? 1.0F
                              : std::clamp(1.2929362F * std::pow(temperature - 60.0F, -0.13320476F), 0.0F, 1.0F);
        const float green = temperature <= 66.0F
                                ? std::clamp(0.39008158F * std::log(temperature) - 0.63184144F, 0.0F, 1.0F)
                                : std::clamp(1.1298909F * std::pow(temperature - 60.0F, -0.07551485F), 0.0F, 1.0F);
        const float blue = temperature >= 66.0F ? 1.0F
                           : temperature <= 19.0F
                               ? 0.0F
                               : std::clamp(0.5432068F * std::log(temperature - 10.0F) - 1.1962541F, 0.0F, 1.0F);
        return {red, green, blue, 1.0F};
    }

    [[nodiscard]] inline SDL_GPUPresentMode ToSdlPresentMode(const RenderPresentMode mode) noexcept
    {
        switch (mode)
        {
        case RenderPresentMode::Mailbox:
            return SDL_GPU_PRESENTMODE_MAILBOX;
        case RenderPresentMode::Immediate:
            return SDL_GPU_PRESENTMODE_IMMEDIATE;
        case RenderPresentMode::VSync:
        default:
            return SDL_GPU_PRESENTMODE_VSYNC;
        }
    }

    [[nodiscard]] constexpr std::uint32_t SdlAllowedFramesInFlight(const std::uint32_t requested) noexcept
    {
        return requested < 1U ? 1U : (requested > 3U ? 3U : requested);
    }

    [[nodiscard]] inline SDL_GPUSampleCount ToSdlSampleCount(const RenderSampleCount samples) noexcept
    {
        switch (samples)
        {
        case RenderSampleCount::Eight:
            return SDL_GPU_SAMPLECOUNT_8;
        case RenderSampleCount::Four:
            return SDL_GPU_SAMPLECOUNT_4;
        case RenderSampleCount::Two:
            return SDL_GPU_SAMPLECOUNT_2;
        case RenderSampleCount::One:
        default:
            return SDL_GPU_SAMPLECOUNT_1;
        }
    }

    [[nodiscard]] inline RenderSampleCount FromSdlSampleCount(const SDL_GPUSampleCount samples) noexcept
    {
        switch (samples)
        {
        case SDL_GPU_SAMPLECOUNT_8:
            return RenderSampleCount::Eight;
        case SDL_GPU_SAMPLECOUNT_4:
            return RenderSampleCount::Four;
        case SDL_GPU_SAMPLECOUNT_2:
            return RenderSampleCount::Two;
        case SDL_GPU_SAMPLECOUNT_1:
        default:
            return RenderSampleCount::One;
        }
    }
} // namespace Keire::RenderBackend
