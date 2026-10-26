#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/RenderingAssets.h"

#include "KeireInternal/Assets/ShaderCompilerJobs.h"
#include "KeireInternal/Process.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace
{
    [[nodiscard]] std::vector<std::byte> TextBytes(const std::string& text)
    {
        const auto bytes = std::as_bytes(std::span(text));
        return {bytes.begin(), bytes.end()};
    }

    struct CompilerCancellationFixture final
    {
        CompilerCancellationFixture() : Root(KeireTests::MakeTestDirectory("ShaderCancellation"))
        {
            std::filesystem::create_directories(Root);
        }

        ~CompilerCancellationFixture()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        std::filesystem::path Root;
    };
} // namespace

TEST_CASE("shader compiler cancellation rejects stopped work before process launch or source reads")
{
    std::stop_source cancellation;
    REQUIRE(cancellation.request_stop());
    CHECK_THROWS_AS(
        Keire::Detail::RunShaderCompilerProcess({}, {}, {}, std::chrono::seconds(1), cancellation.get_token()),
        Keire::AssetOperationCancelled);

    Keire::ShaderImporterSpecification specification;
    const auto importer = Keire::Detail::CreateCancellableShaderAssetImporter(specification, cancellation.get_token());
    CHECK_THROWS_AS(importer.ContextualImport({}, {}), Keire::AssetOperationCancelled);
}

TEST_CASE("shader compiler process keeps success failure diagnostics and timeout semantics")
{
    const auto directory = KeireTests::TestExecutable.parent_path();
    const std::array success{std::string("--child-process-success")};
    CHECK_NOTHROW(Keire::Detail::RunShaderCompilerProcess(KeireTests::TestExecutable, success, directory,
                                                          std::chrono::seconds(5), {}));

    const std::array failure{std::string("--child-process-probe")};
    CHECK_THROWS_WITH(Keire::Detail::RunShaderCompilerProcess(KeireTests::TestExecutable, failure, directory,
                                                              std::chrono::seconds(5), {}),
                      doctest::Contains("exit code 23.\nchild-process-output"));

    const std::array hanging{std::string("--child-process-hang")};
    CHECK_THROWS_WITH(Keire::Detail::RunShaderCompilerProcess(KeireTests::TestExecutable, hanging, directory,
                                                              std::chrono::milliseconds(100), {}),
                      doctest::Contains("timed out after 100 ms"));
}

TEST_CASE("shader compiler cancellation terminates an already running owned process")
{
    CompilerCancellationFixture fixture;
    const auto marker = fixture.Root / "started.txt";
    std::string mode;
    SUBCASE("quiet native compiler") { mode = "--child-process-record-hang"; }
    SUBCASE("continuous diagnostic output cannot monopolize polling") { mode = "--child-process-record-flood"; }
    const std::array arguments{mode, marker.string()};
    std::stop_source cancellation;
    std::atomic<std::uint64_t> childId{0};
    std::jthread stopper(
        [&](const std::stop_token stopped)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!stopped.stop_requested() && std::chrono::steady_clock::now() < deadline)
            {
                std::uint64_t observedId = 0;
                if (std::ifstream(marker) >> observedId)
                {
                    childId = observedId;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            (void)cancellation.request_stop();
        });

    const auto started = std::chrono::steady_clock::now();
    CHECK_THROWS_AS(Keire::Detail::RunShaderCompilerProcess(KeireTests::TestExecutable, arguments, fixture.Root,
                                                            std::chrono::seconds(10), cancellation.get_token()),
                    Keire::AssetOperationCancelled);
    stopper.join();
    REQUIRE(childId.load() != 0);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(5));
    CHECK_FALSE(Keire::Detail::IsProcessAlive(childId));
}

TEST_CASE("shader importer cancellation stops both parallel native stages and joins before returning")
{
    CompilerCancellationFixture fixture;
    const auto templateRoot = std::filesystem::current_path() / "KeireHubContent/Templates/Payloads/Starter3D";
    auto manifest =
        nlohmann::json::parse(KeireTests::ReadFile(templateRoot / "Assets/Shaders/DefaultUnlit.keireshader"));
    const std::filesystem::path sourcePath = "Assets/Shaders/cancellation-probe.hlsl";
    manifest["source"] = sourcePath.generic_string();
    const auto source = "// cancellation-marker-root=" + fixture.Root.generic_string() + '\n' +
                        KeireTests::ReadFile(templateRoot / "Assets/Shaders/StarterUnlit.hlsl");

    Keire::AssetImportContext context;
    context.ProjectRoot = fixture.Root;
    context.SourceRoot = fixture.Root / "Assets";
    context.RelativePath = "Shaders/cancellation-probe.keireshader";
    context.SourcePath = context.SourceRoot / context.RelativePath;
    context.ReadProjectFile = [&](const std::filesystem::path& requested)
    {
        if (requested != sourcePath)
        {
            throw std::runtime_error("Unexpected cancellation fixture source.");
        }
        return TextBytes(source);
    };
    Keire::ShaderImporterSpecification specification;
    specification.Compiler = KeireTests::TestExecutable;
    specification.Formats = {Keire::ShaderBinaryFormat::SpirV};
    specification.Timeout = std::chrono::seconds(10);
    std::stop_source cancellation;
    const auto importer = Keire::Detail::CreateCancellableShaderAssetImporter(specification, cancellation.get_token());
    std::array<std::uint64_t, 2> childIds{};
    std::jthread stopper(
        [&](const std::stop_token stopped)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!stopped.stop_requested() && std::chrono::steady_clock::now() < deadline)
            {
                (void)(std::ifstream(fixture.Root / "vertex.txt") >> childIds[0]);
                (void)(std::ifstream(fixture.Root / "fragment.txt") >> childIds[1]);
                if (childIds[0] != 0 && childIds[1] != 0)
                {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            (void)cancellation.request_stop();
        });
    const auto started = std::chrono::steady_clock::now();
    CHECK_THROWS_AS(importer.ContextualImport(context, TextBytes(manifest.dump())), Keire::AssetOperationCancelled);
    stopper.join();
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(5));
    for (const auto childId : childIds)
    {
        REQUIRE(childId != 0);
        CHECK_FALSE(Keire::Detail::IsProcessAlive(childId));
    }
}
