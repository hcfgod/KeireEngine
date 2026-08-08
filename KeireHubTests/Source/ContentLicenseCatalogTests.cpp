#include <KeireHubTests/TestSupport.h>

#include "KeireHubRuntime/ContentCatalog.h"
#include "KeireHubRuntime/LicenseCatalog.h"

#include <KeireHubRuntimeInternal/DistributionEncoding.h>

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <ranges>
#include <span>
#include <string>

using namespace KeireHub;

namespace
{
    void WriteRequiredHubLicenses(const std::filesystem::path& root)
    {
        KeireHubTests::WriteText(root / "LICENSE.txt", "Kéire MIT license\n");
        KeireHubTests::WriteText(root / "THIRD_PARTY_NOTICES.md", "Third-party notices\n");
    }

    [[nodiscard]] const ResolvedLicenseEntry* FindLicense(const std::vector<ResolvedLicenseEntry>& licenses,
                                                          const std::string_view id)
    {
        const auto found = std::ranges::find(licenses, id, &ResolvedLicenseEntry::Id);
        return found == licenses.end() ? nullptr : &*found;
    }
} // namespace

TEST_CASE("Local content catalogs resolve real files and hide unsafe or unsupported entries")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "distribution";
    KeireHubTests::WriteText(root / "Docs" / "Guide.md", "guide\n");
    KeireHubTests::WriteText(root / "outside.md", "outside\n");
    const auto catalog = temporary.Path() / "content.json";
    KeireHubTests::WriteText(catalog, R"({
      "schemaVersion":1,
      "locale":"en-US",
      "learn":[
        {"id":"valid-guide","title":"Guide","summary":"Local guide","difficulty":"beginner",
         "category":"Fundamentals","type":"guide","localPath":"Docs/Guide.md","tags":["local"]},
        {"id":"traversal","title":"Traversal","summary":"Unsafe","difficulty":"beginner",
         "category":"Invalid","type":"guide","localPath":"../outside.md"},
        {"id":"missing","title":"Missing","summary":"Missing","difficulty":"beginner",
         "category":"Invalid","type":"guide","localPath":"Docs/Missing.md"},
        {"id":"unknown","title":"Unknown","summary":"Unsupported","difficulty":"beginner",
         "category":"Invalid","type":"video","localPath":"Docs/Guide.md"}
      ],
      "resources":[
        {"id":"repository","title":"Repository","summary":"Source","difficulty":"reference",
         "category":"Project","type":"repository","url":"https://example.com/source"},
        {"id":"insecure","title":"Insecure","summary":"HTTP","difficulty":"reference",
         "category":"Invalid","type":"externalLink","url":"http://example.com"}
      ]
    })");

    ContentCatalog manager(catalog, root);
    REQUIRE(manager.Load());
    const auto snapshot = manager.Snapshot();
    REQUIRE(snapshot->Learn.size() == 1);
    CHECK(snapshot->Learn.front().Metadata.Id == "valid-guide");
    REQUIRE(snapshot->Learn.front().LocalFile);
    CHECK(std::filesystem::is_regular_file(*snapshot->Learn.front().LocalFile));
    REQUIRE(snapshot->Resources.size() == 1);
    CHECK(snapshot->Resources.front().Metadata.HttpsUrl == "https://example.com/source");
}

TEST_CASE("Content catalog failures are typed and do not replace an immutable prior snapshot")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "root";
    KeireHubTests::WriteText(root / "Docs" / "Guide.md", "guide\n");
    const auto catalog = temporary.Path() / "content.json";
    KeireHubTests::WriteText(catalog, R"({
      "schemaVersion":1,"locale":"en-US",
      "learn":[{"id":"guide","title":"Guide","summary":"Summary","difficulty":"beginner",
                "category":"Core","type":"guide","localPath":"Docs/Guide.md"}],
      "resources":[]
    })");

    ContentCatalog manager(catalog, root);
    REQUIRE(manager.Load());
    const auto retained = manager.Snapshot();
    REQUIRE(retained->Learn.size() == 1);

    KeireHubTests::WriteText(catalog, R"({"schemaVersion":99,"locale":"en-US","learn":[],"resources":[]})");
    const auto unsupported = manager.Load();
    REQUIRE_FALSE(unsupported);
    CHECK(unsupported.Error().Code == HubErrorCode::UnsupportedSchema);
    CHECK(manager.Snapshot() == retained);

    KeireHubTests::WriteText(catalog, "{");
    const auto malformed = manager.Load();
    REQUIRE_FALSE(malformed);
    CHECK(malformed.Error().Code == HubErrorCode::InvalidData);
    CHECK(manager.Snapshot() == retained);

    KeireHubTests::WriteText(catalog, std::string(4 * 1024 * 1024 + 1, ' '));
    const auto oversized = manager.Load();
    REQUIRE_FALSE(oversized);
    CHECK(oversized.Error().Code == HubErrorCode::InvalidData);
    CHECK(manager.Snapshot() == retained);
}

