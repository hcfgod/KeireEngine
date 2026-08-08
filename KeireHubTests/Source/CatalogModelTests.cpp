#include "KeireHubRuntime/CatalogModels.h"

#include <doctest/doctest.h>

using namespace KeireHub;

TEST_CASE("Template catalogs parse manifest-driven templates and package requirements")
{
    constexpr auto document = R"({
      "schemaVersion":1,
      "templates":[{
        "id":"keire.3d-starter","version":"1.1.0","displayName":"3D Starter",
        "description":"A small production-ready 3D starting point.","category":"core",
        "tags":["3D","Starter"],"thumbnail":"Thumbnails/3d.png",
        "screenshots":["Screenshots/3d-main.png"],"compatibleEditors":"^1.0.0",
        "projectSchema":3,"platformTarget":"desktop","estimatedSizeBytes":4096,
        "payloadRoot":"Payload","defaultProjectConfiguration":{"renderer":"forward"},
        "starterContent":["Assets/Scenes/Main.keirescene"],
        "requiredPackages":[{"packageId":"template.payload.3d","version":"=1.1.0"}],
        "recommendedPackages":[],"licenses":["Licenses/MIT.txt"],"featured":true
      }]
    })";

    auto catalog = ParseTemplateCatalog(document);
    REQUIRE(catalog);
    REQUIRE(catalog.Value().Templates.size() == 1);
    const auto& entry = catalog.Value().Templates.front();
    CHECK(entry.Id == "keire.3d-starter");
    CHECK(entry.Category == TemplateCategory::Core);
    CHECK(entry.CompatibleEditors.Matches(SemanticVersion::Parse("1.5.0").Value()));
    CHECK(entry.RequiredPackages.front().PackageId == "template.payload.3d");
    CHECK(entry.Featured);
}

TEST_CASE("Template catalogs reject payload and starter-content traversal")
{
    constexpr auto traversal = R"({
      "schemaVersion":1,
      "templates":[{
        "id":"unsafe","version":"1.0.0","displayName":"Unsafe","description":"Unsafe template",
        "category":"core","thumbnail":"Thumb.png","compatibleEditors":"*","projectSchema":3,
        "platformTarget":"desktop","estimatedSizeBytes":1,"payloadRoot":"../Payload",
        "starterContent":[],"requiredPackages":[],"recommendedPackages":[],"licenses":[]
      }]
    })";
    const auto result = ParseTemplateCatalog(traversal);
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == HubErrorCode::InvalidData);
}

TEST_CASE("Content catalogs require one confined local path or credential-free HTTPS URL")
{
    constexpr auto document = R"({
      "schemaVersion":1,"locale":"en-US",
      "learn":[{
        "id":"getting-started","title":"Getting Started","summary":"Create and open your first project.",
        "difficulty":"beginner","category":"Fundamentals","type":"guide",
        "localPath":"Docs/GettingStarted.md","tags":["Projects"],"requiredEditorVersion":"^1.0.0",
        "featured":true
      }],
      "resources":[{
        "id":"repository","title":"Repository","summary":"Kéire source repository.",
        "difficulty":"reference","category":"Development","type":"repository",
        "url":"https://github.com/hcfgod/KeireEngine","tags":[]
      }]
    })";
    auto result = ParseContentCatalog(document);
    REQUIRE(result);
    CHECK(result.Value().Learn.front().LocalPath == std::filesystem::path("Docs/GettingStarted.md"));
    CHECK(result.Value().Resources.front().HttpsUrl == "https://github.com/hcfgod/KeireEngine");

    constexpr auto insecure = R"({
      "schemaVersion":1,"locale":"en-US","learn":[],
      "resources":[{"id":"bad","title":"Bad","summary":"Bad URL","difficulty":"reference",
      "category":"Development","type":"externalLink","url":"http://example.com","tags":[]}]
    })";
    CHECK_FALSE(ParseContentCatalog(insecure));

    constexpr auto ambiguous = R"({
      "schemaVersion":1,"locale":"en-US","learn":[{"id":"bad","title":"Bad","summary":"Two targets",
      "difficulty":"beginner","category":"Docs","type":"guide","localPath":"Docs/a.md",
      "url":"https://example.com","tags":[]}],"resources":[]
    })";
    CHECK_FALSE(ParseContentCatalog(ambiguous));
}

TEST_CASE("License catalogs preserve package grouping and reject escaping source paths")
{
    constexpr auto document = R"({
      "schemaVersion":1,
      "licenses":[
        {"id":"keire-hub","displayName":"Kéire Hub","scope":"hub","sourcePath":"LICENSE.txt",
         "embeddedText":"MIT License"},
        {"id":"sdl","displayName":"SDL","scope":"editor","packageId":"editor.windows",
         "version":"3.4.10","sourcePath":"Licenses/SDL.txt"}
      ]
    })";
    auto catalog = ParseLicenseCatalog(document);
    REQUIRE(catalog);
    REQUIRE(catalog.Value().Licenses.size() == 2);
    CHECK(catalog.Value().Licenses[1].PackageId == "editor.windows");
    CHECK(catalog.Value().Licenses[1].Scope == LicenseScope::Editor);

    constexpr auto unsafe = R"({
      "schemaVersion":1,
      "licenses":[{"id":"bad","displayName":"Bad","scope":"hub","sourcePath":"../LICENSE.txt"}]
    })";
    CHECK_FALSE(ParseLicenseCatalog(unsafe));
}

TEST_CASE("Catalogs reject duplicate identities")
{
    constexpr auto duplicates = R"({
      "schemaVersion":1,"locale":"en-US",
      "learn":[{"id":"same","title":"One","summary":"One","difficulty":"beginner","category":"Docs",
      "type":"guide","localPath":"Docs/one.md","tags":[]}],
      "resources":[{"id":"same","title":"Two","summary":"Two","difficulty":"reference","category":"Docs",
      "type":"documentation","localPath":"Docs/two.md","tags":[]}]
    })";
    const auto result = ParseContentCatalog(duplicates);
    REQUIRE_FALSE(result);
    CHECK(result.Error().Code == HubErrorCode::DuplicateIdentifier);
}
