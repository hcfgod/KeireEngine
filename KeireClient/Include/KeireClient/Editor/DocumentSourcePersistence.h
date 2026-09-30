#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace KeireEditor
{
    class DocumentSourceConflict final : public std::runtime_error
    {
      public:
        DocumentSourceConflict();
    };

    /// Owns the source revision independently of document, selection, undo, and workspace lifetimes.
    class DocumentSourcePersistence final
    {
      public:
        void Bind(std::filesystem::path path, std::optional<std::vector<std::byte>> loadedBytes = std::nullopt);
        void Publish(std::vector<std::byte> bytes, bool overwriteExternalChanges = false);
        [[nodiscard]] const std::optional<std::vector<std::byte>>& LoadedBytes() const noexcept { return m_Expected; }
        void DismissConflict() noexcept { m_Conflict = false; }
        void Relocate(std::filesystem::path path) { m_Path = std::move(path); }
        [[nodiscard]] bool Conflict() const noexcept { return m_Conflict; }

      private:
        std::filesystem::path m_Path;
        std::optional<std::vector<std::byte>> m_Expected;
        bool m_Conflict = false;
    };
} // namespace KeireEditor