TEST_CASE("Packaged content manifest exposes only existing learning and resource targets")
{
    const auto root = std::filesystem::current_path();
    ContentCatalog manager(root / "KeireHubContent" / "Content" / "en-US.json", root);
    REQUIRE(manager.Load());
    CHECK(manager.Snapshot()->Learn.size() == 11);
    CHECK(manager.Snapshot()->Resources.size() == 5);
    CHECK(std::ranges::all_of(manager.Snapshot()->Learn,
                              [](const auto& item) { return item.LocalFile || item.Metadata.HttpsUrl; }));
    const auto sandbox = std::ranges::find(manager.Snapshot()->Learn, "keire-sandbox",
                                           [](const auto& item) -> const std::string& { return item.Metadata.Id; });
    REQUIRE(sandbox != manager.Snapshot()->Learn.end());
    REQUIRE(sandbox->LocalFile);
    CHECK(*sandbox->LocalFile == std::filesystem::weakly_canonical(root / "Samples/KeireSandbox/README.md"));
}

TEST_CASE("License catalog aggregates and groups actual Hub and package license files")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "hub";
    WriteRequiredHubLicenses(root);
    KeireHubTests::WriteText(root / "third-party" / "licenses" / "dependency-LICENSE.txt", "Dependency license\n");

    const auto package = temporary.Path() / "package";
    constexpr std::string_view scopes[]{"editor", "buildSupport", "template", "content"};
    for (const auto scope : scopes)
        KeireHubTests::WriteText(package / "licenses" / (std::string(scope) + ".txt"), std::string(scope) + " text\n");
    const auto packageCatalog = temporary.Path() / "package-licenses.json";
    KeireHubTests::WriteText(packageCatalog, R"({
      "schemaVersion":1,
      "licenses":[
        {"id":"editor-license","displayName":"Editor dependency","scope":"editor",
         "packageId":"editor.pkg","version":"1.2.0","sourcePath":"licenses/editor.txt"},
        {"id":"support-license","displayName":"Build Support dependency","scope":"buildSupport",
         "packageId":"support.pkg","sourcePath":"licenses/buildSupport.txt"},
        {"id":"template-license","displayName":"Template dependency","scope":"template",
         "packageId":"template.pkg","sourcePath":"licenses/template.txt"},
        {"id":"content-license","displayName":"Content dependency","scope":"content",
         "packageId":"content.pkg","sourcePath":"licenses/content.txt"}
      ]
    })");

    LicenseCatalog manager(root, {{.CatalogPath = packageCatalog, .ContentRoot = package}});
    REQUIRE(manager.Load());
    const auto snapshot = manager.Snapshot();
    REQUIRE(snapshot->size() >= 7);
    REQUIRE(FindLicense(*snapshot, "hub.keire-mit"));
    REQUIRE(FindLicense(*snapshot, "editor-license"));
    CHECK(FindLicense(*snapshot, "editor-license")->Group == "Editor · editor.pkg 1.2.0");
    CHECK(FindLicense(*snapshot, "support-license")->Group == "Build Support · support.pkg");
    CHECK(FindLicense(*snapshot, "template-license")->Group == "Template · template.pkg");
    CHECK(FindLicense(*snapshot, "content-license")->Group == "Content · content.pkg");

    const auto matches = manager.Search("template.pkg");
    REQUIRE(matches.size() == 1);
    CHECK((*snapshot)[matches.front()].Id == "template-license");
    CHECK(manager.Search("dependency license").size() >= 1);
}

