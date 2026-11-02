#include "Keire/Water/WaterSurface.h"

#include "KeireInternal/Spatial/FrameRotation.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace Keire
{
    namespace
    {
        Detail::FrameRotation SurfaceRotation(const WaterSurfaceDefinition& definition)
        {
            const auto& value = definition.Rotation;
            return Detail::FrameRotation::Normalize(value[0], value[1], value[2], value[3]);
        }

        Vector3d Rotate(const Detail::FrameRotation& rotation, const Vector3d value)
        {
            const auto result = rotation.Apply({value.X, value.Y, value.Z});
            return {result[0], result[1], result[2]};
        }

        void RequireFiniteResult(const Vector3d value)
        {
            if (!Math::IsFinite(value))
            {
                throw std::out_of_range("Water surface query exceeds the finite coordinate or velocity range.");
            }
        }

        Vector3d ToSimulation(const std::array<Vector3d, 3>& axes, const Vector3d local)
        {
            const Vector3d result{axes[0].X * local.X + axes[1].X * local.Y + axes[2].X * local.Z,
                                  axes[0].Y * local.X + axes[1].Y * local.Y + axes[2].Y * local.Z,
                                  axes[0].Z * local.X + axes[1].Z * local.Y + axes[2].Z * local.Z};
            RequireFiniteResult(result);
            return result;
        }

        Vector3d ToLocal(const std::array<Vector3d, 3>& axes, const Vector3d relative)
        {
            const auto dot = [relative](const Vector3d axis)
            { return axis.X * relative.X + axis.Y * relative.Y + axis.Z * relative.Z; };
            const Vector3d result{dot(axes[0]), dot(axes[1]), dot(axes[2])};
            RequireFiniteResult(result);
            return result;
        }
    } // namespace

    WaterSurface::WaterSurface(WaterSurfaceDefinition definition) : m_Definition(std::move(definition))
    {
        switch (m_Definition.Shape)
        {
        case WaterSurfaceShape::InfinitePlane:
        case WaterSurfaceShape::Rectangle:
        case WaterSurfaceShape::Ellipse:
        case WaterSurfaceShape::Sphere:
            break;
        default:
            throw std::invalid_argument("Water surface shape is invalid.");
        }
        if (!Math::IsFinite(m_Definition.Origin) || !Math::IsFinite(m_Definition.FlowVelocityLocal) ||
            !Math::IsFinite(m_Definition.FrameLinearVelocity) || !Math::IsFinite(m_Definition.FrameAngularVelocity))
        {
            throw std::invalid_argument("Water surface frame and flow must be finite.");
        }
        for (const double value :
             {m_Definition.HalfExtentMetres[0], m_Definition.HalfExtentMetres[1], m_Definition.RadiusMetres,
              m_Definition.DepthMetres, m_Definition.DensityKilogramsPerCubicMetre})
        {
            if (!std::isfinite(value) || value <= 0.0)
            {
                throw std::invalid_argument("Water surface dimensions and density must be finite and positive.");
            }
        }
        const auto rotation = SurfaceRotation(m_Definition);
        m_Definition.Rotation = rotation.Components();
        m_Axes = {Rotate(rotation, {1.0, 0.0, 0.0}), Rotate(rotation, {0.0, 1.0, 0.0}),
                  Rotate(rotation, {0.0, 0.0, 1.0})};
    }

    const WaterSurfaceDefinition& WaterSurface::Definition() const noexcept { return m_Definition; }

    std::optional<WaterSurfaceSample> WaterSurface::Sample(const Vector3d position) const
    {
        if (!Math::IsFinite(position))
        {
            throw std::invalid_argument("Water surface query position must be finite.");
        }
        const Vector3d relative{position.X - m_Definition.Origin.X, position.Y - m_Definition.Origin.Y,
                                position.Z - m_Definition.Origin.Z};
        RequireFiniteResult(relative);
        Vector3d normal = m_Axes[1];
        double distance = 0.0;
        if (m_Definition.Shape == WaterSurfaceShape::Sphere)
        {
            // A sphere is rotation-invariant. Avoid rotating planet-sized coordinates twice and
            // introducing orientation-dependent height error at the waterline.
            const double radius = std::hypot(relative.X, relative.Y, relative.Z);
            if (!std::isfinite(radius))
            {
                throw std::out_of_range("Water sphere query radius exceeds the finite range.");
            }
            if (radius == 0.0)
            {
                return std::nullopt;
            }
            normal = {relative.X / radius, relative.Y / radius, relative.Z / radius};
            distance = radius - m_Definition.RadiusMetres;
        }
        else
        {
            const auto local = ToLocal(m_Axes, relative);
            distance = local.Y;
            if (m_Definition.Shape != WaterSurfaceShape::InfinitePlane)
            {
                const double x = std::abs(local.X) / m_Definition.HalfExtentMetres[0];
                const double z = std::abs(local.Z) / m_Definition.HalfExtentMetres[1];
                if (x > 1.0 || z > 1.0 || (m_Definition.Shape == WaterSurfaceShape::Ellipse && std::hypot(x, z) > 1.0))
                {
                    return std::nullopt;
                }
            }
        }
        WaterSurfaceSample result;
        result.Normal = normal;
        result.SurfaceOffset = {-result.Normal.X * distance, -result.Normal.Y * distance, -result.Normal.Z * distance};
        const Vector3d surfaceRelative{relative.X + result.SurfaceOffset.X, relative.Y + result.SurfaceOffset.Y,
                                       relative.Z + result.SurfaceOffset.Z};
        RequireFiniteResult(surfaceRelative);
        const auto flow = ToSimulation(m_Axes, m_Definition.FlowVelocityLocal);
        const auto& angular = m_Definition.FrameAngularVelocity;
        const auto& linear = m_Definition.FrameLinearVelocity;
        result.Velocity = {linear.X + flow.X + angular.Y * surfaceRelative.Z - angular.Z * surfaceRelative.Y,
                           linear.Y + flow.Y + angular.Z * surfaceRelative.X - angular.X * surfaceRelative.Z,
                           linear.Z + flow.Z + angular.X * surfaceRelative.Y - angular.Y * surfaceRelative.X};
        RequireFiniteResult(result.SurfaceOffset);
        RequireFiniteResult(result.Velocity);
        result.SignedDistanceMetres = distance;
        result.DensityKilogramsPerCubicMetre = m_Definition.DensityKilogramsPerCubicMetre;
        result.WithinFluid = distance <= 0.0 && distance >= -m_Definition.DepthMetres;
        return result;
    }
} // namespace Keire
