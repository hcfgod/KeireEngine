#pragma once

#include "Keire/Vfx/VfxSystem.h"

#include <cstdint>
#include <limits>

namespace Keire::RenderBackend
{
    [[nodiscard]] inline Vector3 VfxPositionInRenderFrame(const Vector3 localPosition, const Vector3d simulationOrigin,
                                                          const Vector3d renderOrigin)
    {
        const Vector3d relative{(simulationOrigin.X - renderOrigin.X) + localPosition.X,
                                (simulationOrigin.Y - renderOrigin.Y) + localPosition.Y,
                                (simulationOrigin.Z - renderOrigin.Z) + localPosition.Z};
        return Math::RelativeFloat(relative, {}, std::numeric_limits<double>::max());
    }

    [[nodiscard]] inline bool VfxMaterialSupportsRenderOrigin(const std::uint8_t abiVersion,
                                                              const Vector3d simulationOrigin,
                                                              const Vector3d renderOrigin) noexcept
    {
        return simulationOrigin == renderOrigin || abiVersion == 1U;
    }

    [[nodiscard]] inline Matrix4 VfxFrameTranslation(const Vector3d sourceOrigin, const Vector3d destinationOrigin)
    {
        return Math::ComposeTransform(VfxPositionInRenderFrame({}, sourceOrigin, destinationOrigin), {},
                                      {1.0F, 1.0F, 1.0F});
    }

    struct VfxCollisionFrame final
    {
        Matrix4 ViewProjection;
        Matrix4 InverseViewProjection;
    };

    [[nodiscard]] inline VfxCollisionFrame MakeVfxCollisionFrame(const Matrix4& depthViewProjection,
                                                                 const Matrix4& depthInverseViewProjection,
                                                                 const Vector3d simulationOrigin,
                                                                 const Vector3d depthOrigin)
    {
        const auto offset = VfxPositionInRenderFrame({}, simulationOrigin, depthOrigin);
        const auto toDepth = Math::ComposeTransform(offset, {}, {1.0F, 1.0F, 1.0F});
        const auto toSimulation = Math::ComposeTransform({-offset.X, -offset.Y, -offset.Z}, {}, {1.0F, 1.0F, 1.0F});
        return {Math::Multiply(depthViewProjection, toDepth), Math::Multiply(toSimulation, depthInverseViewProjection)};
    }

    [[nodiscard]] inline VfxRenderParticle VfxParticleInRenderFrame(const VfxRenderParticle& particle,
                                                                    const Vector3d simulationOrigin,
                                                                    const Vector3d renderOrigin,
                                                                    const Vector3d previousRenderOrigin)
    {
        auto result = particle;
        result.Position = VfxPositionInRenderFrame(particle.Position, simulationOrigin, renderOrigin);
        result.PreviousPosition =
            VfxPositionInRenderFrame(particle.PreviousPosition, simulationOrigin, previousRenderOrigin);
        return result;
    }
} // namespace Keire::RenderBackend
