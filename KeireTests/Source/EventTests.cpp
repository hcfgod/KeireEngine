#include "Keire/Event.h"

#include <doctest/doctest.h>

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace
{
    struct TestEvent
    {
        int Value = 0;
    };

    struct ProducerEvent
    {
        std::size_t Producer = 0;
        std::size_t Sequence = 0;
    };

    struct MoveOnlyEvent
    {
        std::unique_ptr<int> Value;
    };
} // namespace

TEST_CASE("EventBus merges typed and generic listeners by priority and stops handled propagation")
{
    auto bus = Keire::CreateRef<Keire::EventBus>();
    std::vector<int> order;
    auto low = bus->Subscribe<TestEvent>(
        [&order](const TestEvent&)
        {
            order.push_back(4);
            return Keire::EventFlow::Continue;
        },
        Keire::EventPriorities::Low);
    auto normalFirst = bus->Subscribe<TestEvent>(
        [&order](const TestEvent&)
        {
            order.push_back(2);
            return Keire::EventFlow::Handled;
        });
    auto normalSecond = bus->Subscribe<TestEvent>(
        [&order](const TestEvent&)
        {
            order.push_back(3);
            return Keire::EventFlow::Continue;
        });
    auto any = bus->SubscribeAny(
        [&order](const Keire::EventView& event)
        {
            CHECK(event.Is<TestEvent>());
            REQUIRE(event.TryGet<TestEvent>() != nullptr);
            CHECK(event.TryGet<TestEvent>()->Value == 42);
            order.push_back(1);
            return Keire::EventFlow::Continue;
        },
        Keire::EventPriorities::High);

    CHECK(bus->Dispatch(TestEvent{42}));
    CHECK(order == std::vector<int>{1, 2});
    CHECK(bus->Statistics().DispatchedEvents == 1);
    CHECK(low.Connected());
    CHECK(normalFirst.Connected());
    CHECK(normalSecond.Connected());
    CHECK(any.Connected());
}

TEST_CASE("EventBus applies subscription mutation at stable dispatch boundaries and permits nesting")
{
    auto bus = Keire::CreateRef<Keire::EventBus>();
    std::vector<int> order;
    std::optional<Keire::EventSubscription> late;
    auto removed = bus->Subscribe<TestEvent>(
        [&order](const TestEvent&)
        {
            order.push_back(3);
            return Keire::EventFlow::Continue;
        },
        Keire::EventPriorities::Low);
    bool mutated = false;
    auto first = bus->Subscribe<TestEvent>(
        [&](const TestEvent& event)
        {
            order.push_back(event.Value);
            if (!mutated)
            {
                mutated = true;
                removed.Disconnect();
                late.emplace(bus->Subscribe<TestEvent>(
                    [&order](const TestEvent&)
                    {
                        order.push_back(4);
                        return Keire::EventFlow::Continue;
                    },
                    Keire::EventPriorities::Low));
                CHECK_FALSE(bus->Dispatch(TestEvent{2}));
            }
            return Keire::EventFlow::Continue;
        });

    CHECK_FALSE(bus->Dispatch(TestEvent{1}));
    CHECK(order == std::vector<int>{1, 2, 4});
    order.clear();
    CHECK_FALSE(bus->Dispatch(TestEvent{5}));
    CHECK(order == std::vector<int>{5, 4});
    CHECK(first.Connected());
    CHECK(late->Connected());
}

TEST_CASE("EventBus queue is bounded and drains a fixed snapshot")
{
    Keire::EventBusSpecification specification;
    specification.QueueCapacity = 2;
    auto bus = Keire::CreateRef<Keire::EventBus>(specification);
    std::vector<int> received;
    auto subscription = bus->Subscribe<TestEvent>(
        [&](const TestEvent& event)
        {
            received.push_back(event.Value);
            if (event.Value == 1)
            {
                CHECK(bus->TryEnqueue(TestEvent{3}));
            }
            return Keire::EventFlow::Continue;
        });

    CHECK(bus->TryEnqueue(TestEvent{1}));
    CHECK(bus->TryEnqueue(TestEvent{2}));
    CHECK_FALSE(bus->TryEnqueue(TestEvent{99}));
    CHECK(bus->DispatchQueued() == 2);
    CHECK(received == std::vector<int>{1, 2});
    CHECK(bus->Statistics().QueueSize == 1);
    CHECK(bus->Statistics().QueueHighWaterMark == 2);
    CHECK(bus->Statistics().DroppedEvents == 1);
    CHECK(bus->DispatchQueued() == 1);
    CHECK(received == std::vector<int>{1, 2, 3});
    CHECK(subscription.Connected());
}

