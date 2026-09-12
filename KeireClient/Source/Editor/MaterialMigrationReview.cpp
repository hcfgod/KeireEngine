#include "KeireClient/Editor/MaterialMigrationReview.h"

#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    void MaterialMigrationReview::Refresh(const std::filesystem::path& root, const Inspector& inspect)
    {
        Clear();
        const auto normalized = std::filesystem::weakly_canonical(root);
        auto report = inspect(normalized);
        m_Root = normalized;
        m_Report = std::move(report);
    }

    Keire::ShaderGraphMigrationReport MaterialMigrationReview::Apply(const std::filesystem::path& root,
                                                                     const Publisher& publish)
    {
        if (!CanApply())
            throw std::logic_error("Review a valid material conversion before applying it.");
        auto report = std::move(*m_Report);
        const auto reviewedRoot = m_Root;
        Clear();
        if (std::filesystem::weakly_canonical(root) != reviewedRoot)
            throw std::logic_error("The project changed since material conversion was reviewed.");
        return publish(reviewedRoot, report);
    }

    void MaterialMigrationReview::Clear() noexcept
    {
        m_Report.reset();
        m_Root.clear();
    }

    const std::optional<Keire::ShaderGraphMigrationReport>& MaterialMigrationReview::Report() const noexcept
    {
        return m_Report;
    }

    bool MaterialMigrationReview::CanApply() const noexcept
    {
        return m_Report && !m_Report->ReviewFingerprint.empty() && m_Report->CanApply() &&
               m_Report->PendingCount() != 0;
    }
} // namespace KeireEditor
