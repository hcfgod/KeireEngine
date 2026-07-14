#include "doctest/doctest.h"

#include "Keire/Ref.h"

#include <atomic>
#include <concepts>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace
{
struct RefProbe : Keire::RefCounted
{
    explicit RefProbe(std::atomic<int>& destructions) : Destructions(destructions) {}
    ~RefProbe() override { Destructions.fetch_add(1); }

    std::atomic<int>& Destructions;
    int Value = 42;
};

struct RefBase : Keire::RefCounted
{
    virtual int GetValue() const noexcept { return 1; }
};

struct RefDerived final : RefBase
{
    explicit RefDerived(std::atomic<int>& destructions) : Destructions(destructions) {}
    ~RefDerived() override { Destructions.fetch_add(1); }
    int GetValue() const noexcept override { return 2; }

    std::atomic<int>& Destructions;
};

struct CycleNode final : Keire::RefCounted
{
    explicit CycleNode(std::atomic<int>& destructions) : Destructions(destructions) {}
    ~CycleNode() override { Destructions.fetch_add(1); }

    std::atomic<int>& Destructions;
    Keire::Ref<CycleNode> Child;
    Keire::WeakRef<CycleNode> Parent;
};

struct ThrowingRef final : Keire::RefCounted
{
    ThrowingRef() { throw std::runtime_error("construction failed"); }
};

struct IncompleteRef;

struct IncompleteOwner
{
    ~IncompleteOwner();

    Keire::Ref<IncompleteRef> Strong;
    Keire::WeakRef<IncompleteRef> Weak;
};

struct IncompleteRef final : Keire::RefCounted
{
};

IncompleteOwner::~IncompleteOwner() = default;

static_assert(!std::copy_constructible<Keire::RefCounted>);
static_assert(!std::movable<Keire::RefCounted>);
static_assert(!std::constructible_from<Keire::Ref<RefProbe>, RefProbe*>);
} // namespace

TEST_CASE("Ref supports null, copy, move, reset, and self-assignment")
{
    std::atomic<int> destructions = 0;
    Keire::Ref<RefProbe> empty;
    CHECK_FALSE(empty);
    CHECK(empty == nullptr);
    CHECK(empty.UseCount() == 0);

    auto reference = Keire::CreateRef<RefProbe>(destructions);
    CHECK(reference);
    CHECK(reference != nullptr);
    CHECK(reference.UseCount() == 1);
    CHECK(reference->Value == 42);

    auto copy = reference;
    CHECK(copy == reference);
    CHECK(reference.UseCount() == 2);
    copy = copy;
    CHECK(copy.UseCount() == 2);
    copy = std::move(copy);
    CHECK(copy.UseCount() == 2);

    auto moved = std::move(copy);
    CHECK_FALSE(copy);
    CHECK(moved.UseCount() == 2);
    moved.Reset();
    CHECK(reference.UseCount() == 1);
    reference = nullptr;
    CHECK(destructions.load() == 1);
}

TEST_CASE("Ref supports members declared with incomplete object types")
{
    IncompleteOwner owner;
    owner.Strong = Keire::CreateRef<IncompleteRef>();
    owner.Weak = owner.Strong;
    CHECK(owner.Weak.Lock() == owner.Strong);
}

TEST_CASE("Ref polymorphic conversion retains the original deleter")
{
    std::atomic<int> destructions = 0;
    auto derived = Keire::CreateRef<RefDerived>(destructions);
    Keire::Ref<RefBase> base = derived;
    Keire::WeakRef<RefBase> weakBase = derived;
    CHECK(base->GetValue() == 2);
    CHECK(base.UseCount() == 2);
    CHECK(weakBase.Lock()->GetValue() == 2);

    Keire::Ref<RefBase> movedBase = std::move(derived);
    CHECK_FALSE(derived);
    movedBase.Reset();
    CHECK(base.UseCount() == 1);
    base.Reset();
    CHECK(destructions.load() == 1);
}

TEST_CASE("WeakRef locks while alive and expires after the final strong reference")
{
    std::atomic<int> destructions = 0;
    auto strong = Keire::CreateRef<RefProbe>(destructions);
    Keire::WeakRef<RefProbe> weak = strong;
    CHECK_FALSE(weak.Expired());
    CHECK(weak.UseCount() == 1);

    {
        auto locked = weak.Lock();
        REQUIRE(locked);
        CHECK(locked->Value == 42);
        CHECK(weak.UseCount() == 2);
    }

    strong.Reset();
    CHECK(weak.Expired());
    CHECK_FALSE(weak.Lock());
    CHECK(destructions.load() == 1);
    weak.Reset();
}

TEST_CASE("WeakRef breaks ownership cycles")
{
    std::atomic<int> destructions = 0;
    auto parent = Keire::CreateRef<CycleNode>(destructions);
    auto child = Keire::CreateRef<CycleNode>(destructions);
    parent->Child = child;
    child->Parent = parent;

    child.Reset();
    parent.Reset();
    CHECK(destructions.load() == 2);
}

TEST_CASE("CreateRef propagates constructor exceptions")
{
    CHECK_THROWS_AS(Keire::CreateRef<ThrowingRef>(), std::runtime_error);
}

TEST_CASE("Ref atomic ownership supports concurrent copying")
{
    std::atomic<int> destructions = 0;
    auto root = Keire::CreateRef<RefProbe>(destructions);
    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int threadIndex = 0; threadIndex < 8; ++threadIndex)
    {
        threads.emplace_back(
            [root]()
            {
                for (int iteration = 0; iteration < 2000; ++iteration)
                {
                    auto copy = root;
                    CHECK(copy->Value == 42);
                }
            });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
    CHECK(root.UseCount() == 1);
    root.Reset();
    CHECK(destructions.load() == 1);
}

TEST_CASE("WeakRef locking races safely with final strong release")
{
    std::atomic<int> destructions = 0;
    auto strong = Keire::CreateRef<RefProbe>(destructions);
    Keire::WeakRef<RefProbe> weak = strong;
    std::atomic<bool> start = false;

    std::vector<std::thread> lockers;
    lockers.reserve(4);
    for (int threadIndex = 0; threadIndex < 4; ++threadIndex)
    {
        lockers.emplace_back(
            [&]()
            {
                while (!start.load(std::memory_order_acquire))
                {
                    std::this_thread::yield();
                }
                for (int iteration = 0; iteration < 2000; ++iteration)
                {
                    auto locked = weak.Lock();
                    if (locked)
                    {
                        CHECK(locked->Value == 42);
                    }
                }
            });
    }

    start.store(true, std::memory_order_release);
    strong.Reset();
    for (auto& locker : lockers)
    {
        locker.join();
    }

    CHECK(weak.Expired());
    CHECK(destructions.load() == 1);
}
