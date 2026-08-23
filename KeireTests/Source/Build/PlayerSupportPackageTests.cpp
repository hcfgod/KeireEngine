#include "Keire/BuildInfo.h"
#include "KeireInternal/Build/PlayerSupportPackage.h"

#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>

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
                   ("keire-player-support-package-" + Keire::AssetId::Generate().ToString()))
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

    [[nodiscard]] Keire::Detail::PlayerSupportManifest Manifest()
    {
        return {.Id = "windows-x86_64-tests",
                .EngineVersion = std::string(Keire::GetBuildInfo().Version),
                .Platform = Keire::PlayerPlatform::Windows,
                .Architecture = Keire::PlayerArchitecture::X86_64,
                .ModuleFingerprint = "test-modules",
                .SourceModules = {"TestModule"},
                .Variants = {{.Configuration = Keire::PlayerBuildConfiguration::Development,
                              .Root = "Development",
                              .Executable = "Player.exe"}}};
    }

    [[nodiscard]] Keire::Detail::PlayerSupportManifest LinuxManifest()
    {
        auto manifest = Manifest();
        manifest.Id = "linux-x86_64-tests";
        manifest.Platform = Keire::PlayerPlatform::Linux;
        manifest.Variants.front().Executable = "Player";
        return manifest;
    }

    void Write(const std::filesystem::path& path, const std::string_view contents)
    {
        std::filesystem::create_directories(path.parent_path());
        Keire::Detail::WriteTextFileAtomically(path, contents);
    }

    void WriteWindowsPayload(const std::filesystem::path& payload)
    {
        Write(payload / "Development/Player.exe", "player-template");
        Write(payload / "Development/nethost.dll", "nethost");
        Write(payload / "Development/MSVCP140.dll", "msvcp");
        Write(payload / "Development/MSVCP140_ATOMIC_WAIT.dll", "msvcp-atomic-wait");
        Write(payload / "Development/MSVCP140_1.dll", "msvcp-1");
        Write(payload / "Development/VCRUNTIME140.dll", "vcruntime");
        Write(payload / "Development/VCRUNTIME140_1.dll", "vcruntime-1");
        Write(payload / "Development/Managed/Coral.Managed.dll", "coral");
        Write(payload / "Development/Managed/Coral.Managed.deps.json", "{}");
        Write(payload / "Development/Managed/Coral.Managed.runtimeconfig.json", "{}");
        Write(payload / "Development/Managed/Keire.Managed.dll", "keire-managed");
        Write(payload / "Development/Managed/Dotnet/host/fxr/10.0.10/hostfxr.dll", "hostfxr");
        const auto runtime = payload / "Development/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11";
        Write(runtime / "coreclr.dll", "coreclr");
        Write(runtime / "hostpolicy.dll", "hostpolicy");
        Write(runtime / "System.Private.CoreLib.dll", "corelib");
        for (const auto filename : RequiredLicenseFiles)
            Write(payload / "Development/Licenses" / filename, "license");
    }

    void WriteLinuxPayload(const std::filesystem::path& payload, const bool includeCreateDump)
    {
        Write(payload / "Development/Player", "player-template");
        Write(payload / "Development/libnethost.so", "nethost");
        Write(payload / "Development/Managed/Coral.Managed.dll", "coral");
        Write(payload / "Development/Managed/Coral.Managed.deps.json", "{}");
        Write(payload / "Development/Managed/Coral.Managed.runtimeconfig.json", "{}");
        Write(payload / "Development/Managed/Keire.Managed.dll", "keire-managed");
        Write(payload / "Development/Managed/Dotnet/host/fxr/10.0.10/libhostfxr.so", "hostfxr");
        const auto runtime = payload / "Development/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11";
        Write(runtime / "libcoreclr.so", "coreclr");
        Write(runtime / "libhostpolicy.so", "hostpolicy");
        Write(runtime / "System.Private.CoreLib.dll", "corelib");
        if (includeCreateDump)
            Write(runtime / "createdump", "createdump");
        for (const auto filename : RequiredLicenseFiles)
            Write(payload / "Development/Licenses" / filename, "license");
    }
} // namespace

