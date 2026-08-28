#include "KeireHub/HubProjectCreationUi.h"
#include "KeireHub/HubTemplateBrowser.h"
#include "KeireHubRuntime/TemplateManager.h"

#include <KeireHubTests/TestSupport.h>

#include <doctest/doctest.h>

#include <array>
#include <span>
#include <utility>

using namespace KeireHub;

namespace
{
    [[nodiscard]] HubTemplateUiRecord Template(std::string id, std::string name, std::string category,
                                               const bool featured = false)
    {
        return {.Id = std::move(id),
                .Version = "1.0.0",
                .Name = std::move(name),
                .Description = "A verified project starting point.",
                .Category = category,
                .Tags = {std::move(category), "Desktop"},
                .Thumbnail = {},
                .Screenshots = {},
                .CompatibleEditors = ">=1.0.0 <2.0.0",
                .ProjectSchema = 3,
                .PlatformTarget = "desktop",
                .EstimatedBytes = 1024,
                .StarterContent = {},
                .RequiredPackages = {},
                .RecommendedPackages = {},
                .LicenseReferences = {},
                .Featured = featured};
    }

    [[nodiscard]] HubTemplateEditorCompatibilityInput Editor(const std::string& version = "1.2.0")
    {
        return {.Version = version,
                .MinimumProjectSchema = 2,
                .MaximumProjectSchema = 3,
                .Healthy = true,
                .HasEntrypoint = true,
                .HasAssetToolEntrypoint = true};
    }

    void WriteValidPng(const std::filesystem::path& path)
    {
        constexpr std::array<unsigned char, 71> png{
            0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
            0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0xf4, 0x22, 0x7f, 0x8a, 0x00, 0x00, 0x00,
            0x0e, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x04, 0x01, 0x10, 0xf8, 0x03,
            0xfd, 0x4e, 0x95, 0xc1, 0x6f, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
        KeireHubTests::WriteText(
            path, std::string_view(reinterpret_cast<const char*>(png.data()), static_cast<std::size_t>(png.size())));
    }
} // namespace

TEST_CASE("Template UI translation preserves manifest detail and confines artwork")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto thumbnail = temporary.Path() / "Thumbnails" / "starter.png";
    WriteValidPng(thumbnail);

    HubTemplateManifest manifest;
    manifest.Id = "keire.3d-starter";
    manifest.Version = SemanticVersion::Parse("1.1.0").Value();
    manifest.DisplayName = "3D Starter";
    manifest.Description = "A compact 3D starting point.";
    manifest.Category = TemplateCategory::Core;
    manifest.Tags = {"3D", "Starter"};
    manifest.Thumbnail = "Thumbnails/starter.png";
    manifest.Screenshots = {"Screenshots/missing.png"};
    manifest.CompatibleEditors = VersionConstraint::Parse("^1.0.0").Value();
    manifest.ProjectSchema = 3;
    manifest.PlatformTarget = "desktop";
    manifest.EstimatedSizeBytes = 4096;
    manifest.StarterContent = {"Assets/Shaders/Starter.hlsl"};
    manifest.RequiredPackages = {{"renderer.forward", VersionConstraint::Parse("=1.0.0").Value()}};
    manifest.RecommendedPackages = {{"desktop.tools", VersionConstraint::Parse("*").Value()}};
    manifest.LicenseReferences = {"Licenses/MIT.txt"};
    manifest.Featured = true;

    const auto record = MakeHubTemplateUiRecord(manifest, temporary.Path());
    CHECK(record.Tags == manifest.Tags);
    CHECK(record.Thumbnail.Available);
    CHECK(record.Thumbnail.ResolvedPath == std::filesystem::weakly_canonical(thumbnail));
    CHECK(record.Thumbnail.Image.IsValid());
    REQUIRE(record.Screenshots.size() == 1);
    CHECK_FALSE(record.Screenshots.front().Available);
    CHECK(record.CompatibleEditors == ">=1.0.0 <2.0.0");
    CHECK(record.ProjectSchema == 3);
    CHECK(record.PlatformTarget == "desktop");
    CHECK(record.StarterContent == manifest.StarterContent);
    CHECK(record.RequiredPackages ==
          std::vector<HubTemplatePackageUiRecord>{{.PackageId = "renderer.forward", .VersionConstraint = "=1.0.0"}});
    CHECK(record.RecommendedPackages ==
          std::vector<HubTemplatePackageUiRecord>{{.PackageId = "desktop.tools", .VersionConstraint = "*"}});
    CHECK(record.LicenseReferences == manifest.LicenseReferences);
}