TEST_CASE("License catalogs hide unsafe entries and report malformed roots without losing prior data")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "hub";
    WriteRequiredHubLicenses(root);
    const auto package = temporary.Path() / "package";
    KeireHubTests::WriteText(package / "licenses" / "Valid.txt", "Valid package license\n");
    KeireHubTests::WriteText(temporary.Path() / "outside.txt", "Outside\n");
    const auto catalog = temporary.Path() / "licenses.json";
    KeireHubTests::WriteText(catalog, R"({
      "schemaVersion":1,
      "licenses":[
        {"id":"valid","displayName":"Valid","scope":"template","packageId":"template.valid",
         "sourcePath":"licenses/Valid.txt"},
        {"id":"traversal","displayName":"Traversal","scope":"content","sourcePath":"../outside.txt"},
        {"id":"missing","displayName":"Missing","scope":"editor","sourcePath":"licenses/Missing.txt"}
      ]
    })");

    LicenseCatalog manager(root, {{.CatalogPath = catalog, .ContentRoot = package}});
    REQUIRE(manager.Load());
    const auto retained = manager.Snapshot();
    CHECK(FindLicense(*retained, "valid"));
    CHECK_FALSE(FindLicense(*retained, "traversal"));
    CHECK_FALSE(FindLicense(*retained, "missing"));

    KeireHubTests::WriteText(catalog, R"({"schemaVersion":8,"licenses":[]})");
    const auto unsupported = manager.Load();
    REQUIRE_FALSE(unsupported);
    CHECK(unsupported.Error().Code == HubErrorCode::UnsupportedSchema);
    CHECK(manager.Snapshot() == retained);

    KeireHubTests::WriteText(catalog, "not-json");
    const auto malformed = manager.Load();
    REQUIRE_FALSE(malformed);
    CHECK(malformed.Error().Code == HubErrorCode::InvalidData);
    CHECK(manager.Snapshot() == retained);
}

TEST_CASE("Installed package licenses are grouped and reverified from managed editor receipts")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "managed-editor";
    constexpr std::string_view licenseText = "Package license text\n";
    const auto licensePath = std::filesystem::path("licenses") / "dependency.txt";
    KeireHubTests::WriteText(root / licensePath, licenseText);
    auto version = SemanticVersion::Parse("1.2.3");
    REQUIRE(version);

    InstalledPackageRecord package{.Id = "keire.editor",
                                   .Version = std::move(version).Value(),
                                   .Kind = PackageKind::Editor,
                                   .ArtifactSizeBytes = licenseText.size(),
                                   .ArtifactSha256 = KeireHubTests::Digest('a'),
                                   .InstalledSizeBytes = licenseText.size(),
                                   .Files = {{.Path = licensePath,
                                              .SizeBytes = licenseText.size(),
                                              .Sha256 = Detail::Sha256Hex(std::as_bytes(std::span(licenseText))),
                                              .Mode = 0644U}},
                                   .LicenseReferences = {"licenses/dependency.txt"}};
    PackageInstallReceipt receipt{.AggregateIdentitySha256 = KeireHubTests::Digest('b'),
                                  .AggregateInstalledSizeBytes = licenseText.size(),
                                  .Packages = {package}};
    const auto encodedReceipt = EncodePackageInstallReceipt(receipt);
    REQUIRE(encodedReceipt);
    KeireHubTests::WriteText(root / PackageInstallReceiptFileName, encodedReceipt.Value());
    const auto receiptSha256 = Detail::Sha256Hex(std::as_bytes(std::span(encodedReceipt.Value())));

    EditorInstallation managed{.Id = "managed-editor-a",
                               .Version = "1.2.3",
                               .Root = std::filesystem::absolute(root),
                               .Ownership = InstallationOwnership::Managed,
                               .PackageTreeIdentity = receipt.AggregateIdentitySha256,
                               .PackageReceiptSha256 = receiptSha256,
                               .InstalledPackages = {std::move(package)}};
    EditorInstallation external = managed;
    external.Id = "external-editor";
    external.Ownership = InstallationOwnership::External;

    const std::array installations{managed, external};
    auto resolved = ResolveInstalledPackageLicenses(installations);
    REQUIRE(resolved);
    REQUIRE(resolved.Value().size() == 1U);
    CHECK(resolved.Value().front().Group == "Editor - keire.editor 1.2.3");
    CHECK(resolved.Value().front().Text == licenseText);
    CHECK(resolved.Value().front().SourcePath == std::filesystem::weakly_canonical(root / licensePath));

    KeireHubTests::WriteText(root / licensePath, "tampered\n");
    resolved = ResolveInstalledPackageLicenses(installations);
    REQUIRE_FALSE(resolved);
    CHECK(resolved.Error().Code == HubErrorCode::EditorInventoryInvalid);
}
