#pragma once

#include "KeireHubRuntime/CatalogModels.h"
#include "KeireHubRuntime/EditorInstallationRegistry.h"
#include "KeireHubRuntime/HubError.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub
{
    struct LicenseCatalogSource final
    {
        std::filesystem::path CatalogPath;
        std::filesystem::path ContentRoot;
    };

    struct ResolvedLicenseEntry final
    {
        std::string Id;
        std::string DisplayName;
        std::string Group;
        LicenseScope Scope = LicenseScope::Hub;
        std::optional<std::string> PackageId;
        std::optional<std::string> Version;
        std::filesystem::path SourcePath;
        std::string Text;
    };

    class LicenseCatalog final
    {
      public:
        explicit LicenseCatalog(std::filesystem::path hubRoot, std::vector<LicenseCatalogSource> sources = {});

        [[nodiscard]] HubStatus Load();
        [[nodiscard]] std::shared_ptr<const std::vector<ResolvedLicenseEntry>> Snapshot() const noexcept;
        [[nodiscard]] std::vector<std::size_t> Search(std::string_view query) const;

        [[nodiscard]] const std::filesystem::path& HubRoot() const noexcept;

      private:
        std::filesystem::path m_HubRoot;
        std::vector<LicenseCatalogSource> m_Sources;
        std::shared_ptr<const std::vector<ResolvedLicenseEntry>> m_Snapshot;
    };

    // Resolves only license files declared by the immutable installed-package receipt snapshot. File size and digest
    // are checked again before text is exposed to the UI so a damaged installation never presents altered notices.
    [[nodiscard]] HubResult<std::vector<ResolvedLicenseEntry>>
    ResolveInstalledPackageLicenses(std::span<const EditorInstallation> installations);
} // namespace KeireHub
