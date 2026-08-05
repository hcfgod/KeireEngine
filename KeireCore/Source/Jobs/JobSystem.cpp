#include "Keire/Jobs/JobSystem.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Keire
{
    namespace
    {
        constexpr std::size_t PriorityCount = 5;

        [[nodiscard]] constexpr std::size_t PriorityIndex(const JobPriority priority) noexcept
        {
            return static_cast<std::size_t>(priority);
        }

        [[nodiscard]] bool IsTerminal(const JobStatus status) noexcept
        {
            return status == JobStatus::Succeeded || status == JobStatus::Failed || status == JobStatus::Cancelled;
        }

        struct ScopeState final
        {
            mutable std::mutex Mutex;
            std::vector<std::weak_ptr<Detail::JobState>> Jobs;
            std::atomic<bool> Cancelled{false};
        };
    } // namespace

    namespace Detail
    {
        class JobSchedulerState;

        class JobState final
        {
          public:
            mutable std::mutex Mutex;
            std::condition_variable Completion;
            std::weak_ptr<JobSchedulerState> Scheduler;
            std::weak_ptr<ScopeState> Scope;
            std::vector<std::shared_ptr<JobState>> Dependents;
            std::vector<JobCompletionFunction> CompletionCallbacks;
            JobDescription Description;
            JobFunction Function;
            std::exception_ptr Failure;
            std::exception_ptr DependencyFailure;
            std::uint64_t Id = 0;
            std::uint64_t Sequence = 0;
            std::size_t RemainingDependencies = 0;
            JobStatus Status = JobStatus::Waiting;
            bool DependencyRegistrationComplete = false;
            bool CompletionCallbacksFinished = false;
            std::atomic<bool> CancellationRequested{false};
            std::stop_source StopSource;
        };

        struct Worker final
        {
            std::mutex Mutex;
            std::array<std::deque<std::shared_ptr<JobState>>, PriorityCount> Queues;
            std::jthread Thread;
        };

        class JobSchedulerState final : public std::enable_shared_from_this<JobSchedulerState>
        {
          public:
            JobSchedulerState(JobSystemSpecification value, std::pmr::memory_resource* scratchUpstream)
                : Specification(value),
                  ScratchUpstream(scratchUpstream ? scratchUpstream : std::pmr::get_default_resource())
            {
            }

            [[nodiscard]] JobHandle Submit(JobDescription description, JobFunction function,
                                           const std::shared_ptr<void>& scopeState);

            JobSystemSpecification Specification;
            std::pmr::memory_resource* ScratchUpstream = nullptr;
            std::atomic<bool> Accepting{true};
            std::atomic<bool> Stopping{false};
            std::atomic<bool> DeterministicSimulation{false};
            std::mutex SubmissionMutex;
            std::mutex WakeMutex;
            std::condition_variable Wake;
            std::mutex InjectionMutex;
            std::array<std::deque<std::shared_ptr<JobState>>, PriorityCount> Injection;
            std::mutex BlockingMutex;
            std::array<std::deque<std::shared_ptr<JobState>>, PriorityCount> Blocking;
            std::vector<std::unique_ptr<Worker>> Workers;
            std::vector<std::jthread> BlockingWorkers;
            std::mutex RegistryMutex;
            std::vector<std::weak_ptr<JobState>> Registry;
            std::weak_ptr<JobState> LastDeterministicSimulation;
            std::atomic<std::uint64_t> NextId{1};
            std::atomic<std::uint64_t> NextSequence{1};
            std::atomic<std::size_t> Outstanding{0};
            std::atomic<std::size_t> Waiting{0};
            std::atomic<std::size_t> Queued{0};
            std::atomic<std::size_t> Running{0};
            std::atomic<std::size_t> QueueHighWaterMark{0};
            std::atomic<std::uint64_t> Submitted{0};
            std::atomic<std::uint64_t> Completed{0};
            std::atomic<std::uint64_t> Failed{0};
            std::atomic<std::uint64_t> Cancelled{0};
            std::atomic<std::uint64_t> Stolen{0};
        };
    } // namespace Detail

    class JobContext::Impl final
    {
      public:
        Impl(std::shared_ptr<Detail::JobState> value, const std::size_t worker,
             std::pmr::memory_resource* scratch) noexcept
            : State(std::move(value)), Worker(worker), Scratch(scratch)
        {
        }

        std::shared_ptr<Detail::JobState> State;
        std::size_t Worker = 0;
        std::pmr::memory_resource* Scratch = nullptr;
    };

    namespace
    {
        thread_local Detail::JobSchedulerState* CurrentScheduler = nullptr;
        thread_local std::size_t CurrentWorker = std::numeric_limits<std::size_t>::max();
        thread_local bool CurrentBlockingWorker = false;

        void UpdateHighWaterMark(Detail::JobSchedulerState& scheduler, const std::size_t value) noexcept
        {
            auto current = scheduler.QueueHighWaterMark.load(std::memory_order_relaxed);
            while (current < value && !scheduler.QueueHighWaterMark.compare_exchange_weak(
                                          current, value, std::memory_order_relaxed, std::memory_order_relaxed))
            {
            }
        }

        void Enqueue(const std::shared_ptr<Detail::JobSchedulerState>& scheduler,
                     const std::shared_ptr<Detail::JobState>& job)
        {
            {
                std::scoped_lock lock(job->Mutex);
                if (job->Status != JobStatus::Waiting || !job->DependencyRegistrationComplete ||
                    job->RemainingDependencies != 0)
                    return;
                job->Status = JobStatus::Queued;
            }
            scheduler->Waiting.fetch_sub(1, std::memory_order_relaxed);
            const auto queued = scheduler->Queued.fetch_add(1, std::memory_order_relaxed) + 1;
            UpdateHighWaterMark(*scheduler, queued);

            if (job->Description.Class == JobClass::Blocking)
            {
                std::scoped_lock lock(scheduler->BlockingMutex);
                scheduler->Blocking[PriorityIndex(job->Description.Priority)].push_back(job);
            }
            else if (CurrentScheduler == scheduler.get() && !CurrentBlockingWorker &&
                     CurrentWorker < scheduler->Workers.size())
            {
                auto& worker = *scheduler->Workers[CurrentWorker];
                std::scoped_lock lock(worker.Mutex);
                worker.Queues[PriorityIndex(job->Description.Priority)].push_back(job);
            }
            else
            {
                std::scoped_lock lock(scheduler->InjectionMutex);
                scheduler->Injection[PriorityIndex(job->Description.Priority)].push_back(job);
            }
            scheduler->Wake.notify_all();
        }

        void FinishJob(const std::shared_ptr<Detail::JobSchedulerState>& scheduler,
                       const std::shared_ptr<Detail::JobState>& job, const JobStatus status,
                       const std::exception_ptr& failure = {})
        {
            std::vector<std::shared_ptr<Detail::JobState>> dependents;
            JobStatus previousStatus = JobStatus::Waiting;
            {
                std::scoped_lock lock(job->Mutex);
                if (IsTerminal(job->Status))
                    return;
                previousStatus = job->Status;
                job->Status = status;
                job->Failure = failure;
                job->Function = {};
                dependents.swap(job->Dependents);
            }

            if (previousStatus == JobStatus::Waiting)
                scheduler->Waiting.fetch_sub(1, std::memory_order_relaxed);
            else if (previousStatus == JobStatus::Queued)
                scheduler->Queued.fetch_sub(1, std::memory_order_relaxed);
            else if (previousStatus == JobStatus::Running)
                scheduler->Running.fetch_sub(1, std::memory_order_relaxed);
            scheduler->Outstanding.fetch_sub(1, std::memory_order_relaxed);
            if (status == JobStatus::Succeeded)
                scheduler->Completed.fetch_add(1, std::memory_order_relaxed);
            else if (status == JobStatus::Failed)
                scheduler->Failed.fetch_add(1, std::memory_order_relaxed);
            else
                scheduler->Cancelled.fetch_add(1, std::memory_order_relaxed);
            for (const auto& dependent : dependents)
            {
                bool ready = false;
                bool cancel = false;
                std::exception_ptr dependencyFailure;
                {
                    std::scoped_lock lock(dependent->Mutex);
                    if (IsTerminal(dependent->Status) || dependent->RemainingDependencies == 0)
                        continue;
                    if (status != JobStatus::Succeeded &&
                        dependent->Description.DependencyPolicy == JobDependencyPolicy::RequireSuccess &&
                        !dependent->DependencyFailure)
                    {
                        dependent->DependencyFailure = failure;
                    }
                    --dependent->RemainingDependencies;
                    ready = dependent->RemainingDependencies == 0 && dependent->DependencyRegistrationComplete;
                    cancel =
                        ready && (dependent->CancellationRequested.load(std::memory_order_acquire) ||
                                  (dependent->Description.DependencyPolicy == JobDependencyPolicy::RequireSuccess &&
                                   (status != JobStatus::Succeeded || dependent->DependencyFailure)));
                    dependencyFailure = dependent->DependencyFailure;
                }
                if (ready)
                {
                    if (cancel)
                        FinishJob(scheduler, dependent, JobStatus::Cancelled, dependencyFailure);
                    else
                        Enqueue(scheduler, dependent);
                }
            }
            for (;;)
            {
                std::vector<JobCompletionFunction> callbacks;
                JobResult result;
                {
                    std::scoped_lock lock(job->Mutex);
                    callbacks.swap(job->CompletionCallbacks);
                    result = {job->Status, job->Failure ? job->Failure : job->DependencyFailure};
                    if (callbacks.empty())
                    {
                        job->CompletionCallbacksFinished = true;
                        break;
                    }
                }
                for (const auto& callback : callbacks)
                {
                    try
                    {
                        callback(result);
                    }
                    catch (...)
                    {
                    }
                }
            }
            job->Completion.notify_all();
            scheduler->Wake.notify_all();
        }

        [[nodiscard]] std::shared_ptr<Detail::JobState>
        PopQueue(const std::shared_ptr<Detail::JobSchedulerState>& scheduler,
                 std::array<std::deque<std::shared_ptr<Detail::JobState>>, PriorityCount>& queues, const bool local)
        {
            std::size_t selected = PriorityCount;
            std::size_t effectivePriority = PriorityCount;
            std::uint64_t selectedSequence = (std::numeric_limits<std::uint64_t>::max)();
            const auto currentSequence = scheduler->NextSequence.load(std::memory_order_relaxed);
            for (std::size_t index = 0; index < queues.size(); ++index)
            {
                auto& queue = queues[index];
                if (queue.empty())
                    continue;
                const auto sequence = queue.front()->Sequence;
                const auto age = currentSequence > sequence ? currentSequence - sequence : 0;
                const auto promotions = age / scheduler->Specification.PriorityAgingSubmissions;
                const auto effective = promotions >= index ? 0 : index - static_cast<std::size_t>(promotions);
                if (effective < effectivePriority || (effective == effectivePriority && sequence < selectedSequence))
                {
                    selected = index;
                    effectivePriority = effective;
                    selectedSequence = sequence;
                }
            }
            if (selected == PriorityCount)
                return {};
            auto& queue = queues[selected];
            const bool promoted = effectivePriority < selected;
            auto job = local && !promoted ? std::move(queue.back()) : std::move(queue.front());
            if (local && !promoted)
                queue.pop_back();
            else
                queue.pop_front();
            return job;
        }

        [[nodiscard]] std::shared_ptr<Detail::JobState>
        TryTakeCompute(const std::shared_ptr<Detail::JobSchedulerState>& scheduler, const std::size_t workerIndex)
        {
            if (workerIndex < scheduler->Workers.size())
            {
                auto& local = *scheduler->Workers[workerIndex];
                std::scoped_lock lock(local.Mutex);
                if (auto job = PopQueue(scheduler, local.Queues, true))
                    return job;
            }
            {
                std::scoped_lock lock(scheduler->InjectionMutex);
                if (auto job = PopQueue(scheduler, scheduler->Injection, false))
                    return job;
            }
            for (std::size_t offset = 1; offset < scheduler->Workers.size(); ++offset)
            {
                const auto candidateIndex = (workerIndex + offset) % scheduler->Workers.size();
                auto& candidate = *scheduler->Workers[candidateIndex];
                std::scoped_lock lock(candidate.Mutex);
                if (auto job = PopQueue(scheduler, candidate.Queues, false))
                {
                    scheduler->Stolen.fetch_add(1, std::memory_order_relaxed);
                    return job;
                }
            }
            return {};
        }

        [[nodiscard]] std::shared_ptr<Detail::JobState>
        TryTakeBlocking(const std::shared_ptr<Detail::JobSchedulerState>& scheduler)
        {
            std::scoped_lock lock(scheduler->BlockingMutex);
            return PopQueue(scheduler, scheduler->Blocking, false);
        }

        void Execute(const std::shared_ptr<Detail::JobSchedulerState>& scheduler,
                     const std::shared_ptr<Detail::JobState>& job, const std::size_t workerIndex)
        {
            bool run = false;
            {
                std::scoped_lock lock(job->Mutex);
                if (IsTerminal(job->Status))
                    return;
                const auto scope = job->Scope.lock();
                run = !job->CancellationRequested.load(std::memory_order_acquire) &&
                      (!scope || !scope->Cancelled.load(std::memory_order_acquire));
                scheduler->Queued.fetch_sub(1, std::memory_order_relaxed);
                job->Status = JobStatus::Running;
                scheduler->Running.fetch_add(1, std::memory_order_relaxed);
            }

            if (!run)
            {
                FinishJob(scheduler, job, JobStatus::Cancelled);
                return;
            }

            std::exception_ptr failure;
            try
            {
                std::pmr::vector<std::byte> scratchStorage(scheduler->Specification.ScratchBytesPerJob,
                                                           scheduler->ScratchUpstream);
                std::pmr::monotonic_buffer_resource scratch(scratchStorage.data(), scratchStorage.size(),
                                                            scheduler->ScratchUpstream);
                JobContext::Impl implementation(job, workerIndex, &scratch);
                JobContext context(implementation);
                job->Function(context);
            }
            catch (...)
            {
                failure = std::current_exception();
            }
            const auto scope = job->Scope.lock();
            const bool cancelled = job->CancellationRequested.load(std::memory_order_acquire) ||
                                   (scope && scope->Cancelled.load(std::memory_order_acquire));
            FinishJob(scheduler, job,
                      failure     ? JobStatus::Failed
                      : cancelled ? JobStatus::Cancelled
                                  : JobStatus::Succeeded,
                      failure);
        }

        void ComputeWorkerMain(const std::shared_ptr<Detail::JobSchedulerState>& scheduler,
                               const std::size_t workerIndex, const std::stop_token stop)
        {
            CurrentScheduler = scheduler.get();
            CurrentWorker = workerIndex;
            CurrentBlockingWorker = false;
            while (!stop.stop_requested())
            {
                if (auto job = TryTakeCompute(scheduler, workerIndex))
                {
                    Execute(scheduler, job, workerIndex);
                    continue;
                }
                std::unique_lock lock(scheduler->WakeMutex);
                scheduler->Wake.wait_for(lock, std::chrono::milliseconds(20),
                                         [&] { return stop.stop_requested() || scheduler->Queued.load() != 0; });
            }
            CurrentScheduler = nullptr;
            CurrentWorker = std::numeric_limits<std::size_t>::max();
        }

        void BlockingWorkerMain(const std::shared_ptr<Detail::JobSchedulerState>& scheduler, const std::size_t index,
                                const std::stop_token stop)
        {
            CurrentScheduler = scheduler.get();
            CurrentWorker = scheduler->Workers.size() + index;
            CurrentBlockingWorker = true;
            while (!stop.stop_requested())
            {
                if (auto job = TryTakeBlocking(scheduler))
                {
                    Execute(scheduler, job, CurrentWorker);
                    continue;
                }
                std::unique_lock lock(scheduler->WakeMutex);
                scheduler->Wake.wait_for(lock, std::chrono::milliseconds(20),
                                         [&] { return stop.stop_requested() || scheduler->Queued.load() != 0; });
            }
            CurrentScheduler = nullptr;
            CurrentWorker = std::numeric_limits<std::size_t>::max();
            CurrentBlockingWorker = false;
        }

        [[nodiscard]] std::size_t DefaultWorkerCount() noexcept
        {
            const auto hardware = std::max(1U, std::thread::hardware_concurrency());
            return std::clamp<std::size_t>(hardware > 1 ? hardware - 1 : 1, 1, 32);
        }
    } // namespace

    JobContext::JobContext(Impl& implementation) noexcept : m_Impl(&implementation) {}

    bool JobContext::StopRequested() const noexcept
    {
        if (!m_Impl || !m_Impl->State)
            return true;
        if (m_Impl->State->CancellationRequested.load(std::memory_order_acquire))
            return true;
        const auto scope = m_Impl->State->Scope.lock();
        return scope && scope->Cancelled.load(std::memory_order_acquire);
    }

    std::stop_token JobContext::StopToken() const noexcept
    {
        return m_Impl && m_Impl->State ? m_Impl->State->StopSource.get_token() : std::stop_token{};
    }

    std::size_t JobContext::WorkerIndex() const noexcept { return m_Impl ? m_Impl->Worker : 0; }

    JobDomain JobContext::Domain() const noexcept
    {
        return m_Impl && m_Impl->State ? m_Impl->State->Description.Domain : JobDomain::General;
    }

    std::pmr::memory_resource* JobContext::ScratchResource() const noexcept
    {
        return m_Impl && m_Impl->Scratch ? m_Impl->Scratch : std::pmr::get_default_resource();
    }

    JobHandle::JobHandle(std::shared_ptr<Detail::JobState> state) noexcept : m_State(std::move(state)) {}

    bool JobHandle::IsValid() const noexcept { return m_State != nullptr; }

    std::uint64_t JobHandle::Id() const noexcept { return m_State ? m_State->Id : 0; }

    JobStatus JobHandle::Status() const noexcept
    {
        if (!m_State)
            return JobStatus::Cancelled;
        std::scoped_lock lock(m_State->Mutex);
        return m_State->Status;
    }

    bool JobHandle::IsComplete() const noexcept
    {
        if (!m_State)
            return true;
        std::scoped_lock lock(m_State->Mutex);
        return IsTerminal(m_State->Status) && m_State->CompletionCallbacksFinished;
    }

    bool JobHandle::Wait(const std::chrono::milliseconds timeout) const
    {
        if (!m_State)
            return true;
        const auto scheduler = m_State->Scheduler.lock();
        const auto deadline = timeout == std::chrono::milliseconds::max() ? std::chrono::steady_clock::time_point::max()
                                                                          : std::chrono::steady_clock::now() + timeout;
        while (!IsComplete())
        {
            if (scheduler && CurrentScheduler == scheduler.get() && !CurrentBlockingWorker)
            {
                if (auto work = TryTakeCompute(scheduler, CurrentWorker))
                {
                    Execute(scheduler, work, CurrentWorker);
                    continue;
                }
            }
            std::unique_lock lock(m_State->Mutex);
            if (deadline == std::chrono::steady_clock::time_point::max())
                m_State->Completion.wait(
                    lock, [&] { return IsTerminal(m_State->Status) && m_State->CompletionCallbacksFinished; });
            else if (!m_State->Completion.wait_until(
                         lock, deadline,
                         [&] { return IsTerminal(m_State->Status) && m_State->CompletionCallbacksFinished; }))
                return false;
        }
        return true;
    }

    JobResult JobHandle::Result() const noexcept
    {
        if (!m_State)
            return {};
        std::scoped_lock lock(m_State->Mutex);
        return {m_State->Status, m_State->Failure ? m_State->Failure : m_State->DependencyFailure};
    }

    void JobHandle::OnComplete(JobCompletionFunction completion) const
    {
        if (!completion)
            throw std::invalid_argument("A job completion callback is required.");
        if (!m_State)
        {
            completion({});
            return;
        }
        JobResult result;
        {
            std::scoped_lock lock(m_State->Mutex);
            if (!m_State->CompletionCallbacksFinished)
            {
                m_State->CompletionCallbacks.push_back(std::move(completion));
                return;
            }
            result = {m_State->Status, m_State->Failure ? m_State->Failure : m_State->DependencyFailure};
        }
        completion(result);
    }

    void JobHandle::Cancel() const noexcept
    {
        if (!m_State)
            return;
        m_State->CancellationRequested.store(true, std::memory_order_release);
        m_State->StopSource.request_stop();
        if (const auto scheduler = m_State->Scheduler.lock())
        {
            JobStatus status;
            {
                std::scoped_lock lock(m_State->Mutex);
                status = m_State->Status;
            }
            if (status == JobStatus::Waiting || status == JobStatus::Queued)
                FinishJob(scheduler, m_State, JobStatus::Cancelled);
            scheduler->Wake.notify_all();
        }
    }

    void JobHandle::RethrowIfFailed() const
    {
        if (!m_State)
            return;
        std::exception_ptr failure;
        {
            std::scoped_lock lock(m_State->Mutex);
            failure = m_State->Failure ? m_State->Failure : m_State->DependencyFailure;
        }
        if (failure)
            std::rethrow_exception(failure);
    }

    bool JobHandle::operator==(const JobHandle& other) const noexcept { return m_State == other.m_State; }

    void JobResult::RethrowIfFailed() const
    {
        if (Failure)
            std::rethrow_exception(Failure);
    }

    JobHandle Detail::JobSchedulerState::Submit(JobDescription description, JobFunction function,
                                                const std::shared_ptr<void>& scopeState)
    {
        if (!function)
            throw std::invalid_argument("A job function is required.");
        const auto state = shared_from_this();
        std::scoped_lock submissionLock(SubmissionMutex);
        if (!Accepting.load(std::memory_order_acquire))
            throw std::logic_error("The job system is closed.");

        for (const auto& dependencyHandle : description.Dependencies)
        {
            const auto dependency = dependencyHandle.m_State;
            if (!dependency)
                throw std::invalid_argument("Job dependencies must be valid.");
            if (dependency->Scheduler.lock() != state)
                throw std::invalid_argument("Job dependencies must use one scheduler.");
        }
        auto job = std::make_shared<JobState>();
        job->Scheduler = state;
        job->Description = std::move(description);
        job->Function = std::move(function);
        const auto outstanding = Outstanding.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (outstanding > Specification.QueueCapacity)
        {
            Outstanding.fetch_sub(1, std::memory_order_relaxed);
            throw std::length_error("The job queue capacity was reached.");
        }
        job->Id = NextId.fetch_add(1, std::memory_order_relaxed);
        job->Sequence = NextSequence.fetch_add(1, std::memory_order_relaxed);
        if (scopeState)
            job->Scope = std::static_pointer_cast<ScopeState>(scopeState);

        try
        {
            std::scoped_lock lock(RegistryMutex);
            const bool deterministic = DeterministicSimulation.load(std::memory_order_acquire) &&
                                       job->Description.Domain == JobDomain::Simulation;
            if (deterministic)
                if (const auto previous = LastDeterministicSimulation.lock())
                    job->Description.Dependencies.push_back(JobHandle(previous));
            Registry.push_back(job);
            if (deterministic)
                LastDeterministicSimulation = job;
        }
        catch (...)
        {
            Outstanding.fetch_sub(1, std::memory_order_relaxed);
            throw;
        }

        Waiting.fetch_add(1, std::memory_order_relaxed);
        Submitted.fetch_add(1, std::memory_order_relaxed);

        bool ready = false;
        bool cancel = false;
        std::exception_ptr registrationFailure;
        {
            std::scoped_lock jobLock(job->Mutex);
            try
            {
                for (const auto& dependencyHandle : job->Description.Dependencies)
                {
                    const auto& dependency = dependencyHandle.m_State;
                    std::scoped_lock dependencyLock(dependency->Mutex);
                    if (dependency->Status == JobStatus::Succeeded)
                        continue;
                    if (dependency->Status == JobStatus::Failed || dependency->Status == JobStatus::Cancelled)
                    {
                        if (!job->DependencyFailure)
                        {
                            job->DependencyFailure =
                                dependency->Failure ? dependency->Failure : dependency->DependencyFailure;
                        }
                        continue;
                    }
                    dependency->Dependents.push_back(job);
                    ++job->RemainingDependencies;
                }

                if (const auto scope = job->Scope.lock())
                {
                    std::scoped_lock scopeLock(scope->Mutex);
                    scope->Jobs.push_back(job);
                    if (scope->Cancelled.load(std::memory_order_acquire))
                    {
                        job->CancellationRequested.store(true, std::memory_order_release);
                        job->StopSource.request_stop();
                    }
                }
            }
            catch (...)
            {
                registrationFailure = std::current_exception();
            }
            job->DependencyRegistrationComplete = true;
            ready = job->RemainingDependencies == 0;
            cancel =
                registrationFailure || job->CancellationRequested.load(std::memory_order_acquire) ||
                (job->Description.DependencyPolicy == JobDependencyPolicy::RequireSuccess && job->DependencyFailure);
        }

        if (registrationFailure)
        {
            FinishJob(state, job, JobStatus::Cancelled, registrationFailure);
            std::rethrow_exception(registrationFailure);
        }
        if (ready && cancel)
            FinishJob(state, job, JobStatus::Cancelled, job->DependencyFailure);
        else if (ready)
            Enqueue(state, job);
        return JobHandle(std::move(job));
    }

    JobSystem::JobSystem(JobSystemSpecification specification, std::pmr::memory_resource* scratchUpstream)
    {
        if (specification.WorkerCount == 0)
            specification.WorkerCount = DefaultWorkerCount();
        if (specification.WorkerCount > 32)
            throw std::invalid_argument("Job worker count must be in the range 1..32.");
        if (specification.BlockingWorkerCount == 0 || specification.BlockingWorkerCount > 8)
            throw std::invalid_argument("Blocking job worker count must be in the range 1..8.");
        if (specification.QueueCapacity == 0)
            throw std::invalid_argument("Job queue capacity must be greater than zero.");
        if (specification.ScratchBytesPerJob == 0)
            throw std::invalid_argument("Job scratch capacity must be greater than zero.");
        if (specification.PriorityAgingSubmissions == 0)
            throw std::invalid_argument("Job priority aging interval must be greater than zero.");

        m_State = std::make_shared<Detail::JobSchedulerState>(specification, scratchUpstream);
        m_State->DeterministicSimulation.store(specification.DeterministicSimulation);
        m_State->Workers.reserve(specification.WorkerCount);
        for (std::size_t index = 0; index < specification.WorkerCount; ++index)
            m_State->Workers.push_back(std::make_unique<Detail::Worker>());
        for (std::size_t index = 0; index < specification.WorkerCount; ++index)
        {
            auto state = m_State;
            m_State->Workers[index]->Thread =
                std::jthread([state, index](const std::stop_token stop) { ComputeWorkerMain(state, index, stop); });
        }
        m_State->BlockingWorkers.reserve(specification.BlockingWorkerCount);
        for (std::size_t index = 0; index < specification.BlockingWorkerCount; ++index)
        {
            auto state = m_State;
            m_State->BlockingWorkers.emplace_back([state, index](const std::stop_token stop)
                                                  { BlockingWorkerMain(state, index, stop); });
        }
    }

    JobSystem::~JobSystem() { Close(); }

    JobHandle JobSystem::Submit(JobDescription description, JobFunction function)
    {
        return SubmitScoped(std::move(description), std::move(function), {});
    }

    JobHandle JobSystem::ContinueWith(JobHandle prerequisite, JobDescription description,
                                      JobContinuationFunction function)
    {
        if (!prerequisite || !function)
            throw std::invalid_argument("A valid prerequisite and continuation function are required.");
        description.Dependencies.push_back(prerequisite);
        description.DependencyPolicy = JobDependencyPolicy::ObserveCompletion;
        const auto state = m_State;
        if (!state)
            throw std::logic_error("The job system is closed.");
        return state->Submit(
            std::move(description),
            [prerequisite = std::move(prerequisite), function = std::move(function)](JobContext& context)
            { function(context, prerequisite.Result()); }, {});
    }

    JobHandle JobSystem::SubmitScoped(JobDescription description, JobFunction function,
                                      const std::shared_ptr<void>& scopeState)
    {
        if (!m_State)
            throw std::logic_error("The job system is closed.");
        return m_State->Submit(std::move(description), std::move(function), scopeState);
    }

    JobHandle JobSystem::ParallelFor(JobDescription description, const std::size_t count,
                                     const std::size_t minimumGrain, ParallelJobFunction function)
    {
        if (!function)
            throw std::invalid_argument("A parallel job function is required.");
        if (count == 0)
            return Submit(std::move(description), [](JobContext&) {});
        const auto grain = std::max<std::size_t>(1, minimumGrain);
        std::vector<JobHandle> chunks;
        chunks.reserve(1U + ((count - 1U) / grain));
        const auto dependencies = description.Dependencies;
        auto sharedFunction = std::make_shared<ParallelJobFunction>(std::move(function));
        for (std::size_t begin = 0; begin < count;)
        {
            auto chunkDescription = description;
            chunkDescription.Name += " [" + std::to_string(begin) + "]";
            chunkDescription.Dependencies = dependencies;
            const auto end = begin + std::min(grain, count - begin);
            chunks.push_back(Submit(std::move(chunkDescription), [sharedFunction, begin, end](JobContext& context)
                                    { (*sharedFunction)(begin, end, context); }));
            begin = end;
        }
        description.Name += " completion";
        description.Dependencies = std::move(chunks);
        return Submit(std::move(description), [](JobContext&) {});
    }

    Ref<JobScope> JobSystem::CreateScope(const std::string_view name)
    {
        if (!IsOpen())
            throw std::logic_error("The job system is closed.");
        return CreateRef<JobScope>(m_State, std::string(name));
    }

    JobSystemStatistics JobSystem::Statistics() const noexcept
    {
        JobSystemStatistics result;
        if (!m_State)
            return result;
        result.WorkerCount = m_State->Workers.size();
        result.BlockingWorkerCount = m_State->BlockingWorkers.size();
        result.WaitingJobs = m_State->Waiting.load(std::memory_order_relaxed);
        result.QueuedJobs = m_State->Queued.load(std::memory_order_relaxed);
        result.RunningJobs = m_State->Running.load(std::memory_order_relaxed);
        result.QueueHighWaterMark = m_State->QueueHighWaterMark.load(std::memory_order_relaxed);
        result.SubmittedJobs = m_State->Submitted.load(std::memory_order_relaxed);
        result.CompletedJobs = m_State->Completed.load(std::memory_order_relaxed);
        result.FailedJobs = m_State->Failed.load(std::memory_order_relaxed);
        result.CancelledJobs = m_State->Cancelled.load(std::memory_order_relaxed);
        result.StolenJobs = m_State->Stolen.load(std::memory_order_relaxed);
        return result;
    }

    bool JobSystem::IsOpen() const noexcept { return m_State && m_State->Accepting.load(std::memory_order_acquire); }

    void JobSystem::SetDeterministicSimulation(const bool enabled) noexcept
    {
        if (m_State)
            m_State->DeterministicSimulation.store(enabled, std::memory_order_release);
    }

    void JobSystem::Close() noexcept
    {
        const auto state = m_State;
        std::vector<std::shared_ptr<Detail::JobState>> jobs;
        {
            if (!state)
                return;
            std::scoped_lock submissionLock(state->SubmissionMutex);
            if (!state->Accepting.exchange(false, std::memory_order_acq_rel))
                return;
            state->Stopping.store(true, std::memory_order_release);
            std::scoped_lock registryLock(state->RegistryMutex);
            for (const auto& weakJob : state->Registry)
            {
                if (const auto job = weakJob.lock())
                    jobs.push_back(job);
            }
        }
        for (const auto& job : jobs)
        {
            job->CancellationRequested.store(true, std::memory_order_release);
            job->StopSource.request_stop();
        }
        state->Wake.notify_all();
        const auto currentThread = std::this_thread::get_id();
        const bool workerInitiated = std::ranges::any_of(state->Workers, [&](const auto& worker)
                                                         { return worker->Thread.get_id() == currentThread; }) ||
                                     std::ranges::any_of(state->BlockingWorkers, [&](const auto& worker)
                                                         { return worker.get_id() == currentThread; });
        for (auto& worker : state->Workers)
        {
            worker->Thread.request_stop();
            if (worker->Thread.joinable())
            {
                if (workerInitiated)
                    worker->Thread.detach();
                else
                    worker->Thread.join();
            }
        }
        for (auto& worker : state->BlockingWorkers)
        {
            worker.request_stop();
            if (worker.joinable())
            {
                if (workerInitiated)
                    worker.detach();
                else
                    worker.join();
            }
        }
        for (const auto& job : jobs)
        {
            JobStatus status;
            {
                std::scoped_lock lock(job->Mutex);
                status = job->Status;
            }
            if (status != JobStatus::Running && !IsTerminal(status))
                FinishJob(state, job, JobStatus::Cancelled);
        }
    }

    class JobScope::Impl final
    {
      public:
        Impl(std::shared_ptr<Detail::JobSchedulerState> value, std::string scopeName)
            : Jobs(std::move(value)), Name(std::move(scopeName)), State(std::make_shared<ScopeState>())
        {
        }

        std::shared_ptr<Detail::JobSchedulerState> Jobs;
        std::string Name;
        std::shared_ptr<ScopeState> State;
    };

    JobScope::JobScope(std::shared_ptr<Detail::JobSchedulerState> jobs, std::string name)
        : m_Impl(std::make_unique<Impl>(std::move(jobs), std::move(name)))
    {
    }

    JobScope::~JobScope()
    {
        Cancel();
        Wait();
    }

    JobHandle JobScope::Submit(JobDescription description, JobFunction function)
    {
        if (StopRequested())
            throw std::logic_error("The job scope is cancelled.");
        if (!m_Impl->Jobs)
            throw std::logic_error("The job system is closed.");
        return m_Impl->Jobs->Submit(std::move(description), std::move(function), m_Impl->State);
    }

    JobHandle JobScope::ParallelFor(JobDescription description, const std::size_t count, const std::size_t minimumGrain,
                                    ParallelJobFunction function)
    {
        if (count == 0)
            return Submit(std::move(description), [](JobContext&) {});
        const auto grain = std::max<std::size_t>(1, minimumGrain);
        std::vector<JobHandle> chunks;
        chunks.reserve(1U + ((count - 1U) / grain));
        const auto dependencies = description.Dependencies;
        auto sharedFunction = std::make_shared<ParallelJobFunction>(std::move(function));
        for (std::size_t begin = 0; begin < count;)
        {
            auto chunkDescription = description;
            chunkDescription.Name += " [" + std::to_string(begin) + "]";
            chunkDescription.Dependencies = dependencies;
            const auto end = begin + std::min(grain, count - begin);
            chunks.push_back(Submit(std::move(chunkDescription), [sharedFunction, begin, end](JobContext& context)
                                    { (*sharedFunction)(begin, end, context); }));
            begin = end;
        }
        description.Name += " completion";
        description.Dependencies = std::move(chunks);
        return Submit(std::move(description), [](JobContext&) {});
    }

    void JobScope::Cancel() noexcept
    {
        if (!m_Impl)
            return;
        m_Impl->State->Cancelled.store(true, std::memory_order_release);
        std::vector<std::shared_ptr<Detail::JobState>> jobs;
        {
            std::scoped_lock lock(m_Impl->State->Mutex);
            for (const auto& weakJob : m_Impl->State->Jobs)
            {
                if (const auto job = weakJob.lock())
                    jobs.push_back(job);
            }
        }
        for (const auto& job : jobs)
        {
            job->CancellationRequested.store(true, std::memory_order_release);
            job->StopSource.request_stop();
            JobStatus status;
            {
                std::scoped_lock lock(job->Mutex);
                status = job->Status;
            }
            if (status == JobStatus::Waiting)
            {
                if (const auto scheduler = job->Scheduler.lock())
                    FinishJob(scheduler, job, JobStatus::Cancelled);
            }
        }
    }

    void JobScope::Wait() const noexcept
    {
        if (!m_Impl)
            return;
        std::vector<JobHandle> jobs;
        {
            std::scoped_lock lock(m_Impl->State->Mutex);
            jobs.reserve(m_Impl->State->Jobs.size());
            for (const auto& weakJob : m_Impl->State->Jobs)
            {
                if (const auto job = weakJob.lock())
                    jobs.push_back(JobHandle(job));
            }
        }
        for (const auto& job : jobs)
            (void)job.Wait();
    }

    bool JobScope::StopRequested() const noexcept
    {
        return !m_Impl || m_Impl->State->Cancelled.load(std::memory_order_acquire);
    }

    std::string_view JobScope::Name() const noexcept
    {
        return m_Impl ? std::string_view(m_Impl->Name) : std::string_view{};
    }
} // namespace Keire
