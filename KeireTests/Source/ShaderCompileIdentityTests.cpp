#include "doctest/doctest.h"

#include "KeireInternal/Assets/ShaderCompileIdentity.h"
#include "KeireInternal/Assets/ShaderCompilerJobs.h"
#include "KeireInternal/FileSystem.h"
#include "KeireTests/TestSupport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <span>
#include <stop_token>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    struct CompileIdentityDirectory final
    {
        CompileIdentityDirectory() : Root(KeireTests::MakeTestDirectory("ShaderCompileIdentity"))
        {
            std::filesystem::create_directories(Root);
        }

        ~CompileIdentityDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        std::filesystem::path Root;
    };

    [[nodiscard]] Keire::ShaderCompileManifest Unit()
    {
        Keire::ShaderCompileManifest unit;
        unit.ToolchainSha256 = std::string(64, 'a');
        unit.SourceSha256 = std::string(64, 'b');
        unit.Defines = {{"GLOBAL", "1"}, {"PASS_PRIMARY", "1"}};
        unit.Dependencies = {{"Shaders/Shared.hlsli", std::string(64, 'c')}};
        return unit;
    }
} // namespace

TEST_CASE("shader import identities retain ordered roots and exact source and unit contracts")
{
    std::array units{Unit()};
    std::array<std::filesystem::path, 2> roots{"Shaders/First", "Shaders/Second"};
    std::string manifestDigest(64, 'd');
    const auto key = [&]
    { return Keire::Detail::ShaderImportWorkKey(units, "Shaders/Main.hlsl", roots, manifestDigest); };
    const auto original = key();
    CHECK(Keire::IsShaderCompileWorkKey(original));
    CHECK(key() == original);
    std::ranges::reverse(roots);
    CHECK(key() != original);
    std::ranges::reverse(roots);
    roots[0] = "Shaders/First/../First";
    CHECK(key() == original);
    std::ranges::reverse(units[0].Defines);
    CHECK(key() == original);

    SUBCASE("changed source content") { units[0].SourceSha256 = std::string(64, 'e'); }
    SUBCASE("changed runtime library closure") { units[0].ToolchainSha256 = std::string(64, 'e'); }
    SUBCASE("changed transitive include contents") { units[0].Dependencies[0].Sha256 = std::string(64, 'e'); }
    SUBCASE("changed entry point") { units[0].EntryPoint = "OtherVS"; }
    SUBCASE("changed stage")
    {
        units[0].Stage = Keire::ShaderCompileStage::Fragment;
        units[0].EntryPoint = "PSMain";
    }
    SUBCASE("changed architecture") { units[0].Target.Architecture = Keire::ShaderCompileArchitecture::Arm64; }
    SUBCASE("changed optimization policy") { units[0].Optimization = Keire::ShaderCompileOptimization::Shipping; }
    SUBCASE("changed define value") { units[0].Defines[0].Value = "2"; }
    SUBCASE("changed pass") { units[0].PassRole = "depthVelocity"; }
    SUBCASE("changed target")
    {
        units[0].Target = {Keire::ShaderCompilePlatform::Linux, Keire::ShaderCompileArchitecture::X86_64,
                           Keire::ShaderCompileBinaryFormat::SpirV};
    }
    SUBCASE("changed authored shader manifest") { manifestDigest = std::string(64, 'e'); }
    CHECK(key() != original);
    CHECK(Keire::Detail::ShaderImportWorkKey(units, "Shaders/Other.hlsl", roots, manifestDigest) != original);
}

TEST_CASE("shader import identities reject unsafe roots and incomplete unbounded units")
{
    const std::array units{Unit()};
    const std::array<std::filesystem::path, 1> escape{"../escape"};
    const std::array<std::filesystem::path, 1> root{"."};
    const std::string digest(64, 'd');
    CHECK_NOTHROW((void)Keire::Detail::ShaderImportWorkKey(units, "Main.hlsl", root, digest));
    CHECK_THROWS_AS((void)Keire::Detail::ShaderImportWorkKey(units, "Main.hlsl", escape, digest),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::Detail::ShaderImportWorkKey(units, "../Main.hlsl", {}, digest), std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::Detail::ShaderImportWorkKey(units, "Main.hlsl", {}, "bad"), std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::Detail::ShaderImportWorkKey({}, "Main.hlsl", {}, digest), std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::Detail::ShaderImportWorkKey(std::vector<Keire::ShaderCompileManifest>(193, Unit()),
                                                             "Main.hlsl", {}, digest),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::Detail::ShaderImportWorkKey(units, "Main.hlsl",
                                                             std::vector<std::filesystem::path>(17, "Shaders"), digest),
                    std::invalid_argument);
}

