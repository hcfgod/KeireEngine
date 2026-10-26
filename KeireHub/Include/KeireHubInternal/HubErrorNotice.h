#pragma once

#include <string>

namespace KeireHub
{
    class HubController;

    namespace Detail
    {
        // Synchronous, non-owning controller access. Failure to persist must not hide the visible error.
        void PublishHubErrorNotice(HubController* controller, const std::string& message, std::string& notice,
                                   bool& noticeError, std::string& lastPersistedError) noexcept;
    } // namespace Detail
} // namespace KeireHub
