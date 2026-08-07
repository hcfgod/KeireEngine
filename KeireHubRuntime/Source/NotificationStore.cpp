#include "KeireHubRuntime/NotificationStore.h"

#include "Persistence.h"

#include <algorithm>
#include <array>
#include <ranges>

namespace KeireHub
{
    namespace
    {
        constexpr std::size_t MaximumStoreBytes = 2 * 1024 * 1024;
        constexpr std::size_t AbsoluteMaximumNotifications = 1024;

        [[nodiscard]] std::string_view ToString(const NotificationSeverity value) noexcept
        {
            constexpr std::array names{"info", "success", "warning", "error"};
            return names[static_cast<std::size_t>(value)];
        }

        [[nodiscard]] std::optional<NotificationSeverity> ParseSeverity(const std::string_view value) noexcept
        {
            constexpr std::array values{NotificationSeverity::Info, NotificationSeverity::Success,
                                        NotificationSeverity::Warning, NotificationSeverity::Error};
            for (const auto candidate : values)
            {
                if (ToString(candidate) == value)
                    return candidate;
            }
            return std::nullopt;
        }

        [[nodiscard]] HubStatus Validate(const HubNotification& notification)
        {
            if (notification.Severity < NotificationSeverity::Info ||
                notification.Severity > NotificationSeverity::Error || !Detail::IsBoundedIdentifier(notification.Id) ||
                notification.Title.empty() || notification.Title.size() > 256 || notification.Message.empty() ||
                notification.Message.size() > 4096 ||
                (notification.RelatedTaskId && !Detail::IsBoundedIdentifier(*notification.RelatedTaskId)) ||
                (notification.ActionRoute && notification.ActionRoute->size() > 1024))
            {
                return HubStatus::Failure({.Code = HubErrorCode::InvalidArgument,
                                           .Message = "The notification contains invalid metadata.",
                                           .AffectedItem = notification.Id});
            }
            return HubStatus::Success();
        }

        [[nodiscard]] Detail::Json Serialize(const std::vector<HubNotification>& notifications)
        {
            Detail::Json values = Detail::Json::array();
            for (const auto& notification : notifications)
            {
                Detail::Json value{{"id", notification.Id},
                                   {"severity", ToString(notification.Severity)},
                                   {"title", notification.Title},
                                   {"message", notification.Message},
                                   {"created", notification.CreatedUnixSeconds},
                                   {"read", notification.Read}};
                if (notification.RelatedTaskId)
                    value["relatedTaskId"] = *notification.RelatedTaskId;
                if (notification.ActionRoute)
                    value["actionRoute"] = *notification.ActionRoute;
                values.push_back(std::move(value));
            }
            return {{"schemaVersion", NotificationStore::CurrentSchemaVersion}, {"notifications", std::move(values)}};
        }

        [[nodiscard]] HubResult<std::vector<HubNotification>> Parse(const Detail::Json& document)
        {
            try
            {
                if (document.at("schemaVersion").get<std::uint32_t>() != NotificationStore::CurrentSchemaVersion)
                {
                    return HubResult<std::vector<HubNotification>>::Failure(
                        {.Code = HubErrorCode::UnsupportedSchema,
                         .Message = "This notification history uses an unsupported schema.",
                         .AffectedItem = "notifications"});
                }
                const auto& values = document.at("notifications");
                if (!values.is_array() || values.size() > AbsoluteMaximumNotifications)
                    throw std::invalid_argument("Invalid notification collection.");
                std::vector<HubNotification> result;
                result.reserve(values.size());
                for (const auto& value : values)
                {
                    HubNotification notification;
                    notification.Id = value.at("id").get<std::string>();
                    const auto severity = ParseSeverity(value.at("severity").get<std::string>());
                    if (!severity)
                        throw std::invalid_argument("Unknown notification severity.");
                    notification.Severity = *severity;
                    notification.Title = value.at("title").get<std::string>();
                    notification.Message = value.at("message").get<std::string>();
                    notification.CreatedUnixSeconds = value.at("created").get<std::uint64_t>();
                    notification.Read = value.value("read", false);
                    if (value.contains("relatedTaskId"))
                        notification.RelatedTaskId = value.at("relatedTaskId").get<std::string>();
                    if (value.contains("actionRoute"))
                        notification.ActionRoute = value.at("actionRoute").get<std::string>();
                    if (const auto status = Validate(notification); !status)
                        throw std::invalid_argument(status.Error().Message);
                    if (std::ranges::find(result, notification.Id, &HubNotification::Id) != result.end())
                        throw std::invalid_argument("Duplicate notification identity.");
                    result.push_back(std::move(notification));
                }
                return HubResult<std::vector<HubNotification>>::Success(std::move(result));
            }
            catch (const std::exception& error)
            {
                return HubResult<std::vector<HubNotification>>::Failure(
                    {.Code = HubErrorCode::InvalidData,
                     .Message = "The notification history is malformed.",
                     .AffectedItem = "notifications",
                     .TechnicalDetails = error.what()});
            }
        }

