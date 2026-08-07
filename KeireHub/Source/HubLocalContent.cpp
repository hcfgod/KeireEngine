#include "KeireHub/HubLocalContent.h"

#include "Keire/BuildInfo.h"

#include "KeireHubRuntime/ContentCatalog.h"
#include "KeireHubRuntime/LicenseCatalog.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] std::string_view DifficultyLabel(const ContentDifficulty difficulty) noexcept
        {
            switch (difficulty)
            {
            case ContentDifficulty::Beginner:
                return "Beginner";
            case ContentDifficulty::Intermediate:
                return "Intermediate";
            case ContentDifficulty::Advanced:
                return "Advanced";
            case ContentDifficulty::Reference:
                return "Reference";
            }
            return "Reference";
        }

        void AppendContent(const std::vector<ResolvedContentItem>& source, std::vector<HubContentUiRecord>& destination)
        {
            destination.reserve(destination.size() + source.size());
            for (const auto& item : source)
            {
                destination.push_back({.Id = item.Metadata.Id,
                                       .Title = item.Metadata.Title,
                                       .Summary = item.Metadata.Summary,
                                       .Category = item.Metadata.Category,
                                       .Difficulty = std::string(DifficultyLabel(item.Metadata.Difficulty)),
                                       .LocalPath = item.LocalFile.value_or(std::filesystem::path{}),
                                       .Url = item.Metadata.HttpsUrl.value_or(std::string{}),
                                       .Featured = item.Metadata.Featured});
            }
        }

        [[nodiscard]] std::filesystem::path FindCatalog(const std::filesystem::path& distribution,
                                                        const std::filesystem::path& packaged,
                                                        const std::filesystem::path& repository)
        {
            const std::array candidates{distribution / packaged, distribution / repository,
                                        std::filesystem::current_path() / repository};
            const auto found = std::ranges::find_if(candidates, [](const auto& candidate)
                                                    { return std::filesystem::is_regular_file(candidate); });
            return found == candidates.end() ? candidates.front() : *found;
        }
    } // namespace

    std::filesystem::path ResolveHubDistributionRoot(const std::filesystem::path& executable)
    {
        std::array candidates{executable.parent_path().parent_path(), std::filesystem::current_path()};
        const auto found =
            std::ranges::find_if(candidates,
                                 [](const auto& candidate)
                                 {
                                     return std::filesystem::is_regular_file(candidate / "LICENSE.txt") &&
                                            (std::filesystem::is_directory(candidate / "docs") ||
                                             std::filesystem::is_directory(candidate / "Docs"));
                                 });
        return found == candidates.end() ? executable.parent_path().parent_path() : *found;
    }

    std::filesystem::path ResolveHubTemplatesRoot(const std::filesystem::path& executable)
    {
        const auto distribution = ResolveHubDistributionRoot(executable);
        const std::array candidates{distribution / "content" / "Templates",
                                    distribution / "KeireHubContent" / "Templates",
                                    std::filesystem::current_path() / "KeireHubContent" / "Templates"};
        const auto found =
            std::ranges::find_if(candidates, [](const auto& candidate)
                                 { return std::filesystem::is_regular_file(candidate / "catalog.json"); });
        return found == candidates.end() ? candidates.front() : *found;
    }

    std::filesystem::path ResolveHubContentCatalogPath(const std::filesystem::path& executable)
    {
        return FindCatalog(ResolveHubDistributionRoot(executable), "content/Content/en-US.json",
                           "KeireHubContent/Content/en-US.json");
    }

    std::filesystem::path ResolveHubLicenseCatalogPath(const std::filesystem::path& executable)
    {
        return FindCatalog(ResolveHubDistributionRoot(executable), "content/Licenses/catalog.json",
                           "KeireHubContent/Licenses/catalog.json");
    }

    HubLocalContentLoadReport PopulateLocalHubContent(const std::filesystem::path& executable,
                                                      HubProductSnapshot& snapshot)
    {
        HubLocalContentLoadReport report;
        const auto root = ResolveHubDistributionRoot(executable);
        snapshot.HubVersion = std::string(Keire::GetBuildInfo().Version);
        snapshot.Learn.clear();
        snapshot.Resources.clear();
        snapshot.Licenses.clear();
        snapshot.ContentMessage.clear();
        snapshot.LocalLicenseMessage.clear();

        ContentCatalog content(ResolveHubContentCatalogPath(executable), root);
        if (const auto status = content.Load(); status)
        {
            const auto contentSnapshot = content.Snapshot();
            AppendContent(contentSnapshot->Learn, snapshot.Learn);
            AppendContent(contentSnapshot->Resources, snapshot.Resources);
        }
        else
        {
            snapshot.ContentMessage = "Packaged learning and resource content could not be loaded.";
            auto failure = status.Error();
            failure.Message = snapshot.ContentMessage;
            failure.AffectedItem = "local-content-catalog";
            report.Failures.push_back(std::move(failure));
        }

        std::vector<LicenseCatalogSource> licenseSources;
        const auto catalogPath = ResolveHubLicenseCatalogPath(executable);
        if (std::filesystem::is_regular_file(catalogPath))
            licenseSources.push_back({.CatalogPath = catalogPath, .ContentRoot = root});
        else
        {
            snapshot.LocalLicenseMessage = "The packaged license catalog is unavailable.";
            report.Failures.push_back({.Code = HubErrorCode::NotFound,
                                       .Message = snapshot.LocalLicenseMessage,
                                       .AffectedItem = "local-license-catalog",
                                       .TechnicalDetails = "No catalog file exists at the resolved package path."});
        }
        LicenseCatalog licenses(root, std::move(licenseSources));
        if (const auto status = licenses.Load(); status)
        {
            snapshot.Licenses.reserve(licenses.Snapshot()->size());
            for (const auto& license : *licenses.Snapshot())
            {
                snapshot.Licenses.push_back({.Id = license.Id,
                                             .Name = license.DisplayName,
                                             .Group = license.Group,
                                             .Path = license.SourcePath,
                                             .Text = license.Text});
            }
        }
        else
        {
            snapshot.LocalLicenseMessage = "Packaged license files could not be loaded.";
            auto failure = status.Error();
            failure.Message = snapshot.LocalLicenseMessage;
            failure.AffectedItem = "local-license-catalog";
            report.Failures.push_back(std::move(failure));
        }
        return report;
    }
} // namespace KeireHub
