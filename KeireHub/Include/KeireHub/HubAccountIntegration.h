#pragma once

#include "KeireHub/HubAccountWorkflow.h"

#include <cstdint>
#include <filesystem>

namespace KeireHub
{
    class HubAccountIntegration final
    {
      public:
        [[nodiscard]] HubStatus Start(const std::filesystem::path& configurationPath,
                                      const std::filesystem::path& sessionPath, const HubSettings& settings);
        void Stop() noexcept;
        void RequestRefresh() noexcept;
        [[nodiscard]] HubStatus Tick(const HubSettings& settings, std::uint64_t nowUnixSeconds);
        void ApplySnapshot(HubProductSnapshot& product) const;
        [[nodiscard]] HubStatus Execute(const HubUiCommand& command);

      private:
        HubAccountWorkflow m_Workflow;
        std::filesystem::path m_ConfigurationPath;
        std::filesystem::path m_SessionPath;
        bool m_RefreshPending = false;
    };
} // namespace KeireHub
