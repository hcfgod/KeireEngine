#include "doctest/doctest.h"

#include "Keire/Assets/Asset.h"
#include "KeireInternal/Assets/ShaderCompilerJobs.h"
#include "KeireInternal/Assets/ShaderImportCache.h"
#include "KeireInternal/FileSystem.h"
#include "KeireTests/TestSupport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

namespace
{
    [[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text)
    {
        const auto bytes = std::as_bytes(std::span(text));
        return {bytes.begin(), bytes.end()};
    }

    struct CacheProject final
    {
        CacheProject() : Root(KeireTests::MakeTestDirectory("ShaderImportCache"))
        {
            std::filesystem::create_directories(Root);
            const auto repository = std::filesystem::current_path();
            const auto templateRoot = repository / "KeireHubContent/Templates/Payloads/Starter3D";
            Source = Keire::Detail::ReadTextFile(templateRoot / "Assets/Shaders/StarterUnlit.hlsl", 1024U * 1024U) +
                     "\n#include \"cache-header.hlsli\"\n";
            Header = "#define KEIRE_CACHE_FIXTURE 1\n";
            Manifest = nlohmann::json::parse(
                Keire::Detail::ReadTextFile(templateRoot / "Assets/Shaders/DefaultUnlit.keireshader", 1024U * 1024U));
#if defined(_WIN32)
            Specification.Compiler = repository / "Build/Tools/ShaderCompiler/KeireShaderCompiler.exe";
#else
            Specification.Compiler = repository / "Build/Tools/ShaderCompiler/KeireShaderCompiler";
#endif
            Specification.Formats = {Keire::ShaderBinaryFormat::SpirV};
            Context.ProjectRoot = Root;
            Context.SourceRoot = Root / "Assets";
            Context.RelativePath = "Shaders/DefaultUnlit.keireshader";
            Context.SourcePath = Context.SourceRoot / Context.RelativePath;
            Context.ReadProjectFile = [this](const std::filesystem::path& path)
            {
                if (const auto additional = AdditionalSources.find(path); additional != AdditionalSources.end())
                {
                    return Bytes(additional->second);
                }
                if (path == "Assets/Shaders/cache-header.hlsli")
                {
                    return Bytes(Header);
                }
                if (path == Manifest.at("source").get<std::string>())
                {
                    return Bytes(Source);
                }
                throw std::runtime_error("Unexpected cache fixture dependency.");
            };
        }

        ~CacheProject()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        [[nodiscard]] Keire::AssetImportOutput Import(const std::stop_token cancellation = {})
        {
            return Keire::Detail::CreateCancellableShaderAssetImporter(Specification, cancellation, Statistics)
                .ContextualImport(Context, Bytes(Manifest.dump()));
        }

        [[nodiscard]] std::vector<std::filesystem::path> Slots() const
        {
            const auto directory = Root / "Library/ShaderCompileCache/v1";
            std::vector<std::filesystem::path> result;
            if (std::filesystem::is_directory(directory))
            {
                for (const auto& entry : std::filesystem::directory_iterator(directory))
                {
                    result.push_back(entry.path());
                }
            }
            return result;
        }

        std::filesystem::path Root;
        Keire::AssetImportContext Context;
        Keire::ShaderImporterSpecification Specification;
        std::shared_ptr<Keire::Detail::ShaderCompilerStatistics> Statistics =
            std::make_shared<Keire::Detail::ShaderCompilerStatistics>();
        nlohmann::json Manifest;
        std::string Source;
        std::string Header;
        std::map<std::filesystem::path, std::string> AdditionalSources;
    };
} // namespace

TEST_CASE("shader import cache reuses complete validated artifacts without starting native processes")
{
    CacheProject fixture;
    const auto cold = fixture.Import();
    REQUIRE(fixture.Statistics->NativeStarts == 4U);
    REQUIRE(fixture.Statistics->CacheMisses == 1U);
    const auto warm = fixture.Import();
    CHECK(warm.Bytes == cold.Bytes);
    CHECK(fixture.Statistics->NativeStarts == 4U);
    CHECK(fixture.Statistics->CacheHits == 1U);
    REQUIRE(fixture.Slots().size() == 1U);
    CHECK(std::filesystem::file_size(fixture.Slots().front()) <= Keire::Detail::ShaderImportCacheMaximumEntryBytes);

    SUBCASE("changed transitive include") { fixture.Header += "// revised captured bytes\n"; }
    SUBCASE("changed source") { fixture.Source += "// revised source\n"; }
    SUBCASE("changed defines") { fixture.Manifest["defines"]["CACHE_VARIANT"] = "2"; }
    SUBCASE("changed source virtual path") { fixture.Manifest["source"] = "Assets/Shaders/Renamed.hlsl"; }
    SUBCASE("changed authored render state") { fixture.Manifest["renderState"]["culling"] = "None"; }
    SUBCASE("changed ordered roots")
    {
        fixture.Manifest["includeRoots"] = {"Assets/Shaders", "Assets/Other"};
        (void)fixture.Import();
        CHECK(fixture.Statistics->NativeStarts == 8U);
        fixture.Manifest["includeRoots"] = {"Assets/Other", "Assets/Shaders"};
    }
    SUBCASE("changed target formats")
    {
        fixture.Specification.Formats = {Keire::ShaderBinaryFormat::Dxil, Keire::ShaderBinaryFormat::SpirV,
                                         Keire::ShaderBinaryFormat::Msl};
    }
    SUBCASE("changed logical pass roles")
    {
        fixture.Manifest["schemaVersion"] = 3;
        fixture.Manifest["passes"] = nlohmann::json::array(
            {{{"role", "primary"}, {"define", "PASS_PRIMARY"}}, {{"role", "depthVelocity"}, {"define", "PASS_DEPTH"}}});
    }
    const auto before = fixture.Statistics->NativeStarts.load();
    const auto changed = fixture.Import();
    CHECK(fixture.Statistics->NativeStarts > before);
    const auto after = fixture.Statistics->NativeStarts.load();
    CHECK(fixture.Import().Bytes == changed.Bytes);
    CHECK(fixture.Statistics->NativeStarts == after);
}

TEST_CASE("shader import cache ignores corrupt truncated oversized and replaced entries")
{
    CacheProject fixture;
    const auto original = fixture.Import();
    REQUIRE(fixture.Slots().size() == 1U);
    const auto slot = fixture.Slots().front();
    SUBCASE("truncated") { Keire::Detail::WriteTextFileAtomically(slot, "partial"); }
    SUBCASE("checksum mismatch")
    {
        Keire::Detail::AnchoredFileSystem files(slot.parent_path());
        auto encoded = files.Read(slot.filename(), Keire::Detail::ShaderImportCacheMaximumEntryBytes);
        encoded.back() ^= std::byte{1};
        files.WriteFileAtomically(slot.filename(), encoded);
    }
    SUBCASE("oversized")
    {
        const std::vector<std::byte> oversized(Keire::Detail::ShaderImportCacheMaximumEntryBytes + 1U);
        Keire::Detail::WriteFileAtomically(slot, oversized);
    }
    CHECK(fixture.Import().Bytes == original.Bytes);
    CHECK(fixture.Statistics->NativeStarts == 8U);
    CHECK(fixture.Import().Bytes == original.Bytes);
    CHECK(fixture.Statistics->NativeStarts == 8U);
}

TEST_CASE("native shader compilation respects ordered roots and more than 64 global defines")
{
    CacheProject fixture;
    fixture.AdditionalSources["Assets/First/ordered.hlsli"] = "#define ORDERED_VALUE 1\n";
    fixture.AdditionalSources["Assets/Second/ordered.hlsli"] = "#define ORDERED_VALUE 2\n";
    fixture.Manifest["includeRoots"] = {"Assets/First", "Assets/Second"};
    fixture.Source += "\n#include \"ordered.hlsli\"\n"
                      "#if ORDERED_VALUE != Z_EXPECTED_VALUE\n#error incorrect include precedence\n#endif\n"
                      "#if Z_LAST_GLOBAL != 7\n#error define after position64 was not forwarded\n#endif\n";
    for (std::size_t index = 0; index < 126U; ++index)
    {
        fixture.Manifest["defines"]["GLOBAL_" + std::to_string(index)] = "1";
    }
    fixture.Manifest["defines"]["Z_EXPECTED_VALUE"] = "1";
    fixture.Manifest["defines"]["Z_LAST_GLOBAL"] = "7";
    fixture.Manifest["schemaVersion"] = 3;
    fixture.Manifest["passes"] = nlohmann::json::array({{{"role", "primary"}, {"define", "PASS_PRIMARY"}}});
    fixture.Source += "\n#if PASS_PRIMARY != 1\n#error missing pass selector129\n#endif\n";
    (void)fixture.Import();
    REQUIRE(fixture.Statistics->NativeStarts == 4U);
    (void)fixture.Import();
    CHECK(fixture.Statistics->NativeStarts == 4U);
    fixture.Manifest["includeRoots"] = {"Assets/Second", "Assets/First"};
    fixture.Manifest["defines"]["Z_EXPECTED_VALUE"] = "2";
    (void)fixture.Import();
    CHECK(fixture.Statistics->NativeStarts == 8U);
    (void)fixture.Import();
    CHECK(fixture.Statistics->NativeStarts == 8U);
}

TEST_CASE("shader cache storage failures and volatile inputs preserve direct compiler behavior")
{
    CacheProject fixture;
    SUBCASE("cache directory is a file")
    {
        std::filesystem::create_directories(fixture.Root / "Library");
        Keire::Detail::WriteTextFileAtomically(fixture.Root / "Library/ShaderCompileCache", "not a directory");
    }
    SUBCASE("volatile macro conservatively disables reuse") { fixture.Source += "// __TIME__\n"; }
    SUBCASE("spliced volatile macro conservatively disables reuse") { fixture.Source += "// __TI\\\nME__\n"; }
    SUBCASE("token pasting conservatively disables reuse") { fixture.Source += "// ##\n"; }
    SUBCASE("volatile manifest define conservatively disables reuse")
    {
        fixture.Manifest["defines"]["TIME_ALIAS"] = "__TIME__";
    }
    (void)fixture.Import();
    (void)fixture.Import();
    CHECK(fixture.Statistics->NativeStarts == 8U);
    CHECK(fixture.Statistics->CacheHits == 0U);
}

TEST_CASE("stricter shader output limits cannot reuse a previously valid larger artifact")
{
    CacheProject fixture;
    const auto original = fixture.Import();
    REQUIRE(fixture.Statistics->NativeStarts == 4U);
    fixture.Specification.MaximumOutputBytes = 1;
    CHECK_THROWS_AS((void)fixture.Import(), std::runtime_error);
    CHECK(fixture.Statistics->NativeStarts > 4U);
    const auto afterFailure = fixture.Statistics->NativeStarts.load();
    fixture.Specification.MaximumOutputBytes = 64U * 1024U * 1024U;
    CHECK(fixture.Import().Bytes == original.Bytes);
    CHECK(fixture.Statistics->NativeStarts == afterFailure);
}

TEST_CASE("concurrent shader imports atomically converge on a reusable complete entry")
{
    CacheProject fixture;
    auto first = std::async(std::launch::async, [&] { return fixture.Import(); });
    auto second = std::async(std::launch::async, [&] { return fixture.Import(); });
    const auto firstOutput = first.get();
    CHECK(second.get().Bytes == firstOutput.Bytes);
    const auto starts = fixture.Statistics->NativeStarts.load();
    CHECK((starts == 4U || starts == 8U));
    CHECK(fixture.Import().Bytes == firstOutput.Bytes);
    CHECK(fixture.Statistics->NativeStarts == starts);
    CHECK(fixture.Slots().size() == 1U);
}

TEST_CASE("shader cache committed namespace remains bounded and collisions never alias keys")
{
    CacheProject fixture;
    const auto imported = fixture.Import();
    const auto shader = Keire::ShaderAsset::Decode(imported.Bytes);
    const auto& definition = shader->Definition();
    const auto sources = Keire::Detail::CaptureShaderSourceSnapshot(fixture.Context, definition.Source, {});
    const std::array<Keire::Detail::ShaderPassCompileLane, 1> passes{{{"primary", {}}}};
    // Storage/identity-only fixture: no fake executable is ever launched. Keep retained snapshots tiny while
    // exercising more than 64 distinct keys; native process reduction is tested separately above.
    const auto identityBundle = fixture.Root / "IdentityBundle";
    std::filesystem::create_directories(identityBundle);
#if defined(_WIN32)
    fixture.Specification.Compiler = identityBundle / "KeireShaderCompiler.exe";
    Keire::Detail::WriteTextFileAtomically(identityBundle / "dxcompiler.dll", "dxc identity fixture");
    Keire::Detail::WriteTextFileAtomically(identityBundle / "dxil.dll", "dxil identity fixture");
#elif defined(__APPLE__)
    fixture.Specification.Compiler = identityBundle / "KeireShaderCompiler";
    Keire::Detail::WriteTextFileAtomically(identityBundle / "libdxcompiler.dylib", "dxc identity fixture");
#else
    fixture.Specification.Compiler = identityBundle / "KeireShaderCompiler";
    Keire::Detail::WriteTextFileAtomically(identityBundle / "libdxcompiler.so", "dxc identity fixture");
#endif
    Keire::Detail::WriteTextFileAtomically(fixture.Specification.Compiler, "compiler identity fixture");
    std::vector<std::unique_ptr<Keire::Detail::ShaderImportCache>> occupants;
    bool collisionObserved = false;
    for (std::size_t index = 0; index <= Keire::Detail::ShaderImportCacheSlotCount; ++index)
    {
        const auto authored = Bytes("bounded-slot-fixture:" + std::to_string(index));
        auto cache =
            Keire::Detail::ShaderImportCache::TryCreate(fixture.Specification.Compiler, fixture.Context, definition,
                                                        fixture.Specification, passes, sources, {}, {}, authored);
        REQUIRE(cache);
        cache->TryStore(imported.Bytes);
        REQUIRE_FALSE(cache->TryRead(definition, fixture.Specification, passes).empty());
        for (const auto& previous : occupants)
        {
            if (previous->TryRead(definition, fixture.Specification, passes).empty())
            {
                collisionObserved = true;
                break;
            }
        }
        occupants.push_back(std::move(cache));
    }
    CHECK(collisionObserved);
    CHECK(fixture.Slots().size() <= Keire::Detail::ShaderImportCacheSlotCount);
    for (const auto& slot : fixture.Slots())
    {
        CHECK(std::filesystem::file_size(slot) <= Keire::Detail::ShaderImportCacheMaximumEntryBytes);
    }
    CHECK(fixture.Statistics->NativeStarts == 4U);
}

TEST_CASE("failed and cancelled native shader jobs do not publish reusable partial artifacts")
{
    CacheProject fixture;
    SUBCASE("compiler failure")
    {
        fixture.Source += "\n#error deliberate compile failure\n";
        CHECK_THROWS_AS((void)fixture.Import(), std::runtime_error);
    }
    SUBCASE("cancel while native stage jobs are live")
    {
        std::stop_source cancellation;
        std::jthread cancel(
            [&](const std::stop_token stopped)
            {
                while (!stopped.stop_requested() && fixture.Statistics->NativeStarts == 0U)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                cancellation.request_stop();
            });
        CHECK_THROWS_AS((void)fixture.Import(cancellation.get_token()), Keire::AssetOperationCancelled);
    }
    CHECK(fixture.Statistics->NativeStarts > 0U);
    CHECK(fixture.Slots().empty());
}

#if defined(_WIN32)
TEST_CASE("shader cache invalidates when a retained bundled runtime changes")
{
    CacheProject fixture;
    const auto originalCompiler = fixture.Specification.Compiler;
    const auto bundle = fixture.Root / "Compiler";
    std::filesystem::create_directories(bundle);
    for (const auto name : {"KeireShaderCompiler.exe", "dxcompiler.dll", "dxil.dll"})
    {
        std::filesystem::copy_file(originalCompiler.parent_path() / name, bundle / name);
    }
    fixture.Specification.Compiler = bundle / originalCompiler.filename();
    (void)fixture.Import();
    (void)fixture.Import();
    REQUIRE(fixture.Statistics->NativeStarts == 4U);
    // A trailing PE overlay changes the exact runtime bytes while preserving a runnable test compiler.
    std::ofstream(bundle / "dxcompiler.dll", std::ios::binary | std::ios::app).put('\0');
    (void)fixture.Import();
    CHECK(fixture.Statistics->NativeStarts == 8U);
    (void)fixture.Import();
    CHECK(fixture.Statistics->NativeStarts == 8U);
}
#endif
