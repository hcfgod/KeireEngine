#pragma once

#include "KeireHubRuntime/HubError.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace KeireHub
{
    enum class NotificationSeverity
    {
        Info,
        Success,
        Warning,
        Error
    };

    struct HubNotification final
    {
        std::string Id;
        NotificationSeverity Severity = NotificationSeverity::Info;
        std::string Title;
        std::string Message;
        std::uint64_t CreatedUnixSeconds = 0;
        bool Read = false;
        std::optional<std::string> RelatedTaskId;
        std::optional<std::string> ActionRoute;
    };

    class NotificationStore final
    {
      public:
        static constexpr std::uint32_t CurrentSchemaVersion = 1;

        explicit NotificationStore(std::filesystem::path storePath, std::size_t maximumHistory = 256);

        [[nodiscard]] HubStatus Load();
        [[nodiscard]] HubStatus Add(HubNotification notification);
        [[nodiscard]] HubStatus MarkRead(const std::string& notificationId, bool read = true);
        [[nodiscard]] HubStatus Remove(const std::string& notificationId);
        [[nodiscard]] HubStatus Clear();

        [[nodiscard]] std::shared_ptr<const std::vector<HubNotification>> Snapshot() const noexcept;
        [[nodiscard]] std::size_t UnreadCount() const noexcept;
        [[nodiscard]] const std::filesystem::path& Path() const noexcept;

      private:
        [[nodiscard]] HubStatus Commit(std::vector<HubNotification> notifications);

        std::filesystem::path m_Path;
        std::size_t m_MaximumHistory;
        std::shared_ptr<const std::vector<HubNotification>> m_Snapshot;
    };
} // namespace KeireHub
