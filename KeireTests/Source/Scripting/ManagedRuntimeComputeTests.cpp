#include "KeireInternal/Scripting/ManagedRuntimeCompute.h"

#include <doctest/doctest.h>

#include <atomic>
#include <stdexcept>
#include <thread>

TEST_CASE("managed compute bridge isolates owners and rejects invalid work before backend initialization")
{
    Keire::Detail::ManagedComputeStore first;
    Keire::Detail::ManagedComputeStore second;
    const auto device = first.Command(0, 0, 0, 0, {});
    const auto other = second.Command(0, 0, 0, 0, {});
    CHECK(device != other);
    CHECK_THROWS_AS(first.Command(other, 2, 0, 0, {}), std::out_of_range);
    CHECK_THROWS_AS(first.Command(device, 2, 0, 0, {}), std::invalid_argument);
    CHECK_THROWS_AS(first.Command(device, 4, 0, 0, {}), std::out_of_range);
    CHECK_THROWS_AS(first.Command(device, 11, 0, 0, {}), std::invalid_argument);
    CHECK_THROWS_AS(first.Command(device, 12, 0, 0, {}), std::invalid_argument);
    CHECK_THROWS_AS(first.Command(device, 13, 0, 0, {}), std::out_of_range);
    CHECK_THROWS_AS(first.RegisterProgram({}), std::invalid_argument);
    CHECK_THROWS_AS(first.Command(0, 0, 255, 0, {}), std::invalid_argument);
    CHECK_NOTHROW(first.Command(device, 1, 0, 0, {}));
    CHECK_NOTHROW(first.Command(device, 1, 0, 0, {}));
    CHECK_NOTHROW(second.Command(other, 1, 0, 0, {}));
}

TEST_CASE("managed compute wrong thread rejection preserves the original lazy device")
{
    Keire::Detail::ManagedComputeStore store;
    const auto device = store.Command(0, 0, 0, 0, {});
    std::atomic<bool> rejected = false;
    std::thread worker(
        [&]
        {
            try
            {
                (void)store.Command(device, 1, 0, 0, {});
            }
            catch (const std::logic_error&)
            {
                rejected = true;
            }
        });
    worker.join();
    CHECK(rejected.load());
    // The preserved device reaches buffer argument validation, rather than failing an absent-device lookup.
    CHECK_THROWS_AS(store.Command(device, 2, 0, 0, {}), std::invalid_argument);
    CHECK_NOTHROW(store.Command(device, 1, 0, 0, {}));
}

TEST_CASE("managed compute scopes fail closed while reload is preparing and restore active work")
{
    using Keire::Detail::InvokeManagedComputeCommand;
    std::uint64_t device = 0;
    CHECK(InvokeManagedComputeCommand(0, 0, 0, 0, nullptr, 0, &device) == 0);
    Keire::Detail::ManagedComputeStore store;
    {
        const Keire::Detail::ManagedRuntimeComputeScope active(&store);
        REQUIRE(InvokeManagedComputeCommand(0, 0, 0, 0, nullptr, 0, &device) == 1);
        {
            const Keire::Detail::ManagedRuntimeComputeScope candidate(&store, false);
            std::uint64_t result = 0;
            CHECK(InvokeManagedComputeCommand(0, 0, 0, 0, nullptr, 0, &result) == 0);
            CHECK(InvokeManagedComputeCommand(device, 2, 16, 0, nullptr, 0, &result) == 0);
            CHECK(InvokeManagedComputeCommand(device, 11, 0, 0, nullptr, 0, &result) == 0);
            CHECK(InvokeManagedComputeCommand(device, 12, 0, 0, nullptr, 0, &result) == 0);
        }
        // Rejected candidate work cannot destroy the active resource.
        CHECK_THROWS_AS(store.Command(device, 2, 0, 0, {}), std::invalid_argument);
        std::uint64_t replacement = 0;
        REQUIRE(InvokeManagedComputeCommand(0, 0, 0, 0, nullptr, 0, &replacement) == 1);
        CHECK(replacement != device);
        store.ClearResources();
        CHECK_THROWS_AS(store.Command(device, 2, 16, 0, {}), std::out_of_range);
        std::uint64_t result = 0;
        CHECK(InvokeManagedComputeCommand(device, 1, 0, 0, nullptr, 0, &result) == 1);
        CHECK(InvokeManagedComputeCommand(device, 3, 42, 0, nullptr, 0, &result) == 1);
        CHECK(InvokeManagedComputeCommand(device, 5, 43, 0, nullptr, 0, &result) == 1);
        CHECK(InvokeManagedComputeCommand(device, 10, 44, 0, nullptr, 0, &result) == 1);
    }
    CHECK(InvokeManagedComputeCommand(0, 0, 0, 0, nullptr, 0, &device) == 0);
}
