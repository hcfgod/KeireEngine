#include "KeireHub/HubLocalContent.h"

#include "KeireHubTests/TestSupport.h"

#include <doctest/doctest.h>

namespace
{
    void WriteDistributionMarkers(const std::filesystem::path& root)
    {
        KeireHubTests::WriteText(root / "LICENSE.txt", "license\n");
        KeireHubTests::WriteText(root / "Docs" / "ProjectHub.md", "documentation\n");
    }
} // namespace

TEST_CASE("Hub local content resolves from development executable ancestry")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto repository = temporary.Path() / "source";
    WriteDistributionMarkers(repository);
    KeireHubTests::WriteText(repository / "KeireHubContent" / "Content" / "en-US.json", "{}");
    KeireHubTests::WriteText(repository / "KeireHubContent" / "Licenses" / "catalog.json", "{}");
    KeireHubTests::WriteText(repository / "KeireHubContent" / "Templates" / "catalog.json", "{}");
    const auto executable = repository / "Build" / "Bin" / "Debug-windows-x86_64" / "KeireHub" / "KeireHub.exe";

    CHECK(KeireHub::ResolveHubDistributionRoot(executable) == repository);
    CHECK(KeireHub::ResolveHubContentCatalogPath(executable) ==
          repository / "KeireHubContent" / "Content" / "en-US.json");
    CHECK(KeireHub::ResolveHubLicenseCatalogPath(executable) ==
          repository / "KeireHubContent" / "Licenses" / "catalog.json");
    CHECK(KeireHub::ResolveHubTemplatesRoot(executable) == repository / "KeireHubContent" / "Templates");
}

TEST_CASE("Hub local content prefers the nearest packaged distribution")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto repository = temporary.Path() / "source";
    WriteDistributionMarkers(repository);

    const auto distribution = repository / "Build" / "Distributions" / "keire-hub";
    WriteDistributionMarkers(distribution);
    KeireHubTests::WriteText(distribution / "content" / "Content" / "en-US.json", "{}");
    KeireHubTests::WriteText(distribution / "content" / "Licenses" / "catalog.json", "{}");
    KeireHubTests::WriteText(distribution / "content" / "Templates" / "catalog.json", "{}");
    const auto executable = distribution / "bin" / "KeireHub.exe";

    CHECK(KeireHub::ResolveHubDistributionRoot(executable) == distribution);
    CHECK(KeireHub::ResolveHubContentCatalogPath(executable) == distribution / "content" / "Content" / "en-US.json");
    CHECK(KeireHub::ResolveHubLicenseCatalogPath(executable) == distribution / "content" / "Licenses" / "catalog.json");
    CHECK(KeireHub::ResolveHubTemplatesRoot(executable) == distribution / "content" / "Templates");
}
