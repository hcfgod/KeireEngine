#pragma once

#include "Keire/Project/ProjectAssetPackageImporter.h"

#include <string>

namespace KeireEditor
{
    struct PackageImportConfirmation final
    {
        std::string DisplayName;
        Keire::ProjectAssetImportRequest Request;
    };

    class PackageImportReview final
    {
      public:
        void Prepare(std::string displayName, Keire::ProjectAssetImportRequest request,
                     Keire::ProjectAssetImportPlan plan);
        void Cancel() noexcept;

        [[nodiscard]] bool Active() const noexcept { return m_Active; }
        [[nodiscard]] bool ConsumeOpenRequest() noexcept;
        [[nodiscard]] const std::string& DisplayName() const noexcept { return m_DisplayName; }
        [[nodiscard]] const Keire::ProjectAssetImportPlan& Plan() const noexcept { return m_Plan; }
        [[nodiscard]] PackageImportConfirmation Confirm();

      private:
        std::string m_DisplayName;
        Keire::ProjectAssetImportRequest m_Request;
        Keire::ProjectAssetImportPlan m_Plan;
        bool m_Active = false;
        bool m_OpenRequested = false;
    };
} // namespace KeireEditor
