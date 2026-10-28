#pragma once

#include "KeireInternal/Rendering/RenderShaderDataInternal.h"

#include <cstddef>

namespace Keire::RenderBackend
{
    struct VolumeShadowUniforms final
    {
        AssetDirectionalShadowUniforms Directional;
        Vector4 ViewDepthDirection;
    };

    static_assert(sizeof(VolumeShadowUniforms) == 304);

    [[nodiscard]] inline bool FogReceivesTerrainSunlight(const FogViewUniforms& fog, const Vector4& sunlight,
                                                         const VolumeShadowUniforms& shadows) noexcept
    {
        const bool hasDensity = fog.HeightDensity[2] > 0.0F || fog.Controls[2] != 0U;
        const bool hasSunlight = sunlight.X > 0.0F || sunlight.Y > 0.0F || sunlight.Z > 0.0F;
        return fog.Controls[0] != 0U && hasDensity && hasSunlight &&
               shadows.Directional.DirectionalParameters.X != 0.0F &&
               shadows.Directional.DirectionalParameters.Y > 0.0F;
    }

    [[nodiscard]] inline VolumeShadowUniforms
    MakeVolumeShadowUniforms(const AssetDirectionalShadowUniforms& directional, const Matrix4& view,
                             const Matrix4& inverseView) noexcept
    {
        // Surface shading selects cascades using positive view-space Z in this engine.
        VolumeShadowUniforms result{directional, {view.Elements[2], view.Elements[6], view.Elements[10], 0.0F}};
        for (auto& matrix : result.Directional.DirectionalMatrices)
        {
            // The volume march uses camera-relative positions. Fold the camera translation into
            // each shadow matrix once, instead of adding a large world position at every sample.
            for (std::size_t row = 0; row < 4; ++row)
            {
                const double translated = static_cast<double>(matrix.Elements[row]) * inverseView.Elements[12] +
                                          static_cast<double>(matrix.Elements[4 + row]) * inverseView.Elements[13] +
                                          static_cast<double>(matrix.Elements[8 + row]) * inverseView.Elements[14] +
                                          matrix.Elements[12 + row];
                matrix.Elements[12 + row] = static_cast<float>(translated);
            }
        }
        return result;
    }
} // namespace Keire::RenderBackend
