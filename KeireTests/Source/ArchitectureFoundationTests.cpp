#include "Keire/Core.h"
#include "KeireInternal/StableHandleTable.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory_resource>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace
{
    class CountingMemoryResource final : public std::pmr::memory_resource
    {
      public:
        [[nodiscard]] std::size_t AllocationCount() const noexcept { return m_AllocationCount.load(); }

      private:
        void* do_allocate(const std::size_t bytes, const std::size_t alignment) override
        {
            m_AllocationCount.fetch_add(1);
            return std::pmr::new_delete_resource()->allocate(bytes, alignment);
        }

        void do_deallocate(void* pointer, const std::size_t bytes, const std::size_t alignment) override
        {
            std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        }

        [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
        {
            return this == &other;
        }

        std::atomic<std::size_t> m_AllocationCount{0};
    };

    class ThrowingStableValue final
    {
      public:
        explicit ThrowingStableValue(const bool fail)
        {
            if (fail)
                throw std::runtime_error("intentional stable-handle construction failure");
        }
    };
} // namespace

TEST_CASE("Job dependencies propagate completion and failure")
{
    Keire::JobSystemSpecification specification;
    specification.WorkerCount = 2;
    specification.BlockingWorkerCount = 1;
    auto jobs = Keire::CreateRef<Keire::JobSystem>(specification);

    std::atomic<int> value = 0;
    const auto first = jobs->Submit({.Name = "First"}, [&](Keire::JobContext&) { value.store(7); });
    Keire::JobDescription secondDescription;
    secondDescription.Name = "Second";
    secondDescription.Dependencies.push_back(first);
    const auto second =
        jobs->Submit(std::move(secondDescription), [&](Keire::JobContext&) { value.store(value.load() * 6); });

    REQUIRE(second.Wait(2s));
    CHECK(second.Status() == Keire::JobStatus::Succeeded);
    CHECK(value.load() == 42);

    const auto failed =
        jobs->Submit({.Name = "Failure"}, [](Keire::JobContext&) { throw std::runtime_error("job failed"); });
    REQUIRE(failed.Wait(2s));
    CHECK(failed.Status() == Keire::JobStatus::Failed);
    CHECK_THROWS_WITH_AS(failed.RethrowIfFailed(), "job failed", std::runtime_error);

    Keire::JobDescription blockedDescription;
    blockedDescription.Name = "Blocked";
    blockedDescription.Dependencies.push_back(failed);
    const auto blocked =
        jobs->Submit(std::move(blockedDescription), [](Keire::JobContext&) { FAIL("A blocked dependent executed."); });
    REQUIRE(blocked.Wait(2s));
    CHECK(blocked.Status() == Keire::JobStatus::Cancelled);
    CHECK_THROWS_WITH_AS(blocked.RethrowIfFailed(), "job failed", std::runtime_error);
    jobs->Close();
}

TEST_CASE("Concurrent dependency completion cannot publish a partially registered job")
{
    Keire::JobSystemSpecification specification;
    specification.WorkerCount = 8;
    specification.BlockingWorkerCount = 1;
    auto jobs = Keire::CreateRef<Keire::JobSystem>(specification);

    for (int iteration = 0; iteration < 100; ++iteration)
    {
        constexpr int dependencyCount = 32;
        std::atomic<bool> release = false;
        std::atomic<bool> submitting = false;
        std::atomic<int> completed = 0;
        std::atomic<bool> premature = false;
        std::vector<Keire::JobHandle> dependencies;
        dependencies.reserve(dependencyCount);
        for (int index = 0; index < dependencyCount; ++index)
        {
            dependencies.push_back(jobs->Submit({.Name = "Concurrent dependency"},
                                                [&](Keire::JobContext& context)
                                                {
                                                    while (!release.load(std::memory_order_acquire) &&
                                                           !context.StopRequested())
                                                    {
                                                        std::this_thread::yield();
                                                    }
                                                    completed.fetch_add(1, std::memory_order_release);
                                                }));
        }

        Keire::JobHandle dependent;
        std::thread submitter(
            [&]
            {
                Keire::JobDescription description;
                description.Name = "Fully registered dependent";
                description.Dependencies = dependencies;
                submitting.store(true, std::memory_order_release);
                dependent = jobs->Submit(std::move(description),
                                         [&](Keire::JobContext&)
                                         {
                                             if (completed.load(std::memory_order_acquire) != dependencyCount)
                                                 premature.store(true, std::memory_order_release);
                                         });
            });
        while (!submitting.load(std::memory_order_acquire))
            std::this_thread::yield();
        release.store(true, std::memory_order_release);
        submitter.join();

        REQUIRE(dependent.Wait(2s));
        CHECK(dependent.Status() == Keire::JobStatus::Succeeded);
        CHECK_FALSE(premature.load(std::memory_order_acquire));
    }
    jobs->Close();
}

TEST_CASE("Worker initiated job-system shutdown is safe and completes the caller")
{
    Keire::JobSystemSpecification specification;
    specification.WorkerCount = 2;
    specification.BlockingWorkerCount = 1;
    auto jobs = Keire::CreateRef<Keire::JobSystem>(specification);
    std::atomic<bool> releaseCloser = false;
    std::atomic<bool> waiterStarted = false;
    const auto closer = jobs->Submit({.Name = "Worker shutdown"},
                                     [jobs, &releaseCloser](Keire::JobContext&)
                                     {
                                         while (!releaseCloser.load(std::memory_order_acquire))
                                             std::this_thread::yield();
                                         jobs->Close();
                                     });
    const auto waiter = jobs->Submit({.Name = "Worker waiting on shutdown caller"},
                                     [closer, &waiterStarted](Keire::JobContext&)
                                     {
                                         waiterStarted.store(true, std::memory_order_release);
                                         (void)closer.Wait();
                                     });
    while (!waiterStarted.load(std::memory_order_acquire))
        std::this_thread::yield();
    releaseCloser.store(true, std::memory_order_release);

    REQUIRE(closer.Wait(2s));
    CHECK(closer.Status() == Keire::JobStatus::Cancelled);
    REQUIRE(waiter.Wait(2s));
    CHECK(waiter.Status() == Keire::JobStatus::Cancelled);
    CHECK_FALSE(jobs->IsOpen());
}

TEST_CASE("Job scratch overflow allocations use the configured upstream resource")
{
    CountingMemoryResource upstream;
    Keire::JobSystemSpecification specification;
    specification.WorkerCount = 1;
    specification.BlockingWorkerCount = 1;
    specification.ScratchBytesPerJob = 128;
    auto jobs = Keire::CreateRef<Keire::JobSystem>(specification, &upstream);
    const auto job = jobs->Submit({.Name = "Scratch overflow"},
                                  [](Keire::JobContext& context)
                                  {
                                      std::pmr::vector<std::byte> allocation(context.ScratchResource());
                                      allocation.resize(4096);
                                  });

    REQUIRE(job.Wait(2s));
    CHECK(job.Status() == Keire::JobStatus::Succeeded);
    CHECK(upstream.AllocationCount() >= 2);
    jobs->Close();
}

TEST_CASE("Job continuations observe failed and cancelled prerequisites")
{
    Keire::JobSystemSpecification specification;
    specification.WorkerCount = 1;
    specification.BlockingWorkerCount = 1;
    auto jobs = Keire::CreateRef<Keire::JobSystem>(specification);

    const auto failed =
        jobs->Submit({.Name = "Failure"}, [](Keire::JobContext&) { throw std::runtime_error("observed"); });
    std::atomic<Keire::JobStatus> observed = Keire::JobStatus::Waiting;
    const auto continuation =
        jobs->ContinueWith(failed, {.Name = "Observer"},
                           [&](Keire::JobContext&, const Keire::JobResult& result) { observed.store(result.Status); });
    REQUIRE(continuation.Wait(2s));
    CHECK(continuation.Status() == Keire::JobStatus::Succeeded);
    CHECK(observed.load() == Keire::JobStatus::Failed);
    CHECK_THROWS_WITH_AS(failed.Result().RethrowIfFailed(), "observed", std::runtime_error);
    jobs->Close();
}

TEST_CASE("Job scopes cancel waiting work without waiting for foreign dependencies")
{
    Keire::JobSystemSpecification specification;
    specification.WorkerCount = 2;
    specification.BlockingWorkerCount = 1;
    auto jobs = Keire::CreateRef<Keire::JobSystem>(specification);
    auto scope = jobs->CreateScope("Scene");

    std::atomic<bool> release = false;
    const auto blocker = jobs->Submit({.Name = "Blocker"},
                                      [&](Keire::JobContext& context)
                                      {
                                          while (!release.load() && !context.StopRequested())
                                              std::this_thread::yield();
                                      });
    Keire::JobDescription description;
    description.Name = "Scoped dependent";
    description.Dependencies.push_back(blocker);
    const auto dependent =
        scope->Submit(std::move(description), [](Keire::JobContext&) { FAIL("Cancelled work executed."); });

    scope->Cancel();
    scope->Wait();
    CHECK(dependent.Status() == Keire::JobStatus::Cancelled);
    release.store(true);
    REQUIRE(blocker.Wait(2s));
    jobs->Close();
}

TEST_CASE("Job priority aging prevents background starvation")
{
    Keire::JobSystemSpecification specification;
    specification.WorkerCount = 1;
    specification.BlockingWorkerCount = 1;
    specification.PriorityAgingSubmissions = 1;
    auto jobs = Keire::CreateRef<Keire::JobSystem>(specification);
    std::atomic<bool> release = false;
    const auto blocker = jobs->Submit({.Name = "Blocker"},
                                      [&](Keire::JobContext& context)
                                      {
                                          while (!release.load() && !context.StopRequested())
                                              std::this_thread::yield();
                                      });
    std::mutex mutex;
    std::vector<int> order;
    Keire::JobDescription backgroundDescription;
    backgroundDescription.Name = "Background";
    backgroundDescription.Priority = Keire::JobPriority::Background;
    backgroundDescription.Dependencies.push_back(blocker);
    const auto background = jobs->Submit(std::move(backgroundDescription),
                                         [&](Keire::JobContext&)
                                         {
                                             std::scoped_lock lock(mutex);
                                             order.push_back(0);
                                         });
    std::vector<Keire::JobHandle> critical;
    for (int index = 1; index <= 4; ++index)
    {
        Keire::JobDescription description;
        description.Name = "Critical";
        description.Priority = Keire::JobPriority::Critical;
        description.Dependencies.push_back(blocker);
        critical.push_back(jobs->Submit(std::move(description),
                                        [&, index](Keire::JobContext&)
                                        {
                                            std::scoped_lock lock(mutex);
                                            order.push_back(index);
                                        }));
    }
    release.store(true);
    REQUIRE(background.Wait(2s));
    for (const auto& job : critical)
        REQUIRE(job.Wait(2s));
    REQUIRE(order.size() == 5);
    CHECK(order.front() == 0);
    jobs->Close();
}

TEST_CASE("Job compute and blocking lanes remain isolated")
{
    Keire::JobSystemSpecification specification;
    specification.WorkerCount = 1;
    specification.BlockingWorkerCount = 1;
    auto jobs = Keire::CreateRef<Keire::JobSystem>(specification);
    std::atomic<bool> release = false;
    const auto compute = jobs->Submit({.Name = "Compute blocker"},
                                      [&](Keire::JobContext& context)
                                      {
                                          while (!release.load() && !context.StopRequested())
                                              std::this_thread::yield();
                                      });
    Keire::JobDescription description;
    description.Name = "Blocking lane";
    description.Class = Keire::JobClass::Blocking;
    const auto blocking = jobs->Submit(std::move(description), [](Keire::JobContext&) {});
    CHECK(blocking.Wait(2s));
    CHECK(blocking.Status() == Keire::JobStatus::Succeeded);
    release.store(true);
    CHECK(compute.Wait(2s));
    jobs->Close();
}

TEST_CASE("Worker waits execute local work and allow stealing")
{
    Keire::JobSystemSpecification specification;
    specification.WorkerCount = 4;
    specification.BlockingWorkerCount = 1;
    auto jobs = Keire::CreateRef<Keire::JobSystem>(specification);
    std::atomic<int> completed = 0;
    const auto parent = jobs->Submit({.Name = "Parent"},
                                     [&](Keire::JobContext&)
                                     {
                                         std::vector<Keire::JobHandle> children;
                                         children.reserve(128);
                                         for (int index = 0; index < 128; ++index)
                                             children.push_back(jobs->Submit({.Name = "Child"},
                                                                             [&](Keire::JobContext&)
                                                                             {
                                                                                 std::this_thread::sleep_for(1ms);
                                                                                 completed.fetch_add(1);
                                                                             }));
                                         REQUIRE(children.back().Wait(2s));
                                         for (const auto& child : children)
                                             REQUIRE(child.Wait(2s));
                                     });
    REQUIRE(parent.Wait(5s));
    CHECK(completed.load() == 128);
    CHECK(jobs->Statistics().StolenJobs > 0);
    jobs->Close();
}

TEST_CASE("Job capacity foreign dependencies and shutdown races fail safely")
{
    Keire::JobSystemSpecification limitedSpecification;
    limitedSpecification.WorkerCount = 1;
    limitedSpecification.BlockingWorkerCount = 1;
    limitedSpecification.QueueCapacity = 2;
    auto limited = Keire::CreateRef<Keire::JobSystem>(limitedSpecification);
    std::atomic<bool> release = false;
    const auto first = limited->Submit({.Name = "First"},
                                       [&](Keire::JobContext& context)
                                       {
                                           while (!release.load() && !context.StopRequested())
                                               std::this_thread::yield();
                                       });
    const auto second = limited->Submit({.Name = "Second"}, [](Keire::JobContext&) {});
    CHECK_THROWS_AS((void)limited->Submit({.Name = "Overflow"}, [](Keire::JobContext&) {}), std::length_error);

    limited->Close();
    CHECK(first.Wait(2s));
    CHECK(second.Wait(2s));
    CHECK(first.Status() == Keire::JobStatus::Cancelled);
    CHECK(second.Status() == Keire::JobStatus::Cancelled);

    Keire::JobSystemSpecification dependencySpecification;
    dependencySpecification.WorkerCount = 1;
    dependencySpecification.BlockingWorkerCount = 1;
    auto primary = Keire::CreateRef<Keire::JobSystem>(dependencySpecification);
    auto other = Keire::CreateRef<Keire::JobSystem>(dependencySpecification);
    Keire::JobDescription foreign;
    foreign.Name = "Foreign";
    foreign.Dependencies.push_back(other->Submit({.Name = "Other"}, [](Keire::JobContext&) {}));
    CHECK_THROWS_AS((void)primary->Submit(std::move(foreign), [](Keire::JobContext&) {}), std::invalid_argument);
    Keire::JobDescription invalid;
    invalid.Name = "Invalid";
    invalid.Dependencies.push_back({});
    CHECK_THROWS_AS((void)primary->Submit(std::move(invalid), [](Keire::JobContext&) {}), std::invalid_argument);
    primary->Close();
    other->Close();
}

TEST_CASE("Strict simulation jobs execute in stable submission order")
{
    Keire::JobSystemSpecification specification;
    specification.WorkerCount = 4;
    specification.BlockingWorkerCount = 1;
    specification.DeterministicSimulation = true;
    auto jobs = Keire::CreateRef<Keire::JobSystem>(specification);

    std::mutex orderMutex;
    std::vector<int> order;
    std::vector<Keire::JobHandle> handles;
    for (int index = 0; index < 64; ++index)
    {
        Keire::JobDescription description;
        description.Name = "Simulation";
        description.Domain = Keire::JobDomain::Simulation;
        handles.push_back(jobs->Submit(std::move(description),
                                       [&, index](Keire::JobContext&)
                                       {
                                           std::scoped_lock lock(orderMutex);
                                           order.push_back(index);
                                       }));
    }
    REQUIRE(handles.back().Wait(2s));
    REQUIRE(order.size() == 64);
    for (std::size_t index = 0; index < order.size(); ++index)
        CHECK(order[index] == static_cast<int>(index));
    jobs->Close();
}

TEST_CASE("Memory domains track PMR allocations and frame ownership")
{
    auto memory = Keire::CreateRef<Keire::MemorySystem>();
    const auto domain = memory->RegisterDomain("Tests");
    auto resource = memory->CreateTrackedResource(domain);
    {
        std::pmr::vector<std::byte> bytes(resource.get());
        bytes.resize(4096);
        const auto snapshot = memory->Snapshot();
        const auto found = std::ranges::find(snapshot.Domains, domain, &Keire::MemoryDomainStatistics::Domain);
        REQUIRE(found != snapshot.Domains.end());
        CHECK(found->CurrentBytes >= 4096);
        CHECK(found->PeakBytes >= found->CurrentBytes);
        CHECK(found->AllocationCount > 0);
    }
    const auto released = memory->Snapshot();
    const auto found = std::ranges::find(released.Domains, domain, &Keire::MemoryDomainStatistics::Domain);
    REQUIRE(found != released.Domains.end());
    CHECK(found->CurrentBytes == 0);
    CHECK(found->DeallocationCount > 0);

    std::atomic<bool> rejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                memory->ResetFrameArena();
            }
            catch (const std::logic_error&)
            {
                rejected.store(true);
            }
        });
    worker.join();
    CHECK(rejected.load());
    const auto frameGeneration = memory->Frame().Generation();
    CHECK_NOTHROW(memory->Frame().Validate(frameGeneration));
    CHECK_NOTHROW(memory->ResetFrameArena());
    CHECK_FALSE(memory->Frame().IsCurrent(frameGeneration));
    CHECK_THROWS_AS(memory->Frame().Validate(frameGeneration), std::logic_error);
}