        void SortAndTrim(std::vector<HubNotification>& notifications, const std::size_t maximum)
        {
            std::ranges::sort(notifications,
                              [](const auto& left, const auto& right)
                              {
                                  if (left.CreatedUnixSeconds != right.CreatedUnixSeconds)
                                      return left.CreatedUnixSeconds > right.CreatedUnixSeconds;
                                  return left.Id > right.Id;
                              });
            if (notifications.size() > maximum)
                notifications.resize(maximum);
        }
    } // namespace

    NotificationStore::NotificationStore(std::filesystem::path storePath, const std::size_t maximumHistory)
        : m_Path(std::move(storePath)),
          m_MaximumHistory(std::clamp<std::size_t>(maximumHistory, 1, AbsoluteMaximumNotifications)),
          m_Snapshot(std::make_shared<const std::vector<HubNotification>>())
    {
    }

    HubStatus NotificationStore::Load()
    {
        if (!std::filesystem::exists(m_Path))
        {
            m_Snapshot = std::make_shared<const std::vector<HubNotification>>();
            return HubStatus::Success();
        }
        auto document = Detail::ReadJsonFile(m_Path, MaximumStoreBytes);
        if (!document)
        {
            if (document.Error().Code == HubErrorCode::InvalidData)
                (void)Detail::QuarantineCorruptFile(m_Path);
            return HubStatus::Failure(document.Error());
        }
        auto notifications = Parse(document.Value());
        if (!notifications)
        {
            if (notifications.Error().Code != HubErrorCode::UnsupportedSchema)
                (void)Detail::QuarantineCorruptFile(m_Path);
            return HubStatus::Failure(notifications.Error());
        }
        SortAndTrim(notifications.Value(), m_MaximumHistory);
        m_Snapshot = std::make_shared<const std::vector<HubNotification>>(std::move(notifications).Value());
        return HubStatus::Success();
    }

    HubStatus NotificationStore::Add(HubNotification notification)
    {
        if (const auto status = Validate(notification); !status)
            return status;
        auto notifications = *m_Snapshot;
        if (std::ranges::find(notifications, notification.Id, &HubNotification::Id) != notifications.end())
            return HubStatus::Failure({.Code = HubErrorCode::DuplicateIdentifier,
                                       .Message = "A notification with this identity already exists.",
                                       .AffectedItem = notification.Id});
        notifications.push_back(std::move(notification));
        return Commit(std::move(notifications));
    }

    HubStatus NotificationStore::MarkRead(const std::string& notificationId, const bool read)
    {
        auto notifications = *m_Snapshot;
        const auto found = std::ranges::find(notifications, notificationId, &HubNotification::Id);
        if (found == notifications.end())
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The notification is no longer available.",
                                       .AffectedItem = notificationId});
        found->Read = read;
        return Commit(std::move(notifications));
    }

    HubStatus NotificationStore::Remove(const std::string& notificationId)
    {
        auto notifications = *m_Snapshot;
        if (std::erase_if(notifications, [&](const auto& value) { return value.Id == notificationId; }) == 0)
            return HubStatus::Failure({.Code = HubErrorCode::NotFound,
                                       .Message = "The notification is no longer available.",
                                       .AffectedItem = notificationId});
        return Commit(std::move(notifications));
    }

    HubStatus NotificationStore::Clear() { return Commit({}); }

    std::shared_ptr<const std::vector<HubNotification>> NotificationStore::Snapshot() const noexcept
    {
        return m_Snapshot;
    }

    std::size_t NotificationStore::UnreadCount() const noexcept
    {
        return static_cast<std::size_t>(std::ranges::count(*m_Snapshot, false, &HubNotification::Read));
    }

    const std::filesystem::path& NotificationStore::Path() const noexcept { return m_Path; }

    HubStatus NotificationStore::Commit(std::vector<HubNotification> notifications)
    {
        SortAndTrim(notifications, m_MaximumHistory);
        if (auto status = Detail::WriteJsonFileAtomically(m_Path, Serialize(notifications)); !status)
            return status;
        m_Snapshot = std::make_shared<const std::vector<HubNotification>>(std::move(notifications));
        return HubStatus::Success();
    }
} // namespace KeireHub
