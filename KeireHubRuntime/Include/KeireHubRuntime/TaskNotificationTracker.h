#pragma once

#include "KeireHubRuntime/HubTaskStore.h"
#include "KeireHubRuntime/NotificationStore.h"

#include <cstdint>
#include <map>
#include <span>
#include <string>

namespace KeireHub
{
    class TaskNotificationTracker final
    {
      public:
        [[nodiscard]] HubStatus Observe(std::span<const HubTask> tasks, NotificationStore& notifications,
                                        std::uint64_t nowUnixSeconds);

      private:
        std::map<std::string, HubTaskState, std::less<>> m_States;
        bool m_Initialized = false;
    };
} // namespace KeireHub