TEST_CASE("compile unit manifests support every authored define and its additional pass selector")
{
    auto unit = Unit();
    unit.Defines.clear();
    for (unsigned index = 0; index < 128U; ++index)
    {
        unit.Defines.push_back({"GLOBAL_" + std::to_string(index), "1"});
    }
    CHECK_NOTHROW(Keire::ValidateShaderCompileManifest(unit));
    unit.Defines.push_back({"PASS_PRIMARY", "1"});
    CHECK_NOTHROW(Keire::ValidateShaderCompileManifest(unit));
    const auto key = Keire::ShaderCompileWorkKey(unit);
    unit.Defines.back().Value = "0";
    CHECK(Keire::ShaderCompileWorkKey(unit) != key);
    unit.Defines.push_back({"TOO_MANY", "1"});
    CHECK_THROWS_AS(Keire::ValidateShaderCompileManifest(unit), std::invalid_argument);
}

TEST_CASE("shader toolchain snapshots hash runtime libraries and stage only their captured bytes")
{
    CompileIdentityDirectory directory;
    const auto bundle = directory.Root / "Bundle";
    const auto stage = directory.Root / "Stage";
    std::filesystem::create_directories(bundle);
    std::filesystem::create_directories(stage);
    Keire::Detail::WriteTextFileAtomically(bundle / "compiler", "compiler-v1");
    Keire::Detail::WriteTextFileAtomically(bundle / "dxcompiler.dll", "runtime-v1");
    const std::string validator("validation\0bytes", 16U);
    Keire::Detail::WriteTextFileAtomically(bundle / "dxil.dll", validator);
    std::array<std::filesystem::path, 2> runtimes{"dxcompiler.dll", "dxil.dll"};
    const auto capture = [&] { return Keire::Detail::CaptureShaderToolchain(bundle, "compiler", runtimes, 128U); };
    auto snapshot = capture();
    CHECK(Keire::IsShaderCompileWorkKey(snapshot.Sha256));
    CHECK(snapshot.Files.size() == 3U);
    std::ranges::reverse(runtimes);
    CHECK(capture().Sha256 == snapshot.Sha256);
    Keire::Detail::WriteTextFileAtomically(bundle / "dxcompiler.dll", "runtime-v2");
    const auto changedRuntime = capture().Sha256;
    CHECK(changedRuntime != snapshot.Sha256);
    Keire::Detail::WriteTextFileAtomically(bundle / "compiler", "compiler-v2");
    CHECK(capture().Sha256 != changedRuntime);
    std::ranges::reverse(snapshot.Files);
    CHECK(Keire::Detail::StageShaderToolchain(snapshot, stage) == std::filesystem::canonical(stage) / "compiler");
    CHECK(Keire::Detail::ReadTextFile(stage / "compiler", 128U) == "compiler-v1");
    CHECK(Keire::Detail::ReadTextFile(stage / "dxcompiler.dll", 128U) == "runtime-v1");
    CHECK(Keire::Detail::ReadTextFile(stage / "dxil.dll", 128U) == validator);
}

