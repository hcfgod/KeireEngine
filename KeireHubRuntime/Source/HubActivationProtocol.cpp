#include "KeireHubRuntime/HubActivationProtocol.h"

#include <exception>
#include <utility>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] HubError ProtocolError(std::string message, std::string details = {})
        {
            return {.Code = HubErrorCode::InvalidArgument,
                    .Message = std::move(message),
                    .AffectedItem = "keirehub-protocol",
                    .TechnicalDetails = std::move(details)};
        }

        [[nodiscard]] std::string Utf8Path(const std::filesystem::path& path)
        {
            const auto value = path.generic_u8string();
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }
    } // namespace

    HubResult<HubActivationProtocolRegistration> PlanHubActivationProtocolRegistration(std::filesystem::path executable)
    {
        try
        {
            if (executable.empty() || !executable.is_absolute())
            {
                return HubResult<HubActivationProtocolRegistration>::Failure(
                    ProtocolError("The Hub executable path must be absolute."));
            }
            executable = executable.lexically_normal();
            const auto encoded = Utf8Path(executable);
            if (encoded.empty() || encoded.find('\0') != encoded.npos || encoded.find('"') != encoded.npos)
            {
                return HubResult<HubActivationProtocolRegistration>::Failure(
                    ProtocolError("The Hub executable path cannot be registered as a URL handler."));
            }
            return HubResult<HubActivationProtocolRegistration>::Success({.Executable = std::move(executable),
                                                                          .Description = "URL:Kéire Hub Protocol",
                                                                          .Icon = '"' + encoded + "\",0",
                                                                          .Command = '"' + encoded + "\" \"%1\""});
        }
        catch (const std::exception& exception)
        {
            return HubResult<HubActivationProtocolRegistration>::Failure(
                ProtocolError("The Hub executable path cannot be registered as a URL handler.", exception.what()));
        }
    }

#if !defined(_WIN32)
    HubStatus EnsureHubActivationProtocolRegistration(const std::filesystem::path& executable)
    {
        auto registration = PlanHubActivationProtocolRegistration(executable);
        return registration ? HubStatus::Success() : HubStatus::Failure(registration.Error());
    }
#endif
} // namespace KeireHub
