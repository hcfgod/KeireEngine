#pragma once

#include "KeireHubRuntime/EditorInstallationRegistry.h"
#include "KeireHubRuntime/HubController.h"

#include <filesystem>
#include <optional>
#include <string>

namespace KeireHub
{
    [[nodiscard]] HubResult<EditorInstallation> InspectExternalEditor(std::filesystem::path selected,
                                                                      std::string installationId);
    [[nodiscard]] HubResult<EditorInstallation>
    RegisterExternalEditor(HubController& controller, std::filesystem::path selected, std::string installationId);
    [[nodiscard]] HubResult<std::optional<EditorInstallation>>
    RegisterPackagedEditorIfPresent(HubController& controller, const std::filesystem::path& packageRoot);
} // namespace KeireHub
