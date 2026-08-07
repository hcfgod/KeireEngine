#include "TestSupport.h"

#include "KeireHubRuntime/HubProjectCatalog.h"
#include "KeireHubRuntime/ProjectMetadataScanner.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

using namespace KeireHub;

namespace
{
    constexpr std::string_view ProjectA = "11111111-1111-4111-8111-111111111111";
    constexpr std::string_view ProjectB = "22222222-2222-4222-8222-222222222222";
    constexpr std::string_view CreatedAt = "2026-08-06T12:34:56Z";
    constexpr std::uint64_t CreatedUnixSeconds = 1'786'019'696;

    void WriteProject(const std::filesystem::path& root, const std::string_view id = ProjectA,
                      const std::string_view name = "Project", const std::uint32_t schema = 3,
                      const bool includeTemplate = false)
    {
        nlohmann::json descriptor{{"schemaVersion", schema},
                                  {"id", std::string(id)},
                                  {"name", std::string(name)},
                                  {"createdWithEngineVersion", "0.1.0"},
                                  {"minimumEngineVersion", "0.1.0"},
                                  {"createdAt", std::string(CreatedAt)},
                                  {"lastSavedWithEngineVersion", "0.2.0"}};
        if (includeTemplate)
            descriptor["template"] = {{"id", "keire.sandbox"}, {"version", "1.2.0"}};
        KeireHubTests::WriteText(root / "ProjectSettings/Project.keireproject", descriptor.dump(2) + '\n');
    }

    [[nodiscard]] std::size_t WriteValidPng(const std::filesystem::path& path)
    {
        constexpr std::array<unsigned char, 71> png{
            0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
            0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0xf4, 0x22, 0x7f, 0x8a, 0x00, 0x00, 0x00,
            0x0e, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x04, 0x01, 0x10, 0xf8, 0x03,
            0xfd, 0x4e, 0x95, 0xc1, 0x6f, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
        KeireHubTests::WriteText(
            path, std::string_view(reinterpret_cast<const char*>(png.data()), static_cast<std::size_t>(png.size())));
        return png.size();
    }

    void WritePngSignatureOnly(const std::filesystem::path& path)
    {
        constexpr std::array<char, 8> signature{static_cast<char>(0x89), 'P', 'N', 'G', '\r', '\n',
                                                static_cast<char>(0x1a), '\n'};
        KeireHubTests::WriteText(path, std::string_view(signature.data(), signature.size()));
    }

    [[nodiscard]] ProjectThumbnail MakeThumbnail(const std::filesystem::path& path, std::string id,
                                                 const std::byte fill)
    {
        constexpr std::size_t pixelBytes =
            static_cast<std::size_t>(ProjectThumbnailImage::PixelWidth) * ProjectThumbnailImage::PixelHeight * 4U;
        return {.ProjectId = std::move(id),
                .Metadata = {.Path = path, .SizeBytes = 67, .ModifiedUnixSeconds = 100},
                .Image = {.Width = ProjectThumbnailImage::PixelWidth,
                          .Height = ProjectThumbnailImage::PixelHeight,
                          .RgbaPixels = std::make_shared<const std::vector<std::byte>>(pixelBytes, fill)}};
    }

    [[nodiscard]] std::uint64_t ProjectSize(const std::filesystem::path& root)
    {
        std::uint64_t result = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        {
            if (entry.is_regular_file())
                result += entry.file_size();
        }
        return result;
    }

    [[nodiscard]] ProjectMetadataScanRequest Request(const std::string_view id, const std::filesystem::path& root)
    {
        ProjectMetadataScanRequest request;
        request.Projects.push_back({std::string(id), root});
        return request;
    }
} // namespace

TEST_CASE("Project metadata scanning publishes bounded decoded thumbnail pixels without catalog mutation")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "Sandbox";
    WriteProject(root, ProjectA, "Sandbox", 3, true);
    KeireHubTests::WriteText(root / "Assets/Data.bin", "payload");
    const auto thumbnailBytes = WriteValidPng(root / "ProjectSettings/HubThumbnail.png");

    HubProjectCatalog catalog(temporary.Path() / "HubProjects.json");
    REQUIRE(catalog.Upsert({.Id = std::string(ProjectA), .Root = root, .Name = "Cached Sandbox"}));
    const auto catalogBefore = catalog.Snapshot();
    std::vector<ProjectMetadataScanPhase> phases;