TEST_CASE("shader toolchain snapshots enforce aggregate bytes and confined bundle entries")
{
    CompileIdentityDirectory directory;
    Keire::Detail::WriteTextFileAtomically(directory.Root / "compiler", "12345");
    Keire::Detail::WriteTextFileAtomically(directory.Root / "runtime", "67890");
    const std::array<std::filesystem::path, 1> runtime{"runtime"};
    CHECK_NOTHROW((void)Keire::Detail::CaptureShaderToolchain(directory.Root, "compiler", runtime, 10U));
    CHECK_THROWS((void)Keire::Detail::CaptureShaderToolchain(directory.Root, "compiler", runtime, 9U));
    for (const auto& name : {std::filesystem::path("../compiler"), std::filesystem::path("/compiler"),
                             std::filesystem::path("nested/compiler"), std::filesystem::path("compiler:stream")})
    {
        CHECK_THROWS_AS((void)Keire::Detail::CaptureShaderToolchain(directory.Root, name, {}, 10U),
                        std::invalid_argument);
    }
    CHECK_THROWS_AS((void)Keire::Detail::CaptureShaderToolchain(directory.Root, "compiler",
                                                                std::array<std::filesystem::path, 1>{"compiler"}, 10U),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::Detail::CaptureShaderToolchain(directory.Root, "compiler",
                                                                std::vector<std::filesystem::path>(64, "runtime"), 10U),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)Keire::Detail::CaptureShaderToolchain(directory.Root, "compiler", {}, 0U),
                    std::invalid_argument);
    auto snapshot = Keire::Detail::CaptureShaderToolchain(directory.Root, "compiler", runtime, 10U);
    snapshot.Files.back().Name = "../escape";
    CHECK_THROWS_AS((void)Keire::Detail::StageShaderToolchain(snapshot, directory.Root), std::invalid_argument);
    CHECK_FALSE(std::filesystem::exists(directory.Root.parent_path() / "escape"));

    snapshot = Keire::Detail::CaptureShaderToolchain(directory.Root, "compiler", runtime, 10U);
    snapshot.Files.back().Bytes.front() = std::byte{'x'};
    CHECK_THROWS_AS((void)Keire::Detail::StageShaderToolchain(snapshot, directory.Root), std::invalid_argument);
    CHECK(Keire::Detail::ReadTextFile(directory.Root / "compiler", 10U) == "12345");
}

TEST_CASE("captured pinned shader toolchain compiles from its isolated staged runtime bundle")
{
    CompileIdentityDirectory directory;
    const auto bundle = std::filesystem::current_path() / "Build/Tools/ShaderCompiler";
#if defined(_WIN32)
    const std::filesystem::path compiler = "KeireShaderCompiler.exe";
#else
    const std::filesystem::path compiler = "KeireShaderCompiler";
#endif
    if (!std::filesystem::is_regular_file(bundle / compiler))
    {
        MESSAGE("Pinned host compiler is unavailable; isolated real-tool acceptance was not run.");
        return;
    }
    std::vector<std::filesystem::path> runtimes;
    for (const auto& item : std::filesystem::directory_iterator(bundle))
    {
        const auto name = item.path().filename().string();
        if (item.is_regular_file() && (item.path().extension() == ".dll" || name.find(".so") != std::string::npos ||
                                       name.find(".dylib") != std::string::npos))
        {
            runtimes.push_back(item.path().filename());
        }
    }
    const auto snapshot = Keire::Detail::CaptureShaderToolchain(bundle, compiler, runtimes);
    const auto staged = Keire::Detail::StageShaderToolchain(snapshot, directory.Root);
    const auto source = directory.Root / "SnapshotProbe.hlsl";
    const auto output = directory.Root / "SnapshotProbe.spv";
    Keire::Detail::WriteTextFileAtomically(source, "float4 VSMain(float3 position : POSITION) : SV_Position "
                                                   "{ return float4(position, 1.0); }\n");
    const std::array arguments{Keire::Detail::PathToUtf8(source),
                               std::string("-s"),
                               std::string("HLSL"),
                               std::string("-d"),
                               std::string("SPIRV"),
                               std::string("-t"),
                               std::string("vertex"),
                               std::string("-e"),
                               std::string("VSMain"),
                               std::string("-o"),
                               Keire::Detail::PathToUtf8(output)};
    CHECK_NOTHROW(Keire::Detail::RunShaderCompilerProcess(staged, arguments, directory.Root, std::chrono::seconds(30),
                                                          std::stop_token{}));
    const auto bytes = Keire::Detail::ReadTextFile(output, 1024U * 1024U);
    REQUIRE(bytes.size() >= 20U);
    CHECK(static_cast<unsigned char>(bytes[0]) == 0x03U);
    CHECK(static_cast<unsigned char>(bytes[1]) == 0x02U);
    CHECK(static_cast<unsigned char>(bytes[2]) == 0x23U);
    CHECK(static_cast<unsigned char>(bytes[3]) == 0x07U);
}