TEST_CASE("Player support packages stream, install, verify, repair, and remove transactionally")
{
    TemporaryDirectory directory;
    const auto payload = directory.Path / "payload";
    WriteWindowsPayload(payload);
    Write(payload / "Development/Managed/runtime.dll", "managed-runtime");
    const auto package = directory.Path / "windows.keireplayersupport";
    Keire::Detail::PlayerSupportPackageResult created;
    REQUIRE_NOTHROW(created = Keire::Detail::CreatePlayerSupportPackage(Manifest(), payload, package, 1));
    CHECK(created.ArchiveSize == std::filesystem::file_size(package));
    CHECK(created.Manifest.Files.size() == RequiredLicenseFiles.size() + 16);

    Keire::Detail::PlayerSupportManifest decoded;
    REQUIRE_NOTHROW(decoded = Keire::Detail::ReadPlayerSupportPackageManifest(package));
    CHECK(decoded.Id == created.Manifest.Id);
    CHECK(decoded.Files == created.Manifest.Files);

    const auto storage = directory.Path / "installed";
    Keire::Detail::PlayerSupportPackageResult installed;
    REQUIRE_NOTHROW(installed = Keire::Detail::InstallPlayerSupportPackage(package, "test-modules", {}, storage));
    const auto installation = storage / installed.Manifest.EngineVersion / installed.Manifest.Id;
    std::string diagnostic;
    CHECK(Keire::Detail::ValidateInstalledPlayerSupport(installation, diagnostic));
    CHECK(diagnostic.empty());
    REQUIRE(Keire::Detail::InstalledPlayerSupport(storage).size() == 1);

    Write(installation / "Development/Player.exe", "corrupt");
    CHECK_FALSE(Keire::Detail::ValidateInstalledPlayerSupport(installation, diagnostic));
    CHECK(diagnostic == "Installed Build Support files are missing or corrupt.");
    CHECK(diagnostic.find(Keire::Detail::PathToUtf8(directory.Path)) == std::string::npos);
    CHECK_FALSE(Keire::Detail::ValidateInstalledPlayerSupportInventory(installation, "0.0.0", diagnostic));
    CHECK(diagnostic == "Installed Build Support files are missing or corrupt.");
    (void)Keire::Detail::InstallPlayerSupportPackage(package, "test-modules", {}, storage);
    CHECK(Keire::Detail::ValidateInstalledPlayerSupport(installation, diagnostic));

    Keire::Detail::RemovePlayerSupport(installed.Manifest.EngineVersion, installed.Manifest.Id, storage);
    CHECK_FALSE(std::filesystem::exists(installation));
    CHECK(Keire::Detail::InstalledPlayerSupport(storage).empty());
}

TEST_CASE("Player support failures expose stable status codes and user-facing messages")
{
    const auto inventory =
        Keire::Detail::DescribePlayerSupportFailure(Keire::Detail::PlayerSupportFailureKind::InstalledInventoryInvalid);
    CHECK(inventory.Code == "build_support.inventory_invalid");
    CHECK(inventory.Message == "Installed Build Support files are missing or corrupt.");

    const auto installation =
        Keire::Detail::DescribePlayerSupportFailure(Keire::Detail::PlayerSupportFailureKind::InstallationFailed);
    CHECK(installation.Code == "build_support.install_failed");
    CHECK(installation.Message.find("exception") == std::string_view::npos);

    const auto catalog =
        Keire::Detail::DescribePlayerSupportFailure(Keire::Detail::PlayerSupportFailureKind::CatalogUnavailable);
    CHECK(catalog.Code == "build_support.catalog_unavailable");

    const auto download = Keire::Detail::DescribePlayerSupportFailure(
        Keire::Detail::PlayerSupportFailureKind::DownloadAndInstallationFailed);
    CHECK(download.Code == "build_support.download_install_failed");
}