    ProjectMetadataScanner scanner;
    auto future = scanner.ScanAsync(
        Request(ProjectA, root), {.ReportProgress = [&](const auto& progress) { phases.push_back(progress.Phase); }});
    auto scanned = future.get();
    REQUIRE(scanned);
    const auto snapshot = scanned.Value();
    REQUIRE(snapshot->State == ProjectMetadataScanState::Completed);
    REQUIRE(snapshot->CandidatesCompleted == 1);
    REQUIRE(snapshot->Results.size() == 1);
    const auto& result = snapshot->Results.front();
    CHECK(result.ProjectId == ProjectA);
    CHECK(result.Root == std::filesystem::weakly_canonical(root));
    CHECK(result.State == ProjectMetadataItemState::Ready);
    CHECK(result.DisplayName == "Sandbox");
    CHECK(result.Metadata.Status == HubProjectStatus::Ready);
    CHECK(result.Metadata.ProjectSchemaVersion == 3);
    CHECK(result.Metadata.CreatedUnixSeconds == CreatedUnixSeconds);
    CHECK(result.Metadata.CreatedWithEngineVersion == "0.1.0");
    CHECK(result.Metadata.LastSavedWithEngineVersion == "0.2.0");
    CHECK(result.Metadata.MinimumEngineVersion == "0.1.0");
    CHECK(result.Metadata.SizeBytes == ProjectSize(root));
    CHECK(result.Metadata.ModifiedUnixSeconds.has_value());
    CHECK(result.TemplateId == "keire.sandbox");
    CHECK(result.TemplateVersion == "1.2.0");
    REQUIRE(result.Thumbnail.has_value());
    CHECK(result.Thumbnail->Path == std::filesystem::weakly_canonical(root / "ProjectSettings/HubThumbnail.png"));
    CHECK(result.Thumbnail->SizeBytes == thumbnailBytes);
    REQUIRE(result.ThumbnailImage.has_value());
    CHECK(result.ThumbnailImage->IsValid());
    CHECK(result.ThumbnailImage->Width == ProjectThumbnailImage::PixelWidth);
    CHECK(result.ThumbnailImage->Height == ProjectThumbnailImage::PixelHeight);
    REQUIRE(result.ThumbnailImage->RgbaPixels);
    CHECK(result.ThumbnailImage->RgbaPixels->size() ==
          static_cast<std::size_t>(ProjectThumbnailImage::PixelWidth) * ProjectThumbnailImage::PixelHeight * 4U);
    CHECK_FALSE(result.ThumbnailError.has_value());
    CHECK_FALSE(result.Error.has_value());
    REQUIRE_FALSE(phases.empty());
    CHECK(phases.front() == ProjectMetadataScanPhase::Validating);
    CHECK(phases.back() == ProjectMetadataScanPhase::Completed);
    CHECK(catalog.Snapshot() == catalogBefore);
    CHECK_FALSE(catalog.Snapshot()->front().CachedMetadata.SizeBytes.has_value());
}

TEST_CASE("Project metadata scanning reports missing malformed and unsupported projects independently")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto malformed = temporary.Path() / "Malformed";
    const auto unsupported = temporary.Path() / "Future";
    KeireHubTests::WriteText(malformed / "ProjectSettings/Project.keireproject", "{not json");
    WriteProject(unsupported, ProjectB, "Future", 4);

    ProjectMetadataScanRequest request;
    request.Projects = {{std::string(ProjectA), temporary.Path() / "Missing"},
                        {std::string(ProjectA) + "-malformed", malformed},
                        {std::string(ProjectB), unsupported}};
    ProjectMetadataScanner scanner;
    auto scanned = scanner.ScanAsync(std::move(request)).get();
    REQUIRE(scanned);
    REQUIRE(scanned.Value()->State == ProjectMetadataScanState::Completed);
    REQUIRE(scanned.Value()->Results.size() == 3);

    const auto& missing = scanned.Value()->Results[0];
    CHECK(missing.State == ProjectMetadataItemState::Missing);
    CHECK(missing.Metadata.Status == HubProjectStatus::Missing);
    REQUIRE(missing.Error.has_value());
    CHECK(missing.Error->Code == HubErrorCode::NotFound);

    const auto& invalid = scanned.Value()->Results[1];
    CHECK(invalid.State == ProjectMetadataItemState::Invalid);
    CHECK(invalid.Metadata.Status == HubProjectStatus::Invalid);
    REQUIRE(invalid.Error.has_value());
    CHECK(invalid.Error->Code == HubErrorCode::ProjectValidationFailed);

    const auto& future = scanned.Value()->Results[2];
    CHECK(future.State == ProjectMetadataItemState::Ready);
    CHECK(future.Metadata.Status == HubProjectStatus::UnsupportedSchema);
    CHECK(future.Metadata.ProjectSchemaVersion == 4);
    CHECK(future.Metadata.SizeBytes.has_value());
}

