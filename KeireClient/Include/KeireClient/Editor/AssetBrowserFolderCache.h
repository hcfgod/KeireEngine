#pragma once

#include <filesystem>
#include <span>
#include <vector>

namespace KeireEditor
{
    class AssetBrowserFolderCache final
    {
      public:
        [[nodiscard]] bool Refresh(const std::filesystem::path& assetRoot) noexcept;
        [[nodiscard]] std::span<const std::filesystem::path> Folders() const noexcept { return m_Folders; }
        void Clear() noexcept { m_Folders.clear(); }

      private:
        std::vector<std::filesystem::path> m_Folders;
    };
} // namespace KeireEditor
