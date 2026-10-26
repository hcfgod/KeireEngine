#include "KeireHubInternal/HubErrorNotice.h"

#include "Keire/Assets/Asset.h"
#include "Keire/Log.h"
#include "KeireHub/HubRuntimeUiBridge.h"
#include "KeireHubRuntime/HubController.h"

#include <cstdio>

namespace KeireHub::Detail
{
    void PublishHubErrorNotice(HubController* controller, const std::string& message, std::string& notice,
                               bool& noticeError, std::string& lastPersistedError) noexcept
    {
        try
        {
            notice = message;
            noticeError = true;
        }
        catch (...)
        {
        }
        if (controller && message != lastPersistedError)
        {
            try
            {
                const auto status =
                    controller->Notifications().Add({.Id = "hub-error-" + Keire::AssetId::Generate().ToString(),
                                                     .Severity = KeireHub::NotificationSeverity::Error,
                                                     .Title = "Hub action needs attention",
                                                     .Message = message,
                                                     .CreatedUnixSeconds = KeireHub::HubNowUnixSeconds()});
                if (status)
                {
                    lastPersistedError = message;
                }
                else
                {
                    KEIRE_CLIENT_WARN("[Project Hub] Error notification could not be persisted: {}",
                                      status.Error().Message);
                }
            }
            catch (...)
            {
            }
        }
        try
        {
            KEIRE_CLIENT_ERROR("[Project Hub] {}", message);
        }
        catch (...)
        {
            std::fprintf(stderr, "[Project Hub] %s\n", message.c_str());
        }
    }
} // namespace KeireHub::Detail