TEST_CASE("Project metadata scanning preserves upgradeable project schema status")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "Upgradeable";
    WriteProject(root, ProjectA, "Upgradeable", 2);

    ProjectMetadataScanner scanner;
    auto scanned = scanner.ScanAsync(Request(ProjectA, root)).get();
    REQUIRE(scanned);
    REQUIRE(scanned.Value()->Results.size() == 1);
    CHECK(scanned.Value()->Results.front().State == ProjectMetadataItemState::Ready);
    CHECK(scanned.Value()->Results.front().Metadata.ProjectSchemaVersion == 2);
    CHECK(scanned.Value()->Results.front().Metadata.Status == HubProjectStatus::UpgradeAvailable);
}

TEST_CASE("Project metadata scanning reports interrupted upgrade recovery")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "Recovery";
    WriteProject(root);
    KeireHubTests::WriteText(root / "Library/ProjectUpgrades/Active/journal.json", "{}\n");

    ProjectMetadataScanner scanner;
    auto scanned = scanner.ScanAsync(Request(ProjectA, root)).get();
    REQUIRE(scanned);
    REQUIRE(scanned.Value()->Results.size() == 1);
    CHECK(scanned.Value()->Results.front().State == ProjectMetadataItemState::Ready);
    CHECK(scanned.Value()->Results.front().Metadata.Status == HubProjectStatus::RecoveryRequired);
}

TEST_CASE("Project metadata scanning keeps an invalid optional thumbnail separate from project health")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "Project";
    WriteProject(root);
    KeireHubTests::WriteText(root / "ProjectSettings/HubThumbnail.png", "not-a-png");

    ProjectMetadataScanner scanner;
    auto scanned = scanner.ScanAsync(Request(ProjectA, root)).get();
    REQUIRE(scanned);
    REQUIRE(scanned.Value()->Results.size() == 1);
    const auto& result = scanned.Value()->Results.front();
    CHECK(result.State == ProjectMetadataItemState::Ready);
    CHECK(result.Metadata.Status == HubProjectStatus::Ready);
    CHECK_FALSE(result.Thumbnail.has_value());
    CHECK_FALSE(result.ThumbnailImage.has_value());
    REQUIRE(result.ThumbnailError.has_value());
    CHECK(result.ThumbnailError->Code == HubErrorCode::ProjectValidationFailed);
}

TEST_CASE("Project metadata scanning rejects signature-only thumbnails without changing project health")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "Project";
    WriteProject(root);
    WritePngSignatureOnly(root / "ProjectSettings/HubThumbnail.png");

    ProjectMetadataScanner scanner;
    auto scanned = scanner.ScanAsync(Request(ProjectA, root)).get();
    REQUIRE(scanned);
    REQUIRE(scanned.Value()->Results.size() == 1);
    const auto& result = scanned.Value()->Results.front();
    CHECK(result.State == ProjectMetadataItemState::Ready);
    CHECK_FALSE(result.Thumbnail.has_value());
    CHECK_FALSE(result.ThumbnailImage.has_value());
    REQUIRE(result.ThumbnailError.has_value());
    CHECK(result.ThumbnailError->Code == HubErrorCode::ProjectValidationFailed);
}

