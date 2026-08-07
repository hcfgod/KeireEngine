#pragma once

#include "Keire/Ui.h"

#include "KeireHub/HubProjectUpgradeWorkflow.h"

#include <filesystem>
#include <optional>
#include <span>

namespace KeireHub
{
    enum class HubProjectUpgradeAction
    {
        None,
        Refresh,
        Reopen
    };

    struct HubProjectUpgradeUiResult final
    {
        HubProjectUpgradeAction Action = HubProjectUpgradeAction::None;
        std::filesystem::path Root;
        std::optional<HubError> Failure;
    };

    class HubProjectUpgradeUi final
    {
      public:
        HubProjectUpgradeUi();

        void Begin(const std::filesystem::path& root, std::span<const Keire::ProjectUpgradeStep> upgrades);
        [[nodiscard]] HubProjectUpgradeUiResult Draw(Keire::UiFrame& ui);

      private:
        HubProjectUpgradeWorkflow m_Workflow;
        std::optional<HubError> m_StartFailure;
    };
} // namespace KeireHub