TEST_CASE("String interning is stable and concurrent")
{
    auto strings = Keire::CreateRef<Keire::StringInterner>();
    const auto first = strings->Intern("Assets/Characters/Hero");
    const auto second = strings->Intern("Assets/Characters/Hero");
    CHECK(first == second);
    CHECK(strings->Resolve(first) == "Assets/Characters/Hero");

    std::vector<std::thread> workers;
    workers.reserve(8);
    std::atomic<bool> stable = true;
    for (int index = 0; index < 8; ++index)
        workers.emplace_back(
            [&]
            {
                if (strings->Intern("Shared") != strings->Intern("Shared"))
                    stable.store(false);
            });
    for (auto& worker : workers)
        worker.join();
    CHECK(strings->Size() == 2);
    CHECK(stable.load());
}

TEST_CASE("Stable handles reject stale generations")
{
    struct TestHandleTag;
    Keire::Internal::StableHandleTable<TestHandleTag, int> values;
    const auto first = values.Emplace(12);
    CHECK(values.Contains(first));
    REQUIRE(values.Erase(first));
    CHECK_FALSE(values.Contains(first));
    const auto second = values.Emplace(42);
    CHECK(second.Index() == first.Index());
    CHECK(second.Generation() != first.Generation());
    CHECK_FALSE(values.Erase(first));
    REQUIRE(values.With(second, [](int& value) { value += 1; }));
}

