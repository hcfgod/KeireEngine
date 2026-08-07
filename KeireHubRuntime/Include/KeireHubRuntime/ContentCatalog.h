#pragma once

#include "KeireHubRuntime/CatalogModels.h"
#include "KeireHubRuntime/HubError.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace KeireHub
{
    struct ResolvedContentItem final
    {
        HubContentItem Metadata;
        std::optional<std::filesystem::path> LocalFile;
        std::optional<std::filesystem::path> ThumbnailFile;
    };

    struct ContentCatalogSnapshot final
    {
        std::string Locale = "en-US";
        std::vector<ResolvedContentItem> Learn;
        std::vector<ResolvedContentItem> Resources;
    };

    class ContentCatalog final
    {
      public:
        ContentCatalog(std::filesystem::path catalogPath, std::filesystem::path contentRoot);

        [[nodiscard]] HubStatus Load();
        [[nodiscard]] std::shared_ptr<const ContentCatalogSnapshot> Snapshot() const noexcept;

        [[nodiscard]] const std::filesystem::path& CatalogPath() const noexcept;
        [[nodiscard]] const std::filesystem::path& ContentRoot() const noexcept;

      private:
        std::filesystem::path m_CatalogPath;
        std::filesystem::path m_ContentRoot;
        std::shared_ptr<const ContentCatalogSnapshot> m_Snapshot;
    };
} // namespace KeireHub
