#include "KeireClient/Editor/PackageManagerPanel.h"

#include "Keire/BuildInfo.h"
#include "Keire/PlatformDirectories.h"

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <ranges>
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
    } // namespace

    PackageManagerPanel::~PackageManagerPanel() = default;

    void PackageManagerPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.package-manager", "Package Manager", false});
    }

    void PackageManagerPanel::Initialize(const std::filesystem::path& projectRoot)
    {
        Shutdown();
        try
        {
            Keire::ProjectPackageManagerSpecification specification{
                .ProjectRoot = projectRoot,
                .GlobalCacheRoot = Keire::GetPreferenceDirectory() / "Hub" / "MarketplacePackages",
                .EngineVersion = std::string(Keire::GetBuildInfo().Version),
                .Platform = std::string(HostPlatform()),
                .Architecture = std::string(HostArchitecture()),
                .RendererCapabilities = {"surface", "compute"},
                .Events = [this](const Keire::ProjectPackageEvent& event) { m_LastEvent = event; }};
            m_Manager = std::make_unique<Keire::ProjectPackageManager>(std::move(specification));
            m_AssetImporter =
                std::make_unique<Keire::ProjectAssetPackageImporter>(Keire::ProjectAssetPackageImporterSpecification{
                    .ProjectRoot = projectRoot,
                    .EngineVersion = std::string(Keire::GetBuildInfo().Version),
                    .Platform = std::string(HostPlatform()),
                    .Architecture = std::string(HostArchitecture()),
                    .RendererCapabilities = {"surface", "compute"}});
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
        m_Manifest = {};
        m_Lock = {};
        m_SelectedPackage.clear();
        m_LocalArchive.clear();
        m_LocalSearch.clear();
        m_Status.clear();
        m_Error.clear();
        m_LastEvent = {};
        m_LocalMetadata.reset();
        m_AllowExecutableCode = false;
        m_KeepLocalConflicts = true;
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
                ui.Text("Your personal and organization libraries are delivered by Kéire Hub.");
                ui.TextColored(theme.MutedText,
                               "Hub authorization and the local broker keep OAuth tokens outside the Editor.");
            }
            if (auto registry = ui.BeginTabItem("Kéire Registry"); registry)
            {
                ui.Text("Registry discovery is connected through Kéire Hub's verified marketplace cache.");
                ui.TextColored(theme.MutedText,
                               "Only compatible, entitled, signed versions become installable project packages.");
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
    }
} // namespace KeireEditor
