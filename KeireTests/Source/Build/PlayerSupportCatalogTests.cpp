#include "KeireInternal/Build/PlayerSupportCatalog.h"

#include "KeireInternal/FileSystem.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace
{
    class TemporaryDirectory final
    {
      public:
        TemporaryDirectory()
            : Path(std::filesystem::temp_directory_path() /
                   ("keire-player-support-catalog-" + Keire::AssetId::Generate().ToString()))
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

TEST_CASE("Player support release catalogs resolve legacy ID-named package files")
{
    TemporaryDirectory directory;
    const auto url = Keire::Detail::DefaultPlayerSupportCatalogUrl("example/engine", "1.2.3");
    CHECK(url == "https://github.com/example/engine/releases/download/v1.2.3/player-support-catalog.json");
    const auto path = directory.Path / "catalog.json";
    Keire::Detail::WriteTextFileAtomically(
        path,
        R"({"schemaVersion":1,"engineVersion":"1.2.3","packages":[{"id":"windows-x86_64-1.2.3","platform":"windows","architecture":"x86_64","file":"windows-x86_64-1.2.3.keireplayersupport","size":42,"sha256":"0000000000000000000000000000000000000000000000000000000000000000"}]})");
    const auto catalog = Keire::Detail::LoadPlayerSupportCatalog(path, url, "1.2.3");
    REQUIRE(catalog.Packages.size() == 1);
    CHECK(catalog.Packages.front().Url ==
          "https://github.com/example/engine/releases/download/v1.2.3/windows-x86_64-1.2.3.keireplayersupport");
    CHECK(catalog.Packages.front().Size == 42);
}

TEST_CASE("Player support release catalogs resolve SHA-256-suffixed package files")
{
    TemporaryDirectory directory;
    const auto path = directory.Path / "catalog.json";
    constexpr std::string_view digest = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const auto filename = "linux-arm64-1.2.3-" + std::string(digest) + ".keireplayersupport";
    Keire::Detail::WriteTextFileAtomically(
        path, "{\"schemaVersion\":1,\"engineVersion\":\"1.2.3\",\"packages\":[{\"id\":\"linux-arm64-1.2.3\","
              "\"platform\":\"linux\",\"architecture\":\"arm64\",\"file\":\"" +
                  filename + "\",\"size\":84,\"sha256\":\"" + std::string(digest) + "\"}]}\n");

    const auto catalog = Keire::Detail::LoadPlayerSupportCatalog(
        path, "https://github.com/example/engine/releases/download/v1.2.3/player-support-catalog.json", "1.2.3");
    REQUIRE(catalog.Packages.size() == 1);
    CHECK(catalog.Packages.front().File == filename);
    CHECK(catalog.Packages.front().Url == "https://github.com/example/engine/releases/download/v1.2.3/" + filename);
}

TEST_CASE("Player support release catalogs reject identity traversal corruption and version mismatches")
{
    CHECK_THROWS_AS((void)Keire::Detail::DefaultPlayerSupportCatalogUrl("../engine", "1.2.3"), std::invalid_argument);
    TemporaryDirectory directory;
    const auto path = directory.Path / "catalog.json";
    Keire::Detail::WriteTextFileAtomically(
        path,
        R"({"schemaVersion":1,"engineVersion":"1.2.3","packages":[{"id":"test","platform":"linux","architecture":"arm64","file":"../test.keireplayersupport","size":1,"sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"}]})");
    CHECK_THROWS_AS(
        (void)Keire::Detail::LoadPlayerSupportCatalog(
            path, "https://github.com/example/engine/releases/download/v1.2.3/player-support-catalog.json", "1.2.3"),
        std::invalid_argument);
    Keire::Detail::WriteTextFileAtomically(path, R"({"schemaVersion":1,"engineVersion":"2.0.0","packages":[]})");
    CHECK_THROWS_AS(
        (void)Keire::Detail::LoadPlayerSupportCatalog(
            path, "https://github.com/example/engine/releases/download/v2.0.0/player-support-catalog.json", "1.2.3"),
        std::invalid_argument);
}