TEST_CASE("Player support installation cancellation removes staging and preserves no partial module")
{
    TemporaryDirectory directory;
    const auto payload = directory.Path / "payload";
    WriteWindowsPayload(payload);
    Write(payload / "Development/Player.exe", std::string(std::size_t{1024} * 1024, 'x'));
    const auto package = directory.Path / "cancel.keireplayersupport";
    const auto created = Keire::Detail::CreatePlayerSupportPackage(Manifest(), payload, package, 1);
    const auto storage = directory.Path / "installed";
    bool cancel = false;
    CHECK_THROWS((
        [&]
        {
            (void)Keire::Detail::InstallPlayerSupportPackage(
                package, "test-modules",
                {.Cancelled = [&] { return cancel; },
                 .Progress = [&](const float value, std::string_view) { cancel = value > 0.1F; }},
                storage);
        }()));
    CHECK_FALSE(std::filesystem::exists(storage / created.Manifest.EngineVersion / created.Manifest.Id));
    if (std::filesystem::is_directory(storage / created.Manifest.EngineVersion))
    {
        for (const auto& entry : std::filesystem::directory_iterator(storage / created.Manifest.EngineVersion))
            CHECK_FALSE(entry.path().filename().string().starts_with(".install-"));
    }
}

TEST_CASE("Player support installation refuses redirected version storage")
{
    TemporaryDirectory directory;
    const auto payload = directory.Path / "payload";
    WriteWindowsPayload(payload);
    const auto package = directory.Path / "redirected-install.keireplayersupport";
    const auto created = Keire::Detail::CreatePlayerSupportPackage(Manifest(), payload, package, 1);
    const auto storage = directory.Path / "installed";
    const auto redirected = directory.Path / "redirect-target";
    std::filesystem::create_directories(storage);
    std::filesystem::create_directories(redirected);
    Write(redirected / "sentinel.txt", "preserve");

    std::error_code error;
    std::filesystem::create_directory_symlink(redirected, storage / created.Manifest.EngineVersion, error);
    if (error)
    {
        MESSAGE("Directory-link creation is unavailable; redirect test skipped: " << error.message());
        return;
    }

    CHECK_THROWS(([&] { (void)Keire::Detail::InstallPlayerSupportPackage(package, "test-modules", {}, storage); }()));
    CHECK(Keire::Detail::ReadTextFile(redirected / "sentinel.txt", 64) == "preserve");
    CHECK_FALSE(std::filesystem::exists(redirected / created.Manifest.Id));
}

TEST_CASE("Installed player support validation rejects redirected files and directories")
{
    TemporaryDirectory directory;
    const auto payload = directory.Path / "payload";
    WriteWindowsPayload(payload);
    const auto package = directory.Path / "redirected-validation.keireplayersupport";
    (void)Keire::Detail::CreatePlayerSupportPackage(Manifest(), payload, package, 1);
    const auto storage = directory.Path / "installed";
    const auto installed = Keire::Detail::InstallPlayerSupportPackage(package, "test-modules", {}, storage);
    const auto installation = storage / installed.Manifest.EngineVersion / installed.Manifest.Id;
    std::string diagnostic;

    const auto originalFile = installation / "Development/Player.exe";
    const auto redirectedFile = directory.Path / "redirected-player.exe";
    std::filesystem::rename(originalFile, redirectedFile);
    std::error_code error;
    std::filesystem::create_symlink(redirectedFile, originalFile, error);
    if (!error)
    {
        CHECK_FALSE(Keire::Detail::ValidateInstalledPlayerSupport(installation, diagnostic));
        std::filesystem::remove(originalFile);
    }
    else
        MESSAGE("File-link creation is unavailable; file redirect check skipped: " << error.message());
    std::filesystem::rename(redirectedFile, originalFile);

    const auto originalDirectory = installation / "Development/Managed";
    const auto redirectedDirectory = directory.Path / "redirected-managed";
    std::filesystem::rename(originalDirectory, redirectedDirectory);
    error.clear();
    std::filesystem::create_directory_symlink(redirectedDirectory, originalDirectory, error);
    if (!error)
    {
        CHECK_FALSE(Keire::Detail::ValidateInstalledPlayerSupport(installation, diagnostic));
        std::filesystem::remove(originalDirectory);
    }
    else
        MESSAGE("Directory-link creation is unavailable; directory redirect check skipped: " << error.message());
    std::filesystem::rename(redirectedDirectory, originalDirectory);
    CHECK(Keire::Detail::ValidateInstalledPlayerSupport(installation, diagnostic));
}

TEST_CASE("Installed player support validation fails closed when a file becomes a redirect before hashing")
{
    TemporaryDirectory directory;
    const auto linkProbeTarget = directory.Path / "link-probe-target";
    const auto linkProbe = directory.Path / "link-probe";
    Write(linkProbeTarget, "probe");
    std::error_code error;
    std::filesystem::create_symlink(linkProbeTarget, linkProbe, error);
    if (error)
    {
        MESSAGE("File-link creation is unavailable; validation race test skipped: " << error.message());
        return;
    }
    std::filesystem::remove(linkProbe);

    const auto payload = directory.Path / "payload";
    WriteWindowsPayload(payload);
    const auto package = directory.Path / "redirect-race.keireplayersupport";
    (void)Keire::Detail::CreatePlayerSupportPackage(Manifest(), payload, package, 1);
    const auto storage = directory.Path / "installed";
    const auto installed = Keire::Detail::InstallPlayerSupportPackage(package, "test-modules", {}, storage);
    const auto installation = storage / installed.Manifest.EngineVersion / installed.Manifest.Id;
    const auto original = installation / "Development/Player.exe";
    const auto moved = directory.Path / "moved-player.exe";
    bool swapped = false;
    Keire::Detail::SetAnchoredFileSystemOperationHookForTesting(
        [&](const std::string_view operation, const std::filesystem::path& relative)
        {
            if (operation != "read" || relative != std::filesystem::path("Development/Player.exe"))
                return;
            Keire::Detail::SetAnchoredFileSystemOperationHookForTesting({});
            std::error_code swapError;
            std::filesystem::rename(original, moved, swapError);
            if (swapError)
                return;
            std::filesystem::create_symlink(moved, original, swapError);
            if (swapError)
            {
                std::filesystem::rename(moved, original, swapError);
                return;
            }
            swapped = true;
        });

    std::string diagnostic;
    CHECK_FALSE(Keire::Detail::ValidateInstalledPlayerSupport(installation, diagnostic));
    Keire::Detail::SetAnchoredFileSystemOperationHookForTesting({});
    CHECK(swapped);
    if (swapped)
    {
        std::filesystem::remove(original);
        std::filesystem::rename(moved, original);
    }
}

TEST_CASE("Player support inventory completes or rolls back bounded removal journals after interruption")
{
    TemporaryDirectory directory;
    const auto payload = directory.Path / "payload";
    WriteWindowsPayload(payload);
    const auto package = directory.Path / "recovery.keireplayersupport";
    (void)Keire::Detail::CreatePlayerSupportPackage(Manifest(), payload, package, 1);
    const auto storage = directory.Path / "installed";
    const auto installed = Keire::Detail::InstallPlayerSupportPackage(package, "test-modules", {}, storage);
    const auto versionRoot = storage / installed.Manifest.EngineVersion;
    const auto installation = versionRoot / installed.Manifest.Id;

    const auto tombstoneName = std::string(".remove-interrupted");
    const auto tombstone = versionRoot / tombstoneName;
    const auto journal = versionRoot / (tombstoneName + ".json");
    Write(journal, "{\"schemaVersion\":1,\"engineVersion\":\"" + installed.Manifest.EngineVersion + "\",\"packId\":\"" +
                       installed.Manifest.Id + "\",\"tombstone\":\"" + tombstoneName + "\"}\n");
    std::filesystem::rename(installation, tombstone);

    CHECK(Keire::Detail::InstalledPlayerSupport(storage).empty());
    CHECK_FALSE(std::filesystem::exists(tombstone));
    CHECK_FALSE(std::filesystem::exists(journal));
    CHECK(Keire::Detail::ReadTextFile(versionRoot / "registry.json", std::size_t{64} * 1024)
              .find(installed.Manifest.Id) == std::string::npos);

    (void)Keire::Detail::InstallPlayerSupportPackage(package, "test-modules", {}, storage);
    const auto preRenameName = std::string(".remove-before-rename");
    const auto preRenameJournal = versionRoot / (preRenameName + ".json");
    Write(preRenameJournal, "{\"schemaVersion\":1,\"engineVersion\":\"" + installed.Manifest.EngineVersion +
                                "\",\"packId\":\"" + installed.Manifest.Id + "\",\"tombstone\":\"" + preRenameName +
                                "\"}\n");

    REQUIRE(Keire::Detail::InstalledPlayerSupport(storage).size() == 1);
    CHECK(std::filesystem::is_directory(installation));
    CHECK_FALSE(std::filesystem::exists(preRenameJournal));
}

#if defined(_WIN32)
TEST_CASE("Player support installation supports Windows payload paths beyond the legacy limit")
{
    TemporaryDirectory directory;
    const auto payload = directory.Path / "payload";
    const auto nested = std::filesystem::path(std::string(45, 'p')) / std::string(45, 'q') / "runtime.dll";
    WriteWindowsPayload(payload);
    Write(payload / "Development" / nested, "managed-runtime");
    const auto package = directory.Path / "windows-long-path.keireplayersupport";
    (void)Keire::Detail::CreatePlayerSupportPackage(Manifest(), payload, package, 1);

    const auto storage = directory.Path / std::string(40, 's');
    const auto installed = Keire::Detail::InstallPlayerSupportPackage(package, "test-modules", {}, storage);
    const auto installation = storage / installed.Manifest.EngineVersion / installed.Manifest.Id;
    CHECK((installation / "Development" / nested).wstring().size() > 260);
    std::string diagnostic;
    CHECK(Keire::Detail::ValidateInstalledPlayerSupport(installation, diagnostic));
}
#endif

#if !defined(_WIN32)
TEST_CASE("Player support packages preserve allowed executable modes and reject unexpected executable files")
{
    TemporaryDirectory directory;
    const auto payload = directory.Path / "payload";
    WriteLinuxPayload(payload, true);
    const auto executable = payload / "Development/Player";
    const auto createDump = payload / "Development/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/createdump";
    const auto executableMode = std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                                std::filesystem::perms::others_exec;
    std::filesystem::permissions(executable, executableMode, std::filesystem::perm_options::replace);
    std::filesystem::permissions(createDump, executableMode, std::filesystem::perm_options::replace);

    const auto package = directory.Path / "linux.keireplayersupport";
    const auto created = Keire::Detail::CreatePlayerSupportPackage(LinuxManifest(), payload, package, 1);
    const auto createdDumpRecord =
        std::ranges::find(created.Manifest.Files,
                          std::filesystem::path("Development/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/"
                                                "createdump"),
                          &Keire::Detail::PlayerSupportFile::Path);
    REQUIRE(createdDumpRecord != created.Manifest.Files.end());
    CHECK(createdDumpRecord->Mode == 0755U);

    const auto storage = directory.Path / "installed";
    const auto installed = Keire::Detail::InstallPlayerSupportPackage(package, "test-modules", {}, storage);
    const auto installedCreateDump = storage / installed.Manifest.EngineVersion / installed.Manifest.Id /
                                     "Development/Managed/Dotnet/shared/Microsoft.NETCore.App/10.0.11/createdump";
    CHECK((std::filesystem::status(installedCreateDump).permissions() & std::filesystem::perms::owner_exec) !=
          std::filesystem::perms::none);
    std::string diagnostic;
    CHECK(Keire::Detail::ValidateInstalledPlayerSupport(
        storage / installed.Manifest.EngineVersion / installed.Manifest.Id, diagnostic));
    constexpr std::array tamperedModes{
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write, std::filesystem::perms::owner_all,
        std::filesystem::perms::owner_all | std::filesystem::perms::group_all | std::filesystem::perms::others_all};
    for (const auto mode : tamperedModes)
    {
        std::filesystem::permissions(installedCreateDump, mode, std::filesystem::perm_options::replace);
        CHECK_FALSE(Keire::Detail::ValidateInstalledPlayerSupport(
            storage / installed.Manifest.EngineVersion / installed.Manifest.Id, diagnostic));
        std::filesystem::permissions(installedCreateDump, executableMode, std::filesystem::perm_options::replace);
        CHECK(Keire::Detail::ValidateInstalledPlayerSupport(
            storage / installed.Manifest.EngineVersion / installed.Manifest.Id, diagnostic));
    }

    Write(payload / "Development/Managed/helper.dat", "helper");
    std::filesystem::permissions(payload / "Development/Managed/helper.dat", executableMode,
                                 std::filesystem::perm_options::replace);
    CHECK_THROWS_AS(
        Keire::Detail::CreatePlayerSupportPackage(LinuxManifest(), payload, directory.Path / "invalid.package", 1),
        std::invalid_argument);
}
#endif

TEST_CASE("Player support manifests reject case collisions oversized entries and escaping identities")
{
    auto manifest = Manifest();
    manifest.Files = {{.Path = "Development/Player.exe", .Size = 1, .Sha256 = std::string(64, '0')},
                      {.Path = "development/player.EXE", .Size = 1, .Sha256 = std::string(64, '1')}};
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(manifest), std::invalid_argument);

    manifest.Files.resize(1);
    manifest.Files.front().Size = 16ULL * 1024ULL * 1024ULL * 1024ULL + 1;
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(manifest), std::invalid_argument);

    manifest.Files.clear();
    manifest.Id = "../escape";
    CHECK_THROWS_AS(Keire::Detail::ValidatePlayerSupportManifest(manifest), std::invalid_argument);

    TemporaryDirectory directory;
    CHECK_THROWS_AS(Keire::Detail::RemovePlayerSupport("C:", "pack", directory.Path), std::invalid_argument);
    CHECK_THROWS_AS(Keire::Detail::RemovePlayerSupport("1.0.0", "D:pack", directory.Path), std::invalid_argument);
    CHECK_THROWS_AS(Keire::Detail::RemovePlayerSupport("1.0.0", ".remove-shadow", directory.Path),
                    std::invalid_argument);
    CHECK_THROWS_AS(Keire::Detail::RemovePlayerSupport("1.0.0", "NUL", directory.Path), std::invalid_argument);
}

TEST_CASE("Player support packages reject corrupted compressed streams")
{
    TemporaryDirectory directory;
    const auto payload = directory.Path / "payload";
    WriteWindowsPayload(payload);
    const auto package = directory.Path / "corrupt.keireplayersupport";
    (void)Keire::Detail::CreatePlayerSupportPackage(Manifest(), payload, package, 1);
    {
        std::fstream stream(package, std::ios::binary | std::ios::in | std::ios::out);
        REQUIRE(stream.good());
        stream.seekp(static_cast<std::streamoff>(std::filesystem::file_size(package) / 2));
        const char corrupt = static_cast<char>(0xff);
        stream.write(&corrupt, 1);
    }
    CHECK_THROWS((
        [&]
        {
            (void)Keire::Detail::InstallPlayerSupportPackage(package, "test-modules", {}, directory.Path / "install");
        }()));
}
