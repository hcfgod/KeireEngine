#include "KeireClient/Editor/PackageManagerPanel.h"

#include "KeireClient/Editor/EditorRendererCapabilities.h"

#include "Keire/BuildInfo.h"
#include "Keire/PlatformDirectories.h"

#include "KeireInternal/FileSystem.h"

#include "KeireHubRuntime/MarketplaceClient.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <string>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] std::string_view HostPlatform() noexcept
        {
#if defined(_WIN32)
            return "windows";
#elif defined(__APPLE__)
            return "macos";
#else
            return "linux";
#endif
        }

        [[nodiscard]] std::string_view HostArchitecture() noexcept
        {
#if defined(_M_ARM64) || defined(__aarch64__)
            return "arm64";
#else
            return "x86_64";
#endif
        }

        [[nodiscard]] std::uint64_t NowUnixSeconds() noexcept
        {
            const auto elapsed = std::chrono::system_clock::now().time_since_epoch();
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count());
        }

        [[nodiscard]] std::string TrustLabel(const Keire::ProjectPackageTrust trust)
        {
            switch (trust)
            {
            case Keire::ProjectPackageTrust::Unverified:
                return "Unverified";
            case Keire::ProjectPackageTrust::CatalogHashVerified:
                return "Catalog verified";
            case Keire::ProjectPackageTrust::MarketplaceSignatureVerified:
                return "Marketplace signed";
            case Keire::ProjectPackageTrust::Embedded:
                return "Embedded";
            }
            return "Unknown";
        }

        [[nodiscard]] std::string_view ImportDispositionLabel(const Keire::ProjectAssetImportDisposition disposition)
        {
            switch (disposition)
            {
            case Keire::ProjectAssetImportDisposition::Install:
                return "Install";
            case Keire::ProjectAssetImportDisposition::Replace:
                return "Replace";
            case Keire::ProjectAssetImportDisposition::ReuseIdentical:
                return "Reuse identical";
            case Keire::ProjectAssetImportDisposition::KeepLocal:
                return "Keep local";
            case Keire::ProjectAssetImportDisposition::Conflict:
                return "Conflict";
            }
            return "Unknown";
        }

        [[nodiscard]] std::optional<std::string> ReadTrustedKey(const std::filesystem::path& executable)
        {
            std::vector<std::filesystem::path> candidates{
                executable.parent_path() / "Config" / "Marketplace" / "trusted-marketplace-key.json",
                executable.parent_path().parent_path() / "Config" / "Marketplace" / "trusted-marketplace-key.json"};
            std::error_code error;
            auto root = std::filesystem::current_path(error);
            for (std::size_t depth = 0; !error && depth < 8U && !root.empty(); ++depth)
            {
                candidates.push_back(root / "Config" / "Marketplace" / "trusted-marketplace-key.json");
                const auto parent = root.parent_path();
                if (parent == root)
                    break;
                root = parent;
            }
            for (const auto& candidate : candidates)
            {
                error.clear();
                if (!std::filesystem::is_regular_file(candidate, error) || error)
                    continue;
                std::ifstream stream(candidate, std::ios::binary);
                std::string document{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
                if (stream && !document.empty() && document.size() <= std::size_t{16U} * 1024U)
                    return document;
            }
            return std::nullopt;
        }

        [[nodiscard]] KeireHub::MarketplacePublication VerifyPublication(const KeireHub::MarketplaceCacheItem& item,
                                                                         const KeireHub::CatalogTrustStore& trust)
        {
            auto publication = KeireHub::DecodeMarketplacePublication(item.SignedPublication);
            if (!publication)
                throw std::runtime_error(publication.Error().Message);
            if (const auto verified =
                    KeireHub::VerifyMarketplacePublication(publication.Value(), item.ProductId, item.VersionId,
                                                           item.ArchiveSha256, item.ArchiveSizeBytes, trust);
                !verified)
            {
                throw std::runtime_error(verified.Error().Message);
            }
            return std::move(publication).Value();
        }
    } // namespace

    PackageManagerPanel::~PackageManagerPanel() = default;

    void PackageManagerPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.package-manager", "Package Manager", true});
    }

    void PackageManagerPanel::Initialize(const std::filesystem::path& projectRoot,
                                         const std::filesystem::path& executable)
    {
        Shutdown();
        try
        {
            const auto cacheRoot = Keire::GetPreferenceDirectory() / "Hub" / "MarketplacePackages";
            m_MarketplaceCache = std::make_unique<KeireHub::MarketplaceCacheStore>(cacheRoot);
            m_MarketplaceSession = std::make_unique<KeireHub::MarketplaceSessionLeaseStore>(cacheRoot);
            const auto key = ReadTrustedKey(executable);
            if (!key)
            {
                m_Error = "Marketplace downloads are unavailable because the trusted Kéire public key is missing. "
                          "Local package workflows remain available.";
            }
            else
            {
                auto trust =
                    KeireHub::CatalogTrustStore::Create({.TrustedPublicKeyDocuments = {*key}, .NativeLibraryPath = {}});
                if (trust)
                    m_MarketplaceTrust = std::make_unique<KeireHub::CatalogTrustStore>(std::move(trust).Value());
                else
                    m_Error = trust.Error().Message + " Local package workflows remain available.";
            }
            const auto verify = [this](const std::string_view algorithm, const std::string_view keyId,
                                       const std::span<const std::byte> message,
                                       const std::span<const std::byte> signature)
            { return m_MarketplaceTrust && m_MarketplaceTrust->VerifySignature(algorithm, keyId, message, signature); };
            Keire::ProjectPackageManagerSpecification specification{
                .ProjectRoot = projectRoot,
                .GlobalCacheRoot = cacheRoot,
                .EngineVersion = std::string(Keire::GetBuildInfo().Version),
                .Platform = std::string(HostPlatform()),
                .Architecture = std::string(HostArchitecture()),
                .RendererCapabilities = EditorRendererCapabilities(),
                .VerifyMarketplaceSignature = verify,
                .Events = [this](const Keire::ProjectPackageEvent& event) { m_LastEvent = event; }};
            m_Manager = std::make_unique<Keire::ProjectPackageManager>(std::move(specification));
            m_AssetImporter =
                std::make_unique<Keire::ProjectAssetPackageImporter>(Keire::ProjectAssetPackageImporterSpecification{
                    .ProjectRoot = projectRoot,
                    .EngineVersion = std::string(Keire::GetBuildInfo().Version),
                    .Platform = std::string(HostPlatform()),
                    .Architecture = std::string(HostArchitecture()),
                    .RendererCapabilities = EditorRendererCapabilities(),
                    .VerifyMarketplaceSignature = verify});
            const auto packageRecovery = m_Manager->RecoverInterruptedOperations();
            const auto importRecovery = m_AssetImporter->RecoverInterruptedOperations();
            const auto recovered = packageRecovery.RecoveredOperations + importRecovery.RecoveredOperations;
            m_Status = recovered == 0U
                           ? "Package state is ready."
                           : "Recovered " + std::to_string(recovered) + " interrupted package operation(s).";
            if (!packageRecovery.Diagnostics.empty())
                m_Error = packageRecovery.Diagnostics.front();
            else if (!importRecovery.Diagnostics.empty())
                m_Error = importRecovery.Diagnostics.front();
            Refresh();
            RefreshMarketplaceCache(true);
        }
        catch (const std::exception& error)
        {
            m_Error = error.what();
            m_Manager.reset();
        }
    }

    void PackageManagerPanel::Shutdown() noexcept
    {
        m_Manager.reset();
        m_AssetImporter.reset();
        m_MarketplaceCache.reset();
        m_MarketplaceSession.reset();
        m_MarketplaceTrust.reset();
        m_MarketplaceSnapshot = {};
        m_Manifest = {};
        m_Lock = {};
        m_SelectedPackage.clear();
        m_LocalArchive.clear();
        m_LocalSearch.clear();
        m_SelectedMarketplaceProduct.clear();
        m_MarketplaceSessionMessage.clear();
        m_Status.clear();
        m_Error.clear();
        m_LastEvent = {};
        m_LocalMetadata.reset();
        m_ImportReview.Cancel();
        m_AllowExecutableCode = false;
        m_KeepLocalConflicts = true;
        m_MarketplaceSessionAuthorized = false;
        m_NextMarketplaceRefresh = {};
    }

    void PackageManagerPanel::InspectLocalPackage()
    {
        try
        {
            const auto archive =
                std::filesystem::absolute(Keire::Detail::PathFromUtf8(m_LocalArchive)).lexically_normal();
            if (!std::filesystem::is_regular_file(archive) || archive.extension() != ".keireassetpackage")
                throw std::invalid_argument("Choose an existing .keireassetpackage file.");
            m_LocalMetadata = Keire::InspectAssetPackageArchive(archive);
            m_Status =
                "Inspected " + m_LocalMetadata->Manifest.DisplayName + " " + m_LocalMetadata->Manifest.Version + ".";
            m_Error.clear();
        }
        catch (const std::exception& error)
        {
            m_LocalMetadata.reset();
            m_Error = error.what();
        }
    }

    void PackageManagerPanel::Refresh()
    {
        if (!m_Manager)
            return;
        m_Manifest = m_Manager->Manifest();
        m_Lock = m_Manager->Lock();
        if (!m_SelectedPackage.empty() &&
            std::ranges::find(m_Lock.Packages, m_SelectedPackage, &Keire::ProjectPackageLockEntry::PackageId) ==
                m_Lock.Packages.end())
        {
            m_SelectedPackage.clear();
        }
    }

    void PackageManagerPanel::RefreshMarketplaceCache(const bool focusRequestedProduct)
    {
        m_NextMarketplaceRefresh = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        if (!m_MarketplaceCache || !m_MarketplaceSession)
            return;
        const auto nowUnixSeconds = NowUnixSeconds();
        auto lease = m_MarketplaceSession->Load();
        if (!lease)
        {
            m_MarketplaceSessionAuthorized = false;
            m_MarketplaceSessionMessage = lease.Error().Message;
            m_MarketplaceSnapshot = {};
            m_SelectedMarketplaceProduct.clear();
            return;
        }
        if (!lease.Value().SignedIn || lease.Value().AccountId.empty())
        {
            m_MarketplaceSessionAuthorized = false;
            m_MarketplaceSessionMessage = "Sign in to Kéire Hub to view My Assets.";
            m_MarketplaceSnapshot = {};
            m_SelectedMarketplaceProduct.clear();
            return;
        }
        if (lease.Value().ExpiresAtUnixSeconds < nowUnixSeconds)
        {
            m_MarketplaceSessionAuthorized = false;
            m_MarketplaceSessionMessage = "Keep Kéire Hub running and signed in to view My Assets.";
            m_MarketplaceSnapshot = {};
            m_SelectedMarketplaceProduct.clear();
            return;
        }
        auto loaded = m_MarketplaceCache->Load();
        if (!loaded)
        {
            m_MarketplaceSessionAuthorized = false;
            m_MarketplaceSessionMessage = loaded.Error().Message;
            m_MarketplaceSnapshot = {};
            m_SelectedMarketplaceProduct.clear();
            return;
        }
        if (!KeireHub::MarketplaceSessionAuthorizes(loaded.Value(), lease.Value(), nowUnixSeconds))
        {
            m_MarketplaceSessionAuthorized = false;
            m_MarketplaceSessionMessage =
                "The marketplace cache belongs to a different account. Open an asset from the website to "
                "synchronize this account.";
            m_MarketplaceSnapshot = {};
            m_SelectedMarketplaceProduct.clear();
            return;
        }
        const auto previousRequest = m_MarketplaceSnapshot.RequestedProductId;
        m_MarketplaceSnapshot = std::move(loaded).Value();
        m_MarketplaceSessionAuthorized = true;
        m_MarketplaceSessionMessage.clear();
        if (focusRequestedProduct && !m_MarketplaceSnapshot.RequestedProductId.empty() &&
            (m_MarketplaceSnapshot.RequestedProductId != previousRequest || m_SelectedMarketplaceProduct.empty()))
        {
            m_SelectedMarketplaceProduct = m_MarketplaceSnapshot.RequestedProductId;
            m_Registration.SetVisible(true);
        }
        if (m_SelectedMarketplaceProduct.empty())
        {
            const auto entitled =
                std::ranges::find(m_MarketplaceSnapshot.Items, true, &KeireHub::MarketplaceCacheItem::Entitled);
            if (entitled != m_MarketplaceSnapshot.Items.end())
                m_SelectedMarketplaceProduct = entitled->ProductId;
        }
    }

    bool PackageManagerPanel::HasAuthorizedMarketplaceSession(std::string& diagnostic) const
    {
        if (!m_MarketplaceSession)
        {
            diagnostic = "The Kéire Hub marketplace session is unavailable.";
            return false;
        }
        const auto lease = m_MarketplaceSession->Load();
        if (!lease)
        {
            diagnostic = lease.Error().Message;
            return false;
        }
        if (!lease.Value().SignedIn || lease.Value().AccountId.empty())
        {
            diagnostic = "Sign in to Kéire Hub before using My Assets.";
            return false;
        }
        const auto nowUnixSeconds = NowUnixSeconds();
        if (lease.Value().ExpiresAtUnixSeconds < nowUnixSeconds)
        {
            diagnostic = "Keep Kéire Hub running and signed in before using My Assets.";
            return false;
        }
        if (!KeireHub::MarketplaceSessionAuthorizes(m_MarketplaceSnapshot, lease.Value(), nowUnixSeconds))
        {
            diagnostic = "The marketplace cache does not belong to the signed-in Kéire Hub account.";
            return false;
        }
        return true;
    }

    void PackageManagerPanel::InstallMarketplacePackage(const KeireHub::MarketplaceCacheItem& item)
    {
        if (!m_Manager || !m_MarketplaceCache)
            return;
        std::string sessionDiagnostic;
        if (!HasAuthorizedMarketplaceSession(sessionDiagnostic))
        {
            m_Error = std::move(sessionDiagnostic);
            return;
        }
        if (!m_MarketplaceTrust)
        {
            m_Error = "Marketplace signature verification is unavailable. Reinstall this Editor package before "
                      "installing marketplace content.";
            return;
        }
        try
        {
            if (!item.Entitled || item.State != KeireHub::MarketplaceCacheState::Ready ||
                item.InstallKind != "registry")
            {
                throw std::invalid_argument("This marketplace package is not ready for Registry installation.");
            }
            const auto archive = m_MarketplaceCache->ArchivePath(item);
            const auto publication = VerifyPublication(item, *m_MarketplaceTrust);
            const auto metadata =
                Keire::InspectAssetPackageArchive(archive, {.RequireSignature = false,
                                                            .ExpectedArchiveSizeBytes = item.ArchiveSizeBytes,
                                                            .ExpectedArchiveSha256 = item.ArchiveSha256});
            if (metadata.Manifest.PackageId != item.PackageId || metadata.Manifest.Version != item.Version ||
                metadata.Manifest.InstallKind != Keire::AssetPackageInstallKind::Registry)
            {
                throw std::runtime_error("The verified archive does not match the selected marketplace version.");
            }
            auto requirements = m_Manifest.Dependencies;
            const auto existing =
                std::ranges::find(requirements, item.PackageId, &Keire::ProjectPackageRequirement::PackageId);
            const Keire::ProjectPackageRequirement requirement{item.PackageId, item.Version};
            if (existing == requirements.end())
                requirements.push_back(requirement);
            else
                *existing = requirement;
            std::ranges::sort(requirements, {}, &Keire::ProjectPackageRequirement::PackageId);
            Keire::ProjectPackageInstallRequest request{
                .Archives = {{.Archive = archive,
                              .CatalogSource = "marketplace:" + item.ProductId + '/' + item.VersionId,
                              .ExpectedArchiveSizeBytes = item.ArchiveSizeBytes,
                              .ExpectedArchiveSha256 = item.ArchiveSha256,
                              .TrustedSignatureKeyId = publication.KeyId,
                              .RequireMarketplaceSignature = false}},
                .DirectDependencies = std::move(requirements)};
            const auto plan = m_Manager->PreflightInstall(request);
            if (!plan.Valid())
                throw std::runtime_error("Package preflight failed: " + plan.Conflicts.front().Message);
            static_cast<void>(m_Manager->Install(request));
            m_SelectedPackage = item.PackageId;
            m_Status = "Installed " + item.DisplayName + " " + item.Version + " from My Assets.";
            m_Error.clear();
            Refresh();
        }
        catch (const std::exception& error)
        {
            m_Error = error.what();
        }
    }

    void PackageManagerPanel::PrepareMarketplaceImport(const KeireHub::MarketplaceCacheItem& item)
    {
        if (!m_AssetImporter || !m_MarketplaceCache)
            return;
        std::string sessionDiagnostic;
        if (!HasAuthorizedMarketplaceSession(sessionDiagnostic))
        {
            m_Error = std::move(sessionDiagnostic);
            return;
        }
        if (!m_MarketplaceTrust)
        {
            m_Error = "Marketplace signature verification is unavailable. Reinstall this Editor package before "
                      "importing marketplace content.";
            return;
        }
        try
        {
            if (!item.Entitled || item.State != KeireHub::MarketplaceCacheState::Ready ||
                item.InstallKind != "asset_import")
            {
                throw std::invalid_argument("This marketplace package is not ready for Asset Import.");
            }
            const auto archive = m_MarketplaceCache->ArchivePath(item);
            static_cast<void>(VerifyPublication(item, *m_MarketplaceTrust));
            Keire::ProjectAssetImportRequest request{.Archive = archive,
                                                     .ExpectedArchiveSizeBytes = item.ArchiveSizeBytes,
                                                     .ExpectedArchiveSha256 = item.ArchiveSha256,
                                                     .RequireMarketplaceSignature = false,
                                                     .AllowExecutableCode = m_AllowExecutableCode};
            auto plan = m_AssetImporter->Preflight(request);
            if (!plan.Valid() && m_KeepLocalConflicts)
            {
                for (const auto& conflict : plan.Conflicts)
                {
                    if (!conflict.Path.empty() &&
                        (conflict.Kind == Keire::ProjectAssetImportConflictKind::Path ||
                         conflict.Kind == Keire::ProjectAssetImportConflictKind::ModifiedLocalFile))
                    {
                        request.Decisions.push_back({conflict.Path, Keire::ProjectAssetImportResolution::KeepLocal});
                    }
                }
                plan = m_AssetImporter->Preflight(request);
            }
            m_ImportReview.Prepare(item.DisplayName, std::move(request), std::move(plan));
            m_Status = "Review the verified import plan before changing project files.";
            m_Error.clear();
        }
        catch (const std::exception& error)
        {
            m_Error = error.what();
        }
    }

    void PackageManagerPanel::ImportReviewedPackage(const PackageImportConfirmation& confirmation)
    {
        if (!m_AssetImporter)
            return;
        try
        {
            const auto result = m_AssetImporter->Import(confirmation.Request);
            m_Status = "Imported " + std::to_string(result.Written.size()) + " file(s) from " +
                       confirmation.DisplayName + "; retained " + std::to_string(result.Retained.size()) +
                       " local file(s).";
            m_Error.clear();
        }
        catch (const std::exception& error)
        {
            m_Error = error.what();
        }
    }

    void PackageManagerPanel::DrawImportReview(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme)
    {
        if (m_ImportReview.ConsumeOpenRequest())
            ui.OpenPopup("Import Asset Package");
        if (auto popup = ui.BeginPopupModal("Import Asset Package"); popup)
        {
            if (!m_ImportReview.Active())
            {
                ui.CloseCurrentPopup();
                return;
            }
            const auto& plan = m_ImportReview.Plan();
            ui.TextColored(theme.Accent, m_ImportReview.DisplayName());
            ui.Text(plan.Package.Manifest.PackageId + "  " + plan.Package.Manifest.Version);
            ui.TextColored(theme.MutedText, std::to_string(plan.Entries.size()) +
                                                " planned file(s); no project files change until you "
                                                "confirm.");
            if (plan.ContainsExecutableCode)
                ui.TextColored(theme.Warning, "This package contains executable C# assemblies.");
            ui.Separator();
            if (auto files = ui.BeginChild("PackageImportReviewFiles", {620.0F, 260.0F}, true); files)
            {
                for (const auto& entry : plan.Entries)
                {
                    ui.Text(entry.ProjectPath.generic_string() + "  [" +
                            std::string(ImportDispositionLabel(entry.Disposition)) + "]");
                }
                for (const auto& conflict : plan.Conflicts)
                    ui.TextColored(theme.Error, conflict.Message);
            }
            if (!plan.Valid())
                ui.TextColored(theme.Warning,
                               "Resolve the listed conflict options in My Assets, then open this review again.");
            if (auto disabled = ui.BeginDisabled(!plan.Valid()); disabled)
            {
                if (ui.Button("Import"))
                {
                    auto confirmation = m_ImportReview.Confirm();
                    ui.CloseCurrentPopup();
                    ImportReviewedPackage(confirmation);
                }
            }
            ui.SameLine();
            if (ui.Button("Cancel"))
            {
                m_ImportReview.Cancel();
                ui.CloseCurrentPopup();
            }
        }
    }

    void PackageManagerPanel::DrawMarketplaceLibrary(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme)
    {
        if (ui.Button("Refresh Hub cache"))
            RefreshMarketplaceCache(true);
        ui.SameLine();
        ui.TextColored(theme.MutedText, std::to_string(std::ranges::count(m_MarketplaceSnapshot.Items, true,
                                                                          &KeireHub::MarketplaceCacheItem::Entitled)) +
                                            " entitled asset(s)");
        ui.Separator();
        if (!m_MarketplaceSessionAuthorized)
        {
            ui.TextColored(theme.Warning, m_MarketplaceSessionMessage);
            return;
        }
        if (m_MarketplaceSnapshot.Items.empty())
        {
            ui.Text("No marketplace assets have been synchronized yet.");
            ui.TextColored(theme.MutedText,
                           "Claim an asset on the Kéire website, choose Open in Editor, and keep Kéire Hub signed in.");
            return;
        }
        if (auto list = ui.BeginChild("MarketplaceLibraryList", {300.0F, 0.0F}, true); list)
        {
            for (const auto& item : m_MarketplaceSnapshot.Items)
            {
                if (!item.Entitled)
                    continue;
                auto label = item.DisplayName;
                if (!item.Version.empty())
                    label += "  " + item.Version;
                if (item.State == KeireHub::MarketplaceCacheState::Downloading)
                    label += "  [downloading]";
                else if (item.State == KeireHub::MarketplaceCacheState::Failed)
                    label += "  [attention]";
                if (ui.Selectable(label, m_SelectedMarketplaceProduct == item.ProductId))
                    m_SelectedMarketplaceProduct = item.ProductId;
            }
        }
        ui.SameLine();
        if (auto details = ui.BeginChild("MarketplaceLibraryDetails", {}, true); details)
        {
            const auto selected = std::ranges::find(m_MarketplaceSnapshot.Items, m_SelectedMarketplaceProduct,
                                                    &KeireHub::MarketplaceCacheItem::ProductId);
            if (selected == m_MarketplaceSnapshot.Items.end() || !selected->Entitled)
            {
                ui.TextColored(theme.MutedText, "Select an entitled asset to inspect it.");
                return;
            }
            ui.TextColored(theme.Accent, selected->DisplayName);
            if (!selected->PublisherName.empty())
                ui.Text("By " + selected->PublisherName);
            ui.Text(selected->ShortDescription);
            if (!selected->Version.empty())
                ui.Text("Version " + selected->Version);
            if (!selected->LicenseSpdx.empty())
                ui.Text("License: " + selected->LicenseSpdx);
            ui.Spacing();
            if (selected->State == KeireHub::MarketplaceCacheState::Downloading)
            {
                ui.TextColored(theme.MutedText, "Kéire Hub is downloading and verifying this package.");
            }
            else if (selected->State == KeireHub::MarketplaceCacheState::Failed)
            {
                ui.TextColored(theme.Error, selected->FailureMessage);
                ui.TextColored(theme.MutedText, "Choose Open in Editor on the marketplace page to retry through Hub.");
            }
            else if (selected->State != KeireHub::MarketplaceCacheState::Ready)
            {
                ui.TextColored(theme.MutedText, "Open this asset from the website to prepare a verified local copy.");
            }
            else if (selected->InstallKind == "registry")
            {
                if (ui.Button("Install to Project"))
                    InstallMarketplacePackage(*selected);
            }
            else if (selected->InstallKind == "asset_import")
            {
                static_cast<void>(ui.Checkbox("Allow package C# assemblies to compile", m_AllowExecutableCode));
                static_cast<void>(ui.Checkbox("Keep locally modified files on conflicts", m_KeepLocalConflicts));
                if (ui.Button("Import into Project..."))
                    PrepareMarketplaceImport(*selected);
            }
            else
            {
                ui.TextColored(theme.Warning, "Complete projects are created and registered through Kéire Hub.");
            }
            if (selected->State == KeireHub::MarketplaceCacheState::Ready)
            {
                ui.TextColored(theme.MutedText, "SHA-256: " + selected->ArchiveSha256);
                ui.TextColored(theme.MutedText, "Trust: marketplace signature verified");
            }
        }
    }

    void PackageManagerPanel::InstallLocalPackage()
    {
        if (!m_Manager || m_LocalArchive.empty())
            return;
        try
        {
            const auto archive =
                std::filesystem::absolute(Keire::Detail::PathFromUtf8(m_LocalArchive)).lexically_normal();
            if (!std::filesystem::is_regular_file(archive) || archive.extension() != ".keireassetpackage")
                throw std::invalid_argument("Choose an existing .keireassetpackage file.");
            const auto metadata = Keire::InspectAssetPackageArchive(archive);
            if (metadata.Manifest.InstallKind != Keire::AssetPackageInstallKind::Registry)
                throw std::invalid_argument("Local project installation requires a Registry package.");
            auto requirements = m_Manifest.Dependencies;
            const auto existing = std::ranges::find(requirements, metadata.Manifest.PackageId,
                                                    &Keire::ProjectPackageRequirement::PackageId);
            const Keire::ProjectPackageRequirement requirement{metadata.Manifest.PackageId, metadata.Manifest.Version};
            if (existing == requirements.end())
                requirements.push_back(requirement);
            else
                *existing = requirement;
            std::ranges::sort(requirements, {}, &Keire::ProjectPackageRequirement::PackageId);
            Keire::ProjectPackageInstallRequest request{
                .Archives = {{.Archive = archive,
                              .CatalogSource = "local:" + archive.generic_string(),
                              .ExpectedArchiveSizeBytes = metadata.ArchiveSizeBytes,
                              .ExpectedArchiveSha256 = metadata.ArchiveSha256,
                              .RequireMarketplaceSignature = false}},
                .DirectDependencies = std::move(requirements)};
            const auto plan = m_Manager->PreflightInstall(request);
            if (!plan.Valid())
                throw std::runtime_error("Package preflight failed: " + plan.Conflicts.front().Message);
            static_cast<void>(m_Manager->Install(request));
            m_SelectedPackage = metadata.Manifest.PackageId;
            m_Status = "Installed " + metadata.Manifest.DisplayName + " " + metadata.Manifest.Version + '.';
            m_Error.clear();
            Refresh();
        }
        catch (const std::exception& error)
        {
            m_Error = error.what();
        }
    }

    void PackageManagerPanel::ImportLocalAssetPackage()
    {
        if (!m_AssetImporter || !m_LocalMetadata)
            return;
        try
        {
            if (m_LocalMetadata->Manifest.InstallKind != Keire::AssetPackageInstallKind::AssetImport)
                throw std::invalid_argument("This package does not use the Asset Import workflow.");
            Keire::ProjectAssetImportRequest request{
                .Archive = std::filesystem::absolute(Keire::Detail::PathFromUtf8(m_LocalArchive)).lexically_normal(),
                .ExpectedArchiveSizeBytes = m_LocalMetadata->ArchiveSizeBytes,
                .ExpectedArchiveSha256 = m_LocalMetadata->ArchiveSha256,
                .RequireMarketplaceSignature = false,
                .AllowExecutableCode = m_AllowExecutableCode};
            auto plan = m_AssetImporter->Preflight(request);
            if (!plan.Valid() && m_KeepLocalConflicts)
            {
                for (const auto& conflict : plan.Conflicts)
                {
                    if (!conflict.Path.empty() &&
                        (conflict.Kind == Keire::ProjectAssetImportConflictKind::Path ||
                         conflict.Kind == Keire::ProjectAssetImportConflictKind::ModifiedLocalFile))
                    {
                        request.Decisions.push_back({conflict.Path, Keire::ProjectAssetImportResolution::KeepLocal});
                    }
                }
                plan = m_AssetImporter->Preflight(request);
            }
            if (!plan.Valid())
                throw std::runtime_error("Asset import preflight failed: " + plan.Conflicts.front().Message);
            const auto result = m_AssetImporter->Import(request);
            m_Status = "Imported " + std::to_string(result.Written.size()) + " file(s) from " +
                       m_LocalMetadata->Manifest.DisplayName + "; retained " + std::to_string(result.Retained.size()) +
                       " local file(s).";
            m_Error.clear();
        }
        catch (const std::exception& error)
        {
            m_Error = error.what();
        }
    }

    void PackageManagerPanel::RemoveSelected()
    {
        if (!m_Manager || m_SelectedPackage.empty())
            return;
        try
        {
            const auto removed = m_SelectedPackage;
            static_cast<void>(m_Manager->Remove(removed));
            m_Status = "Removed " + removed + " and unreachable transitive dependencies.";
            m_Error.clear();
            Refresh();
        }
        catch (const std::exception& error)
        {
            m_Error = error.what();
        }
    }

    void PackageManagerPanel::EmbedSelected()
    {
        if (!m_Manager || m_SelectedPackage.empty())
            return;
        try
        {
            static_cast<void>(m_Manager->Embed(m_SelectedPackage));
            m_Status = "Embedded " + m_SelectedPackage + " as writable project content.";
            m_Error.clear();
            Refresh();
        }
        catch (const std::exception& error)
        {
            m_Error = error.what();
        }
    }

    void PackageManagerPanel::RevertSelected()
    {
        if (!m_Manager || m_SelectedPackage.empty())
            return;
        try
        {
            static_cast<void>(m_Manager->RevertEmbedded(m_SelectedPackage));
            m_Status = "Reverted " + m_SelectedPackage + " to its immutable verified cache entry.";
            m_Error.clear();
            Refresh();
        }
        catch (const std::exception& error)
        {
            m_Error = error.what();
        }
    }

    void PackageManagerPanel::DrawInProject(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme)
    {
        if (ui.Button("Refresh"))
        {
            try
            {
                Refresh();
                m_Error.clear();
            }
            catch (const std::exception& error)
            {
                m_Error = error.what();
            }
        }
        ui.SameLine();
        ui.TextColored(theme.MutedText, std::to_string(m_Lock.Packages.size()) + " resolved package(s)");
        ui.Separator();
        if (m_Lock.Packages.empty())
        {
            ui.Text("No registry packages are installed in this project.");
            ui.TextColored(theme.MutedText, "Existing projects remain valid until their first package operation.");
            return;
        }
        if (auto list = ui.BeginChild("PackageManagerInstalled", {280.0F, 0.0F}, true); list)
        {
            for (const auto& package : m_Lock.Packages)
            {
                const auto direct =
                    std::ranges::find(m_Manifest.Dependencies, package.PackageId,
                                      &Keire::ProjectPackageRequirement::PackageId) != m_Manifest.Dependencies.end();
                auto label = package.PackageId + "  " + package.Version;
                if (!direct)
                    label += "  [dependency]";
                if (package.Embedded)
                    label += "  [embedded]";
                if (ui.Selectable(label, m_SelectedPackage == package.PackageId))
                    m_SelectedPackage = package.PackageId;
            }
        }
        ui.SameLine();
        if (auto details = ui.BeginChild("PackageManagerDetails", {}, true); details)
        {
            const auto selected =
                std::ranges::find(m_Lock.Packages, m_SelectedPackage, &Keire::ProjectPackageLockEntry::PackageId);
            if (selected == m_Lock.Packages.end())
            {
                ui.TextColored(theme.MutedText, "Select a package to inspect its exact resolved state.");
                return;
            }
            ui.TextColored(theme.Accent, selected->PackageId);
            ui.Text("Version " + selected->Version);
            ui.Text("Source: " + selected->Source);
            ui.Text("SHA-256: " + selected->ArchiveSha256);
            ui.Text("Trust: " + TrustLabel(selected->Embedded ? Keire::ProjectPackageTrust::Embedded
                                           : selected->SignatureKeyId.empty()
                                               ? Keire::ProjectPackageTrust::CatalogHashVerified
                                               : Keire::ProjectPackageTrust::MarketplaceSignatureVerified));
            ui.Text(std::to_string(selected->Dependencies.size()) + " direct package dependency(ies)");
            ui.Spacing();
            if (selected->Embedded)
            {
                if (ui.Button("Revert to Registry"))
                    RevertSelected();
            }
            else if (ui.Button("Embed"))
                EmbedSelected();
            const auto direct =
                std::ranges::find(m_Manifest.Dependencies, selected->PackageId,
                                  &Keire::ProjectPackageRequirement::PackageId) != m_Manifest.Dependencies.end();
            if (auto disabled = ui.BeginDisabled(!direct); disabled)
            {
                ui.SameLine();
                if (ui.Button("Remove"))
                    RemoveSelected();
            }
            if (!direct)
                ui.TextColored(theme.MutedText, "Remove the direct package that owns this transitive dependency.");
        }
    }

    void PackageManagerPanel::DrawLocalPackages(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme)
    {
        ui.Text("Inspect a deterministic Registry or Asset Import package from disk.");
        ui.TextColored(theme.MutedText,
                       "Local packages are structure- and hash-verified. Marketplace trust requires a Hub download "
                       "grant and signature.");
        static_cast<void>(
            ui.InputTextWithHint("##LocalAssetPackage", "Absolute path to .keireassetpackage", m_LocalArchive));
        if (ui.Button("Inspect"))
            InspectLocalPackage();
        if (!m_LocalMetadata)
            return;
        const auto& manifest = m_LocalMetadata->Manifest;
        ui.Separator();
        ui.TextColored(theme.Accent, manifest.DisplayName + "  " + manifest.Version);
        ui.Text(manifest.PackageId);
        ui.TextColored(theme.MutedText,
                       std::to_string(manifest.Files.size()) + " file(s) · " +
                           std::to_string(manifest.InstalledSizeBytes) + " installed bytes · " +
                           (manifest.InstallKind == Keire::AssetPackageInstallKind::Registry ? "Registry"
                            : manifest.InstallKind == Keire::AssetPackageInstallKind::AssetImport
                                ? "Asset Import"
                                : "Complete Project"));
        static_cast<void>(ui.InputTextWithHint("##LocalPackageSearch", "Filter package files", m_LocalSearch));
        if (auto files = ui.BeginChild("LocalPackageFiles", {0.0F, 180.0F}, true); files)
        {
            for (const auto& file : manifest.Files)
            {
                const auto path = file.Path.generic_string();
                if (!m_LocalSearch.empty() && path.find(m_LocalSearch) == std::string::npos)
                    continue;
                const auto code = file.Path.extension() == ".cs" || file.Path.extension() == ".keireassembly";
                ui.Text((code ? "[CODE]  " : "         ") + path + "  " + std::to_string(file.SizeBytes) + " B");
            }
        }
        if (manifest.InstallKind == Keire::AssetPackageInstallKind::Registry)
        {
            if (ui.Button("Preflight and Install"))
                InstallLocalPackage();
        }
        else if (manifest.InstallKind == Keire::AssetPackageInstallKind::AssetImport)
        {
            if (!manifest.ManagedAssemblies.empty())
            {
                static_cast<void>(ui.Checkbox("Allow this package's C# assemblies to compile", m_AllowExecutableCode));
                ui.TextColored(theme.Warning, "Executable code consent is recorded per imported package version.");
            }
            static_cast<void>(ui.Checkbox("Keep locally modified files on conflicts", m_KeepLocalConflicts));
            if (ui.Button("Preflight and Import All"))
                ImportLocalAssetPackage();
        }
        else
            ui.TextColored(theme.Warning, "Complete projects must be created through Kéire Hub.");
    }

    void PackageManagerPanel::Draw(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme)
    {
        if (std::chrono::steady_clock::now() >= m_NextMarketplaceRefresh)
            RefreshMarketplaceCache(true);
        auto panel = ui.BeginPanel(m_Registration);
        if (!panel)
            return;
        ui.TextColored(theme.Accent, "PACKAGE MANAGER");
        ui.SameLine();
        ui.TextColored(theme.MutedText, "Deterministic dependencies, immutable cache, recoverable changes");
        ui.Separator();
        if (!m_Manager)
        {
            ui.TextColored(theme.Error, m_Error.empty() ? "Package management is unavailable." : m_Error);
            return;
        }
        if (auto tabs = ui.BeginTabBar("PackageManagerTabs"); tabs)
        {
            if (auto assets = ui.BeginTabItem("My Assets"); assets)
            {
                DrawMarketplaceLibrary(ui, theme);
            }
            if (auto registry = ui.BeginTabItem("Kéire Registry"); registry)
            {
                ui.Text("Registry discovery is synchronized through Kéire Hub's token-free marketplace cache.");
                ui.TextColored(theme.MutedText,
                               "Browse and claim packages on the Kéire Marketplace. Entitled, compatible, signed "
                               "versions appear in My Assets for project installation.");
                ui.TextColored(theme.MutedText,
                               std::to_string(m_MarketplaceSnapshot.Items.size()) +
                                   " cached catalog product(s); OAuth tokens and signed URLs remain in Hub.");
            }
            if (auto project = ui.BeginTabItem("In Project"); project)
                DrawInProject(ui, theme);
            if (auto updates = ui.BeginTabItem("Updates"); updates)
            {
                ui.Text("No catalog-backed updates are pending.");
                ui.TextColored(theme.MutedText,
                               "Embedded packages remain pinned until explicitly reverted to the Registry.");
            }
            if (auto local = ui.BeginTabItem("Local Packages"); local)
                DrawLocalPackages(ui, theme);
            if (auto builtIn = ui.BeginTabItem("Built-in"); builtIn)
            {
                for (const auto name : {"Kéire Core", "Rendering", "Physics", "Audio", "Managed Scripting"})
                    ui.Text(name);
                ui.TextColored(theme.MutedText,
                               "Built-in modules ship with this Editor version and are not marketplace content.");
            }
        }
        if (m_LastEvent.TotalBytes != 0U)
        {
            const auto progress =
                static_cast<float>(m_LastEvent.CompletedBytes) / static_cast<float>(m_LastEvent.TotalBytes);
            ui.ProgressBar(std::clamp(progress, 0.0F, 1.0F), {}, m_LastEvent.Message);
        }
        if (!m_Status.empty())
            ui.TextColored(theme.Success, m_Status);
        if (!m_Error.empty())
            ui.TextColored(theme.Error, m_Error);
        DrawImportReview(ui, theme);
    }
} // namespace KeireEditor
