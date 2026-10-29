#pragma once

#include "KeireInternal/Rendering/CloudTemporalResources.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace Keire::RenderBackend
{
    struct CloudTemporalResolveUniforms final
    {
        Matrix4 PreviousRotationViewProjection;
        Vector4 CloudPositionOffset;
        Vector4 ScenePositionOffset;
        Vector4 HistoryControls;
        Vector4 CloudRotationX{1.0F, 0.0F, 0.0F, 0.0F};
        Vector4 CloudRotationY{0.0F, 1.0F, 0.0F, 0.0F};
        Vector4 CloudRotationZ{0.0F, 0.0F, 1.0F, 0.0F};
    };

    static_assert(sizeof(CloudTemporalResolveUniforms) == 160);

    namespace CloudTemporalDetail
    {
        [[nodiscard]] inline bool SimilarRadiance(const Vector4 current, const Vector4 previous) noexcept
        {
            const auto similar = [](const float first, const float second)
            {
                // Reuse gradual daylight changes, but reset rather than leave a trail after a lighting jump.
                return std::isfinite(first) && std::isfinite(second) &&
                       std::abs(first - second) <= 0.0001F + 0.01F * std::max(std::abs(first), std::abs(second));
            };
            return similar(current.X, previous.X) && similar(current.Y, previous.Y) && similar(current.Z, previous.Z) &&
                   current.W == previous.W;
        }

        [[nodiscard]] inline float PhaseDisplacement(const float current, const float previous,
                                                     const float patternSize) noexcept
        {
            // The cloud field wraps every 256 pattern units. Keep a wrap crossing locally continuous.
            return std::remainder(current - previous, 256.0F) * patternSize;
        }
    } // namespace CloudTemporalDetail

    [[nodiscard]] inline std::optional<CloudTemporalResolveUniforms>
    MakeCloudTemporalReprojection(const CloudTemporalView& current, const CloudTemporalView& previous,
                                  const bool environmentContinuous) noexcept
    {
        if (!environmentContinuous || current.FrameId == 0 || previous.FrameId != current.FrameId - 1 ||
            current.UnjitteredProjection != previous.UnjitteredProjection)
        {
            return std::nullopt;
        }
        const auto& view = current.Volume;
        const auto& old = previous.Volume;
        const float patternSize = view.Volume.Geometry[2];
        if (!std::isfinite(patternSize) || patternSize <= 0.0F || view.Volume.Quality[0] == 0 ||
            view.Volume.Quality != old.Volume.Quality || view.Volume.Density != old.Volume.Density ||
            view.Volume.Albedo != old.Volume.Albedo || view.Volume.PatternOrigin[3] != old.Volume.PatternOrigin[3] ||
            view.Volume.Geometry[1] != old.Volume.Geometry[1] || patternSize != old.Volume.Geometry[2] ||
            view.Volume.Geometry[3] != old.Volume.Geometry[3] || view.ShapeControls != old.ShapeControls ||
            view.RegionalControls != old.RegionalControls || view.HeightProfile != old.HeightProfile ||
            view.WeatherControls != old.WeatherControls || view.Fog != old.Fog ||
            view.CameraControls.X != old.CameraControls.X || view.CameraControls.Z != old.CameraControls.Z ||
            view.CameraControls.W != old.CameraControls.W ||
            !CloudTemporalDetail::SimilarRadiance(view.SunRadiance, old.SunRadiance) ||
            !CloudTemporalDetail::SimilarRadiance(view.AmbientRadiance, old.AmbientRadiance))
        {
            return std::nullopt;
        }
        const auto& air = view.Atmosphere;
        const auto& oldAir = old.Atmosphere;
        if (air.ObserverRadius.W != oldAir.ObserverRadius.W || air.SunHeight.W != oldAir.SunHeight.W ||
            air.RayleighScale != oldAir.RayleighScale || air.Mie != oldAir.Mie ||
            air.AbsorptionPeak != oldAir.AbsorptionPeak || air.SolarWidth != oldAir.SolarWidth ||
            air.Controls != oldAir.Controls)
        {
            return std::nullopt;
        }
        const float sunAlignment = view.SunDirection.X * old.SunDirection.X + view.SunDirection.Y * old.SunDirection.Y +
                                   view.SunDirection.Z * old.SunDirection.Z;
        const float cameraAlignment = view.InverseView.Elements[8] * old.InverseView.Elements[8] +
                                      view.InverseView.Elements[9] * old.InverseView.Elements[9] +
                                      view.InverseView.Elements[10] * old.InverseView.Elements[10];
        if (!std::isfinite(sunAlignment) || sunAlignment < 0.9999F || !std::isfinite(cameraAlignment) ||
            cameraAlignment < 0.8660254F)
        {
            return std::nullopt;
        }
        CloudTemporalResolveUniforms result;
        result.PreviousRotationViewProjection = previous.RotationViewProjection;
        const Vector3d sceneOffset{
            (current.WorldOrigin.X - previous.WorldOrigin.X) +
                (static_cast<double>(view.InverseView.Elements[12]) - old.InverseView.Elements[12]),
            (current.WorldOrigin.Y - previous.WorldOrigin.Y) +
                (static_cast<double>(view.InverseView.Elements[13]) - old.InverseView.Elements[13]),
            (current.WorldOrigin.Z - previous.WorldOrigin.Z) +
                (static_cast<double>(view.InverseView.Elements[14]) - old.InverseView.Elements[14])};
        const double maximumDisplacement = static_cast<double>(patternSize) * 0.25;
        if (!Math::IsFinite(sceneOffset) || std::abs(sceneOffset.X) > maximumDisplacement ||
            std::abs(sceneOffset.Y) > maximumDisplacement || std::abs(sceneOffset.Z) > maximumDisplacement)
        {
            return std::nullopt;
        }
        result.ScenePositionOffset = {static_cast<float>(sceneOffset.X), static_cast<float>(sceneOffset.Y),
                                      static_cast<float>(sceneOffset.Z), 0.0F};
        if (view.PlanetaryCloud.Controls[0] != 0U || old.PlanetaryCloud.Controls[0] != 0U)
        {
            if (!current.PlanetFrame || !previous.PlanetFrame ||
                current.PlanetFrame->Planet != previous.PlanetFrame->Planet ||
                current.PlanetFrame->ObserverFrame != previous.PlanetFrame->ObserverFrame ||
                current.PlanetFrame->TopologyRevision != previous.PlanetFrame->TopologyRevision ||
                current.PlanetFrame->DiscontinuityRevision != previous.PlanetFrame->DiscontinuityRevision ||
                view.PlanetaryCloud.RadiusGeometry != old.PlanetaryCloud.RadiusGeometry)
            {
                return std::nullopt;
            }
            const auto& frame = *current.PlanetFrame;
            const auto& oldFrame = *previous.PlanetFrame;
            std::array<double, 3> displacement{};
            for (std::size_t axis = 0; axis < displacement.size(); ++axis)
            {
                if (std::abs(frame.ObserverFromCenterPlanetMetres[axis] -
                             oldFrame.ObserverFromCenterPlanetMetres[axis]) > maximumDisplacement)
                {
                    return std::nullopt;
                }
                displacement[axis] = CloudTemporalDetail::PhaseDisplacement(
                    view.Volume.PatternOrigin[axis], old.Volume.PatternOrigin[axis], patternSize);
                const double observerMotion =
                    frame.ObserverFromCenterPlanetMetres[axis] - oldFrame.ObserverFromCenterPlanetMetres[axis];
                // Advection translates the noise but not the radial envelope. Keep that deformation
                // below the depth rejection tolerance, rather than treating fast wind as rigid motion.
                if (std::abs(displacement[axis]) > maximumDisplacement ||
                    std::abs(displacement[axis] - observerMotion) >
                        std::max(0.1, static_cast<double>(view.Volume.Geometry[1]) * 0.01))
                {
                    return std::nullopt;
                }
            }
            const auto rotation = oldFrame.PlanetToObserver.Compose(frame.PlanetToObserver.Inverse());
            const auto inverse = rotation.Inverse();
            const auto row = [&inverse](const std::array<double, 3>& axis)
            {
                const auto value = inverse.Apply(axis);
                return Vector4{static_cast<float>(value[0]), static_cast<float>(value[1]), static_cast<float>(value[2]),
                               0.0F};
            };
            result.CloudRotationX = row({1.0, 0.0, 0.0});
            result.CloudRotationY = row({0.0, 1.0, 0.0});
            result.CloudRotationZ = row({0.0, 0.0, 1.0});
            const auto offset = oldFrame.PlanetToObserver.Apply(displacement);
            result.CloudPositionOffset = {static_cast<float>(offset[0]), static_cast<float>(offset[1]),
                                          static_cast<float>(offset[2]), 0.0F};
            // Fog and cloud histories still share one transport layer. Reject mismatched motion rather
            // than dragging stationary fog along a rotating or advecting weather field.
            if (view.Fog.Controls[0] != 0U && (result.CloudPositionOffset != result.ScenePositionOffset ||
                                               result.CloudRotationX != Vector4{1.0F, 0.0F, 0.0F, 0.0F} ||
                                               result.CloudRotationY != Vector4{0.0F, 1.0F, 0.0F, 0.0F} ||
                                               result.CloudRotationZ != Vector4{0.0F, 0.0F, 1.0F, 0.0F}))
            {
                return std::nullopt;
            }
            result.HistoryControls = {0.875F, 50.0F, 0.01F, 0.0F};
            return result;
        }
        result.CloudPositionOffset = {CloudTemporalDetail::PhaseDisplacement(view.Volume.PatternOrigin[0],
                                                                             old.Volume.PatternOrigin[0], patternSize),
                                      old.Volume.Geometry[0] - view.Volume.Geometry[0],
                                      CloudTemporalDetail::PhaseDisplacement(view.Volume.PatternOrigin[2],
                                                                             old.Volume.PatternOrigin[2], patternSize),
                                      0.0F};
        const float verticalPatternDisplacement = CloudTemporalDetail::PhaseDisplacement(
            view.Volume.PatternOrigin[1], old.Volume.PatternOrigin[1], patternSize);
        // Wrapped float phases lose metre precision at large authored pattern sizes. Include that
        // representational uncertainty when distinguishing vertical wind from ordinary camera motion.
        const float phaseTolerance = std::max(0.1F, 512.0F * std::numeric_limits<float>::epsilon() * patternSize);
        const auto bounded = [patternSize](const float displacement)
        { return std::isfinite(displacement) && std::abs(displacement) <= patternSize * 0.25F; };
        if (!bounded(result.CloudPositionOffset.X) || !bounded(result.CloudPositionOffset.Y) ||
            !bounded(result.CloudPositionOffset.Z) || !bounded(result.ScenePositionOffset.X) ||
            !bounded(result.ScenePositionOffset.Y) || !bounded(result.ScenePositionOffset.Z) ||
            !std::isfinite(verticalPatternDisplacement) ||
            std::abs(result.CloudPositionOffset.Y - result.ScenePositionOffset.Y) > 0.01F ||
            std::abs(verticalPatternDisplacement - result.ScenePositionOffset.Y) > phaseTolerance)
        {
            // Vertical noise advection does not move the authored height envelope rigidly. Reset until
            // a deformation-aware mapping is available; also reject anchor changes and large camera cuts.
            return std::nullopt;
        }
        if (view.Fog.Controls[0] != 0U &&
            (std::abs(result.CloudPositionOffset.X - result.ScenePositionOffset.X) > 0.01F ||
             std::abs(result.CloudPositionOffset.Y - result.ScenePositionOffset.Y) > 0.01F ||
             std::abs(result.CloudPositionOffset.Z - result.ScenePositionOffset.Z) > 0.01F))
        {
            // Stationary fog and advecting cloud density cannot share a single motion mapping.
            // Reject this history until separate-medium reconstruction is available.
            return std::nullopt;
        }
        // Eight-sample exponential accumulation. Per-pixel depth and neighborhood checks further limit reuse.
        result.HistoryControls = {0.875F, 50.0F, 0.01F, 0.0F};
        return result;
    }
} // namespace Keire::RenderBackend
