#pragma once

#include "KeireHubRuntime/EditorInstallationRegistry.h"
#include "KeireHubRuntime/HubError.h"

#include <cstdint>
#include <span>
#include <string>

namespace KeireHub
{
    struct EditorSelectionRequest final
    {
        std::string PreferredInstallationId;
        std::string LastSavedVersion;
        std::string MinimumVersion;
        std::uint32_t ProjectSchema = 1;
    };

    [[nodiscard]] HubResult<EditorInstallation> SelectCompatibleEditor(std::span<const EditorInstallation> editors,
                                                                       const EditorSelectionRequest& request);
} // namespace KeireHub
