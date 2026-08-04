#pragma once

#include "Keire/Api.h"
#include "Keire/Ref.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <memory_resource>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    enum class JobPriority : std::uint8_t
    {
        Critical,
        High,
        Normal,
        Low,
        Background
    };

    enum class JobClass : std::uint8_t
    {
        Compute,
        Blocking
    };

    enum class JobDomain : std::uint8_t
    {
        General,
        Simulation,
        Rendering,
        Streaming,
        Tooling
    };

    enum class JobDependencyPolicy : std::uint8_t
    {
        RequireSuccess,
        ObserveCompletion
    };

    enum class JobStatus : std::uint8_t
    {
        Waiting,
        Queued,
        Running,
        Succeeded,
        Failed,
        Cancelled
    };

    struct JobResult
    {
        JobStatus Status = JobStatus::Cancelled;
        std::exception_ptr Failure;

        [[nodiscard]] bool Succeeded() const noexcept { return Status == JobStatus::Succeeded; }
        void RethrowIfFailed() const;
    };

    using JobCompletionFunction = std::function<void(const JobResult&)>;

    struct JobSystemSpecification
    {
        std::size_t WorkerCount = 0;
        std::size_t BlockingWorkerCount = 2;
        std::size_t QueueCapacity = std::size_t{64} * 1024U;
        std::size_t ScratchBytesPerJob = std::size_t{64} * 1024U;
        std::uint64_t PriorityAgingSubmissions = 256;
        bool DeterministicSimulation = false;
    };

    struct JobSystemStatistics
    {
        std::size_t WorkerCount = 0;
        std::size_t BlockingWorkerCount = 0;
        std::size_t WaitingJobs = 0;
        std::size_t QueuedJobs = 0;
        std::size_t RunningJobs = 0;
        std::size_t QueueHighWaterMark = 0;
        std::uint64_t SubmittedJobs = 0;
        std::uint64_t CompletedJobs = 0;
        std::uint64_t FailedJobs = 0;
        std::uint64_t CancelledJobs = 0;
        std::uint64_t StolenJobs = 0;
    };

    class KEIRE_API JobContext final
    {
      public:
        class Impl;

        explicit JobContext(Impl& implementation) noexcept;
        [[nodiscard]] bool StopRequested() const noexcept;
        [[nodiscard]] std::stop_token StopToken() const noexcept;
        [[nodiscard]] std::size_t WorkerIndex() const noexcept;
        [[nodiscard]] JobDomain Domain() const noexcept;
        [[nodiscard]] std::pmr::memory_resource* ScratchResource() const noexcept;

      private:
        Impl* m_Impl = nullptr;
    };

    namespace Detail
    {
        class JobState;
        class JobSchedulerState;
    } // namespace Detail

    class KEIRE_API JobHandle final
    {
      public:
        JobHandle() noexcept = default;

        [[nodiscard]] bool IsValid() const noexcept;
        [[nodiscard]] std::uint64_t Id() const noexcept;
        [[nodiscard]] JobStatus Status() const noexcept;
        [[nodiscard]] bool IsComplete() const noexcept;
        [[nodiscard]] bool Wait(std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) const;
        [[nodiscard]] JobResult Result() const noexcept;
        void OnComplete(JobCompletionFunction completion) const;
        void Cancel() const noexcept;
        void RethrowIfFailed() const;

        [[nodiscard]] explicit operator bool() const noexcept { return IsValid(); }
        [[nodiscard]] bool operator==(const JobHandle& other) const noexcept;

      private:
        friend class JobSystem;
        friend class JobScope;
        friend class Detail::JobSchedulerState;

        explicit JobHandle(std::shared_ptr<Detail::JobState> state) noexcept;

        std::shared_ptr<Detail::JobState> m_State;
    };

    struct JobDescription
    {
        std::string Name;
        JobPriority Priority = JobPriority::Normal;
        JobClass Class = JobClass::Compute;
        JobDomain Domain = JobDomain::General;
        JobDependencyPolicy DependencyPolicy = JobDependencyPolicy::RequireSuccess;
        std::vector<JobHandle> Dependencies;
    };

    using JobFunction = std::function<void(JobContext&)>;
    using JobContinuationFunction = std::function<void(JobContext&, const JobResult&)>;
    using ParallelJobFunction = std::function<void(std::size_t begin, std::size_t end, JobContext& context)>;

    class JobScope;

    class KEIRE_API JobSystem final : public RefCounted
    {
      public:
        explicit JobSystem(JobSystemSpecification specification = {},
                           std::pmr::memory_resource* scratchUpstream = nullptr);
        ~JobSystem() override;

        JobSystem(const JobSystem&) = delete;
        JobSystem& operator=(const JobSystem&) = delete;

        [[nodiscard]] JobHandle Submit(JobDescription description, JobFunction function);
        [[nodiscard]] JobHandle ContinueWith(JobHandle prerequisite, JobDescription description,
                                             JobContinuationFunction function);
        [[nodiscard]] JobHandle ParallelFor(JobDescription description, std::size_t count, std::size_t minimumGrain,
                                            ParallelJobFunction function);
        [[nodiscard]] Ref<JobScope> CreateScope(std::string_view name = {});
        [[nodiscard]] JobSystemStatistics Statistics() const noexcept;
        [[nodiscard]] bool IsOpen() const noexcept;
        void SetDeterministicSimulation(bool enabled) noexcept;
        void Close() noexcept;

      private:
        friend class JobScope;

        [[nodiscard]] JobHandle SubmitScoped(JobDescription description, JobFunction function,
                                             const std::shared_ptr<void>& scopeState);

        std::shared_ptr<Detail::JobSchedulerState> m_State;
    };

    class KEIRE_API JobScope final : public RefCounted
    {
      public:
        ~JobScope() override;

        JobScope(const JobScope&) = delete;
        JobScope& operator=(const JobScope&) = delete;

        [[nodiscard]] JobHandle Submit(JobDescription description, JobFunction function);
        [[nodiscard]] JobHandle ParallelFor(JobDescription description, std::size_t count, std::size_t minimumGrain,
                                            ParallelJobFunction function);
        void Cancel() noexcept;
        void Wait() const noexcept;
        [[nodiscard]] bool StopRequested() const noexcept;
        [[nodiscard]] std::string_view Name() const noexcept;

      private:
        friend class JobSystem;
        template <typename T, typename... Args> friend Ref<T> CreateRef(Args&&... args);

        JobScope(std::shared_ptr<Detail::JobSchedulerState> jobs, std::string name);

        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire
