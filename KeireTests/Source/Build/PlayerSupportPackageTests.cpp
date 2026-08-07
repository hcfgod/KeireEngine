#include "Keire/BuildInfo.h"
#include "KeireInternal/Build/PlayerSupportPackage.h"

#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

namespace
{
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

    void Write(const std::filesystem::path& path, const std::string_view contents)
    {
        std::filesystem::create_directories(path.parent_path());
        Keire::Detail::WriteTextFileAtomically(path, contents);
    }
} // namespace

TEST_CASE("Player support packages stream, install, verify, repair, and remove transactionally")
{
    TemporaryDirectory directory;
    const auto payload = directory.Path / "payload";
    Write(payload / "Development/Player.exe", "player-template");
    Write(payload / "Development/Managed/runtime.dll", "managed-runtime");
    const auto package = directory.Path / "windows.keireplayersupport";
    Keire::Detail::PlayerSupportPackageResult created;
    REQUIRE_NOTHROW(created = Keire::Detail::CreatePlayerSupportPackage(Manifest(), payload, package, 1));
    CHECK(created.ArchiveSize == std::filesystem::file_size(package));
    CHECK(created.Manifest.Files.size() == 2);

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
    (void)Keire::Detail::InstallPlayerSupportPackage(package, "test-modules", {}, storage);
    CHECK(Keire::Detail::ValidateInstalledPlayerSupport(installation, diagnostic));

    Keire::Detail::RemovePlayerSupport(installed.Manifest.EngineVersion, installed.Manifest.Id, storage);
    CHECK_FALSE(std::filesystem::exists(installation));
    CHECK(Keire::Detail::InstalledPlayerSupport(storage).empty());
}

TEST_CASE("Player support installation cancellation removes staging and preserves no partial module")
{
    TemporaryDirectory directory;
    const auto payload = directory.Path / "payload";
    Write(payload / "Development/Player.exe", std::string(1024 * 1024, 'x'));
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

#if defined(_WIN32)
TEST_CASE("Player support installation supports Windows payload paths beyond the legacy limit")
{
    TemporaryDirectory directory;
    const auto payload = directory.Path / "payload";
    const auto nested = std::filesystem::path(std::string(45, 'p')) / std::string(45, 'q') / "runtime.dll";
    Write(payload / "Development/Player.exe", "player-template");
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
}

TEST_CASE("Player support packages reject corrupted compressed streams")
{
    TemporaryDirectory directory;
    const auto payload = directory.Path / "payload";
    Write(payload / "Development/Player.exe", "player-template");
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
