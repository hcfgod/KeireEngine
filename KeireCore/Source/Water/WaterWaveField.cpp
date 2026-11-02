#include "Keire/Water/WaterWaveField.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace Keire
{
    namespace
    {
        constexpr double FullTurn = 2.0 * std::numbers::pi_v<double>;
        constexpr double MaximumHorizontalSlope = 0.8;

        void RequireFinite(const Vector3d value)
        {
            if (!Math::IsFinite(value))
            {
                throw std::out_of_range("Water wave evaluation exceeds its finite range.");
            }
        }
    } // namespace

    WaterWaveField::WaterWaveField(const std::span<const WaterWaveDefinition> waves, const Vector3d origin)
        : m_Origin(origin)
    {
        if (waves.size() > MaximumWaves || !Math::IsFinite(origin))
        {
            throw std::invalid_argument("Water wave field requires a finite origin and at most 16 waves.");
        }
        double slope = 0.0;
        for (const auto& definition : waves)
        {
            if (!std::isfinite(definition.WavelengthMetres) || definition.WavelengthMetres <= 0.0 ||
                !std::isfinite(definition.AmplitudeMetres) || definition.AmplitudeMetres < 0.0 ||
                !std::isfinite(definition.SpeedMetresPerSecond) || !std::isfinite(definition.DirectionDegrees) ||
                !std::isfinite(definition.PhaseDegrees) || !std::isfinite(definition.Steepness) ||
                definition.Steepness < 0.0 || definition.Steepness > 1.0)
            {
                throw std::invalid_argument("Water wave dimensions, direction, speed or steepness are invalid.");
            }
            if (definition.AmplitudeMetres == 0.0)
            {
                continue;
            }
            auto& wave = m_Waves[m_Count++];
            const double direction = std::remainder(definition.DirectionDegrees, 360.0) * FullTurn / 360.0;
            wave.Wavelength = definition.WavelengthMetres;
            wave.Amplitude = definition.AmplitudeMetres;
            wave.Speed = definition.SpeedMetresPerSecond;
            wave.DirectionX = std::cos(direction);
            wave.DirectionZ = std::sin(direction);
            wave.Phase = std::remainder(definition.PhaseDegrees, 360.0) * FullTurn / 360.0;
            wave.WaveNumber = FullTurn / wave.Wavelength;
            wave.HorizontalAmplitude = wave.Amplitude * definition.Steepness;
            slope += wave.HorizontalAmplitude * wave.WaveNumber;
            if (!std::isfinite(slope) || !std::isfinite(wave.WaveNumber) ||
                (wave.Speed != 0.0 && wave.Wavelength / std::abs(wave.Speed) == 0.0) ||
                !std::isfinite(wave.Amplitude * wave.WaveNumber) ||
                !std::isfinite(wave.Amplitude * wave.WaveNumber * wave.Speed))
            {
                throw std::out_of_range("Water wave derivatives exceed their finite range.");
            }
        }
        if (slope > MaximumHorizontalSlope)
        {
            m_HorizontalScale = MaximumHorizontalSlope / slope;
            for (std::size_t index = 0; index < m_Count; ++index)
            {
                m_Waves[index].HorizontalAmplitude *= m_HorizontalScale;
            }
        }
    }

    double WaterWaveField::HorizontalScale() const noexcept { return m_HorizontalScale; }

    double WaterWaveField::EvaluatePhase(const Wave& wave, const Vector3d position, const double timeSeconds)
    {
        const double distance = wave.DirectionX * position.X + wave.DirectionZ * position.Z;
        double travel = 0.0;
        if (wave.Speed != 0.0)
        {
            const double period = wave.Wavelength / std::abs(wave.Speed);
            travel =
                std::isfinite(period) ? std::remainder(timeSeconds, period) * wave.Speed : timeSeconds * wave.Speed;
        }
        const double phase = (std::remainder(distance, wave.Wavelength) - travel) * wave.WaveNumber + wave.Phase;
        if (!std::isfinite(phase))
        {
            throw std::out_of_range("Water wave phase exceeds its finite range.");
        }
        return std::remainder(phase, FullTurn);
    }

    WaterWaveRenderPatch WaterWaveField::CaptureRenderPatch(const Vector3d origin, const double timeSeconds) const
    {
        static_assert(sizeof(WaterWaveShaderEntry) == 32);
        static_assert(offsetof(WaterWaveShaderParameters, Waves) == 16);
        static_assert(sizeof(WaterWaveShaderParameters) == 528);
        if (!Math::IsFinite(origin) || !std::isfinite(timeSeconds))
        {
            throw std::invalid_argument("Water wave render origin and time must be finite.");
        }
        const Vector3d relative{origin.X - m_Origin.X, origin.Y - m_Origin.Y, origin.Z - m_Origin.Z};
        RequireFinite(relative);
        const auto narrow = [](const double value)
        {
            constexpr double maximum = std::numeric_limits<float>::max();
            if (!std::isfinite(value) || std::abs(value) > maximum)
            {
                throw std::out_of_range("Water wave coefficient exceeds the shader range.");
            }
            const float result = static_cast<float>(value);
            if (value != 0.0 && result == 0.0F)
            {
                throw std::out_of_range("Water wave coefficient is below the shader range.");
            }
            return result;
        };
        WaterWaveRenderPatch result;
        result.Origin = origin;
        result.Parameters.Count = static_cast<std::uint32_t>(m_Count);
        double totalSlope = 0.0;
        double totalAmplitude = 0.0;
        double totalVelocity = 0.0;
        for (std::size_t index = 0; index < m_Count; ++index)
        {
            const auto& wave = m_Waves[index];
            const double verticalVelocity = wave.Amplitude * wave.WaveNumber * wave.Speed;
            const double horizontalVelocity = wave.HorizontalAmplitude * wave.WaveNumber * wave.Speed;
            totalSlope += wave.Amplitude * wave.WaveNumber;
            totalAmplitude += wave.Amplitude + wave.HorizontalAmplitude;
            totalVelocity += std::abs(verticalVelocity) + std::abs(horizontalVelocity);
            result.Parameters.Waves[index] = {{narrow(wave.DirectionX), narrow(wave.DirectionZ),
                                               narrow(wave.WaveNumber),
                                               narrow(EvaluatePhase(wave, relative, timeSeconds))},
                                              {narrow(wave.Amplitude), narrow(wave.HorizontalAmplitude),
                                               narrow(verticalVelocity), narrow(horizontalVelocity)}};
        }
        // Allow headroom for tangent products and the squared normal length, not just coefficients.
        const double maximumSlope = std::pow(static_cast<double>(std::numeric_limits<float>::max()), 0.25) / 8.0;
        if (!std::isfinite(totalSlope) || totalSlope > maximumSlope)
        {
            throw std::out_of_range("Water wave slopes exceed the shader normal range.");
        }
        (void)narrow(totalAmplitude);
        (void)narrow(totalVelocity);
        return result;
    }

    WaterWaveSample WaterWaveField::SampleRelative(const Vector3d position, const double timeSeconds) const
    {
        WaterWaveSample result;
        Vector3d tangentX{1.0, 0.0, 0.0};
        Vector3d tangentZ{0.0, 0.0, 1.0};
        for (std::size_t index = 0; index < m_Count; ++index)
        {
            const auto& wave = m_Waves[index];
            const double phase = EvaluatePhase(wave, position, timeSeconds);
            const double sine = std::sin(phase);
            const double cosine = std::cos(phase);
            const double lateral = wave.HorizontalAmplitude * cosine;
            result.Displacement.X += wave.DirectionX * lateral;
            result.Displacement.Y += wave.Amplitude * sine;
            result.Displacement.Z += wave.DirectionZ * lateral;
            const double horizontalDerivative = -wave.HorizontalAmplitude * wave.WaveNumber * sine;
            const double verticalDerivative = wave.Amplitude * wave.WaveNumber * cosine;
            tangentX.X += horizontalDerivative * wave.DirectionX * wave.DirectionX;
            tangentX.Y += verticalDerivative * wave.DirectionX;
            tangentX.Z += horizontalDerivative * wave.DirectionX * wave.DirectionZ;
            tangentZ.X += horizontalDerivative * wave.DirectionX * wave.DirectionZ;
            tangentZ.Y += verticalDerivative * wave.DirectionZ;
            tangentZ.Z += horizontalDerivative * wave.DirectionZ * wave.DirectionZ;
            const double horizontalVelocity = -horizontalDerivative * wave.Speed;
            result.Velocity.X += wave.DirectionX * horizontalVelocity;
            result.Velocity.Y -= verticalDerivative * wave.Speed;
            result.Velocity.Z += wave.DirectionZ * horizontalVelocity;
        }
        const Vector3d normal{tangentZ.Y * tangentX.Z - tangentZ.Z * tangentX.Y,
                              tangentZ.Z * tangentX.X - tangentZ.X * tangentX.Z,
                              tangentZ.X * tangentX.Y - tangentZ.Y * tangentX.X};
        const double length = std::hypot(normal.X, normal.Y, normal.Z);
        if (!std::isfinite(length) || length <= 0.0)
        {
            throw std::out_of_range("Water wave normal exceeds its finite range.");
        }
        result.Normal = {normal.X / length, normal.Y / length, normal.Z / length};
        RequireFinite(result.Displacement);
        RequireFinite(result.Velocity);
        return result;
    }

    WaterWaveSample WaterWaveField::SampleParametric(const Vector3d position, const double timeSeconds) const
    {
        if (!Math::IsFinite(position) || !std::isfinite(timeSeconds))
        {
            throw std::invalid_argument("Water wave position and time must be finite.");
        }
        const Vector3d relative{position.X - m_Origin.X, position.Y - m_Origin.Y, position.Z - m_Origin.Z};
        RequireFinite(relative);
        return SampleRelative(relative, timeSeconds);
    }

    WaterWaveSample WaterWaveField::SampleSurface(const Vector3d position, const double timeSeconds) const
    {
        if (!Math::IsFinite(position) || !std::isfinite(timeSeconds))
        {
            throw std::invalid_argument("Water wave position and time must be finite.");
        }
        const Vector3d relative{position.X - m_Origin.X, position.Y - m_Origin.Y, position.Z - m_Origin.Z};
        RequireFinite(relative);
        Vector3d parameter = relative;
        constexpr std::size_t maximumIterations = 128;
        constexpr double positionToleranceMetres = 0.000001;
        for (std::size_t iteration = 0; iteration < maximumIterations; ++iteration)
        {
            auto sample = SampleRelative(parameter, timeSeconds);
            const double errorX = (parameter.X - relative.X) + sample.Displacement.X;
            const double errorZ = (parameter.Z - relative.Z) + sample.Displacement.Z;
            if (std::hypot(errorX, errorZ) <= positionToleranceMetres)
            {
                sample.Displacement.X = 0.0;
                sample.Displacement.Z = 0.0;
                return sample;
            }
            parameter.X = relative.X - sample.Displacement.X;
            parameter.Z = relative.Z - sample.Displacement.Z;
            RequireFinite(parameter);
        }
        throw std::out_of_range("Water wave query could not resolve its horizontal surface position.");
    }
} // namespace Keire
