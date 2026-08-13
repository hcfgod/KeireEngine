#include "KeireClient/Editor/PackageImportReview.h"

#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    void PackageImportReview::Prepare(std::string displayName, Keire::ProjectAssetImportRequest request,
                                      Keire::ProjectAssetImportPlan plan)
    {
        if (displayName.empty() || request.Archive.empty())
            throw std::invalid_argument("An asset-package import review requires a package name and archive.");
        m_DisplayName = std::move(displayName);
        m_Request = std::move(request);
        m_Plan = std::move(plan);
        m_Active = true;
        m_OpenRequested = true;
    }

    void PackageImportReview::Cancel() noexcept
    {
        m_DisplayName.clear();
        m_Request = {};
        m_Plan = {};
        m_Active = false;
        m_OpenRequested = false;
    }

    bool PackageImportReview::ConsumeOpenRequest() noexcept
    {
        const bool requested = m_OpenRequested;
        m_OpenRequested = false;
        return requested;
    }

    PackageImportConfirmation PackageImportReview::Confirm()
    {
        if (!m_Active)
            throw std::logic_error("No asset-package import is awaiting review.");
        if (!m_Plan.Valid())
            throw std::logic_error("An asset-package import with unresolved conflicts cannot be confirmed.");
        PackageImportConfirmation result{std::move(m_DisplayName), std::move(m_Request)};
        Cancel();
        return result;
    }
} // namespace KeireEditor
