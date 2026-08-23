#include "KeireInternal/Build/PlayerSupport.h"

#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace
{
    constexpr std::array RequiredLicenseFiles{"Keire-LICENSE.txt",
                                              "Keire-THIRD_PARTY_NOTICES.md",
                                              "Coral-LICENSE.txt",
                                              "dotnet-LICENSE.txt",
                                              "dotnet-ThirdPartyNotices.txt",
                                              "SDL-LICENSE.txt",
                                              "assimp-LICENSE.txt",
                                              "assimp-zlib-LICENSE.txt",
                                              "stb-LICENSE.txt",
                                              "Jolt-LICENSE.txt",
                                              "Recast-LICENSE.txt",
                                              "miniaudio-LICENSE.txt",
                                              "spdlog-LICENSE.txt",
                                              "fmt-LICENSE.rst",
                                              "nlohmann-json-LICENSE.MIT.txt",
                                              "dear-imgui-LICENSE.txt",
                                              "zstandard-LICENSE.txt",
                                              "entt-LICENSE.txt",
                                              "glm-COPYING.txt"};

    class TemporaryDirectory final
    {
      public:
        TemporaryDirectory()
            : Path(std::filesystem::temp_directory_path() /
                   ("keire-player-support-" + Keire::AssetId::Generate().ToString()))
        {
            std::filesystem::create_directories(Path);
        }

        ~TemporaryDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Path, ignored);
        }

        std::filesystem::path Path;
    };

    [[nodiscard]] Keire::Detail::PlayerSupportFile File(const std::string_view path, const std::uint32_t mode = 0644U)
    {
        return {.Path = Keire::Detail::PathFromUtf8(path), .Size = 1, .Sha256 = std::string(64, '0'), .Mode = mode};
    }

    [[nodiscard]] Keire::Detail::PlayerSupportManifest WindowsManifest()
    {
        Keire::Detail::PlayerSupportManifest manifest{
            .Id = "windows-x86_64-test",
            .EngineVersion = "0.1.0",
            .ModuleFingerprint = "modules",
            .Variants = {{.Root = "Development", .Executable = "KeireRuntime.exe"}},
            .Files = {File("Development/KeireRuntime.exe", 0755U), File("Development/nethost.dll"),
                      File("Development/MSVCP140.dll"), File("Development/MSVCP140_ATOMIC_WAIT.dll"),
                      File("Development/MSVCP140_1.dll"), File("Development/VCRUNTIME140.dll"),
                      File("Development/VCRUNTIME140_1.dll"), File("Development/Managed/Coral.Managed.dll"),
                      File("Development/Managed/Coral.Managed.deps.json"),
                      File("Development/Managed/Coral.Managed.runtimeconfig.json"),
                      File("Development/Managed/Keire.Managed.dll"),
                      File("Development/Managed/Dotnet/host/fxr/10.0.10/hostfxr.dll"),
                      File("Development/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/coreclr.dll"),
                      File("Development/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/hostpolicy.dll"),
                      File("Development/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/"
                           "System.Private.CoreLib.dll")}};
        for (const auto filename : RequiredLicenseFiles)
            manifest.Files.push_back(File(std::string("Development/Licenses/") + filename));
        return manifest;
    }

    [[nodiscard]] Keire::Detail::PlayerSupportManifest MacOSManifest()
    {
        Keire::Detail::PlayerSupportManifest manifest{
            .Id = "macos-arm64-test",
            .EngineVersion = "0.1.0",
            .Platform = Keire::PlayerPlatform::MacOS,
            .Architecture = Keire::PlayerArchitecture::Arm64,
            .ModuleFingerprint = "modules",
            .Variants = {{.Root = "Development",
                          .Executable = "KeireRuntime.app/Contents/MacOS/KeireRuntime",
                          .Bundle = "KeireRuntime.app"}},
            .Files = {File("Development/KeireRuntime.app/Contents/MacOS/KeireRuntime", 0755U),
                      File("Development/KeireRuntime.app/Contents/MacOS/libnethost.dylib"),
                      File("Development/KeireRuntime.app/Contents/Resources/Managed/Coral.Managed.dll"),
                      File("Development/KeireRuntime.app/Contents/Resources/Managed/Coral.Managed.deps.json"),
                      File("Development/KeireRuntime.app/Contents/Resources/Managed/Coral.Managed.runtimeconfig.json"),
                      File("Development/KeireRuntime.app/Contents/Resources/Managed/Keire.Managed.dll"),
                      File("Development/KeireRuntime.app/Contents/Resources/Managed/Dotnet/host/fxr/10.0.10/"
                           "libhostfxr.dylib"),
                      File("Development/KeireRuntime.app/Contents/Resources/Managed/Dotnet/shared/"
                           "Microsoft.NETCore.App/10.0.11/libcoreclr.dylib"),
                      File("Development/KeireRuntime.app/Contents/Resources/Managed/Dotnet/shared/"
                           "Microsoft.NETCore.App/10.0.11/libhostpolicy.dylib"),
                      File("Development/KeireRuntime.app/Contents/Resources/Managed/Dotnet/shared/"
                           "Microsoft.NETCore.App/10.0.11/System.Private.CoreLib.dll")}};
        for (const auto filename : RequiredLicenseFiles)
            manifest.Files.push_back(File(std::string("Development/Licenses/") + filename));
        return manifest;
    }

    [[nodiscard]] Keire::Detail::PlayerSupportManifest LinuxManifest()
    {
        Keire::Detail::PlayerSupportManifest manifest{
            .Id = "linux-x86_64-test",
            .EngineVersion = "0.1.0",
            .Platform = Keire::PlayerPlatform::Linux,
            .ModuleFingerprint = "modules",
            .Variants = {{.Root = "Development", .Executable = "KeireRuntime"}},
            .Files = {File("Development/KeireRuntime", 0755U), File("Development/libnethost.so"),
                      File("Development/Managed/Coral.Managed.dll"),
                      File("Development/Managed/Coral.Managed.deps.json"),
                      File("Development/Managed/Coral.Managed.runtimeconfig.json"),
                      File("Development/Managed/Keire.Managed.dll"),
                      File("Development/Managed/Dotnet/host/fxr/10.0.10/libhostfxr.so"),
                      File("Development/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/libcoreclr.so"),
                      File("Development/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/libhostpolicy.so"),
                      File("Development/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/"
                           "System.Private.CoreLib.dll")}};
        for (const auto filename : RequiredLicenseFiles)
            manifest.Files.push_back(File(std::string("Development/Licenses/") + filename));
        return manifest;
    }
} // namespace

