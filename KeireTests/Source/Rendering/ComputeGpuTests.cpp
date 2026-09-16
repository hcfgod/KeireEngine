#include "Keire/Rendering/Compute.h"
#include "Keire/Rendering/ComputeCompilation.h"
#include "Keire/Window.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    void ExerciseComputeBackend(const Keire::ProgramBackend backend)
    {
        auto windows = Keire::CreateRef<Keire::WindowSystem>();
        auto graph = Keire::CreateTargetShaderGraph(Keire::ShaderGraphTarget::Compute);
        const auto color = std::ranges::find(graph.Nodes.front().Pins, "Color", &Keire::ShaderGraphPin::Name);
        REQUIRE(color != graph.Nodes.front().Pins.end());
        color->DefaultValue = Keire::Color{0.25F, 0.5F, 0.75F, 1.0F};
        const auto source = Keire::CompileShaderGraphProgram(graph);
        REQUIRE(source.Succeeded());
        Keire::ComputeCompilationOptions options;
#if defined(_WIN32)
        options.Compiler = std::filesystem::current_path() / "Build/Tools/ShaderCompiler/KeireShaderCompiler.exe";
#else
        options.Compiler = std::filesystem::current_path() / "Build/Tools/ShaderCompiler/KeireShaderCompiler";
#endif
        options.Backends = {backend};
        const auto compiled = Keire::CompileComputeProgramBinaries(source, options);
        Keire::ComputeDevice device(backend, true);
        const auto pipeline = device.CreatePipeline(compiled);
        constexpr std::uint32_t elementCount = 257;
        constexpr std::uint32_t bufferBytes = elementCount * 16;
        const auto output = device.CreateBuffer(bufferBytes);
        const std::vector<std::byte> zeros(bufferBytes);
        device.Upload(output, zeros);
        const std::array bindings{Keire::ComputeBufferBinding{0, output, true}};
        const auto submission = device.Dispatch(pipeline, bindings, {5, 1, 1});
        CHECK_THROWS_AS(device.GetReadback(submission), std::invalid_argument);
        device.Wait(submission);
        CHECK(device.IsComplete(submission));
        device.ReleaseSubmission(submission);
        CHECK_THROWS_AS(device.Wait(submission), std::invalid_argument);
        const auto verify = [&]
        {
            const auto bytes = device.Readback(output);
            REQUIRE(bytes.size() == bufferBytes);
            std::array<float, elementCount * 4> values{};
            std::memcpy(values.data(), bytes.data(), bytes.size());
            for (std::size_t index = 0; index < values.size(); index += 4)
            {
                CHECK(values[index] == doctest::Approx(0.25F));
                CHECK(values[index + 1] == doctest::Approx(0.5F));
                CHECK(values[index + 2] == doctest::Approx(0.75F));
                CHECK(values[index + 3] == doctest::Approx(1.0F));
            }
        };
        verify();

        // A fresh upload followed by indirect submission exercises ordered write-after-write and readback hazards.
        device.Upload(output, zeros);
        const auto arguments = device.CreateBuffer(12, true);
        const std::array<std::uint32_t, 3> groups{5, 1, 1};
        device.Upload(arguments, std::as_bytes(std::span(groups)));
        const auto indirect = device.DispatchIndirect(pipeline, bindings, arguments);
        device.Wait(indirect);
        verify();
        device.ReleaseSubmission(indirect);

        const auto beforeRejected = device.Readback(output);
        CHECK_THROWS_AS(device.Dispatch(pipeline, {}, {1, 1, 1}), std::invalid_argument);
        CHECK_THROWS_AS(device.Dispatch(pipeline, bindings, {0, 1, 1}), std::invalid_argument);
        CHECK_THROWS_AS(device.DispatchIndirect(pipeline, bindings, output), std::invalid_argument);
        for (const auto invalidX : {0U, 65536U})
        {
            const std::array<std::uint32_t, 3> invalidGroups{invalidX, 1, 1};
            device.Upload(arguments, std::as_bytes(std::span(invalidGroups)));
            CHECK_THROWS_AS(device.DispatchIndirect(pipeline, bindings, arguments), std::invalid_argument);
        }
        unsigned rejected = 0;
        std::thread worker(
            [&]
            {
                try
                {
                    device.DestroyBuffer(output);
                }
                catch (const std::logic_error&)
                {
                    ++rejected;
                }
                try
                {
                    (void)device.Readback(output);
                }
                catch (const std::logic_error&)
                {
                    ++rejected;
                }
            });
        worker.join();
        CHECK(rejected == 2);
        CHECK(device.Readback(output) == beforeRejected);
        Keire::ComputeDevice other(backend);
        CHECK_THROWS_AS(other.Readback(output), std::invalid_argument);
        other.Shutdown();

        CHECK_THROWS_AS(device.ReloadPipeline(pipeline, {}), std::invalid_argument);
        CHECK(device.IsOpen());
        const auto retained = device.Dispatch(pipeline, bindings, {5, 1, 1});
        device.ReloadPipeline(pipeline, compiled);
        device.ReleaseSubmission(retained);
        verify();
        const auto snapshot = device.RequestReadback(output, 16, 16);
        device.Upload(output, zeros);
        device.DestroyBuffer(output);
        device.Wait(snapshot);
        CHECK(device.IsComplete(snapshot));
        const auto snapshotBytes = device.GetReadback(snapshot);
        CHECK(snapshotBytes == device.GetReadback(snapshot));
        REQUIRE(snapshotBytes.size() == 16);
        std::array<float, 4> snapshotValues{};
        std::memcpy(snapshotValues.data(), snapshotBytes.data(), snapshotBytes.size());
        CHECK(snapshotValues[0] == doctest::Approx(0.25F));
        CHECK(snapshotValues[3] == doctest::Approx(1.0F));
        device.ReleaseSubmission(snapshot);
        CHECK_THROWS_AS(device.GetReadback(snapshot), std::invalid_argument);
        device.DestroyBuffer(arguments);
        CHECK_THROWS_AS(device.Readback(output), std::invalid_argument);
        device.DestroyPipeline(pipeline);
        CHECK_THROWS_AS(device.Dispatch(pipeline, bindings, {1, 1, 1}), std::invalid_argument);
        device.Shutdown();
        CHECK_FALSE(device.IsOpen());
        CHECK_NOTHROW(device.Shutdown());
        while (windows->PollEvent())
        {
        }
        windows->Shutdown();
    }
} // namespace

// Hardware acceptance is opt-in: run these selected cases with --no-skip. Unsupported requested devices fail.
#if defined(_WIN32)
TEST_CASE("compute GPU D3D12 compiles dispatches and reads back actual kernel results" * doctest::skip())
{
    ExerciseComputeBackend(Keire::ProgramBackend::D3D12);
}
#endif
#if !defined(__APPLE__)
TEST_CASE("compute GPU Vulkan compiles dispatches and reads back actual kernel results" * doctest::skip())
{
    ExerciseComputeBackend(Keire::ProgramBackend::Vulkan);
}
#else
TEST_CASE("compute GPU Metal compiles dispatches and reads back actual kernel results" * doctest::skip())
{
    ExerciseComputeBackend(Keire::ProgramBackend::Metal);
}
#endif
