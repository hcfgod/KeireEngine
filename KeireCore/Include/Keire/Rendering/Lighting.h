#pragma once

#include "Keire/Assets/Asset.h"
#include "Keire/Math/Math.h"

#include <cstdint>

namespace Keire
{
    enum class ShadowQuality : std::uint8_t
    {
        Disabled,
        Hard,
        Soft
    };

    enum class LightBakeMode : std::uint8_t
    {
        Realtime,
        Mixed,
        Baked
    };

    enum class ShadowResolutionHint : std::uint8_t
    {
        Low,
        Medium,
        High,
        VeryHigh
    };

    enum class GIReceiveMode : std::uint8_t
    {
        LightProbes,
        Lightmaps,
        Disabled
    };

    enum class ContactShadowQuality : std::uint8_t
    {
        Disabled,
        Low,
        Medium,
        High
    };

    enum class ReflectionProbeCaptureMode : std::uint8_t
    {
        Baked,
        OnDemand
    };

    enum class ReflectionProbeResolution : std::uint16_t
    {
        Size64 = 64,
        Size128 = 128,
        Size256 = 256,
        Size512 = 512
    };

    enum class LightingBakeBackend : std::uint8_t
    {
        Automatic,
        GPU,
        CPU
    };

    enum class LightingBakeQuality : std::uint8_t
    {
        Preview,
        Medium,
        High,
        Production
    };

    struct LightingBakeSettings
    {
        LightingBakeBackend Backend = LightingBakeBackend::Automatic;
        LightingBakeQuality Quality = LightingBakeQuality::Medium;
        std::uint32_t LightmapResolution = 1024;
        std::uint32_t MaximumLightmapResolution = 4096;
        std::uint32_t TexelsPerUnit = 32;
        std::uint32_t PaddingTexels = 4;
        std::uint32_t IndirectBounceCount = 2;
        std::uint32_t SamplesPerTexel = 64;
        bool BakeAmbientOcclusion = true;
        bool Denoise = true;

        auto operator<=>(const LightingBakeSettings&) const noexcept = default;
    };
} // namespace Keire
