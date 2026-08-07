#include "KeireHub/HubEditorInstallWorkflow.h"

#include "KeireHubRuntime/HubWorkerProtocol.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <ranges>
#include <string_view>
#include <system_error>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] std::optional<std::uint64_t> AvailableBytes(std::filesystem::path destination)
        {
            std::error_code error;
            destination = destination.lexically_normal();
            while (!destination.empty())
            {
                if (std::filesystem::exists(destination, error) && !error)
                {
                    const auto space = std::filesystem::space(destination, error);
                    if (!error)
                        return space.available;
                    return std::nullopt;
                }
                error.clear();
                const auto parent = destination.parent_path();
                if (parent == destination)
                    break;
                destination = parent;
            }
            return std::nullopt;
        }

        [[nodiscard]] HubEditorInstallUiRequest RequestFromPlan(const EditorInstallPlan& plan)
        {
            HubEditorInstallUiRequest result{.Destination = plan.Destination,
                                             .EditorPackageId = plan.EditorPackageId,
                                             .EditorVersion = plan.EditorVersion};
            result.Components.reserve(plan.SelectedComponents.size());
            for (const auto& component : plan.SelectedComponents)
                result.Components.push_back({component.PackageId, component.Version});
            return result;
        }
    } // namespace

    std::vector<HubAvailableEditorUiRecord>
    BuildHubAvailableEditorUiRecords(const EditorInstallCatalogSnapshot& snapshot)
    {
        std::vector<HubAvailableEditorUiRecord> result;
        result.reserve(snapshot.AvailableEditors.size());
        for (const auto& editor : snapshot.AvailableEditors)
        {
            HubAvailableEditorUiRecord record{
                .PackageId = editor.PackageId,
                .Version = editor.Version,
                .DisplayName = editor.DisplayName,
                .Channel = editor.Channel,
                .Platform = editor.Platform,
                .Architecture = editor.Architecture,
                .DownloadBytes = editor.DownloadSizeBytes,
                .InstalledBytes = editor.InstalledSizeBytes,
                .InstalledInstallationIds = editor.InstalledInstallationIds,
                .AvailabilityMessage = editor.AvailabilityError ? editor.AvailabilityError->Message : std::string{}};
            record.Components.reserve(editor.Components.size());
            for (const auto& component : editor.Components)
            {
                record.Components.push_back({.PackageId = component.PackageId,
                                             .Version = component.Version,
                                             .DisplayName = component.DisplayName,
                                             .DownloadBytes = component.DownloadSizeBytes,
                                             .InstalledBytes = component.InstalledSizeBytes,
                                             .Required = component.RequiredByEditor,
                                             .RequiredBy = component.RequiredByPackageIds});
            }
            result.push_back(std::move(record));
        }
        return result;
    }

    HubEditorInstallPreviewUiRecord BuildHubEditorInstallPreviewUiRecord(const EditorInstallPlan& plan)
    {
        HubEditorInstallPreviewUiRecord result{.Request = RequestFromPlan(plan),
                                               .InstallationId = plan.InstallationId,
                                               .Channel = plan.Channel,
                                               .DownloadBytes = plan.DownloadSizeBytes,
                                               .RequiredDiskBytes = plan.RequiredDiskBytes};
        result.Steps.reserve(plan.Steps.size());
        for (const auto& step : plan.Steps)
        {
            result.Steps.push_back({.PackageId = step.Manifest.Id,
                                    .DisplayName = step.Manifest.DisplayName,
                                    .Version = step.Manifest.Version.ToString(),
                                    .DownloadBytes = step.Manifest.ArtifactSizeBytes,
                                    .ExplicitlySelected = step.ExplicitlySelected,
                                    .RequiredBy = step.RequiredByPackageIds});
        }
        return result;
    }

    HubEditorInstallWorkflow::HubEditorInstallWorkflow(EditorInstallationRegistry& registry, std::string hostPlatform,
                                                       std::string hostArchitecture)
        : m_Registry(registry), m_HostPlatform(std::move(hostPlatform)),
          m_HostArchitecture(std::move(hostArchitecture)),
          m_AvailableEditors(std::make_shared<const std::vector<HubAvailableEditorUiRecord>>())
    {
    }

    HubStatus HubEditorInstallWorkflow::Refresh(const HubDistributionWorkflowSnapshot& distribution,
                                                const HubSettings& settings)
    {
        const HubEditorInstallEndpointContext endpoint{.ServiceBaseUrl = distribution.ServiceBaseUrl,
                                                       .AllowInsecureLoopbackDevelopment =
                                                           distribution.AllowInsecureLoopbackDevelopment};
        const auto failureCode = distribution.Failure ? std::optional(distribution.Failure->Code) : std::nullopt;
        const auto failureMessage = distribution.Failure ? distribution.Failure->Message : std::string{};
        const auto installations = m_Registry.Snapshot();
        const bool unchanged =
            m_HasRefreshKey && distribution.Catalogs == m_LastDistribution && distribution.Refreshing == m_Refreshing &&
            installations == m_LastInstallations && settings.EnablePreReleaseChannel == m_EnablePreRelease &&
            settings.EnableNightlyChannel == m_EnableNightly && endpoint.ServiceBaseUrl == m_Endpoint.ServiceBaseUrl &&
            endpoint.AllowInsecureLoopbackDevelopment == m_Endpoint.AllowInsecureLoopbackDevelopment &&
            failureCode == m_LastDistributionFailureCode && failureMessage == m_LastDistributionFailureMessage;
        if (unchanged)
            return HubStatus::Success();

        m_HasRefreshKey = true;
        m_LastDistribution = distribution.Catalogs;
        m_LastInstallations = installations;
        m_EnablePreRelease = settings.EnablePreReleaseChannel;
        m_EnableNightly = settings.EnableNightlyChannel;
        m_Refreshing = distribution.Refreshing;
        m_Endpoint = endpoint;
        m_LastDistributionFailureCode = failureCode;
        m_LastDistributionFailureMessage = failureMessage;
        m_CatalogFailure.reset();
        m_PreviewFailure.reset();
        m_Preview.reset();
        m_PopulatedChannels.clear();
        m_AvailableEditors = std::make_shared<const std::vector<HubAvailableEditorUiRecord>>();

        if (!distribution.Catalogs)
        {
            m_Catalog.reset();
            m_CatalogFailure = distribution.Failure;
            return HubStatus::Success();
        }

        auto catalog = std::make_unique<EditorInstallCatalog>(
            m_Registry, EditorInstallCatalogSpecification{.HostPlatform = m_HostPlatform,
                                                          .HostArchitecture = m_HostArchitecture,
                                                          .EnablePreReleaseChannel = m_EnablePreRelease,
                                                          .EnableNightlyChannel = m_EnableNightly});
        if (const auto status = catalog->Refresh(distribution.Catalogs); !status)
        {
            m_Catalog.reset();
            m_CatalogFailure = status.Error();
            return status;
        }
        const auto snapshot = catalog->Snapshot();
        m_PopulatedChannels = snapshot->PopulatedChannels;
        m_AvailableEditors = std::make_shared<const std::vector<HubAvailableEditorUiRecord>>(
            BuildHubAvailableEditorUiRecords(*snapshot));
        if (m_AvailableEditors->empty() && distribution.Failure)
            m_CatalogFailure = distribution.Failure;
        m_Catalog = std::move(catalog);
        return HubStatus::Success();
    }

    HubResult<EditorInstallPlan> HubEditorInstallWorkflow::PreviewInstall(const HubEditorInstallUiRequest& request)
    {
        if (!m_Catalog)
        {
            auto error = HubError{.Code = HubErrorCode::NotFound,
                                  .Message = "No verified editor catalog is currently available.",
                                  .Retryable = true,
                                  .AffectedItem = request.EditorPackageId};
            m_Preview.reset();
            m_PreviewFailure = error;
            return HubResult<EditorInstallPlan>::Failure(std::move(error));
        }

        std::error_code destinationError;
        const auto destinationStatus = std::filesystem::symlink_status(request.Destination, destinationError);
        if (destinationError && destinationError.default_error_condition() != std::errc::no_such_file_or_directory)
        {
            auto error = HubError{.Code = HubErrorCode::IoRead,
                                  .Message = "The selected editor destination could not be inspected.",
                                  .AffectedItem = request.EditorPackageId,
                                  .TechnicalDetails = destinationError.message()};
            m_Preview.reset();
            m_PreviewFailure = error;
            return HubResult<EditorInstallPlan>::Failure(std::move(error));
        }
        if (!destinationError && destinationStatus.type() != std::filesystem::file_type::not_found)
        {
            auto error = HubError{.Code = HubErrorCode::DestinationConflict,
                                  .Message = "The selected editor destination already exists.",
                                  .AffectedItem = request.EditorPackageId};
            m_Preview.reset();
            m_PreviewFailure = error;
            return HubResult<EditorInstallPlan>::Failure(std::move(error));
        }

        const auto installationId =
            m_Preview && m_Preview->Request == request ? m_Preview->InstallationId : NextInstallationId();
        EditorInstallPreviewRequest preview{.InstallationId = installationId,
                                            .Destination = request.Destination,
                                            .EditorPackageId = request.EditorPackageId,
                                            .EditorVersion = request.EditorVersion,
                                            .AvailableDiskBytes = AvailableBytes(request.Destination.parent_path())};
        preview.Components.reserve(request.Components.size());
        for (const auto& component : request.Components)
            preview.Components.push_back({component.PackageId, component.Version});
        auto plan = m_Catalog->PreviewInstall(preview);
        if (!plan)
        {
            m_Preview.reset();
            m_PreviewFailure = plan.Error();
            return plan;
        }
        if (plan.Value().Steps.empty() || plan.Value().Steps.size() > MaximumHubWorkerInstallPackageSteps)
        {
            auto error = HubError{.Code = HubErrorCode::PackageManifestInvalid,
                                  .Message = "The editor install requires too many package steps.",
                                  .AffectedItem = request.EditorPackageId};
            m_Preview.reset();
            m_PreviewFailure = error;
            return HubResult<EditorInstallPlan>::Failure(std::move(error));
        }
        m_Preview =
            std::make_shared<const HubEditorInstallPreviewUiRecord>(BuildHubEditorInstallPreviewUiRecord(plan.Value()));
        m_PreviewFailure.reset();
        return plan;
    }

    HubResult<EditorRepairPlan> HubEditorInstallWorkflow::PreviewRepair(const EditorManagedOperationPlan& authorization)
    {
        if (!m_Catalog || authorization.Operation != EditorManagedOperation::Repair)
        {
            return HubResult<EditorRepairPlan>::Failure(
                {.Code = m_Catalog ? HubErrorCode::InvalidArgument : HubErrorCode::NotFound,
                 .Message = m_Catalog ? "The managed editor repair authorization is invalid."
                                      : "No verified editor catalog is currently available.",
                 .Retryable = !m_Catalog,
                 .AffectedItem = authorization.InstallationId});
        }
        auto plan = m_Catalog->PreviewRepair({.InstallationId = authorization.InstallationId,
                                              .Destination = authorization.Root,
                                              .ManifestFingerprint = authorization.ManifestFingerprint,
                                              .PackageTreeIdentity = authorization.PackageTreeIdentity,
                                              .PackageReceiptSha256 = authorization.PackageReceiptSha256,
                                              .MarkerNonce = authorization.MarkerNonce,
                                              .AvailableDiskBytes = AvailableBytes(authorization.Root.parent_path())});
        if (plan)
            plan.Value().EditorEntrypoint = authorization.EditorEntrypoint;
        return plan;
    }

    void HubEditorInstallWorkflow::ClearPreview() noexcept
    {
        m_Preview.reset();
        m_PreviewFailure.reset();
    }

    void HubEditorInstallWorkflow::ApplySnapshot(HubProductSnapshot& product) const
    {
        product.PopulatedEditorChannels = m_PopulatedChannels;
        product.AvailableEditors = m_AvailableEditors;
        product.EditorInstallPreview = m_Preview;
        product.EditorCatalogRefreshing = m_Refreshing;
        product.EditorCatalogMessage = m_CatalogFailure ? m_CatalogFailure->Message : std::string{};
        product.EditorInstallPreviewMessage = m_PreviewFailure ? m_PreviewFailure->Message : std::string{};
    }

    std::string HubEditorInstallWorkflow::NextInstallationId()
    {
        const auto installed = m_Registry.Snapshot();
        const auto epoch = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count());
        while (m_NextInstallationSuffix != 0)
        {
            auto candidate =
                "managed-editor-" + std::to_string(epoch) + '-' + std::to_string(m_NextInstallationSuffix++);
            if (std::ranges::find(*installed, candidate, &EditorInstallation::Id) == installed->end())
                return candidate;
        }
        return {};
    }
} // namespace KeireHub
