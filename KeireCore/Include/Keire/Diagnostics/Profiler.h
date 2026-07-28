#pragma once

#include "Keire/Api.h"
#include "Keire/Ref.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    enum class ProfilerMode : std::uint8_t
    {
        Disabled,
        Enabled
    };

    enum class ProfileCategory : std::uint8_t
    {
        Application,
        Assets,
        Scripting,
        Physics,
        Animation,
        Audio,
        Navigation,
        Rendering,
        User
    };

    struct ProfilerSpecification
    {
        ProfilerMode Mode = ProfilerMode::Disabled;
        std::size_t MaximumSpansPerFrame = 16U * 1024U;
        std::size_t MaximumCountersPerFrame = 1024U;
        std::size_t MaximumRetainedFrameSummaries = 600U;
    };

    struct ProfileSpan
    {
        std::string Name;
        ProfileCategory Category = ProfileCategory::User;
        std::uint64_t Thread = 0;
        double StartMicroseconds = 0.0;
        double DurationMicroseconds = 0.0;
    };

    struct ProfileCounter
    {
        std::string Name;
        ProfileCategory Category = ProfileCategory::User;
        double Value = 0.0;
    };

    struct ProfileFrame
    {
        std::uint64_t Sequence = 0;
        double StartMicroseconds = 0.0;
        double DurationMicroseconds = 0.0;
        std::vector<ProfileSpan> Spans;
        std::vector<ProfileCounter> Counters;
        std::size_t DroppedSpans = 0;
        std::size_t DroppedCounters = 0;
        double ApplicationMicroseconds = 0.0;
        double ScriptingMicroseconds = 0.0;
        double PhysicsMicroseconds = 0.0;
        double RenderingMicroseconds = 0.0;
        double AudioMicroseconds = 0.0;
        bool Truncated = false;
    };

    struct ProfileFrameSummary
    {
        std::uint64_t Sequence = 0;
        double StartMicroseconds = 0.0;
        double DurationMicroseconds = 0.0;
        std::size_t SpanCount = 0;
        std::size_t CounterCount = 0;
        std::size_t DroppedSpans = 0;
        std::size_t DroppedCounters = 0;
        double ApplicationMicroseconds = 0.0;
        double ScriptingMicroseconds = 0.0;
        double PhysicsMicroseconds = 0.0;
        double RenderingMicroseconds = 0.0;
        double AudioMicroseconds = 0.0;
        bool Truncated = false;
    };

    class KEIRE_API Profiler final : public RefCounted
    {
      public:
        explicit Profiler(ProfilerSpecification specification = {});
        ~Profiler() override;

        Profiler(const Profiler&) = delete;
        Profiler& operator=(const Profiler&) = delete;

        void BeginFrame();
        void EndFrame();
        void RecordSpan(ProfileCategory category, std::string_view name, double startMicroseconds,
                        double durationMicroseconds) noexcept;
        void SetCounter(ProfileCategory category, std::string_view name, double value) noexcept;
        [[nodiscard]] ProfileFrame LatestFrame() const;
        [[nodiscard]] ProfileFrameSummary LatestSummary() const;
        [[nodiscard]] std::vector<ProfileFrameSummary> RecentSummaries(std::size_t maximumFrames) const;
        [[nodiscard]] bool IsOpen() const noexcept;
        void Close();

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    class KEIRE_API ProfileScope final
    {
      public:
        ProfileScope(Ref<Profiler> profiler, ProfileCategory category, std::string_view name);
        ~ProfileScope();

        ProfileScope(const ProfileScope&) = delete;
        ProfileScope& operator=(const ProfileScope&) = delete;
        ProfileScope(ProfileScope&& other) noexcept;
        ProfileScope& operator=(ProfileScope&& other) noexcept;

      private:
        void Finish() noexcept;

        Ref<Profiler> m_Target;
        ProfileCategory m_Category = ProfileCategory::Application;
        std::string m_Name;
        double m_StartMicroseconds = 0.0;
    };
} // namespace Keire
