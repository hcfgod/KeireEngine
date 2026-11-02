#include "Keire/Water/WaterHydrodynamics.h"

#include "KeireInternal/Spatial/FrameRotation.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace Keire
{
    namespace
    {
        Vector3d Add(const Vector3d a, const Vector3d b) { return {a.X + b.X, a.Y + b.Y, a.Z + b.Z}; }

        Vector3d Subtract(const Vector3d a, const Vector3d b) { return {a.X - b.X, a.Y - b.Y, a.Z - b.Z}; }

        Vector3d Scale(const Vector3d value, const double factor)
        {
            return {value.X * factor, value.Y * factor, value.Z * factor};
        }

        double Dot(const Vector3d a, const Vector3d b) { return a.X * b.X + a.Y * b.Y + a.Z * b.Z; }

        Vector3d Cross(const Vector3d a, const Vector3d b)
        {
            return {a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X};
        }

        void RequireFinite(const Vector3d value)
        {
            if (!Math::IsFinite(value))
            {
                throw std::out_of_range("Water hydrodynamic computation exceeds the finite range.");
            }
        }

        Detail::FrameRotation Rotation(const std::array<double, 4>& value)
        {
            return Detail::FrameRotation::Normalize(value[0], value[1], value[2], value[3]);
        }

        Vector3d Rotate(const Detail::FrameRotation& rotation, const Vector3d value)
        {
            const auto result = rotation.Apply({value.X, value.Y, value.Z});
            return {result[0], result[1], result[2]};
        }

        Vector3d ToLocal(const std::array<Vector3d, 3>& axes, const Vector3d value)
        {
            const Vector3d result{Dot(axes[0], value), Dot(axes[1], value), Dot(axes[2], value)};
            RequireFinite(result);
            return result;
        }

        Vector3d ToSimulation(const std::array<Vector3d, 3>& axes, const Vector3d value)
        {
            const auto result = Add(Add(Scale(axes[0], value.X), Scale(axes[1], value.Y)), Scale(axes[2], value.Z));
            RequireFinite(result);
            return result;
        }

        double FullVolume(const WaterHullProbe& probe)
        {
            const double radius = probe.RadiusMetres;
            return (4.0 / 3.0) * std::numbers::pi_v<double> * radius * radius * radius * probe.VolumeScale;
        }

        struct Immersion
        {
            double Volume = 0.0;
            double Centroid = 0.0;
        };

        // A spherical probe clipped between tangent surface/bed planes. Finite shoreline and curved-surface
        // intersection remain local approximations; their error decreases with smaller probes.
        Immersion MeasureImmersion(const WaterMediumSample& medium, const WaterHullProbe& probe)
        {
            const double radius = probe.RadiusMetres;
            const double distance = medium.Surface.SignedDistanceMetres;
            const double upper = std::clamp(-distance / radius, -1.0, 1.0);
            const double lower = std::clamp((-distance - medium.DepthMetres) / radius, -1.0, 1.0);
            if (upper <= lower || probe.VolumeScale == 0.0)
            {
                return {};
            }
            const double sliceFactor = 1.0 - (upper * upper + upper * lower + lower * lower) / 3.0;
            const double volume = std::numbers::pi_v<double> * radius * radius * radius * (upper - lower) *
                                  sliceFactor * probe.VolumeScale;
            const double firstMoment = (upper + lower) * (0.5 - (upper * upper + lower * lower) * 0.25);
            const double centroid = radius * std::clamp(firstMoment / sliceFactor, lower, upper);
            if (!std::isfinite(volume) || !std::isfinite(centroid) || volume <= 0.0)
            {
                throw std::out_of_range("Water probe immersion exceeds the finite range.");
            }
            return {volume, centroid};
        }

        std::array<double, 9> ValidateWorldInverseInertia(const std::array<double, 9>& tensor)
        {
            // Native tensors originate in float arithmetic; tolerate only relative roundoff.
            constexpr double nativeTolerance = 1.0e-6;
            double scale = 0.0;
            for (const double value : tensor)
            {
                if (!std::isfinite(value))
                {
                    throw std::invalid_argument("Water world inverse inertia must be finite.");
                }
                scale = std::max(scale, std::abs(value));
            }
            if (scale == 0.0)
            {
                return tensor;
            }
            std::array<std::array<double, 3>, 3> matrix{};
            auto symmetric = tensor;
            for (std::size_t row = 0; row < 3; ++row)
            {
                for (std::size_t column = row; column < 3; ++column)
                {
                    const auto first = column * 3 + row;
                    const auto second = row * 3 + column;
                    const double a = tensor[first] / scale;
                    const double b = tensor[second] / scale;
                    if (std::abs(a - b) > nativeTolerance)
                    {
                        throw std::invalid_argument("Water world inverse inertia must be symmetric.");
                    }
                    matrix[row][column] = matrix[column][row] = (a + b) * 0.5;
                    symmetric[first] = symmetric[second] = tensor[first] * 0.5 + tensor[second] * 0.5;
                }
            }
            // Jacobi eigenvalues handle singular locked axes without zero Cholesky pivots.
            for (std::size_t iteration = 0; iteration < 32; ++iteration)
            {
                std::size_t p = 0;
                std::size_t q = 1;
                for (std::size_t row = 0; row < 3; ++row)
                {
                    for (std::size_t column = row + 1; column < 3; ++column)
                    {
                        if (std::abs(matrix[row][column]) > std::abs(matrix[p][q]))
                        {
                            p = row;
                            q = column;
                        }
                    }
                }
                if (std::abs(matrix[p][q]) <= 1.0e-12)
                {
                    break;
                }
                const double angle = 0.5 * std::atan2(2.0 * matrix[p][q], matrix[q][q] - matrix[p][p]);
                const double cosine = std::cos(angle);
                const double sine = std::sin(angle);
                const double pp = matrix[p][p];
                const double qq = matrix[q][q];
                const double pq = matrix[p][q];
                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    if (axis == p || axis == q)
                    {
                        continue;
                    }
                    const double ap = matrix[axis][p];
                    const double aq = matrix[axis][q];
                    matrix[axis][p] = matrix[p][axis] = cosine * ap - sine * aq;
                    matrix[axis][q] = matrix[q][axis] = sine * ap + cosine * aq;
                }
                matrix[p][p] = cosine * cosine * pp - 2.0 * sine * cosine * pq + sine * sine * qq;
                matrix[q][q] = sine * sine * pp + 2.0 * sine * cosine * pq + cosine * cosine * qq;
                matrix[p][q] = matrix[q][p] = 0.0;
            }
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                if (matrix[axis][axis] < -nativeTolerance)
                {
                    throw std::invalid_argument("Water world inverse inertia must be positive semidefinite.");
                }
            }
            return symmetric;
        }

        Vector3d ApplyWorldInverseInertia(const std::array<double, 9>& tensor, const Vector3d value)
        {
            const Vector3d result{tensor[0] * value.X + tensor[3] * value.Y + tensor[6] * value.Z,
                                  tensor[1] * value.X + tensor[4] * value.Y + tensor[7] * value.Z,
                                  tensor[2] * value.X + tensor[5] * value.Y + tensor[8] * value.Z};
            RequireFinite(result);
            return result;
        }
        Vector3d ApplyInverseInertia(const Detail::FrameRotation& rotation, const Vector3d inverseInertia,
                                     const Vector3d value)
        {
            const auto local = Rotate(rotation.Inverse(), value);
            return Rotate(rotation,
                          {local.X * inverseInertia.X, local.Y * inverseInertia.Y, local.Z * inverseInertia.Z});
        }
    } // namespace

    WaterMedium::WaterMedium(AssetId identity, WaterSurfaceDefinition definition,
                             std::span<const WaterWaveDefinition> waves, const std::int32_t priority,
                             const std::uint32_t layer)
        : m_Identity(identity), m_Definition(std::move(definition)), m_Waves(waves), m_Priority(priority),
          m_Layer(layer)
    {
        if (!identity || layer >= 32)
        {
            throw std::invalid_argument("Water medium requires a stable identity and a layer in [0, 31].");
        }
        if (m_Definition.Shape == WaterSurfaceShape::Sphere && !waves.empty())
        {
            throw std::invalid_argument("Spherical water does not support planar directional waves.");
        }
        const WaterSurface validated(m_Definition);
        m_Definition = validated.Definition();
        const auto rotation = Rotation(m_Definition.Rotation);
        m_Axes = {Rotate(rotation, {1.0, 0.0, 0.0}), Rotate(rotation, {0.0, 1.0, 0.0}),
                  Rotate(rotation, {0.0, 0.0, 1.0})};
    }

    std::optional<WaterMediumSample> WaterMedium::Sample(const Vector3d queryOrigin, const Vector3d offset,
                                                         const double timeSeconds) const
    {
        if (!Math::IsFinite(queryOrigin) || !Math::IsFinite(offset) || !std::isfinite(timeSeconds))
        {
            throw std::invalid_argument("Water medium sample requires finite coordinates and time.");
        }
        const auto relative = Add(Subtract(queryOrigin, m_Definition.Origin), offset);
        RequireFinite(relative);
        WaterMediumSample result;
        Vector3d particleVelocity;

        if (m_Definition.Shape == WaterSurfaceShape::Sphere)
        {
            const double radius = std::hypot(relative.X, relative.Y, relative.Z);
            if (!std::isfinite(radius))
            {
                throw std::out_of_range("Water sphere radius exceeds the finite range.");
            }
            if (radius == 0.0)
            {
                return std::nullopt;
            }
            result.Surface.Normal = Scale(relative, 1.0 / radius);
            result.Surface.SignedDistanceMetres = radius - m_Definition.RadiusMetres;
            result.DepthMetres = std::min(m_Definition.DepthMetres, m_Definition.RadiusMetres);
        }
        else
        {
            const auto local = ToLocal(m_Axes, relative);
            if (m_Definition.Shape != WaterSurfaceShape::InfinitePlane)
            {
                const double x = std::abs(local.X) / m_Definition.HalfExtentMetres[0];
                const double z = std::abs(local.Z) / m_Definition.HalfExtentMetres[1];
                if (x > 1.0 || z > 1.0 || (m_Definition.Shape == WaterSurfaceShape::Ellipse && std::hypot(x, z) > 1.0))
                {
                    return std::nullopt;
                }
            }
            const auto wave = m_Waves.SampleSurface({local.X, 0.0, local.Z}, timeSeconds);
            result.DepthMetres = (wave.Displacement.Y + m_Definition.DepthMetres) * wave.Normal.Y;
            if (result.DepthMetres <= 0.0)
            {
                return std::nullopt;
            }
            result.Surface.Normal = ToSimulation(m_Axes, wave.Normal);
            result.Surface.SignedDistanceMetres = (local.Y - wave.Displacement.Y) * wave.Normal.Y;

            particleVelocity = ToSimulation(m_Axes, wave.Velocity);
        }
        auto& sample = result.Surface;
        sample.SurfaceOffset = Scale(sample.Normal, -sample.SignedDistanceMetres);
        sample.Velocity = Add(Add(m_Definition.FrameLinearVelocity, Cross(m_Definition.FrameAngularVelocity, relative)),
                              Add(ToSimulation(m_Axes, m_Definition.FlowVelocityLocal), particleVelocity));
        result.FrameAngularVelocity = m_Definition.FrameAngularVelocity;
        sample.DensityKilogramsPerCubicMetre = m_Definition.DensityKilogramsPerCubicMetre;
        sample.WithinFluid = sample.SignedDistanceMetres <= 0.0 && sample.SignedDistanceMetres >= -result.DepthMetres;
        RequireFinite(sample.SurfaceOffset);
        RequireFinite(sample.Velocity);
        if (!std::isfinite(sample.SignedDistanceMetres) || !std::isfinite(result.DepthMetres))
        {
            throw std::out_of_range("Water depth exceeds the finite range.");
        }
        return result;
    }

    void ValidateWaterHydrodynamicSettings(const WaterHydrodynamicSettings& settings)
    {
        if (settings.Probes.empty() || settings.Probes.size() > WaterHydrodynamicSettings::MaximumProbes ||
            !std::isfinite(settings.BuoyancyScale) || settings.BuoyancyScale < 0.0 ||
            !std::isfinite(settings.DragRatePerSecond) || settings.DragRatePerSecond < 0.0)
        {
            throw std::invalid_argument("Buoyancy requires 1-32 probes and finite nonnegative force scales.");
        }
        double volume = 0.0;
        for (const auto& probe : settings.Probes)
        {
            if (!Math::IsFinite(probe.Offset) || !std::isfinite(probe.RadiusMetres) || probe.RadiusMetres <= 0.0 ||
                !std::isfinite(probe.VolumeScale) || probe.VolumeScale < 0.0)
            {
                throw std::invalid_argument(
                    "Water hull probe requires finite offset, positive radius and weight >= 0.");
            }
            volume += FullVolume(probe);
        }
        if (!std::isfinite(volume) || volume <= 0.0)
        {
            throw std::invalid_argument("Water hull total displaced volume must be finite and positive.");
        }
    }

    WaterBodyLoads EvaluateWaterBodyLoads(const WaterBodyKinematics& body, const WaterHydrodynamicSettings& settings,
                                          const std::span<const WaterMedium> media,
                                          const std::span<const Vector3d> probeGravity, const double timeSeconds,
                                          const double deltaSeconds)
    {
        ValidateWaterHydrodynamicSettings(settings);
        if (!Math::IsFinite(body.CenterOfMass) || !Math::IsFinite(body.LinearVelocity) ||
            !Math::IsFinite(body.AngularVelocity) || !std::isfinite(body.InverseMass) || body.InverseMass <= 0.0 ||
            !std::isfinite(timeSeconds) || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0 ||
            probeGravity.size() != settings.Probes.size())
        {
            throw std::invalid_argument("Water force preparation requires finite dynamic body state and positive dt.");
        }
        for (const auto gravity : probeGravity)
        {
            if (!Math::IsFinite(gravity))
            {
                throw std::invalid_argument("Water probe gravity must be finite.");
            }
        }
        for (std::size_t index = 0; index < media.size(); ++index)
        {
            for (std::size_t other = 0; other < index; ++other)
            {
                if (media[index].Identity() == media[other].Identity())
                {
                    throw std::invalid_argument("Water media snapshot contains duplicate identities.");
                }
            }
        }
        const auto bodyRotation = Rotation(body.BodyRotation);
        std::optional<std::array<double, 9>> worldInverseInertia;
        auto inertiaRotation = Rotation({0.0, 0.0, 0.0, 1.0});
        if (body.WorldInverseInertia)
        {
            worldInverseInertia = ValidateWorldInverseInertia(*body.WorldInverseInertia);
        }
        else
        {
            if (!Math::IsFinite(body.InverseInertia) || body.InverseInertia.X < 0.0 || body.InverseInertia.Y < 0.0 ||
                body.InverseInertia.Z < 0.0)
            {
                throw std::invalid_argument("Water principal inverse inertia must be finite and nonnegative.");
            }
            inertiaRotation = Rotation(body.InertiaRotation);
        }
        double hullVolume = 0.0;
        for (const auto& probe : settings.Probes)
        {
            hullVolume += FullVolume(probe);
        }
        const double dragDecay = -std::expm1(-settings.DragRatePerSecond * deltaSeconds);
        WaterBodyLoads result;
        for (std::size_t index = 0; index < settings.Probes.size(); ++index)
        {
            const auto& probe = settings.Probes[index];
            if (probe.VolumeScale == 0.0)
            {
                continue;
            }
            const auto probeOffset = Rotate(bodyRotation, probe.Offset);
            const WaterMedium* selected = nullptr;
            WaterMediumSample selectedSample;
            Immersion selectedImmersion;
            for (const auto& candidate : media)
            {
                if ((settings.WaterLayerMask & (std::uint32_t{1} << candidate.Layer())) == 0)
                {
                    continue;
                }
                const auto sample = candidate.Sample(body.CenterOfMass, probeOffset, timeSeconds);
                if (!sample)
                {
                    continue;
                }
                const auto immersion = MeasureImmersion(*sample, probe);
                if (immersion.Volume == 0.0)
                {
                    continue;
                }
                if (selected &&
                    (candidate.Priority() < selected->Priority() ||
                     (candidate.Priority() == selected->Priority() && candidate.Identity() > selected->Identity())))
                {
                    continue;
                }
                selected = &candidate;
                selectedSample = *sample;
                selectedImmersion = immersion;
            }
            if (!selected)
            {
                continue;
            }
            const auto applicationOffset =
                Add(probeOffset, Scale(selectedSample.Surface.Normal, selectedImmersion.Centroid));
            const double displacedMass =
                selectedImmersion.Volume * selectedSample.Surface.DensityKilogramsPerCubicMetre;
            auto force = Scale(probeGravity[index], -displacedMass * settings.BuoyancyScale);
            const auto pointVelocity = Add(body.LinearVelocity, Cross(body.AngularVelocity, applicationOffset));
            const auto fluidVelocity = Add(selectedSample.Surface.Velocity,
                                           Cross(selectedSample.FrameAngularVelocity,
                                                 Scale(selectedSample.Surface.Normal, selectedImmersion.Centroid)));
            const auto relativeVelocity = Subtract(fluidVelocity, pointVelocity);
            const double speed = std::hypot(relativeVelocity.X, relativeVelocity.Y, relativeVelocity.Z);
            if (!std::isfinite(speed))
            {
                throw std::out_of_range("Relative water velocity exceeds the finite range.");
            }
            if (speed > 0.0 && dragDecay > 0.0)
            {
                const auto direction = Scale(relativeVelocity, 1.0 / speed);
                const auto angularDirection = Cross(applicationOffset, direction);
                const auto angularResponse =
                    worldInverseInertia ? ApplyWorldInverseInertia(*worldInverseInertia, angularDirection)
                                        : ApplyInverseInertia(inertiaRotation, body.InverseInertia, angularDirection);
                const double angularInverseMass = Dot(angularDirection, angularResponse);
                if (!std::isfinite(angularInverseMass))
                {
                    throw std::out_of_range("Water drag angular effective mass exceeds the finite range.");
                }
                // Tolerated native roundoff must not reduce the translational contribution.
                const double inverseEffectiveMass = body.InverseMass + std::max(0.0, angularInverseMass);
                if (!std::isfinite(inverseEffectiveMass) || inverseEffectiveMass <= 0.0)
                {
                    throw std::out_of_range("Water drag effective point mass exceeds the finite range.");
                }
                // Immersed / full hull volume combines normalized probe weights and each immersion fraction.
                const double dragFactor =
                    (dragDecay / deltaSeconds) * (selectedImmersion.Volume / hullVolume) / inverseEffectiveMass;
                force = Add(force, Scale(relativeVelocity, dragFactor));
            }
            RequireFinite(force);
            result.ForceNewtons = Add(result.ForceNewtons, force);
            result.TorqueNewtonMetres = Add(result.TorqueNewtonMetres, Cross(applicationOffset, force));
            result.SubmergedVolumeCubicMetres += selectedImmersion.Volume;
            ++result.WetProbeCount;
        }
        RequireFinite(result.ForceNewtons);
        RequireFinite(result.TorqueNewtonMetres);
        if (!std::isfinite(result.SubmergedVolumeCubicMetres))
        {
            throw std::out_of_range("Total water displacement exceeds the finite range.");
        }
        return result;
    }
} // namespace Keire
