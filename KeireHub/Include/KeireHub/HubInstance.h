#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace KeireHub
{
    struct HubActivationRequest final
    {
        std::optional<std::string> Platform;
        std::optional<std::string> Architecture;

        [[nodiscard]] bool RequestsBuildSupport() const noexcept
        {
            return Platform.has_value() && Architecture.has_value();
        }
    };

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
