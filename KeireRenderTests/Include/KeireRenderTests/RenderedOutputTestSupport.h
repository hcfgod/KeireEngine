#pragma once

#include "Keire/Application.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace
{
    constexpr std::uint32_t SurfaceSize = 96;
    [[maybe_unused]] constexpr float ColorTolerance = 0.04F;
    [[maybe_unused]] constexpr float MinimumBehaviorDelta = 0.08F;
    [[maybe_unused]] constexpr float MinimumNormalResponseDelta = 0.04F;
    [[maybe_unused]] constexpr float MinimumShadowDelta = 0.025F;
    [[maybe_unused]] constexpr float MinimumShadowDepthDelta = 0.01F;

    struct PixelStatistics final
    {
        float Red = 0.0F;
        float Green = 0.0F;
        float Blue = 0.0F;
        float Alpha = 0.0F;

        [[nodiscard]] float Luminance() const noexcept { return Red * 0.2126F + Green * 0.7152F + Blue * 0.0722F; }
    };

    [[nodiscard]] inline PixelStatistics MeasureCenter(const std::vector<std::uint8_t>& pixels)
    {
        REQUIRE(pixels.size() == static_cast<std::size_t>(SurfaceSize * SurfaceSize * 4));
        constexpr std::uint32_t minimum = SurfaceSize / 4;
        constexpr std::uint32_t maximum = SurfaceSize - minimum;
        PixelStatistics result;
        std::uint32_t count = 0;
        for (std::uint32_t y = minimum; y < maximum; ++y)
        {
            for (std::uint32_t x = minimum; x < maximum; ++x)
            {
                const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(SurfaceSize) + x) * 4U;
                result.Red += static_cast<float>(pixels[offset]) / 255.0F;
                result.Green += static_cast<float>(pixels[offset + 1]) / 255.0F;
                result.Blue += static_cast<float>(pixels[offset + 2]) / 255.0F;
                result.Alpha += static_cast<float>(pixels[offset + 3]) / 255.0F;
                ++count;
            }
        }
        result.Red /= static_cast<float>(count);
        result.Green /= static_cast<float>(count);
        result.Blue /= static_cast<float>(count);
        result.Alpha /= static_cast<float>(count);
        return result;
    }

    [[nodiscard]] inline PixelStatistics MeasureSkyCorner(const std::vector<std::uint8_t>& pixels)
    {
        REQUIRE(pixels.size() == static_cast<std::size_t>(SurfaceSize * SurfaceSize * 4));
        PixelStatistics result;
        constexpr std::uint32_t extent = 12;
        for (std::uint32_t y = 0; y < extent; ++y)
        {
            for (std::uint32_t x = 0; x < extent; ++x)
            {
                const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(SurfaceSize) + x) * 4U;
                result.Red += static_cast<float>(pixels[offset]) / 255.0F;
                result.Green += static_cast<float>(pixels[offset + 1]) / 255.0F;
                result.Blue += static_cast<float>(pixels[offset + 2]) / 255.0F;
                result.Alpha += static_cast<float>(pixels[offset + 3]) / 255.0F;
            }
        }
        constexpr float count = static_cast<float>(extent * extent);
        result.Red /= count;
        result.Green /= count;
        result.Blue /= count;
        result.Alpha /= count;
        return result;
    }

    [[nodiscard]] inline float GreenDominance(const std::vector<std::uint8_t>& pixels, const bool left)
    {
        REQUIRE(pixels.size() == static_cast<std::size_t>(SurfaceSize * SurfaceSize * 4));
        const auto minimumX = left ? 0U : SurfaceSize / 2U;
        const auto maximumX = left ? SurfaceSize / 2U : SurfaceSize;
        float result = 0.0F;
        for (std::uint32_t y = 0; y < SurfaceSize; ++y)
        {
            for (std::uint32_t x = minimumX; x < maximumX; ++x)
            {
                const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(SurfaceSize) + x) * 4U;
                const auto red = static_cast<float>(pixels[offset]) / 255.0F;
                const auto green = static_cast<float>(pixels[offset + 1]) / 255.0F;
                const auto blue = static_cast<float>(pixels[offset + 2]) / 255.0F;
                result += std::max(green - std::max(red, blue), 0.0F);
            }
        }
        return result / (static_cast<float>(SurfaceSize) * static_cast<float>(SurfaceSize) / 2.0F);
    }

    [[nodiscard]] inline float MaximumDarkening(const std::vector<std::uint8_t>& unshadowed,
                                                const std::vector<std::uint8_t>& shadowed)
    {
        if (unshadowed.size() != shadowed.size())
            return 0.0F;
        float maximum = 0.0F;
        for (std::size_t offset = 0; offset + 3 < unshadowed.size(); offset += 4)
        {
            const auto luminance = [offset](const std::vector<std::uint8_t>& frame)
            {
                return (0.2126F * static_cast<float>(frame[offset]) + 0.7152F * static_cast<float>(frame[offset + 1]) +
                        0.0722F * static_cast<float>(frame[offset + 2])) /
                       255.0F;
            };
            maximum = std::max(maximum, luminance(unshadowed) - luminance(shadowed));
        }
        return maximum;
    }

    [[nodiscard]] inline float MaximumDifference(const std::vector<float>& left, const std::vector<float>& right)
    {
        if (left.size() != right.size())
            return 0.0F;
        float maximum = 0.0F;
        for (std::size_t index = 0; index < left.size(); ++index)
            maximum = std::max(maximum, std::abs(left[index] - right[index]));
        return maximum;
    }

    [[nodiscard]] inline bool HasStableMaterialBinding(const std::vector<std::uint64_t>& builds) noexcept
    {
        return builds.size() >= 2 && builds.back() > 0 && builds[builds.size() - 2] == builds.back();
    }

    [[nodiscard]] inline Keire::ApplicationSpecification RenderTestSpecification()
    {
        const char* backend = SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "KEIRE_GPU_TEST_BACKEND");
        if (backend && *backend && !SDL_SetHintWithPriority(SDL_HINT_GPU_DRIVER, backend, SDL_HINT_OVERRIDE))
            throw std::runtime_error("Could not restore the requested GPU backend after SDL shutdown.");
        Keire::ApplicationSpecification specification;
        specification.MainWindow.Title = "Kéire rendered output tests";
        specification.MainWindow.Width = SurfaceSize;
        specification.MainWindow.Height = SurfaceSize;
        specification.MainWindow.Visible = false;
        specification.Render.Mode = Keire::RenderMode::Rendered;
        specification.Render.PreferredSampleCount = Keire::RenderSampleCount::One;
        specification.Render.MaximumFramesInFlight = 1;
        specification.Render.EnableGpuValidation =
            SDL_GetEnvironmentVariable(SDL_GetEnvironment(), "KEIRE_GPU_VALIDATION") != nullptr;
        specification.Ui.Mode = Keire::UiMode::Disabled;
        specification.Input.Mode = Keire::InputMode::Disabled;
        specification.Scenes.Mode = Keire::SceneMode::Disabled;
        specification.ManageLogging = false;
        specification.SuspendWhenMainWindowMinimized = false;
        return specification;
    }
} // namespace