TEST_CASE("EventBus supports move-only queued events and inert retained handles after close")
{
    auto bus = Keire::CreateRef<Keire::EventBus>();
    int received = 0;
    auto subscription = bus->Subscribe<MoveOnlyEvent>(
        [&received](const MoveOnlyEvent& event)
        {
            received = *event.Value;
            return Keire::EventFlow::Continue;
        });
    CHECK(bus->TryEnqueue(MoveOnlyEvent{std::make_unique<int>(17)}));
    CHECK(bus->DispatchQueued() == 1);
    CHECK(received == 17);

    bus->Close();
    CHECK_FALSE(bus->IsOpen());
    CHECK_FALSE(bus->TryEnqueue(TestEvent{}));
    CHECK_FALSE(subscription.Connected());
    CHECK_NOTHROW(subscription.Disconnect());
}

TEST_CASE("EventBus enforces owner operations while allowing worker enqueue and disconnect")
{
    auto bus = Keire::CreateRef<Keire::EventBus>();
    int received = 0;
    auto subscription = bus->Subscribe<TestEvent>(
        [&received](const TestEvent& event)
        {
            received += event.Value;
            return Keire::EventFlow::Continue;
        });
    bool dispatchRejected = false;
    bool subscribeRejected = false;
    bool enqueued = false;
    std::thread worker(
        [&]
        {
            enqueued = bus->TryEnqueue(TestEvent{9});
            try
            {
                (void)bus->Dispatch(TestEvent{});
            }
            catch (const std::logic_error&)
            {
                dispatchRejected = true;
            }
            try
            {
                auto invalid = bus->Subscribe<TestEvent>([](const TestEvent&) { return Keire::EventFlow::Continue; });
                (void)invalid;
            }
            catch (const std::logic_error&)
            {
                subscribeRejected = true;
            }
        });
    worker.join();

    CHECK(enqueued);
    CHECK(dispatchRejected);
    CHECK(subscribeRejected);
    CHECK(bus->DispatchQueued() == 1);
    CHECK(received == 9);

    std::thread disconnectWorker([token = std::move(subscription)]() mutable { token.Disconnect(); });
    disconnectWorker.join();
    CHECK_FALSE(bus->Dispatch(TestEvent{10}));
    CHECK(received == 9);
}

TEST_CASE("EventBus preserves each producer's queued order")
{
    constexpr std::size_t producerCount = 4;
    constexpr std::size_t eventsPerProducer = 100;
    Keire::EventBusSpecification specification;
    specification.QueueCapacity = producerCount * eventsPerProducer;
    auto bus = Keire::CreateRef<Keire::EventBus>(specification);
    std::array<std::size_t, producerCount> next{};
    auto subscription = bus->Subscribe<ProducerEvent>(
        [&next](const ProducerEvent& event)
        {
            REQUIRE(event.Producer < next.size());
            CHECK(event.Sequence == next[event.Producer]++);
            return Keire::EventFlow::Continue;
        });

    std::vector<std::thread> producers;
    std::atomic<bool> enqueueSucceeded = true;
    for (std::size_t producer = 0; producer < producerCount; ++producer)
    {
        producers.emplace_back(
            [bus, producer, &enqueueSucceeded]
            {
                for (std::size_t sequence = 0; sequence < eventsPerProducer; ++sequence)
                {
                    if (!bus->TryEnqueue(ProducerEvent{producer, sequence}))
                    {
                        enqueueSucceeded.store(false, std::memory_order_release);
                    }
                }
            });
    }
    for (auto& producer : producers)
    {
        producer.join();
    }
    REQUIRE(enqueueSucceeded.load(std::memory_order_acquire));
    CHECK(bus->DispatchQueued() == producerCount * eventsPerProducer);
    for (const auto count : next)
    {
        CHECK(count == eventsPerProducer);
    }
    CHECK(subscription.Connected());
}