TEST_CASE("Project metadata scanning rejects broad traversal duplicate and symbolic-link roots")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "Project";
    WriteProject(root);
    ProjectMetadataScanner scanner;

    auto relative = scanner.ScanAsync(Request(ProjectA, "RelativeProject")).get();
    REQUIRE_FALSE(relative);
    CHECK(relative.Error().Code == HubErrorCode::InvalidArgument);

    auto broad = scanner.ScanAsync(Request(ProjectA, std::filesystem::current_path().root_path())).get();
    REQUIRE_FALSE(broad);
    CHECK(broad.Error().Code == HubErrorCode::InvalidArgument);

    auto traversal = scanner.ScanAsync(Request(ProjectA, root / ".." / "Project")).get();
    REQUIRE_FALSE(traversal);
    CHECK(traversal.Error().Code == HubErrorCode::InvalidArgument);

    auto duplicateRequest = Request(ProjectA, root);
    duplicateRequest.Projects.push_back({std::string(ProjectA), temporary.Path() / "Other"});
    auto duplicate = scanner.ScanAsync(std::move(duplicateRequest)).get();
    REQUIRE_FALSE(duplicate);
    CHECK(duplicate.Error().Code == HubErrorCode::DuplicateIdentifier);

    std::error_code linkError;
    const auto link = temporary.Path() / "ProjectLink";
    std::filesystem::create_directory_symlink(root, link, linkError);
    if (linkError)
    {
        MESSAGE("Directory symlinks are unavailable in this test environment: " << linkError.message());
        return;
    }
    auto linked = scanner.ScanAsync(Request(ProjectA, link)).get();
    REQUIRE_FALSE(linked);
    CHECK(linked.Error().Code == HubErrorCode::InvalidArgument);
}

TEST_CASE("Project metadata scanning enforces entry byte and candidate limits")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "Project";
    WriteProject(root);
    KeireHubTests::WriteText(root / "Assets/Payload.bin", "payload");
    ProjectMetadataScanner scanner;

    auto entryRequest = Request(ProjectA, root);
    entryRequest.Limits.MaximumEntries = 1;
    auto entries = scanner.ScanAsync(std::move(entryRequest)).get();
    REQUIRE(entries);
    CHECK(entries.Value()->State == ProjectMetadataScanState::LimitReached);
    REQUIRE(entries.Value()->Results.size() == 1);
    CHECK(entries.Value()->Results.front().State == ProjectMetadataItemState::LimitExceeded);
    CHECK_FALSE(entries.Value()->Results.front().Metadata.SizeBytes.has_value());

    auto byteRequest = Request(ProjectA, root);
    byteRequest.Limits.MaximumBytes = 1;
    auto bytes = scanner.ScanAsync(std::move(byteRequest)).get();
    REQUIRE(bytes);
    CHECK(bytes.Value()->State == ProjectMetadataScanState::LimitReached);
    CHECK(bytes.Value()->BytesVisited <= 1);

    auto candidateRequest = Request(ProjectA, root);
    candidateRequest.Limits.MaximumCandidates = 1;
    candidateRequest.Projects.push_back({std::string(ProjectB), temporary.Path() / "Other"});
    auto candidates = scanner.ScanAsync(std::move(candidateRequest)).get();
    REQUIRE_FALSE(candidates);
    CHECK(candidates.Error().Code == HubErrorCode::InvalidArgument);
}

TEST_CASE("Project metadata scanning cancellation publishes only complete prior candidates")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto first = temporary.Path() / "First";
    const auto second = temporary.Path() / "Second";
    WriteProject(first, ProjectA, "First");
    WriteProject(second, ProjectB, "Second");
    ProjectMetadataScanRequest request;
    request.Projects = {{std::string(ProjectA), first}, {std::string(ProjectB), second}};
    std::atomic_bool cancel = false;
    std::vector<ProjectMetadataScanPhase> phases;

    ProjectMetadataScanner scanner;
    auto scanned =
        scanner
            .ScanAsync(std::move(request), {.IsCancelled = [&] { return cancel.load(std::memory_order_acquire); },
                                            .ReportProgress =
                                                [&](const ProjectMetadataScanProgress& progress)
                                            {
                                                phases.push_back(progress.Phase);
                                                if (progress.CandidatesCompleted == 1)
                                                    cancel.store(true, std::memory_order_release);
                                            }})
            .get();
    REQUIRE(scanned);
    CHECK(scanned.Value()->State == ProjectMetadataScanState::Cancelled);
    CHECK(scanned.Value()->CandidatesCompleted == 1);
    REQUIRE(scanned.Value()->Results.size() == 1);
    CHECK(scanned.Value()->Results.front().ProjectId == ProjectA);
    REQUIRE_FALSE(phases.empty());
    CHECK(phases.back() == ProjectMetadataScanPhase::Cancelled);
}