TEST_CASE("Checked-in template catalog thumbnails decode into Hub artwork")
{
    const auto templatesRoot = std::filesystem::current_path() / "KeireHubContent" / "Templates";
    TemplateManager manager(templatesRoot);
    REQUIRE(manager.Load());

    const auto templates = manager.Snapshot();
    REQUIRE(templates->size() == 3);
    for (const auto& item : *templates)
    {
        CAPTURE(item.Id);
        const auto record = MakeHubTemplateUiRecord(item, templatesRoot);
        CHECK(record.Thumbnail.Available);
        CHECK(record.Thumbnail.Image.IsValid());
        CHECK(record.Thumbnail.Image.Width == ProjectThumbnailImage::PixelWidth);
        CHECK(record.Thumbnail.Image.Height == ProjectThumbnailImage::PixelHeight);
    }
}

TEST_CASE("Template artwork resolution rejects files outside the template root")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto templatesRoot = temporary.Path() / "Templates";
    const auto outsideArtwork = temporary.Path() / "outside.svg";
    KeireHubTests::WriteText(outsideArtwork, "<svg/>");
    std::filesystem::create_directories(templatesRoot / "ArtworkDirectory");

    HubTemplateManifest manifest;
    manifest.Id = "keire.confined-artwork";
    manifest.Version = SemanticVersion::Parse("1.0.0").Value();
    manifest.DisplayName = "Confined Artwork";
    manifest.Description = "Artwork paths remain inside the template package.";
    manifest.Category = TemplateCategory::Core;
    manifest.Thumbnail = "../outside.svg";
    manifest.Screenshots = {outsideArtwork, "ArtworkDirectory", "missing.svg"};
    manifest.CompatibleEditors = VersionConstraint::Parse("*").Value();
    manifest.ProjectSchema = 3;
    manifest.PlatformTarget = "desktop";

    const auto record = MakeHubTemplateUiRecord(manifest, templatesRoot);
    CHECK(record.Thumbnail.DeclaredPath == std::filesystem::path("../outside.svg"));
    CHECK_FALSE(record.Thumbnail.Available);
    CHECK(record.Thumbnail.ResolvedPath.empty());
    REQUIRE(record.Screenshots.size() == 3);
    CHECK_FALSE(record.Screenshots[0].Available);
    CHECK(record.Screenshots[0].ResolvedPath.empty());
    CHECK_FALSE(record.Screenshots[1].Available);
    CHECK(record.Screenshots[1].ResolvedPath.empty());
    CHECK_FALSE(record.Screenshots[2].Available);
    CHECK(record.Screenshots[2].ResolvedPath.empty());
}

TEST_CASE("Project creation requests default to opening the created project and preserve opt out")
{
    HubCreateProjectRequest request;
    CHECK(request.OpenAfterCreation);

    request.OpenAfterCreation = false;
    CHECK_FALSE(request.OpenAfterCreation);
}

TEST_CASE("Template compatibility explains editor version schema platform and health failures")
{
    auto item = Template("keire.empty", "Empty", "Core");
    CHECK(EvaluateTemplateCompatibility(item, Editor()).Compatible());

    auto unavailable = Editor();
    unavailable.Healthy = false;
    CHECK(EvaluateTemplateCompatibility(item, unavailable).Status == HubTemplateCompatibilityStatus::EditorUnavailable);
    CHECK(EvaluateTemplateCompatibility(item, Editor("2.0.0")).Status ==
          HubTemplateCompatibilityStatus::EditorVersionUnsupported);

    auto oldSchema = Editor();
    oldSchema.MaximumProjectSchema = 2;
    CHECK(EvaluateTemplateCompatibility(item, oldSchema).Status ==
          HubTemplateCompatibilityStatus::ProjectSchemaUnsupported);
    CHECK(EvaluateTemplateCompatibility(item, Editor(), "mobile").Status ==
          HubTemplateCompatibilityStatus::PlatformUnsupported);

    const std::vector editors{Editor(), Editor("2.0.0"), unavailable};
    CHECK(CountCompatibleEditors(item, std::span<const HubTemplateEditorCompatibilityInput>(editors)) == 1);
}

TEST_CASE("Template browser searches metadata filters categories and orders featured entries first")
{
    const std::vector items{Template("keire.empty", "Empty", "Core"),
                            Template("keire.sandbox", "Kéire Sandbox", "Learning", true),
                            Template("keire.3d", "3D Starter", "Core", true)};

    auto result = QueryTemplateIndices(items, {.Search = "starter", .Category = HubTemplateCategoryFilter::All});
    REQUIRE(result.size() == 1);
    CHECK(items[result[0]].Id == "keire.3d");

    result = QueryTemplateIndices(items, {.Search = "desktop", .Category = HubTemplateCategoryFilter::Core});
    REQUIRE(result.size() == 2);

    result = QueryTemplateIndices(items, {.Search = {}, .Category = HubTemplateCategoryFilter::Learning});
    REQUIRE(result.size() == 1);
    CHECK(items[result.front()].Id == "keire.sandbox");

    result = QueryTemplateIndices(items, {});
    REQUIRE(result.size() == 3);
    CHECK(items[result[0]].Name == "3D Starter");
    CHECK(items[result[1]].Name == "Kéire Sandbox");
    CHECK(items[result[2]].Name == "Empty");
}
