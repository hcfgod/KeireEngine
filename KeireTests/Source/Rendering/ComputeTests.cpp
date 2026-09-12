#include "Keire/Rendering/Compute.h"

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <stdexcept>
#include <thread>

TEST_CASE("compute devices reject invalid input before initializing native services")
{
    Keire::ComputeDevice device(Keire::ProgramBackend::Vulkan);
    CHECK(device.IsOpen());
    CHECK_THROWS_AS(device.CreateBuffer(0), std::invalid_argument);
    CHECK_THROWS_AS(device.CreateBuffer(3), std::invalid_argument);
    CHECK_THROWS_AS(device.CreateBuffer(4, true), std::invalid_argument);
    CHECK_THROWS_AS(device.CreateBuffer(128U * 1024U * 1024U + 4), std::invalid_argument);
    CHECK_THROWS_AS(device.DestroyBuffer({}), std::invalid_argument);
    CHECK_THROWS_AS(device.DestroyPipeline({}), std::invalid_argument);
    CHECK_THROWS_AS(device.IsComplete({}), std::invalid_argument);
    CHECK_THROWS_AS(device.Wait({}), std::invalid_argument);
    CHECK_THROWS_AS(device.ReleaseSubmission({}), std::invalid_argument);
    CHECK_THROWS_AS(device.Readback({}), std::invalid_argument);
    const std::array<std::byte, 4> bytes{};
    CHECK_THROWS_AS(device.Upload({}, bytes), std::invalid_argument);
    CHECK_THROWS_AS(device.Dispatch({}, {}, {0, 1, 1}), std::invalid_argument);
    CHECK_THROWS_AS(device.Dispatch({}, {}, {65536, 1, 1}), std::invalid_argument);
    CHECK_THROWS_AS(device.DispatchIndirect({}, {}, {}), std::invalid_argument);
    CHECK_THROWS_AS(device.CreatePipeline({}), std::invalid_argument);
    CHECK(device.IsOpen());
    CHECK_NOTHROW(device.WaitIdle());
    CHECK_NOTHROW(device.Shutdown());
}

TEST_CASE("compute shutdown is idempotent and all subsequent mutations reject")
{
    Keire::ComputeDevice device(Keire::ProgramBackend::D3D12);
    device.Shutdown();
    CHECK_FALSE(device.IsOpen());
    CHECK_NOTHROW(device.Shutdown());
    CHECK_THROWS_AS(device.CreateBuffer(16), std::logic_error);
    CHECK_THROWS_AS(device.CreatePipeline({}), std::logic_error);
    CHECK_THROWS_AS(device.DestroyBuffer({}), std::logic_error);
    CHECK_THROWS_AS(device.DestroyPipeline({}), std::logic_error);
    CHECK_THROWS_AS(device.Dispatch({}, {}, {}), std::logic_error);
    CHECK_THROWS_AS(device.Readback({}), std::logic_error);
    CHECK_THROWS_AS(device.WaitIdle(), std::logic_error);
    CHECK_THROWS_AS(device.IsComplete({}), std::logic_error);
}

TEST_CASE("compute owner thread rejection leaves the device open")
{
    Keire::ComputeDevice device(Keire::ProgramBackend::Vulkan);
    unsigned rejected = 0;
    std::thread worker(
        [&]
        {
            const auto reject = [&](auto&& operation)
            {
                try
                {
                    operation();
                }
                catch (const std::logic_error&)
                {
                    ++rejected;
                }
            };
            reject([&] { device.Shutdown(); });
            reject([&] { (void)device.CreateBuffer(16); });
            reject([&] { device.WaitIdle(); });
            reject([&] { (void)device.Dispatch({}, {}, {}); });
        });
    worker.join();
    CHECK(rejected == 4);
    CHECK(device.IsOpen());
    CHECK_NOTHROW(device.WaitIdle());
    CHECK_NOTHROW(device.Shutdown());
}

TEST_CASE("compute backend values are validated eagerly")
{
    CHECK_THROWS_AS(Keire::ComputeDevice(static_cast<Keire::ProgramBackend>(255)), std::invalid_argument);
}
