#include "Keire/Diagnostics/Profiler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Keire
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        [[nodiscard]] double Microseconds(const Clock::time_point value) noexcept
        {
            return std::chrono::duration<double, std::micro>(value.time_since_epoch()).count();
        }

        [[nodiscard]] std::string EscapeJson(const std::string_view value)
        {
            std::string result;
            result.reserve(value.size());
            for (const char character : value)
            {
                switch (character)
                {
                case '\\':
                    result += "\\\\";
                    break;
                case '"':
                    result += "\\\"";
                    break;
                case '\n':
                    result += "\\n";
                    break;
                case '\r':
                    result += "\\r";
                    break;
                case '\t':
                    result += "\\t";
                    break;
                default:
                    result.push_back(character);
                    break;
                }
            }
            return result;
        }

        [[nodiscard]] std::string_view CategoryName(const ProfileCategory category) noexcept
        {
            switch (category)
            {
            case ProfileCategory::Application:
                return "Application";
            case ProfileCategory::Assets:
                return "Assets";
            case ProfileCategory::Scripting:
                return "Scripting";
            case ProfileCategory::Physics:
                return "Physics";
            case ProfileCategory::Animation:
                return "Animation";
            case ProfileCategory::Audio:
                return "Audio";
            case ProfileCategory::Navigation:
                return "Navigation";
            case ProfileCategory::Rendering:
                return "Rendering";
            case ProfileCategory::User:
                return "User";
            }
            return "Unknown";
        }
    } // namespace

    class Profiler::Impl final
    {
      public:
        explicit Impl(ProfilerSpecification value) : Specification(value), OwnerThread(std::this_thread::get_id())
        {
            if (Specification.MaximumSpansPerFrame == 0 || Specification.MaximumCountersPerFrame == 0 ||
                Specification.MaximumRetainedFrameSummaries == 0)
                throw std::invalid_argument("Profiler bounds must be greater than zero.");
            Spans.reserve(std::min<std::size_t>(Specification.MaximumSpansPerFrame, 1024U));
            Counters.reserve(std::min<std::size_t>(Specification.MaximumCountersPerFrame, 128U));
            Summaries.resize(Specification.MaximumRetainedFrameSummaries);
        }

        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != OwnerThread)
                throw std::logic_error(std::string("Profiler::") + operation + " must run on the owner thread.");
        }

        ProfilerSpecification Specification;
        std::thread::id OwnerThread;
        std::atomic<bool> Open{true};
        std::mutex Mutex;
        bool Recording = false;
        bool Truncated = false;
        std::size_t DroppedSpans = 0;
        std::size_t DroppedCounters = 0;
        std::size_t SpanCount = 0;
        std::size_t CounterCount = 0;
        std::uint64_t NextSequence = 1;
        Clock::time_point FrameStart;
        std::vector<ProfileSpan> Spans;
        std::vector<ProfileCounter> Counters;
        ProfileFrame Latest;
        std::vector<ProfileFrameSummary> Summaries;
        std::size_t SummaryWrite = 0;
        std::size_t SummaryCount = 0;
    };

    Profiler::Profiler(ProfilerSpecification specification) : m_Impl(std::make_unique<Impl>(specification)) {}

    Profiler::~Profiler() = default;

    void Profiler::BeginFrame()
    {
        m_Impl->RequireOwner("BeginFrame");
        if (!m_Impl->Open.load(std::memory_order_acquire))
            throw std::logic_error("Profiler is closed.");
        std::scoped_lock lock(m_Impl->Mutex);
        if (m_Impl->Recording)
            throw std::logic_error("Profiler frame recording is already active.");
        m_Impl->SpanCount = 0;
        m_Impl->CounterCount = 0;
        m_Impl->Truncated = false;
        m_Impl->DroppedSpans = 0;
        m_Impl->DroppedCounters = 0;
        m_Impl->FrameStart = Clock::now();
        m_Impl->Recording = true;
    }

    void Profiler::EndFrame()
    {
        m_Impl->RequireOwner("EndFrame");
        if (!m_Impl->Open.load(std::memory_order_acquire))
            throw std::logic_error("Profiler is closed.");
        std::scoped_lock lock(m_Impl->Mutex);
        if (!m_Impl->Recording)
            throw std::logic_error("Profiler frame recording is not active.");
        const auto frameEnd = Clock::now();
        const auto sequence = m_Impl->NextSequence++;
        const auto startMicroseconds = Microseconds(m_Impl->FrameStart);
        const auto durationMicroseconds =
            std::chrono::duration<double, std::micro>(frameEnd - m_Impl->FrameStart).count();
        m_Impl->Latest.Sequence = sequence;
        m_Impl->Latest.StartMicroseconds = startMicroseconds;
        m_Impl->Latest.DurationMicroseconds = durationMicroseconds;
        m_Impl->Latest.Spans.assign(m_Impl->Spans.begin(),
                                    m_Impl->Spans.begin() + static_cast<std::ptrdiff_t>(m_Impl->SpanCount));
        m_Impl->Latest.Counters.assign(m_Impl->Counters.begin(),
                                       m_Impl->Counters.begin() + static_cast<std::ptrdiff_t>(m_Impl->CounterCount));
        m_Impl->Latest.DroppedSpans = m_Impl->DroppedSpans;
        m_Impl->Latest.DroppedCounters = m_Impl->DroppedCounters;
        m_Impl->Latest.Truncated = m_Impl->Truncated;
        double applicationMicroseconds = 0.0;
        double scriptingMicroseconds = 0.0;
        double physicsMicroseconds = 0.0;
        double renderingMicroseconds = 0.0;
        double audioMicroseconds = 0.0;
        double assetsMicroseconds = 0.0;
        double animationMicroseconds = 0.0;
        double navigationMicroseconds = 0.0;
        double userMicroseconds = 0.0;
        for (std::size_t index = 0; index < m_Impl->SpanCount; ++index)
        {
            const auto& span = m_Impl->Spans[index];
            switch (span.Category)
            {
            case ProfileCategory::Application:
                applicationMicroseconds += span.DurationMicroseconds;
                break;
            case ProfileCategory::Assets:
                assetsMicroseconds += span.DurationMicroseconds;
                break;
            case ProfileCategory::Scripting:
                scriptingMicroseconds += span.DurationMicroseconds;
                break;
            case ProfileCategory::Physics:
                physicsMicroseconds += span.DurationMicroseconds;
                break;
            case ProfileCategory::Animation:
                animationMicroseconds += span.DurationMicroseconds;
                break;
            case ProfileCategory::Rendering:
                renderingMicroseconds += span.DurationMicroseconds;
                break;
            case ProfileCategory::Audio:
                audioMicroseconds += span.DurationMicroseconds;
                break;
            case ProfileCategory::Navigation:
                navigationMicroseconds += span.DurationMicroseconds;
                break;
            case ProfileCategory::User:
                userMicroseconds += span.DurationMicroseconds;
                break;
            }
        }
        m_Impl->Latest.ApplicationMicroseconds = applicationMicroseconds;
        m_Impl->Latest.ScriptingMicroseconds = scriptingMicroseconds;
        m_Impl->Latest.PhysicsMicroseconds = physicsMicroseconds;
        m_Impl->Latest.RenderingMicroseconds = renderingMicroseconds;
        m_Impl->Latest.AudioMicroseconds = audioMicroseconds;
        m_Impl->Latest.AssetsMicroseconds = assetsMicroseconds;
        m_Impl->Latest.AnimationMicroseconds = animationMicroseconds;
        m_Impl->Latest.NavigationMicroseconds = navigationMicroseconds;
        m_Impl->Latest.UserMicroseconds = userMicroseconds;
        auto& summary = m_Impl->Summaries[m_Impl->SummaryWrite];
        summary = {sequence,
                   startMicroseconds,
                   durationMicroseconds,
                   m_Impl->SpanCount,
                   m_Impl->CounterCount,
                   m_Impl->DroppedSpans,
                   m_Impl->DroppedCounters,
                   applicationMicroseconds,
                   scriptingMicroseconds,
                   physicsMicroseconds,
                   renderingMicroseconds,
                   audioMicroseconds,
                   m_Impl->Truncated};
        summary.AssetsMicroseconds = assetsMicroseconds;
        summary.AnimationMicroseconds = animationMicroseconds;
        summary.NavigationMicroseconds = navigationMicroseconds;
        summary.UserMicroseconds = userMicroseconds;
        m_Impl->SummaryWrite = (m_Impl->SummaryWrite + 1) % m_Impl->Summaries.size();
        m_Impl->SummaryCount = std::min(m_Impl->SummaryCount + 1, m_Impl->Summaries.size());
        m_Impl->Recording = false;
    }

    void Profiler::RecordSpan(const ProfileCategory category, const std::string_view name,
                              const double startMicroseconds, const double durationMicroseconds) noexcept
    {
        if (!m_Impl->Open.load(std::memory_order_acquire) || name.empty() || !std::isfinite(startMicroseconds) ||
            !std::isfinite(durationMicroseconds) || durationMicroseconds < 0.0)
            return;
        try
        {
            std::scoped_lock lock(m_Impl->Mutex);
            if (!m_Impl->Recording)
                return;
            if (m_Impl->SpanCount >= m_Impl->Specification.MaximumSpansPerFrame)
            {
                m_Impl->Truncated = true;
                ++m_Impl->DroppedSpans;
                return;
            }
            const auto thread = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
            if (m_Impl->SpanCount < m_Impl->Spans.size())
            {
                auto& span = m_Impl->Spans[m_Impl->SpanCount];
                span.Name.assign(name);
                span.Category = category;
                span.Thread = thread;
                span.StartMicroseconds = startMicroseconds;
                span.DurationMicroseconds = durationMicroseconds;
            }
            else
            {
                m_Impl->Spans.push_back({std::string(name), category, thread, startMicroseconds, durationMicroseconds});
            }
            ++m_Impl->SpanCount;
        }
        catch (...)
        {
        }
    }

    void Profiler::SetCounter(const ProfileCategory category, const std::string_view name, const double value) noexcept
    {
        if (!m_Impl->Open.load(std::memory_order_acquire) || name.empty() || !std::isfinite(value))
            return;
        try
        {
            std::scoped_lock lock(m_Impl->Mutex);
            if (!m_Impl->Recording)
                return;
            const auto activeEnd = m_Impl->Counters.begin() + static_cast<std::ptrdiff_t>(m_Impl->CounterCount);
            const auto found = std::find_if(m_Impl->Counters.begin(), activeEnd, [&](const ProfileCounter& counter)
                                            { return counter.Category == category && counter.Name == name; });
            if (found != activeEnd)
            {
                found->Value = value;
                return;
            }
            if (m_Impl->CounterCount >= m_Impl->Specification.MaximumCountersPerFrame)
            {
                m_Impl->Truncated = true;
                ++m_Impl->DroppedCounters;
                return;
            }
            if (m_Impl->CounterCount < m_Impl->Counters.size())
            {
                auto& counter = m_Impl->Counters[m_Impl->CounterCount];
                counter.Name.assign(name);
                counter.Category = category;
                counter.Value = value;
            }
            else
            {
                m_Impl->Counters.push_back({std::string(name), category, value});
            }
            ++m_Impl->CounterCount;
        }
        catch (...)
        {
        }
    }

    ProfileFrame Profiler::LatestFrame() const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        return m_Impl->Latest;
    }

    std::string Profiler::LatestChromeTrace() const
    {
        const auto frame = LatestFrame();
        std::ostringstream trace;
        trace << "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[";
        bool first = true;
        for (const auto& span : frame.Spans)
        {
            if (!first)
                trace << ',';
            first = false;
            trace << "{\"name\":\"" << EscapeJson(span.Name) << "\",\"cat\":\"" << CategoryName(span.Category)
                  << "\",\"ph\":\"X\",\"ts\":" << span.StartMicroseconds << ",\"dur\":" << span.DurationMicroseconds
                  << ",\"pid\":1,\"tid\":" << span.Thread << '}';
        }
        for (const auto& counter : frame.Counters)
        {
            if (!first)
                trace << ',';
            first = false;
            trace << "{\"name\":\"" << EscapeJson(counter.Name) << "\",\"cat\":\"" << CategoryName(counter.Category)
                  << "\",\"ph\":\"C\",\"ts\":" << frame.StartMicroseconds
                  << ",\"pid\":1,\"tid\":0,\"args\":{\"value\":" << counter.Value << "}}";
        }
        trace << "]}";
        return trace.str();
    }

    ProfileFrameSummary Profiler::LatestSummary() const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        if (m_Impl->SummaryCount == 0)
            return {};
        const auto index = (m_Impl->SummaryWrite + m_Impl->Summaries.size() - 1) % m_Impl->Summaries.size();
        return m_Impl->Summaries[index];
    }

    std::vector<ProfileFrameSummary> Profiler::RecentSummaries(const std::size_t maximumFrames) const
    {
        std::scoped_lock lock(m_Impl->Mutex);
        const auto count = std::min(maximumFrames, m_Impl->SummaryCount);
        std::vector<ProfileFrameSummary> result;
        result.reserve(count);
        const auto oldest =
            (m_Impl->SummaryWrite + m_Impl->Summaries.size() - m_Impl->SummaryCount) % m_Impl->Summaries.size();
        const auto first = (oldest + m_Impl->SummaryCount - count) % m_Impl->Summaries.size();
        for (std::size_t index = 0; index < count; ++index)
            result.push_back(m_Impl->Summaries[(first + index) % m_Impl->Summaries.size()]);
        return result;
    }

    bool Profiler::IsOpen() const noexcept { return m_Impl->Open.load(std::memory_order_acquire); }

    void Profiler::Close()
    {
        m_Impl->RequireOwner("Close");
        if (!m_Impl->Open.exchange(false, std::memory_order_acq_rel))
            return;
        std::scoped_lock lock(m_Impl->Mutex);
        m_Impl->Recording = false;
        m_Impl->Spans.clear();
        m_Impl->Counters.clear();
        m_Impl->SpanCount = 0;
        m_Impl->CounterCount = 0;
        m_Impl->Latest = {};
        m_Impl->SummaryWrite = 0;
        m_Impl->SummaryCount = 0;
    }

    ProfileScope::ProfileScope(Ref<Profiler> profiler, const ProfileCategory category, const std::string_view name)
        : m_Target(std::move(profiler)), m_Category(category), m_Name(name),
          m_StartMicroseconds(Microseconds(Clock::now()))
    {
    }

    ProfileScope::~ProfileScope() { Finish(); }

    ProfileScope::ProfileScope(ProfileScope&& other) noexcept
        : m_Target(std::move(other.m_Target)), m_Category(other.m_Category), m_Name(std::move(other.m_Name)),
          m_StartMicroseconds(other.m_StartMicroseconds)
    {
        other.m_StartMicroseconds = 0.0;
    }

    ProfileScope& ProfileScope::operator=(ProfileScope&& other) noexcept
    {
        if (this != &other)
        {
            Finish();
            m_Target = std::move(other.m_Target);
            m_Category = other.m_Category;
            m_Name = std::move(other.m_Name);
            m_StartMicroseconds = other.m_StartMicroseconds;
            other.m_StartMicroseconds = 0.0;
        }
        return *this;
    }

    void ProfileScope::Finish() noexcept
    {
        if (!m_Target)
            return;
        const auto endMicroseconds = Microseconds(Clock::now());
        m_Target->RecordSpan(m_Category, m_Name, m_StartMicroseconds, endMicroseconds - m_StartMicroseconds);
        m_Target.Reset();
    }
} // namespace Keire
