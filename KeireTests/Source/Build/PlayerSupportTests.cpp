#include "KeireInternal/Build/PlayerSupport.h"

#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>

#include <filesystem>

namespace
{
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
