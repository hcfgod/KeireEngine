#pragma once

#include "Keire/Api.h"

#include <chrono>
#include <compare>
#include <cstdint>

namespace Keire
{
    class TimeStep final
    {
      public:
        constexpr TimeStep() noexcept = default;

        [[nodiscard]] static constexpr TimeStep FromSeconds(const double seconds) noexcept { return TimeStep(seconds); }
        [[nodiscard]] static constexpr TimeStep FromMilliseconds(const double milliseconds) noexcept
        {
            return TimeStep(milliseconds / 1000.0);
        }

        template <typename Rep, typename Period>
        [[nodiscard]] static TimeStep FromChrono(const std::chrono::duration<Rep, Period>& duration) noexcept
        {
            return TimeStep(std::chrono::duration<double>(duration).count());
        }

        [[nodiscard]] constexpr double Seconds() const noexcept { return m_Seconds; }
        [[nodiscard]] constexpr double Milliseconds() const noexcept { return m_Seconds * 1000.0; }
        [[nodiscard]] constexpr std::chrono::duration<double> Chrono() const noexcept
        {
            return std::chrono::duration<double>(m_Seconds);
        }

        [[nodiscard]] constexpr TimeStep operator+() const noexcept { return *this; }
        [[nodiscard]] constexpr TimeStep operator-() const noexcept { return TimeStep(-m_Seconds); }
        [[nodiscard]] constexpr TimeStep operator+(const TimeStep other) const noexcept
        {
            return TimeStep(m_Seconds + other.m_Seconds);
        }
        [[nodiscard]] constexpr TimeStep operator-(const TimeStep other) const noexcept
        {
            return TimeStep(m_Seconds - other.m_Seconds);
        }
        [[nodiscard]] constexpr TimeStep operator*(const double scalar) const noexcept
        {
            return TimeStep(m_Seconds * scalar);
        }
        [[nodiscard]] constexpr TimeStep operator/(const double scalar) const noexcept
        {
            return TimeStep(m_Seconds / scalar);
        }
        [[nodiscard]] constexpr double operator/(const TimeStep other) const noexcept
        {
            return m_Seconds / other.m_Seconds;
        }
        constexpr TimeStep& operator+=(const TimeStep other) noexcept
        {
            m_Seconds += other.m_Seconds;
            return *this;
        }
        constexpr TimeStep& operator-=(const TimeStep other) noexcept
        {
            m_Seconds -= other.m_Seconds;
            return *this;
        }
        [[nodiscard]] auto operator<=>(const TimeStep&) const noexcept = default;

      private:
        explicit constexpr TimeStep(const double seconds) noexcept : m_Seconds(seconds) {}

        double m_Seconds = 0.0;
    };

    [[nodiscard]] constexpr TimeStep operator*(const double scalar, const TimeStep step) noexcept
    {
        return step * scalar;
    }

    struct TimeSpecification
    {
        TimeStep FixedDeltaTime = TimeStep::FromSeconds(1.0 / 60.0);
        TimeStep MaximumDeltaTime = TimeStep::FromMilliseconds(250.0);
        std::uint32_t MaximumFixedStepsPerFrame = 8;
        TimeStep SmoothingTimeConstant = TimeStep::FromMilliseconds(100.0);
    };

    class KEIRE_API Time final
    {
      public:
        explicit Time(const TimeSpecification& specification = {});

        void Reset() noexcept;
        void AdvanceFrame(TimeStep rawDeltaTime, bool suspendSimulation = false);
        [[nodiscard]] bool ConsumeFixedStep() noexcept;

        void SetTimeScale(double scale);
        [[nodiscard]] double TimeScale() const noexcept { return m_TimeScale; }
        void SetPaused(bool paused) noexcept { m_Paused = paused; }
        [[nodiscard]] bool Paused() const noexcept { return m_Paused; }

        [[nodiscard]] TimeStep RawDeltaTime() const noexcept { return m_RawDeltaTime; }
        [[nodiscard]] TimeStep UnscaledDeltaTime() const noexcept { return m_UnscaledDeltaTime; }
        [[nodiscard]] TimeStep DeltaTime() const noexcept { return m_DeltaTime; }
        [[nodiscard]] TimeStep SmoothDeltaTime() const noexcept { return m_SmoothDeltaTime; }
        [[nodiscard]] TimeStep FixedDeltaTime() const noexcept { return m_Specification.FixedDeltaTime; }
        [[nodiscard]] TimeStep RealtimeSinceStartup() const noexcept { return m_RealtimeSinceStartup; }
        [[nodiscard]] TimeStep UnscaledTime() const noexcept { return m_UnscaledTime; }
        [[nodiscard]] TimeStep TimeSinceStartup() const noexcept { return m_TimeSinceStartup; }
        [[nodiscard]] TimeStep FixedTime() const noexcept { return m_FixedTime; }
        [[nodiscard]] TimeStep DroppedSimulationTime() const noexcept { return m_DroppedSimulationTime; }
        [[nodiscard]] double InterpolationAlpha() const noexcept { return m_InterpolationAlpha; }
        [[nodiscard]] std::uint64_t FrameCount() const noexcept { return m_FrameCount; }
        [[nodiscard]] std::uint64_t FixedTickCount() const noexcept { return m_FixedTickCount; }
        [[nodiscard]] std::uint32_t PendingFixedSteps() const noexcept { return m_PendingFixedSteps; }

      private:
        TimeSpecification m_Specification;
        double m_TimeScale = 1.0;
        bool m_Paused = false;
        bool m_HasSmoothedSample = false;

        TimeStep m_RawDeltaTime;
        TimeStep m_UnscaledDeltaTime;
        TimeStep m_DeltaTime;
        TimeStep m_SmoothedUnscaledDeltaTime;
        TimeStep m_SmoothDeltaTime;
        TimeStep m_RealtimeSinceStartup;
        TimeStep m_UnscaledTime;
        TimeStep m_TimeSinceStartup;
        TimeStep m_FixedTime;
        TimeStep m_FixedAccumulator;
        TimeStep m_DroppedSimulationTime;
        double m_InterpolationAlpha = 0.0;
        std::uint64_t m_FrameCount = 0;
        std::uint64_t m_FixedTickCount = 0;
        std::uint32_t m_PendingFixedSteps = 0;
    };
} // namespace Keire
