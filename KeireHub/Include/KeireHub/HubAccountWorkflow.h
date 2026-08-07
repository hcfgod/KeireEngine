#pragma once

#include "KeireHub/HubProductUi.h"

#include "KeireHubRuntime/AccountSessionStore.h"
#include "KeireHubRuntime/SupabaseAccount.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace KeireHub
{
    using HubAccountClientFactory =
        std::function<HubResult<SupabaseAccountClient>(const SupabaseConfiguration&, const HubSettings&)>;

    struct HubAccountWorkflowSnapshot final
    {
        bool Configured = false;
        bool Busy = false;
        bool SignedIn = false;
        bool PersistentSessionAvailable = false;
        bool ConfirmationRequired = false;
        std::string UserId;
        std::string Email;
        std::string DisplayName;
        std::string AvatarUrl;
        std::string Message;
        std::uint64_t ExpiresAtUnixSeconds = 0;
        std::optional<HubError> Failure;
    };

    class HubAccountWorkflow final
    {
      public:
        explicit HubAccountWorkflow(HubAccountClientFactory clientFactory = {});
        ~HubAccountWorkflow();

        HubAccountWorkflow(const HubAccountWorkflow&) = delete;
        HubAccountWorkflow& operator=(const HubAccountWorkflow&) = delete;

        [[nodiscard]] HubStatus Start(const std::filesystem::path& configurationPath,
                                      const std::filesystem::path& sessionPath, const HubSettings& settings);
        [[nodiscard]] HubStatus SignIn(std::string email, std::string password);
        [[nodiscard]] HubStatus SignUp(std::string email, std::string password);
        [[nodiscard]] HubStatus SignOut();
        [[nodiscard]] HubStatus SaveProfile(std::string displayName);
        void RefreshIfNeeded(std::uint64_t nowUnixSeconds);
        void Stop() noexcept;

        [[nodiscard]] std::shared_ptr<const HubAccountWorkflowSnapshot> Snapshot() const;

      private:
        [[nodiscard]] HubStatus Begin(std::function<void()> operation);
        [[nodiscard]] HubResult<SupabaseAccountClient> CreateClient() const;
        void Publish(HubAccountWorkflowSnapshot snapshot);
        void PublishSessionFailure(HubError error);
        void PublishSession(AccountSession session, AccountProfile profile, std::string message);

        mutable std::mutex m_Mutex;
        std::shared_ptr<const HubAccountWorkflowSnapshot> m_Snapshot;
        std::optional<SupabaseConfiguration> m_Configuration;
        std::optional<AccountSession> m_Session;
        std::filesystem::path m_SessionPath;
        HubSettings m_Settings;
        HubAccountClientFactory m_ClientFactory;
        std::uint64_t m_NextRefreshAttemptUnixSeconds = 0;
        std::jthread m_Worker;
    };

    void ApplyHubAccountSnapshot(const HubAccountWorkflowSnapshot& account, HubProductSnapshot& product);
} // namespace KeireHub