TEST_CASE("Player support manifests load target variants and confined paths")
{
    TemporaryDirectory directory;
    const auto manifest = directory.Path / "manifest.json";
    Keire::Detail::WriteTextFileAtomically(
        manifest,
        R"({"schemaVersion":1,"playerAbi":1,"id":"windows-x86_64-test","engineVersion":"0.1.0","platform":"windows","architecture":"x86_64","moduleFingerprint":"modules","variants":[{"configuration":"development","root":"Development","executable":"KeireRuntime.exe","symbols":["KeireRuntime.pdb"]}]})");
    const auto loaded = Keire::Detail::LoadPlayerSupportManifest(manifest);
    CHECK(loaded.Id == "windows-x86_64-test");
    REQUIRE(loaded.Variants.size() == 1);
    CHECK(loaded.Variants.front().Root == "Development");
    CHECK(loaded.Variants.front().Executable == "KeireRuntime.exe");
}

TEST_CASE("Player support manifests reject traversal duplicate variants and incompatible ABIs")
{
    Keire::Detail::PlayerSupportManifest manifest{.Id = "test",
                                                  .EngineVersion = "0.1.0",
                                                  .ModuleFingerprint = "modules",
                                                  .Variants = {{.Root = "../outside", .Executable = "Player.exe"}}};
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(manifest), std::invalid_argument);

    manifest.Variants.front().Root = "Development";
    manifest.Variants.push_back(manifest.Variants.front());
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(manifest), std::invalid_argument);

    manifest.Variants.resize(1);
    manifest.PlayerAbi = 99;
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(manifest), std::invalid_argument);
}

TEST_CASE("Player support manifest identities are portable single path components")
{
    auto manifest = WindowsManifest();
    for (const std::string_view identity : {"C:", "C:escape", "NUL", "con.txt", "trailing.", "trailing ",
                                            ".install-shadow", "has?wildcard", "has<angle>"})
    {
        CAPTURE(identity);
        manifest.Id = identity;
        CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(manifest), std::invalid_argument);
    }

    manifest = WindowsManifest();
    manifest.EngineVersion = R"(D:\redirect)";
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(manifest), std::invalid_argument);
}

TEST_CASE("Player support manifests enforce an explicit runtime-only closure")
{
    const auto manifest = WindowsManifest();
    CHECK_NOTHROW(Keire::Detail::ValidatePlayerSupportManifest(manifest));

    auto contaminated = manifest;
    contaminated.Files.push_back(File("Development/Managed/Dotnet/sdk/10.0.302/MSBuild.dll"));
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(contaminated), std::invalid_argument);

    contaminated = manifest;
    contaminated.Files.push_back(File("Development/KeireAssetTool.exe"));
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(contaminated), std::invalid_argument);

    contaminated = manifest;
    contaminated.Files.push_back(
        File("Development/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.10/System.Runtime.dll"));
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(contaminated), std::invalid_argument);

    for (const auto& [firstAbi, secondAbi] :
         {std::pair{"Development/avcodec-62.dll", "Development/avcodec-63.dll"},
          std::pair{"Development/libavcodec.62.dylib", "Development/libavcodec.63.dylib"},
          std::pair{"Development/libavcodec.so.62", "Development/libavcodec.so.63"}})
    {
        contaminated = manifest;
        contaminated.Files.push_back(File(firstAbi));
        contaminated.Files.push_back(File(secondAbi));
        CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(contaminated), std::invalid_argument);
    }

    contaminated = manifest;
    contaminated.Files.erase(contaminated.Files.begin());
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(contaminated), std::invalid_argument);
}

