#include "Keire/Diagnostics/Profiler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <mutex>
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
    } // namespace

    class Profiler::Impl final
    {
      public:
        explicit Impl(ProfilerSpecification value) : Specification(value), OwnerThread(std::this_thread::get_id())
        {
            if (Specification.MaximumSpansPerFrame == 0 || Specification.MaximumCountersPerFrame == 0)
                throw std::invalid_argument("Profiler bounds must be greater than zero.");
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
        std::uint64_t NextSequence = 1;
        std::vector<ProfileSpan> Spans;
        std::vector<ProfileCounter> Counters;
        ProfileFrame Latest;
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
        m_Impl->Spans.clear();
        m_Impl->Counters.clear();
        m_Impl->Truncated = false;
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
        m_Impl->Latest = {m_Impl->NextSequence++, m_Impl->Spans, m_Impl->Counters, m_Impl->Truncated};
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
            if (m_Impl->Spans.size() >= m_Impl->Specification.MaximumSpansPerFrame)
            {
                m_Impl->Truncated = true;
                return;
            }
            m_Impl->Spans.push_back(
                {std::string(name), category,
                 static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id())),
                 startMicroseconds, durationMicroseconds});
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
            const auto found = std::ranges::find_if(m_Impl->Counters, [&](const ProfileCounter& counter)
                                                    { return counter.Category == category && counter.Name == name; });
            if (found != m_Impl->Counters.end())
            {
                found->Value = value;
                return;
            }
            if (m_Impl->Counters.size() >= m_Impl->Specification.MaximumCountersPerFrame)
            {
                m_Impl->Truncated = true;
                return;
            }
            m_Impl->Counters.push_back({std::string(name), category, value});
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
    }

    class ProfileScope::Impl final
    {
      public:
        Impl(Ref<Profiler> value, const ProfileCategory profileCategory, const std::string_view profileName)
            : Target(std::move(value)), Category(profileCategory), Name(profileName), Start(Clock::now())
        {
        }

        void Finish() noexcept
        {
            if (!Target)
                return;
            const auto end = Clock::now();
            Target->RecordSpan(Category, Name, Microseconds(Start),
                               std::chrono::duration<double, std::micro>(end - Start).count());
            Target.Reset();
        }

        Ref<Profiler> Target;
        ProfileCategory Category;
        std::string Name;
        Clock::time_point Start;
    };

    ProfileScope::ProfileScope(Ref<Profiler> profiler, const ProfileCategory category, const std::string_view name)
        : m_Impl(std::make_unique<Impl>(std::move(profiler), category, name))
    {
    }

    ProfileScope::~ProfileScope()
    {
        if (m_Impl)
            m_Impl->Finish();
    }

    ProfileScope::ProfileScope(ProfileScope&& other) noexcept = default;

    ProfileScope& ProfileScope::operator=(ProfileScope&& other) noexcept
    {
        if (this != &other)
        {
            if (m_Impl)
                m_Impl->Finish();
            m_Impl = std::move(other.m_Impl);
        }
        return *this;
    }
} // namespace Keire