TEST_CASE("Project metadata scanner has one bounded worker slot")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "Project";
    WriteProject(root);
    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    std::promise<void> release;
    const auto releaseFuture = release.get_future().share();
    std::atomic_bool firstCheck = true;
    ProjectMetadataScanner scanner;

    auto active =
        scanner.ScanAsync(Request(ProjectA, root), {.IsCancelled = [&]
                                                    {
                                                        if (firstCheck.exchange(false, std::memory_order_acq_rel))
                                                        {
                                                            entered.set_value();
                                                            releaseFuture.wait();
                                                        }
                                                        return true;
                                                    }});
    const auto enteredStatus = enteredFuture.wait_for(std::chrono::seconds(5));
    if (enteredStatus != std::future_status::ready)
    {
        release.set_value();
        FAIL("The metadata worker did not start within the test timeout.");
        (void)active.get();
        return;
    }

    auto concurrent = scanner.ScanAsync(Request(ProjectA, root)).get();
    REQUIRE_FALSE(concurrent);
    CHECK(concurrent.Error().Code == HubErrorCode::InvalidTransition);
    release.set_value();
    auto cancelled = active.get();
    REQUIRE(cancelled);
    CHECK(cancelled.Value()->State == ProjectMetadataScanState::Cancelled);
}

TEST_CASE("Thumbnail metadata cache uses deterministic least-recently-used eviction")
{
    KeireHubTests::TemporaryDirectory temporary;
    ProjectThumbnailMetadataCache cache(2);
    const ProjectThumbnailMetadata first{
        .Path = temporary.Path() / "First.png", .SizeBytes = 10, .ModifiedUnixSeconds = 100};
    const ProjectThumbnailMetadata second{
        .Path = temporary.Path() / "Second.png", .SizeBytes = 20, .ModifiedUnixSeconds = 200};
    const ProjectThumbnailMetadata third{
        .Path = temporary.Path() / "Third.png", .SizeBytes = 30, .ModifiedUnixSeconds = 300};

    cache.Store("first", first);
    cache.Store("second", second);
    CHECK(cache.Find("first") == first);
    cache.Store("third", third);
    CHECK_FALSE(cache.Find("second").has_value());
    CHECK(cache.Find("first") == first);
    CHECK(cache.Find("third") == third);
    CHECK(cache.Size() == 2);
    CHECK(cache.Capacity() == 2);

    cache.Erase("first");
    CHECK(cache.Size() == 1);
    cache.Clear();
    CHECK(cache.Size() == 0);
    CHECK_THROWS_AS(ProjectThumbnailMetadataCache{0}, std::invalid_argument);
    CHECK_THROWS_AS(ProjectThumbnailMetadataCache{ProjectThumbnailMetadataCache::MaximumCapacity + 1},
                    std::invalid_argument);
}

TEST_CASE("Decoded thumbnail cache retains immutable snapshots and evicts least-recently-used pixels")
{
    KeireHubTests::TemporaryDirectory temporary;
    ProjectThumbnailCache cache(2);
    const auto first = MakeThumbnail(temporary.Path() / "First.png", "first", std::byte{0x11});
    const auto second = MakeThumbnail(temporary.Path() / "Second.png", "second", std::byte{0x22});
    const auto third = MakeThumbnail(temporary.Path() / "Third.png", "third", std::byte{0x33});

    cache.Store(first);
    cache.Store(second);
    const auto immutable = cache.Snapshot();
    REQUIRE(immutable);
    REQUIRE(immutable->size() == 2);
    CHECK(cache.Find("first")->Image.RgbaPixels == first.Image.RgbaPixels);
    cache.Store(third);
    CHECK_FALSE(cache.Find("second").has_value());
    CHECK(cache.Find("first")->Image.IsValid());
    CHECK(cache.Find("third")->Image.IsValid());
    CHECK(immutable->size() == 2);
    CHECK(std::ranges::any_of(*immutable, [](const auto& thumbnail) { return thumbnail.ProjectId == "second"; }));
    CHECK(cache.Size() == 2);
    CHECK(cache.Capacity() == 2);

    cache.Erase("first");
    CHECK(cache.Size() == 1);
    cache.Clear();
    CHECK(cache.Size() == 0);
    CHECK_THROWS_AS(ProjectThumbnailCache{0}, std::invalid_argument);
    CHECK_THROWS_AS(ProjectThumbnailCache{ProjectThumbnailCache::MaximumCapacity + 1}, std::invalid_argument);

    auto invalid = first;
    invalid.Image.RgbaPixels.reset();
    CHECK_THROWS_AS(cache.Store(std::move(invalid)), std::invalid_argument);
}