TEST_CASE("Stable handle insertion rolls back both new and reused slots when construction fails")
{
    struct TestHandleTag;
    Keire::Internal::StableHandleTable<TestHandleTag, ThrowingStableValue> values;
    values.Reserve(1);
    CHECK_THROWS_AS((void)values.Emplace(true), std::runtime_error);
    const auto first = values.Emplace(false);
    CHECK(first.Index() == 0);
    REQUIRE(values.Erase(first));
    CHECK_THROWS_AS((void)values.Emplace(true), std::runtime_error);
    const auto second = values.Emplace(false);
    CHECK(second.Index() == first.Index());
    CHECK(second.Generation() != first.Generation());
}

TEST_CASE("Structured diagnostics validate IDs and retain bounded records")
{
    auto catalog = Keire::CreateRef<Keire::DiagnosticCatalog>();
    catalog->Register({.Id = Keire::DiagnosticId("KEIRE-JOBS-0001"),
                       .Title = "Job queue capacity reached",
                       .Summary = "Submission exceeded the configured bounded queue."});
    catalog->Freeze();
    CHECK(catalog->Find(Keire::DiagnosticId("KEIRE-JOBS-0001")).has_value());
    CHECK(catalog->OnlineDocumentation(Keire::DiagnosticId("KEIRE-JOBS-0001"))->find("KEIRE-JOBS-0001.md") !=
          std::string::npos);
    CHECK_THROWS_AS(catalog->Register({.Id = Keire::DiagnosticId("KEIRE-JOBS-0002"), .Title = "Late"}),
                    std::logic_error);
    CHECK_THROWS_AS(Keire::DiagnosticId("JOBS-1"), std::invalid_argument);

    auto sink = Keire::CreateRef<Keire::DiagnosticSink>(2);
    sink->Report({.Id = Keire::DiagnosticId("KEIRE-JOBS-0001"), .Message = "one"});
    sink->Report({.Id = Keire::DiagnosticId("KEIRE-JOBS-0001"), .Message = "two"});
    sink->Report({.Id = Keire::DiagnosticId("KEIRE-JOBS-0001"), .Message = "three"});
    const auto diagnostics = sink->Snapshot();
    REQUIRE(diagnostics.size() == 2);
    CHECK(diagnostics.front().Message == "two");
    CHECK(sink->DroppedCount() == 1);
}
