#pragma once

#include "Keire/Project/ShaderGraphMigration.h"

#include <filesystem>
#include <functional>
#include <optional>

namespace KeireEditor
{
    class MaterialMigrationReview final
    {
      public:
        using Inspector = std::function<Keire::ShaderGraphMigrationReport(const std::filesystem::path&)>;
        using Publisher = std::function<Keire::ShaderGraphMigrationReport(const std::filesystem::path&,
                                                                          const Keire::ShaderGraphMigrationReport&)>;

        /// A failed refresh also discards the previous approval: it cannot authorize a different project state.
        void Refresh(const std::filesystem::path& root, const Inspector& inspect);
        /// Approval is consumed before publication, including failed attempts; retry requires another review.
        [[nodiscard]] Keire::ShaderGraphMigrationReport Apply(const std::filesystem::path& root,
                                                              const Publisher& publish);
        void Clear() noexcept;
        [[nodiscard]] const std::optional<Keire::ShaderGraphMigrationReport>& Report() const noexcept;
        [[nodiscard]] bool CanApply() const noexcept;

      private:
        std::filesystem::path m_Root;
        std::optional<Keire::ShaderGraphMigrationReport> m_Report;
    };
} // namespace KeireEditor
