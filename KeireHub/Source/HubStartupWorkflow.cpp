#include "KeireHub/HubStartupWorkflow.h"

#include "Keire/Log.h"

#include <string>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] Keire::LogLevel ResolveLogLevel(const std::string_view level) noexcept
        {
            if (level == "trace")
                return Keire::LogLevel::Trace;
            if (level == "debug")
                return Keire::LogLevel::Debug;
            if (level == "warning")
                return Keire::LogLevel::Warn;
            if (level == "error")
                return Keire::LogLevel::Error;
            return Keire::LogLevel::Info;
        }
    } // namespace

    HubStatus PrepareHubStartupRuntime(HubController& controller, const std::string_view hubVersion,
                                       const std::string_view configuredLogLevel, const std::uint64_t nowUnixSeconds)
    {
        Keire::Log::SetLevel(ResolveLogLevel(configuredLogLevel));
        auto resumed = controller.Updates().Reconcile(hubVersion);
        if (!resumed)
            return HubStatus::Failure(resumed.Error());
        if (resumed.Value().State == HubUpdateResumeState::None)
            return HubStatus::Success();
        const auto updated = resumed.Value().State == HubUpdateResumeState::Updated;
        return controller.Notifications().Add(
            {.Id = "hub-update-" + std::to_string(nowUnixSeconds),
             .Severity = updated ? NotificationSeverity::Success : NotificationSeverity::Warning,
             .Title = updated ? "Hub updated" : "Hub update needs attention",
             .Message = resumed.Value().Message,
             .CreatedUnixSeconds = nowUnixSeconds});
    }
} // namespace KeireHub
