#pragma once

#include "KeireHubRuntime/HubActivation.h"

#include <filesystem>
#include <memory>
#include <optional>

namespace KeireHub
{
    class HubInstanceCoordinator final
    {
      public:
        HubInstanceCoordinator(const std::filesystem::path& executable, const HubActivationRequest& activation,
                               bool coordinate);
        ~HubInstanceCoordinator();

        HubInstanceCoordinator(const HubInstanceCoordinator&) = delete;
        HubInstanceCoordinator& operator=(const HubInstanceCoordinator&) = delete;

        [[nodiscard]] bool IsPrimary() const noexcept;
        [[nodiscard]] std::optional<HubActivationRequest> PollActivation();

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace KeireHub
