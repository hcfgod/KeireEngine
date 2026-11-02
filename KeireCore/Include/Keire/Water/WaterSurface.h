#pragma once

#include "Keire/Api.h"
#include "Keire/Math/Vector3d.h"

#include <array>
#include <cstdint>
#include <optional>

namespace Keire
{
    enum class WaterSurfaceShape : std::uint8_t
    {
        InfinitePlane,
        Rectangle,
        Ellipse,
        Sphere
    };

    /// Undisplaced surface geometry in a single resolved simulation frame. Units are metres and seconds.
    /// Plane shapes use local X/Z coordinates with +Y up; a sphere is centered on Origin.
    struct WaterSurfaceDefinition
    {
        WaterSurfaceShape Shape = WaterSurfaceShape::Rectangle;
        Vector3d Origin;
        /// Local-to-simulation quaternion in X/Y/Z/W order. Finite nonzero inputs are normalized.
        std::array<double, 4> Rotation{0.0, 0.0, 0.0, 1.0};
        std::array<double, 2> HalfExtentMetres{50.0, 50.0};
        double RadiusMetres = 6'371'000.0;
        double DepthMetres = 100.0;
        double DensityKilogramsPerCubicMetre = 1'000.0;
        Vector3d FlowVelocityLocal;
        /// Frame velocity at Origin and angular velocity in simulation axes (radians per second).
        Vector3d FrameLinearVelocity;
        Vector3d FrameAngularVelocity;
    };

    struct WaterSurfaceSample
    {
        /// Simulation-axis displacement from the query to the closest undisplaced surface point.
        /// Relative output avoids adding a small displacement to a very large absolute coordinate.
        Vector3d SurfaceOffset;
        Vector3d Normal;
        Vector3d Velocity;
        /// Positive above the surface, negative below. Sphere distance is radial.
        double SignedDistanceMetres = 0.0;
        double DensityKilogramsPerCubicMetre = 0.0;
        bool WithinFluid = false;
    };

    /// Immutable, allocation-free sampler. Geometry/field validation happens once at construction.
    /// Waves, river splines and rendered water are separate integration layers, not implied by this type.
    class KEIRE_API WaterSurface final
    {
      public:
        explicit WaterSurface(WaterSurfaceDefinition definition);

        [[nodiscard]] const WaterSurfaceDefinition& Definition() const noexcept;

        /// Query and definition must belong to the same resolved frame. Subtraction precedes rotation.
        /// Returns no sample outside a finite footprint or at the undefined center of a sphere.
        /// Invalid input or unrepresentable intermediate/output values throw; no state is mutated.
        [[nodiscard]] std::optional<WaterSurfaceSample> Sample(Vector3d position) const;

      private:
        WaterSurfaceDefinition m_Definition;
        std::array<Vector3d, 3> m_Axes;
    };
} // namespace Keire