TEST_CASE("Player support manifests require exact managed runtime layouts and license inventories")
{
    auto manifest = WindowsManifest();
    manifest.Files.erase(manifest.Files.begin() + 7);
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(manifest), std::invalid_argument);

    manifest = WindowsManifest();
    manifest.Files[7].Path = "Development/Wrong/Coral.Managed.dll";
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(manifest), std::invalid_argument);

    manifest = WindowsManifest();
    manifest.Files.erase(manifest.Files.end() - 1);
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(manifest), std::invalid_argument);

    manifest = WindowsManifest();
    manifest.Files.erase(manifest.Files.begin() + 11);
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(manifest), std::invalid_argument);

    manifest = WindowsManifest();
    manifest.Files.erase(manifest.Files.begin() + 12);
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(manifest), std::invalid_argument);

    auto macOS = MacOSManifest();
    CHECK_NOTHROW(Keire::Detail::ValidatePlayerSupportManifest(macOS));
    macOS.Files[2].Path = "Development/KeireRuntime.app/Contents/MacOS/Managed/Coral.Managed.dll";
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(macOS), std::invalid_argument);
}

TEST_CASE("Player support manifests require hostpolicy and System.Private.CoreLib in the selected runtime")
{
    for (const auto& baseline : {WindowsManifest(), LinuxManifest(), MacOSManifest()})
    {
        CHECK_NOTHROW(Keire::Detail::ValidatePlayerSupportManifest(baseline));
        for (const auto filename : {std::string_view("hostpolicy"), std::string_view("System.Private.CoreLib.dll")})
        {
            auto missing = baseline;
            const auto file = std::ranges::find_if(
                missing.Files, [&](const auto& candidate)
                { return Keire::Detail::PathToUtf8(candidate.Path).find(filename) != std::string::npos; });
            REQUIRE(file != missing.Files.end());
            missing.Files.erase(file);
            CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(missing), std::invalid_argument);
        }
    }
}

TEST_CASE("Player support variant containment follows target platform case semantics")
{
    auto windows = WindowsManifest();
    windows.Files.push_back(File("development/extra.dat"));
    CHECK_NOTHROW(Keire::Detail::ValidatePlayerSupportManifest(windows));

    auto linux = LinuxManifest();
    linux.Files.push_back(File("development/extra.dat"));
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(linux), std::invalid_argument);

    linux = LinuxManifest();
    linux.Files.push_back(File("Development/extra.dat"));
    linux.Files.push_back(File("Development/EXTRA.dat"));
    CHECK_NOTHROW(Keire::Detail::ValidatePlayerSupportManifest(linux));

    auto macOS = MacOSManifest();
    macOS.Files.push_back(File("development/extra.dat"));
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(macOS), std::invalid_argument);
}

TEST_CASE("Windows player support manifests require app-local VC runtime files")
{
    constexpr std::array RequiredWindowsRuntimeFiles{"MSVCP140.dll", "MSVCP140_ATOMIC_WAIT.dll", "MSVCP140_1.dll",
                                                     "VCRUNTIME140.dll", "VCRUNTIME140_1.dll"};
    for (const auto filename : RequiredWindowsRuntimeFiles)
    {
        auto manifest = WindowsManifest();
        const auto path = std::filesystem::path("Development") / filename;
        const auto file = std::ranges::find(manifest.Files, path, &Keire::Detail::PlayerSupportFile::Path);
        REQUIRE(file != manifest.Files.end());
        manifest.Files.erase(file);
        CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(manifest), std::invalid_argument);
    }

    auto executableRuntime = WindowsManifest();
    executableRuntime.Files[2].Mode = 0755U;
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(executableRuntime), std::invalid_argument);

    auto caseInsensitiveRuntime = WindowsManifest();
    caseInsensitiveRuntime.Files[2].Path = "development/msvcp140.dll";
    CHECK_NOTHROW(Keire::Detail::ValidatePlayerSupportManifest(caseInsensitiveRuntime));
}

TEST_CASE("Player support manifests reject unexpected executables and misplaced createdump")
{
    auto manifest = WindowsManifest();
    manifest.Files.push_back(File("Development/Managed/helper.dat", 0755U));
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(manifest), std::invalid_argument);

    manifest = WindowsManifest();
    manifest.Files.push_back(File("Development/createdump.exe", 0755U));
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(manifest), std::invalid_argument);

    manifest = WindowsManifest();
    manifest.Files.push_back(
        File("Development/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/createdump.exe", 0755U));
    CHECK_NOTHROW(Keire::Detail::ValidatePlayerSupportManifest(manifest));
}
