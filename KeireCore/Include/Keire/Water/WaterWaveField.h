#pragma once

#include "Keire/Api.h"
#include "Keire/Math/Vector3d.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Keire
{
    struct WaterWaveDefinition
    {
        double WavelengthMetres = 10.0;
        double AmplitudeMetres = 0.1;
        double SpeedMetresPerSecond = 3.0;
        double DirectionDegrees = 0.0;
        double PhaseDegrees = 0.0;
        double Steepness = 0.5;
    };

    struct WaterWaveSample
    {
        Vector3d Displacement;
        Vector3d Normal{0.0, 1.0, 0.0};
        Vector3d Velocity;
    };

    struct alignas(16) WaterWaveShaderEntry
    {
        // X/Z direction, radians per metre, phase at the patch origin.
        std::array<float, 4> DirectionFrequencyPhase{};
        // Vertical/horizontal amplitude, vertical/horizontal particle velocity coefficient.
        std::array<float, 4> AmplitudesVelocity{};
    };

    struct alignas(16) WaterWaveShaderParameters
    {
        std::uint32_t Count = 0;
        std::array<std::uint32_t, 3> Padding{};
        std::array<WaterWaveShaderEntry, 16> Waves{};
    };

    struct WaterWaveRenderPatch
    {
        Vector3d Origin;
        WaterWaveShaderParameters Parameters;
    };

    /// Immutable directional Gerstner field in a water-local X/Z plane (+Y up).
    /// This is the shared mathematical layer; GPU rendering and curved-ocean mapping are separate.
    class KEIRE_API WaterWaveField final
    {
      public:
        static constexpr std::size_t MaximumWaves = 16;

        explicit WaterWaveField(std::span<const WaterWaveDefinition> waves, Vector3d origin = {});

        /// Evaluate an undisplaced mesh vertex. Subtracts the double-precision origin before phase evaluation.
        [[nodiscard]] WaterWaveSample SampleParametric(Vector3d position, double timeSeconds) const;

        /// Invert horizontal displacement for physics queries at a fixed X/Z location.
        /// Returned displacement is vertical only; velocity is the surface particle velocity.
        /// Throws if a finite, micrometre-accurate inverse cannot be found within the bounded iteration count.
        [[nodiscard]] WaterWaveSample SampleSurface(Vector3d position, double timeSeconds) const;

        /// Captures phase in double precision; shaders receive only nearby X/Z offsets from Origin.
        /// Does not upload resources. Rejects coefficients unsafe for the float shader implementation.
        [[nodiscard]] WaterWaveRenderPatch CaptureRenderPatch(Vector3d origin, double timeSeconds) const;

        /// Common reduction of authored horizontal steepness to prevent folds; vertical amplitude is unchanged.
        [[nodiscard]] double HorizontalScale() const noexcept;

      private:
        struct Wave
        {
            double Wavelength = 0.0;
            double Amplitude = 0.0;
            double Speed = 0.0;
            double DirectionX = 0.0;
            double DirectionZ = 0.0;
            double Phase = 0.0;
            double WaveNumber = 0.0;
            double HorizontalAmplitude = 0.0;
        };

        [[nodiscard]] WaterWaveSample SampleRelative(Vector3d position, double timeSeconds) const;
        [[nodiscard]] static double EvaluatePhase(const Wave& wave, Vector3d position, double timeSeconds);

        Vector3d m_Origin;
        std::array<Wave, MaximumWaves> m_Waves{};
        std::size_t m_Count = 0;
        double m_HorizontalScale = 1.0;
    };
} // namespace Keire
