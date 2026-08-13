#pragma once

#include "Keire/Core.h"

#include "KeireHubRuntime/CatalogClient.h"
#include "KeireHubRuntime/MarketplaceCache.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace KeireEditor
{
    class PackageManagerPanel final
    {
      public:
        PackageManagerPanel() = default;
        ~PackageManagerPanel();

        PackageManagerPanel(const PackageManagerPanel&) = delete;
        PackageManagerPanel& operator=(const PackageManagerPanel&) = delete;

        void Attach(Keire::UiWorkspace& workspace);
        void Initialize(const std::filesystem::path& projectRoot, const std::filesystem::path& executable);
        void Shutdown() noexcept;
        void Draw(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme);
        [[nodiscard]] Keire::UiPanelRegistration& Registration() noexcept { return m_Registration; }

      private:
        void Refresh();
        void InspectLocalPackage();
        void InstallLocalPackage();
        void ImportLocalAssetPackage();
        void RemoveSelected();
        void EmbedSelected();
        void RevertSelected();
        void RefreshMarketplaceCache(bool focusRequestedProduct);
        void InstallMarketplacePackage(const KeireHub::MarketplaceCacheItem& item);
        void ImportMarketplacePackage(const KeireHub::MarketplaceCacheItem& item);
        void DrawMarketplaceLibrary(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme);
        void DrawInProject(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme);
        void DrawLocalPackages(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme);

        Keire::UiPanelRegistration m_Registration;
        std::unique_ptr<Keire::ProjectPackageManager> m_Manager;
        std::unique_ptr<Keire::ProjectAssetPackageImporter> m_AssetImporter;
        std::unique_ptr<KeireHub::MarketplaceCacheStore> m_MarketplaceCache;
        std::unique_ptr<KeireHub::CatalogTrustStore> m_MarketplaceTrust;
        KeireHub::MarketplaceCacheSnapshot m_MarketplaceSnapshot;
        Keire::ProjectPackageManifest m_Manifest;
        Keire::ProjectPackageLock m_Lock;
        std::string m_SelectedPackage;
        std::string m_LocalArchive;
        std::string m_LocalSearch;
        std::string m_SelectedMarketplaceProduct;
        std::string m_Status;
        std::string m_Error;
        Keire::ProjectPackageEvent m_LastEvent;
        std::optional<Keire::AssetPackageArchiveMetadata> m_LocalMetadata;
        bool m_AllowExecutableCode = false;
        bool m_KeepLocalConflicts = true;
        std::chrono::steady_clock::time_point m_NextMarketplaceRefresh{};
    };
} // namespace KeireEditor
