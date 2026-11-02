#pragma once

#include "Keire/Assets/Asset.h"
#include "Keire/Water/WaterSurface.h"
#include "Keire/Water/WaterWaveField.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Keire
{
    struct WaterMediumSample
    {
        /// Surface.Velocity is fluid velocity at the query, not at the projected waterline.
        WaterSurfaceSample Surface;
        /// Rigid medium-frame angular velocity; used to transport velocity to the wetted centroid.
        Vector3d FrameAngularVelocity;
        /// Distance between the local tangent waterline and bed, along Surface.Normal.
        double DepthMetres = 0.0;
    };

    /// Immutable snapshot of one water body, already resolved into the simulation frame.
    /// Directional waves use water-local coordinates. Spherical water supports radial still water.
    class KEIRE_API WaterMedium final
    {
      public:
        WaterMedium(AssetId identity, WaterSurfaceDefinition definition, std::span<const WaterWaveDefinition> waves,
                    std::int32_t priority = 0, std::uint32_t layer = 0);

        [[nodiscard]] AssetId Identity() const noexcept { return m_Identity; }

        [[nodiscard]] std::int32_t Priority() const noexcept { return m_Priority; }

        [[nodiscard]] std::uint32_t Layer() const noexcept { return m_Layer; }

        /// Subtracts queryOrigin - waterOrigin before adding the nearby offset: no float world positions.
        /// A finite shoreline tests the query center; probe/shore intersection is an approximation.
        /// A wave uses a local tangent slab over the authored bed. No sample when its crest is below the bed.
        [[nodiscard]] std::optional<WaterMediumSample> Sample(Vector3d queryOrigin, Vector3d offset,
                                                              double timeSeconds) const;

      private:
        AssetId m_Identity;
        WaterSurfaceDefinition m_Definition;
        WaterWaveField m_Waves;
        std::array<Vector3d, 3> m_Axes;
        std::int32_t m_Priority = 0;
        std::uint32_t m_Layer = 0;
    };

    struct WaterHullProbe
    {
        /// Metres in body-local coordinates, measured from the center of mass; Transform scale is not applied.
        Vector3d Offset;
        double RadiusMetres = 0.5;
        double VolumeScale = 1.0;

        bool operator==(const WaterHullProbe&) const = default;
    };

    struct WaterHydrodynamicSettings
    {
        static constexpr std::size_t MaximumProbes = 32;

        std::vector<WaterHullProbe> Probes{{}};
        std::uint32_t WaterLayerMask = 0xffffffffU;
        double BuoyancyScale = 1.0;
        /// Relative point-velocity decay rate, in inverse seconds. Zero disables drag.
        double DragRatePerSecond = 1.0;

        bool operator==(const WaterHydrodynamicSettings&) const = default;
    };

    /// Captured native dynamic-body state. BodyRotation maps body-local probes; InertiaRotation maps principal inertia
    /// axes.
    struct WaterBodyKinematics
    {
        Vector3d CenterOfMass;
        std::array<double, 4> BodyRotation{0.0, 0.0, 0.0, 1.0};
        std::array<double, 4> InertiaRotation{0.0, 0.0, 0.0, 1.0};
        Vector3d LinearVelocity;
        Vector3d AngularVelocity;
        double InverseMass = 1.0;
        Vector3d InverseInertia{1.0, 1.0, 1.0};
        /// Optional authoritative column-major simulation-axis tensor, including native angular DOF masks.
        /// When present, InverseInertia and InertiaRotation are unused. Must be finite, symmetric and PSD.
        std::optional<std::array<double, 9>> WorldInverseInertia;
    };

    struct WaterBodyLoads
    {
        Vector3d ForceNewtons;
        Vector3d TorqueNewtonMetres;
        double SubmergedVolumeCubicMetres = 0.0;
        std::uint32_t WetProbeCount = 0;
    };

    KEIRE_API void ValidateWaterHydrodynamicSettings(const WaterHydrodynamicSettings& settings);

    /// Pure preparation, never submits native forces. Highest priority wins per wet probe; equal priority
    /// chooses the lowest stable identity, so overlapping media never multiply density. Gravity is sampled
    /// separately at each probe by the scene integration and supplied in probe order.
    /// Drag uses effective point mass and expm1 decay, bounded across normalized probe volume weights.
    [[nodiscard]] KEIRE_API WaterBodyLoads EvaluateWaterBodyLoads(const WaterBodyKinematics& body,
                                                                  const WaterHydrodynamicSettings& settings,
                                                                  std::span<const WaterMedium> media,
                                                                  std::span<const Vector3d> probeGravity,
                                                                  double timeSeconds, double deltaSeconds);
} // namespace Keire
