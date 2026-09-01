#include "doctest/doctest.h"

#include "KeireTests/TestSupport.h"

#include "KeireInternal/Assets/ShaderCompilerJobs.h"
#include "KeireInternal/Process.h"

#include "Keire/Assets/Asset.h"
#include "Keire/Assets/ShaderCompilation.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace
{
    struct TemporaryDirectory final
    {
        TemporaryDirectory() : Path(KeireTests::MakeTestDirectory("ShaderCompilerJobs"))
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
            std::filesystem::create_directories(Path);
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }

        [[nodiscard]] std::filesystem::path CreateJob() const
        {
            const auto path = Path / Keire::AssetId::Generate().ToString();
            std::filesystem::create_directories(path);
            return path;
        }

        std::filesystem::path Path;
    };
} // namespace

TEST_CASE("shader compiler cleanup removes only stale abandoned owned jobs")
{
    TemporaryDirectory directory;
    const auto now = std::filesystem::file_time_type::clock::now();
    const auto stale = directory.CreateJob();
    const auto recent = directory.CreateJob();
    const auto future = directory.CreateJob();
    const auto active = directory.CreateJob();
    const auto unknown = directory.Path / "publisher-cache";
    std::filesystem::create_directories(unknown);
    const auto ownedFile = directory.Path / Keire::AssetId::Generate().ToString();
    std::ofstream(ownedFile, std::ios::binary) << "not a directory";

    Keire::Detail::WriteShaderCompilerJobLease(active, Keire::Detail::CurrentProcessId());
    std::filesystem::last_write_time(stale, now - std::chrono::hours(3));
    std::filesystem::last_write_time(recent, now - std::chrono::minutes(30));
    std::filesystem::last_write_time(future, now + std::chrono::minutes(1));
    std::filesystem::last_write_time(active, now - std::chrono::hours(3));
    std::filesystem::last_write_time(unknown, now - std::chrono::hours(3));

    const auto result = Keire::Detail::CleanupStaleShaderCompilerJobs(directory.Path, now, std::chrono::hours(1));

    CHECK(result.Removed == 1);
    CHECK(result.Retained == 3);
    CHECK(result.Ignored == 2);
    CHECK_FALSE(std::filesystem::exists(stale));
    CHECK(std::filesystem::is_directory(recent));
    CHECK(std::filesystem::is_directory(future));
    CHECK(std::filesystem::is_directory(active));
    CHECK(std::filesystem::is_directory(unknown));
    CHECK(std::filesystem::is_regular_file(ownedFile));
}

TEST_CASE("shader compiler cleanup rejects unsafe roots and invalid leases")
{
    TemporaryDirectory directory;
    const auto fileRoot = directory.Path / "not-a-directory";
    std::ofstream(fileRoot, std::ios::binary) << "sentinel";
    const auto now = std::filesystem::file_time_type::clock::now();

    CHECK_THROWS_WITH_AS((void)Keire::Detail::CleanupStaleShaderCompilerJobs(fileRoot, now, std::chrono::hours(1)),
                         doctest::Contains("not an ordinary directory"), std::runtime_error);
    CHECK_THROWS_AS((void)Keire::Detail::CleanupStaleShaderCompilerJobs({}, now, std::chrono::hours(1)),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::Detail::CleanupStaleShaderCompilerJobs(directory.Path, now, std::chrono::seconds(-1)),
                    std::invalid_argument);
    CHECK_THROWS_AS(Keire::Detail::WriteShaderCompilerJobLease(directory.Path, 0), std::invalid_argument);
    CHECK_THROWS_AS(Keire::Detail::WriteShaderCompilerJobLease(fileRoot, Keire::Detail::CurrentProcessId()),
                    std::invalid_argument);
}

TEST_CASE("shader compile manifests produce order-independent content-addressed work keys")
{
    Keire::ShaderCompileManifest manifest;
    manifest.ToolchainSha256 = std::string(64, 'a');
    manifest.SourceSha256 = std::string(64, 'b');
    manifest.Stage = Keire::ShaderCompileStage::Fragment;
    manifest.EntryPoint = "PSMain";
    manifest.Target = {Keire::ShaderCompilePlatform::Linux, Keire::ShaderCompileArchitecture::X86_64,
                       Keire::ShaderCompileBinaryFormat::SpirV};
    manifest.Defines = {{"USE_FOG", "1"}, {"ALPHA_MODE", "MASK"}};
    manifest.Dependencies = {{"Shaders/Lighting.hlsli", std::string(64, 'c')},
                             {"Shaders/Common.hlsli", std::string(64, 'd')}};

    const auto workKey = Keire::ShaderCompileWorkKey(manifest);
    CHECK(Keire::IsShaderCompileWorkKey(workKey));
    CHECK(workKey.size() == 64U);

    std::ranges::reverse(manifest.Defines);
    std::ranges::reverse(manifest.Dependencies);
    CHECK(Keire::ShaderCompileWorkKey(manifest) == workKey);

    manifest.DebugInformation = true;
    CHECK(Keire::ShaderCompileWorkKey(manifest) != workKey);
}

TEST_CASE("shader compile manifests reject ambiguous unsafe or incompatible jobs")
{
    Keire::ShaderCompileManifest manifest;
    manifest.ToolchainSha256 = std::string(64, 'a');
    manifest.SourceSha256 = std::string(64, 'b');
    CHECK_NOTHROW(Keire::ValidateShaderCompileManifest(manifest));

    auto invalid = manifest;
    invalid.Dependencies = {{"../escape.hlsli", std::string(64, 'c')}};
    CHECK_THROWS_AS(Keire::ValidateShaderCompileManifest(invalid), std::invalid_argument);
    invalid = manifest;
    invalid.Defines = {{"DUPLICATE", "1"}, {"DUPLICATE", "2"}};
    CHECK_THROWS_AS(Keire::ValidateShaderCompileManifest(invalid), std::invalid_argument);
    invalid = manifest;
    invalid.Target = {Keire::ShaderCompilePlatform::Linux, Keire::ShaderCompileArchitecture::X86_64,
                      Keire::ShaderCompileBinaryFormat::Dxil};
    CHECK_THROWS_AS(Keire::ValidateShaderCompileManifest(invalid), std::invalid_argument);

    Keire::ShaderCompilationRequest request;
    request.Manifest = manifest;
    request.Policy = static_cast<Keire::ShaderCompilationPolicy>(255U);
    CHECK_THROWS_AS(Keire::ValidateShaderCompilationRequest(request), std::invalid_argument);
}
