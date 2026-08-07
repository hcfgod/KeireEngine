#include "KeireHub/HubWorkflowError.h"

#include "Keire/Core.h"

#include <stdexcept>

namespace KeireHub
{
    void RequireWorkflowSuccess(const HubStatus& status)
    {
        if (status)
            return;
        if (!status.Error().TechnicalDetails.empty())
        {
            KEIRE_CLIENT_ERROR("[Project Hub] Hub operation failed [{}]: {}", ToString(status.Error().Code),
                               status.Error().TechnicalDetails);
        }
        throw std::runtime_error(status.Error().Message);
    }
} // namespace KeireHub
