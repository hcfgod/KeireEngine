#pragma once

#include "Keire/Assets/RenderingAssets.h"

#include <array>
#include <cstdint>

namespace Keire::RenderBackend
{
    struct DiffuseIrradiance final
    {
        std::array<Vector3, 9> Coefficients{};
    };

    [[nodiscard]] DiffuseIrradiance BakeDiffuseIrradiance(const Texture2DAsset& environment);
    [[nodiscard]] Vector3 EvaluateDiffuseIrradiance(const DiffuseIrradiance& irradiance, Vector3 direction) noexcept;
    [[nodiscard]] Ref<Texture2DAsset> CreateBrdfIntegrationLut(std::uint32_t resolution, std::uint32_t sampleCount);
} // namespace Keire::RenderBackend
