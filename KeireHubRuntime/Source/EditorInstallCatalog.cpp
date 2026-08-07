#include "KeireHubRuntime/EditorInstallCatalog.h"

#include "DistributionEncoding.h"
#include "Persistence.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <string_view>
#include <utility>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumCatalogChannels = 3;
        constexpr std::size_t MaximumPackages = 4096;
        constexpr std::size_t MaximumEditors = 512;
        constexpr std::size_t MaximumComponentsPerEditor = 2048;
        constexpr std::size_t MaximumInstalledEditors = 128;
        constexpr std::size_t MaximumSelectedComponents = 64;

        using PackageIdentity = std::pair<std::string, SemanticVersion>;
        using RequiredByMap = std::map<PackageIdentity, std::vector<std::string>>;

        [[nodiscard]] HubError InstallError(const HubErrorCode code, std::string message, std::string item = {},
                                            std::string details = {})
        {
            return {.Code = code,
                    .Message = std::move(message),
                    .AffectedItem = std::move(item),
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] bool IsHostPlatform(const std::string_view value) noexcept
        {
            return value == "windows" || value == "linux" || value == "macos";
        }

        [[nodiscard]] bool IsHostArchitecture(const std::string_view value) noexcept
        {
            return value == "x86_64" || value == "arm64";
        }

        [[nodiscard]] int ChannelRank(const std::string_view channel) noexcept
        {
            if (channel == "stable")
                return 0;
            if (channel == "preview")
                return 1;
            if (channel == "nightly")
                return 2;
            return -1;
        }

        [[nodiscard]] bool ChannelEnabled(const std::string_view channel,
                                          const EditorInstallCatalogSpecification& specification) noexcept
        {
            if (channel == "stable")
                return true;
            if (channel == "preview")
                return specification.EnablePreReleaseChannel;
            if (channel == "nightly")
                return specification.EnableNightlyChannel;
            return false;
        }

        [[nodiscard]] bool IsPublishedSource(const DistributionCatalogSourceState state) noexcept
        {
            return state == DistributionCatalogSourceState::Online ||
                   state == DistributionCatalogSourceState::LastKnownGood ||
                   state == DistributionCatalogSourceState::OfflineLastKnownGood;
        }

        [[nodiscard]] bool HostMatches(const PackageManifest& package,
                                       const EditorInstallCatalogSpecification& specification) noexcept
        {
            return (package.Platform == "any" || package.Platform == specification.HostPlatform) &&
                   (package.Architecture == "any" || package.Architecture == specification.HostArchitecture);
        }

        [[nodiscard]] bool IsComponent(const PackageKind kind) noexcept
        {
            return kind == PackageKind::BuildSupport || kind == PackageKind::Toolchain;
        }

        [[nodiscard]] bool IsComponent(const PackageManifest& package) noexcept { return IsComponent(package.Kind); }

        [[nodiscard]] PackageIdentity IdentityOf(const PackageManifest& package)
        {
            return {package.Id, package.Version};
        }

        [[nodiscard]] HubResult<VersionConstraint> ExactVersion(const SemanticVersion& version)
        {
            return VersionConstraint::Parse('=' + version.ToString());
        }

        [[nodiscard]] RequiredByMap RequiredBy(const std::vector<PackageManifest>& packages)
        {
            RequiredByMap result;
            for (const auto& parent : packages)
            {
                for (const auto& dependency : parent.Dependencies)
                {
                    const auto child = std::ranges::find_if(packages,
                                                            [&](const PackageManifest& candidate)
                                                            {
                                                                return candidate.Id == dependency.PackageId &&
                                                                       dependency.Versions.Matches(candidate.Version);
                                                            });
                    if (child == packages.end())
                        continue;
                    result[IdentityOf(*child)].push_back(parent.Id);
                }
            }
            for (auto& [identity, parents] : result)
            {
                static_cast<void>(identity);
                std::ranges::sort(parents);
                parents.erase(std::unique(parents.begin(), parents.end()), parents.end());
            }
            return result;
        }

        [[nodiscard]] HubResult<SemanticVersion> RequestedVersion(const std::string_view value,
                                                                  const std::string& affectedItem)
        {
            auto parsed = SemanticVersion::Parse(value);
            if (!parsed)
            {
                return HubResult<SemanticVersion>::Failure(InstallError(
                    HubErrorCode::InvalidArgument, "An install selection uses an invalid version.", affectedItem));
            }
            return parsed;
        }

        [[nodiscard]] bool IsValidDestination(const std::filesystem::path& destination)
        {
            if (destination.empty() || !destination.is_absolute() || destination == destination.root_path() ||
                destination.filename().empty())
            {
                return false;
            }
            try
            {
                if (destination.generic_u8string().size() > 4096U)
                    return false;
            }
            catch (...)
            {
                return false;
            }
            return std::ranges::none_of(destination, [](const std::filesystem::path& component)
                                        { return component == "." || component == ".."; });
        }

        [[nodiscard]] std::string PathKey(const std::filesystem::path& path, const bool windows)
        {
            auto key = Detail::PathToUtf8(path.lexically_normal());
            if (windows)
            {
                std::ranges::transform(key, key.begin(), [](const unsigned char character)
                                       { return static_cast<char>(std::tolower(character)); });
            }
            return key;
        }

        [[nodiscard]] bool ComponentLess(const AvailableEditorComponent& left, const AvailableEditorComponent& right)
        {
            if (left.Kind != right.Kind)
                return left.Kind < right.Kind;
            if (left.DisplayName != right.DisplayName)
                return left.DisplayName < right.DisplayName;
            if (left.PackageId != right.PackageId)
                return left.PackageId < right.PackageId;
            return SemanticVersion::Parse(left.Version).Value() > SemanticVersion::Parse(right.Version).Value();
        }

        [[nodiscard]] bool SameDependencies(const std::vector<PackageDependency>& left,
                                            const std::vector<PackageDependency>& right)
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index = 0; index < left.size(); ++index)
            {
                if (left[index].PackageId != right[index].PackageId ||
                    left[index].Versions.ToString() != right[index].Versions.ToString())
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool SameFiles(const std::vector<PackageFile>& left, const std::vector<PackageFile>& right)
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index = 0; index < left.size(); ++index)
            {
                if (left[index].Path.lexically_normal() != right[index].Path.lexically_normal() ||
                    left[index].SizeBytes != right[index].SizeBytes || left[index].Sha256 != right[index].Sha256 ||
                    left[index].Mode != right[index].Mode)
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool SamePublishedPackage(const PackageManifest& package, const InstalledPackageRecord& installed)
        {
            return package.Id == installed.Id && package.Version == installed.Version &&
                   package.Kind == installed.Kind && package.ArtifactSizeBytes == installed.ArtifactSizeBytes &&
                   package.ArtifactSha256 == installed.ArtifactSha256 &&
                   package.InstalledSizeBytes == installed.InstalledSizeBytes &&
                   SameDependencies(package.Dependencies, installed.Dependencies) &&
                   SameFiles(package.Files, installed.Files) &&
                   package.LicenseReferences == installed.LicenseReferences;
        }
    } // namespace

    EditorInstallCatalog::EditorInstallCatalog(EditorInstallationRegistry& registry,
                                               EditorInstallCatalogSpecification specification)
        : m_Registry(registry), m_Specification(std::move(specification)),
          m_Snapshot(std::make_shared<const EditorInstallCatalogSnapshot>(
              EditorInstallCatalogSnapshot{.InstalledEditors = registry.Snapshot()}))
    {
    }

    HubStatus EditorInstallCatalog::Refresh(std::shared_ptr<const DistributionCatalogSnapshot> distribution)
    {
        if (!distribution || !IsHostPlatform(m_Specification.HostPlatform) ||
            !IsHostArchitecture(m_Specification.HostArchitecture) ||
            distribution->PackageCatalogs.size() > MaximumCatalogChannels)
        {
            return HubStatus::Failure(InstallError(HubErrorCode::InvalidArgument,
                                                   "The editor catalog refresh request is invalid.", "installs"));
        }

        const auto installed = m_Registry.Snapshot();
        if (!installed || installed->size() > MaximumInstalledEditors)
        {
            return HubStatus::Failure(InstallError(
                HubErrorCode::InvalidData, "The editor registry exceeds the Installs catalog limit.", "installations"));
        }

        std::vector<IndexedPackage> packages;
        std::set<std::string, std::less<>> catalogChannels;
        std::set<PackageIdentity> packageIdentities;
        std::map<std::string, PackageKind, std::less<>> packageKinds;
        if (distribution->OnlineDiscoveryEnabled)
        {
            for (const auto& source : distribution->PackageCatalogs)
            {
                if (ChannelRank(source.Channel) < 0 || !catalogChannels.insert(source.Channel).second)
                {
                    return HubStatus::Failure(InstallError(
                        HubErrorCode::InvalidData,
                        "The distribution snapshot contains an invalid or duplicate release channel.", source.Channel));
                }
                if (!ChannelEnabled(source.Channel, m_Specification) || !source.Catalog)
                    continue;
                const auto& catalog = *source.Catalog;
                const auto catalogExpiry = Detail::ParseUtcInstant(catalog.Identity.ExpiresAt);
                const auto sourceExpiry = Detail::ParseUtcInstant(source.Status.ExpiresAt);
                if (catalog.SchemaVersion != DistributionPackageCatalog::CurrentSchemaVersion ||
                    !IsPublishedSource(source.Status.State) || catalog.Identity.Channel != source.Channel ||
                    catalog.Identity.Platform != m_Specification.HostPlatform ||
                    catalog.Identity.Architecture != m_Specification.HostArchitecture ||
                    !Detail::IsDistributionKeyId(catalog.Identity.KeyId) || catalog.Identity.Sequence == 0U ||
                    catalog.Identity.Sequence > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
                    !catalogExpiry || !sourceExpiry || *catalogExpiry != *sourceExpiry ||
                    source.Status.Sequence != catalog.Identity.Sequence ||
                    source.Status.KeyId != catalog.Identity.KeyId || catalog.Packages.size() > MaximumPackages ||
                    packages.size() > MaximumPackages - catalog.Packages.size())
                {
                    return HubStatus::Failure(InstallError(
                        HubErrorCode::CatalogIdentityMismatch,
                        "A distribution catalog does not match the Installs host identity.", source.Channel));
                }
                for (const auto& manifest : catalog.Packages)
                {
                    if (const auto status = ValidatePackageManifest(manifest); !status)
                        return status;
                    if (manifest.Channel != source.Channel)
                    {
                        return HubStatus::Failure(
                            InstallError(HubErrorCode::PackageManifestInvalid,
                                         "A package is published under the wrong Installs channel.", manifest.Id));
                    }
                    if (!HostMatches(manifest, m_Specification))
                    {
                        return HubStatus::Failure(InstallError(
                            HubErrorCode::PackageHostIncompatible,
                            "A package in the host catalog is incompatible with this computer.", manifest.Id));
                    }
                    if (!packageIdentities.insert(IdentityOf(manifest)).second)
                    {
                        return HubStatus::Failure(InstallError(
                            HubErrorCode::DuplicateIdentifier,
                            "The enabled Installs catalogs contain a duplicate package version.", manifest.Id));
                    }
                    const auto [kind, inserted] = packageKinds.emplace(manifest.Id, manifest.Kind);
                    if (!inserted && kind->second != manifest.Kind)
                    {
                        return HubStatus::Failure(
                            InstallError(HubErrorCode::PackageManifestInvalid,
                                         "A package identity changes kind between published versions.", manifest.Id));
                    }
                    packages.push_back({.Manifest = manifest,
                                        .CatalogKeyId = catalog.Identity.KeyId,
                                        .CatalogSequence = catalog.Identity.Sequence});
                }
            }
        }

        auto next = std::make_shared<EditorInstallCatalogSnapshot>();
        next->InstalledEditors = installed;
        const PackageResolver resolver;
        for (const auto& indexedEditor : packages)
        {
            const auto& editor = indexedEditor.Manifest;
            if (editor.Kind != PackageKind::Editor)
                continue;
            if (next->AvailableEditors.size() >= MaximumEditors)
            {
                return HubStatus::Failure(InstallError(
                    HubErrorCode::InvalidData, "The Installs catalog contains too many editor versions.", "editors"));
            }

            AvailableEditorVersion record{.PackageId = editor.Id,
                                          .Version = editor.Version.ToString(),
                                          .DisplayName = editor.DisplayName,
                                          .Channel = editor.Channel,
                                          .Platform = editor.Platform,
                                          .Architecture = editor.Architecture,
                                          .DownloadSizeBytes = editor.ArtifactSizeBytes,
                                          .InstalledSizeBytes = editor.InstalledSizeBytes};
            for (const auto& installation : *installed)
            {
                const auto installedVersion = SemanticVersion::Parse(installation.Version);
                if (installedVersion && installedVersion.Value() == editor.Version &&
                    installation.Channel == record.Channel && installation.Platform == m_Specification.HostPlatform &&
                    installation.Architecture == m_Specification.HostArchitecture)
                {
                    record.InstalledInstallationIds.push_back(installation.Id);
                }
            }
            std::ranges::sort(record.InstalledInstallationIds);

            auto exactEditor = ExactVersion(editor.Version);
            if (!exactEditor)
                return HubStatus::Failure(exactEditor.Error());
            std::vector<PackageManifest> availableInChannel;
            for (const auto& package : packages)
            {
                if (package.Manifest.Channel == editor.Channel)
                    availableInChannel.push_back(package.Manifest);
            }
            auto required = resolver.Resolve(availableInChannel, {{editor.Id, std::move(exactEditor).Value()}},
                                             {.Platform = m_Specification.HostPlatform,
                                              .Architecture = m_Specification.HostArchitecture,
                                              .EngineVersion = editor.Version});
            RequiredByMap requiredBy;
            std::set<PackageIdentity> requiredPackages;
            if (!required)
            {
                record.AvailabilityError = required.Error();
            }
            else
            {
                requiredBy = RequiredBy(required.Value().InstallOrder);
                for (const auto& package : required.Value().InstallOrder)
                    requiredPackages.insert(IdentityOf(package));
            }

            for (const auto& indexedComponent : packages)
            {
                const auto& component = indexedComponent.Manifest;
                if (!IsComponent(component) || component.Channel != editor.Channel ||
                    (component.EngineCompatibility && !component.EngineCompatibility->Matches(editor.Version)))
                {
                    continue;
                }
                if (record.Components.size() >= MaximumComponentsPerEditor)
                {
                    return HubStatus::Failure(
                        InstallError(HubErrorCode::InvalidData,
                                     "An editor version has too many compatible component packages.", editor.Id));
                }
                const auto identity = IdentityOf(component);
                const auto parents = requiredBy.find(identity);
                record.Components.push_back({.PackageId = component.Id,
                                             .Version = component.Version.ToString(),
                                             .Kind = component.Kind,
                                             .DisplayName = component.DisplayName,
                                             .DownloadSizeBytes = component.ArtifactSizeBytes,
                                             .InstalledSizeBytes = component.InstalledSizeBytes,
                                             .RequiredByEditor = requiredPackages.contains(identity),
                                             .RequiredByPackageIds = parents == requiredBy.end()
                                                                         ? std::vector<std::string>{}
                                                                         : parents->second});
            }
            std::ranges::sort(record.Components, ComponentLess);
            next->AvailableEditors.push_back(std::move(record));
        }

        std::ranges::sort(next->AvailableEditors,
                          [](const AvailableEditorVersion& left, const AvailableEditorVersion& right)
                          {
                              if (left.Channel != right.Channel)
                                  return ChannelRank(left.Channel) < ChannelRank(right.Channel);
                              const auto leftVersion = SemanticVersion::Parse(left.Version).Value();
                              const auto rightVersion = SemanticVersion::Parse(right.Version).Value();
                              if (leftVersion != rightVersion)
                                  return leftVersion > rightVersion;
                              return left.PackageId < right.PackageId;
                          });
        for (const auto& channel :
             {std::string_view{"stable"}, std::string_view{"preview"}, std::string_view{"nightly"}})
        {
            if (std::ranges::any_of(next->AvailableEditors,
                                    [&](const AvailableEditorVersion& editor) { return editor.Channel == channel; }))
            {
                next->PopulatedChannels.emplace_back(channel);
            }
        }

        m_Packages = std::move(packages);
        m_Snapshot = std::shared_ptr<const EditorInstallCatalogSnapshot>(std::move(next));
        return HubStatus::Success();
    }

    HubResult<EditorInstallPlan> EditorInstallCatalog::PreviewInstall(const EditorInstallPreviewRequest& request) const
    {
        return Preview(request, false);
    }

    HubResult<EditorInstallPlan> EditorInstallCatalog::Preview(const EditorInstallPreviewRequest& request,
                                                               const bool registeredRepair) const
    {
        if (!Detail::IsBoundedIdentifier(request.InstallationId) ||
            !Detail::IsBoundedIdentifier(request.EditorPackageId) || !IsValidDestination(request.Destination) ||
            request.Components.size() > MaximumSelectedComponents)
        {
            return HubResult<EditorInstallPlan>::Failure(InstallError(HubErrorCode::InvalidArgument,
                                                                      "The editor install preview request is invalid.",
                                                                      request.EditorPackageId));
        }
        auto requestedEditorVersion = RequestedVersion(request.EditorVersion, request.EditorPackageId);
        if (!requestedEditorVersion)
            return HubResult<EditorInstallPlan>::Failure(requestedEditorVersion.Error());

        const auto editor =
            std::ranges::find_if(m_Packages,
                                 [&](const IndexedPackage& candidate)
                                 {
                                     return candidate.Manifest.Kind == PackageKind::Editor &&
                                            candidate.Manifest.Id == request.EditorPackageId &&
                                            candidate.Manifest.Version == requestedEditorVersion.Value();
                                 });
        if (editor == m_Packages.end())
        {
            return HubResult<EditorInstallPlan>::Failure(InstallError(
                HubErrorCode::NotFound, "The selected editor version is unavailable.", request.EditorPackageId));
        }

        const auto installations = m_Registry.Snapshot();
        const auto existingId = std::ranges::find(*installations, request.InstallationId, &EditorInstallation::Id);
        if (!registeredRepair && existingId != installations->end())
        {
            return HubResult<EditorInstallPlan>::Failure(
                InstallError(HubErrorCode::DuplicateIdentifier, "The editor installation identity is already in use.",
                             request.InstallationId));
        }
        const auto destinationKey = PathKey(request.Destination, m_Specification.HostPlatform == "windows");
        const auto existingRoot = std::ranges::find_if(
            *installations, [&](const EditorInstallation& installation)
            { return PathKey(installation.Root, m_Specification.HostPlatform == "windows") == destinationKey; });
        if (!registeredRepair && existingRoot != installations->end())
        {
            return HubResult<EditorInstallPlan>::Failure(
                InstallError(HubErrorCode::DestinationConflict,
                             "The selected editor destination is already registered.", existingRoot->Id));
        }

        std::vector<PackageRequirement> requirements;
        auto exactEditor = ExactVersion(editor->Manifest.Version);
        if (!exactEditor)
            return HubResult<EditorInstallPlan>::Failure(exactEditor.Error());
        requirements.push_back({editor->Manifest.Id, std::move(exactEditor).Value()});
        std::set<std::string, std::less<>> selectedIds;
        std::set<PackageIdentity> explicitlySelected{IdentityOf(editor->Manifest)};
        std::vector<EditorComponentSelection> selectedComponents;
        selectedComponents.reserve(request.Components.size());
        for (const auto& selection : request.Components)
        {
            if (!Detail::IsBoundedIdentifier(selection.PackageId) || !selectedIds.insert(selection.PackageId).second)
            {
                return HubResult<EditorInstallPlan>::Failure(InstallError(
                    HubErrorCode::DuplicateIdentifier,
                    "The component selection contains an invalid or duplicate package.", selection.PackageId));
            }
            auto version = RequestedVersion(selection.Version, selection.PackageId);
            if (!version)
                return HubResult<EditorInstallPlan>::Failure(version.Error());
            const auto component = std::ranges::find_if(m_Packages,
                                                        [&](const IndexedPackage& candidate)
                                                        {
                                                            return candidate.Manifest.Id == selection.PackageId &&
                                                                   candidate.Manifest.Version == version.Value();
                                                        });
            if (component == m_Packages.end() || !IsComponent(component->Manifest))
            {
                return HubResult<EditorInstallPlan>::Failure(InstallError(
                    HubErrorCode::NotFound, "The selected editor component is unavailable.", selection.PackageId));
            }
            if (component->Manifest.Channel != editor->Manifest.Channel ||
                (component->Manifest.EngineCompatibility &&
                 !component->Manifest.EngineCompatibility->Matches(editor->Manifest.Version)))
            {
                return HubResult<EditorInstallPlan>::Failure(InstallError(
                    HubErrorCode::PackageHostIncompatible,
                    "The selected component is incompatible with this editor version.", selection.PackageId));
            }
            auto exactComponent = ExactVersion(component->Manifest.Version);
            if (!exactComponent)
                return HubResult<EditorInstallPlan>::Failure(exactComponent.Error());
            requirements.push_back({component->Manifest.Id, std::move(exactComponent).Value()});
            explicitlySelected.insert(IdentityOf(component->Manifest));
            selectedComponents.push_back(
                {.PackageId = component->Manifest.Id, .Version = component->Manifest.Version.ToString()});
        }
        std::ranges::sort(selectedComponents,
                          [](const EditorComponentSelection& left, const EditorComponentSelection& right)
                          { return left.PackageId < right.PackageId; });

        std::vector<PackageManifest> available;
        for (const auto& package : m_Packages)
        {
            if (package.Manifest.Channel == editor->Manifest.Channel)
                available.push_back(package.Manifest);
        }
        auto resolution = PackageResolver{}.Resolve(available, requirements,
                                                    {.Platform = m_Specification.HostPlatform,
                                                     .Architecture = m_Specification.HostArchitecture,
                                                     .EngineVersion = editor->Manifest.Version,
                                                     .AvailableDiskBytes = request.AvailableDiskBytes});
        if (!resolution)
            return HubResult<EditorInstallPlan>::Failure(resolution.Error());

        EditorInstallPlan plan{.InstallationId = request.InstallationId,
                               .Destination = request.Destination.lexically_normal(),
                               .EditorPackageId = editor->Manifest.Id,
                               .EditorVersion = editor->Manifest.Version.ToString(),
                               .Channel = editor->Manifest.Channel,
                               .RequiredDiskBytes = resolution.Value().RequiredDiskBytes,
                               .SelectedComponents = std::move(selectedComponents)};
        const auto requiredBy = RequiredBy(resolution.Value().InstallOrder);
        plan.Steps.reserve(resolution.Value().InstallOrder.size());
        for (const auto& manifest : resolution.Value().InstallOrder)
        {
            if (plan.DownloadSizeBytes > std::numeric_limits<std::uint64_t>::max() - manifest.ArtifactSizeBytes)
            {
                return HubResult<EditorInstallPlan>::Failure(InstallError(
                    HubErrorCode::InvalidData, "The install plan download size overflows its limit.", manifest.Id));
            }
            plan.DownloadSizeBytes += manifest.ArtifactSizeBytes;
            const auto source =
                std::ranges::find_if(m_Packages, [&](const IndexedPackage& candidate)
                                     { return IdentityOf(candidate.Manifest) == IdentityOf(manifest); });
            if (source == m_Packages.end())
            {
                return HubResult<EditorInstallPlan>::Failure(InstallError(
                    HubErrorCode::InvalidData, "An install-plan package lost its catalog provenance.", manifest.Id));
            }
            const auto parents = requiredBy.find(IdentityOf(manifest));
            plan.Steps.push_back(
                {.Manifest = manifest,
                 .CatalogKeyId = source->CatalogKeyId,
                 .CatalogSequence = source->CatalogSequence,
                 .ExplicitlySelected = explicitlySelected.contains(IdentityOf(manifest)),
                 .RequiredByPackageIds = parents == requiredBy.end() ? std::vector<std::string>{} : parents->second});
        }
        return HubResult<EditorInstallPlan>::Success(std::move(plan));
    }

    HubResult<EditorRepairPlan> EditorInstallCatalog::PreviewRepair(const EditorRepairPreviewRequest& request) const
    {
        if (!Detail::IsBoundedIdentifier(request.InstallationId) || !IsValidDestination(request.Destination) ||
            !Detail::IsSha256(request.ManifestFingerprint) || !Detail::IsSha256(request.PackageTreeIdentity) ||
            !Detail::IsSha256(request.PackageReceiptSha256) || request.MarkerNonce.size() < 32U ||
            request.MarkerNonce.size() > 256U ||
            !std::ranges::all_of(request.MarkerNonce, [](const unsigned char value) { return std::isxdigit(value); }))
        {
            return HubResult<EditorRepairPlan>::Failure(
                InstallError(HubErrorCode::InvalidArgument, "The managed editor repair authorization is invalid.",
                             request.InstallationId));
        }

        const auto installations = m_Registry.Snapshot();
        const auto installation = std::ranges::find(*installations, request.InstallationId, &EditorInstallation::Id);
        const auto destinationKey = PathKey(request.Destination, m_Specification.HostPlatform == "windows");
        if (installation == installations->end() || installation->Ownership != InstallationOwnership::Managed ||
            PathKey(installation->Root, m_Specification.HostPlatform == "windows") != destinationKey ||
            installation->ManifestFingerprint != request.ManifestFingerprint ||
            installation->PackageTreeIdentity != request.PackageTreeIdentity ||
            installation->PackageReceiptSha256 != request.PackageReceiptSha256 ||
            installation->MarkerNonce != request.MarkerNonce || installation->InstalledPackages.empty())
        {
            return HubResult<EditorRepairPlan>::Failure(InstallError(
                HubErrorCode::UnsafeInstallRoot,
                "The repair authorization does not match a receipt-bound managed editor.", request.InstallationId));
        }

        const auto installedEditor =
            std::ranges::find(installation->InstalledPackages, PackageKind::Editor, &InstalledPackageRecord::Kind);
        if (installedEditor == installation->InstalledPackages.end() ||
            std::ranges::count(installation->InstalledPackages, PackageKind::Editor, &InstalledPackageRecord::Kind) !=
                1 ||
            installedEditor->Version.ToString() != installation->Version)
        {
            return HubResult<EditorRepairPlan>::Failure(InstallError(
                HubErrorCode::PackageManifestInvalid,
                "The installed editor receipt cannot identify one exact editor package.", request.InstallationId));
        }

        EditorInstallPreviewRequest preview{.InstallationId = installation->Id,
                                            .Destination = installation->Root,
                                            .EditorPackageId = installedEditor->Id,
                                            .EditorVersion = installedEditor->Version.ToString(),
                                            .AvailableDiskBytes = request.AvailableDiskBytes};
        for (const auto& package : installation->InstalledPackages)
        {
            if (package.Kind == PackageKind::Editor)
                continue;
            if (!IsComponent(package.Kind))
            {
                return HubResult<EditorRepairPlan>::Failure(InstallError(
                    HubErrorCode::PackageManifestInvalid,
                    "The installed editor receipt contains an unsupported repair package kind.", package.Id));
            }
            preview.Components.push_back({.PackageId = package.Id, .Version = package.Version.ToString()});
        }

        auto planned = Preview(preview, true);
        if (!planned)
            return HubResult<EditorRepairPlan>::Failure(planned.Error());
        if (planned.Value().Steps.size() != installation->InstalledPackages.size())
        {
            return HubResult<EditorRepairPlan>::Failure(
                InstallError(HubErrorCode::PackageManifestInvalid,
                             "The signed repair dependency closure differs from the installed package receipt.",
                             request.InstallationId));
        }
        for (const auto& step : planned.Value().Steps)
        {
            const auto installed =
                std::ranges::find(installation->InstalledPackages, step.Manifest.Id, &InstalledPackageRecord::Id);
            if (installed == installation->InstalledPackages.end() || !SamePublishedPackage(step.Manifest, *installed))
            {
                return HubResult<EditorRepairPlan>::Failure(InstallError(
                    HubErrorCode::PackageManifestInvalid,
                    "A signed repair package does not match the installed package receipt.", step.Manifest.Id));
            }
        }
        return HubResult<EditorRepairPlan>::Success({.Install = std::move(planned).Value(),
                                                     .ManifestFingerprint = request.ManifestFingerprint,
                                                     .PackageTreeIdentity = request.PackageTreeIdentity,
                                                     .PackageReceiptSha256 = request.PackageReceiptSha256,
                                                     .MarkerNonce = request.MarkerNonce});
    }

    std::shared_ptr<const EditorInstallCatalogSnapshot> EditorInstallCatalog::Snapshot() const noexcept
    {
        return m_Snapshot;
    }
} // namespace KeireHub
