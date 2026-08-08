#include "KeireHubRuntime/HubMaintenance.h"

#include <KeireHubTests/TestSupport.h>

#include <doctest/doctest.h>

#include <fstream>

using namespace KeireHub;

TEST_CASE("Verified cache clearing is confined to the content-addressed package directory")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto cache = temporary.Path() / "Cache";
    std::filesystem::create_directories(cache / "sha256/aa");
    std::filesystem::create_directories(cache / "catalogs");
    {
        std::ofstream package(cache / "sha256/aa/package.package");
        package << "verified";
        std::ofstream catalog(cache / "catalogs/catalog.json");
        catalog << "preserve";
    }

    REQUIRE(ClearVerifiedPackageCache(std::filesystem::absolute(cache)));
    CHECK_FALSE(std::filesystem::exists(cache / "sha256"));
    CHECK(std::filesystem::is_regular_file(cache / "catalogs/catalog.json"));
    CHECK(ClearVerifiedPackageCache(std::filesystem::absolute(cache)));
}

TEST_CASE("Verified cache clearing rejects ambiguous or non-directory targets")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto cache = temporary.Path() / "Cache";
    std::filesystem::create_directories(cache);
    {
        std::ofstream target(cache / "sha256");
        target << "not a cache directory";
    }

    const auto unsafe = ClearVerifiedPackageCache(std::filesystem::absolute(cache));
    REQUIRE_FALSE(unsafe);
    CHECK(unsafe.Error().Code == HubErrorCode::UnsafeInstallRoot);
    CHECK(std::filesystem::is_regular_file(cache / "sha256"));
    CHECK_FALSE(ClearVerifiedPackageCache("relative-cache"));
}
