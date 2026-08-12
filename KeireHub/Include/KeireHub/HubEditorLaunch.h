#pragma once

#include "KeireHub/HubProductUi.h"

#include "KeireHubRuntime/EditorProcessTracker.h"
#include "KeireHubRuntime/HubError.h"

#include "Keire/Project/Project.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace KeireHub
{
    // Windows blocks Explorer drag-and-drop into a higher-integrity process. An Editor inherited from an elevated Hub
    // would appear healthy while silently losing external asset drops, so the desktop launch must remain unelevated.
    [[nodiscard]] constexpr bool EditorLaunchSupportsExternalFileDrop(const bool processElevated) noexcept
    {
        return !processElevated;
    }

    struct HubSelectedEditor final
    {
        std::string InstallationId;
        std::filesystem::path Executable;
    };

    [[nodiscard]] HubResult<HubSelectedEditor> SelectEditorForProject(std::span<const HubEditorUiRecord> editors,
                                                                      const Keire::ProjectDescriptor& project,
                                                                      std::string_view preferredInstallationId = {});
    [[nodiscard]] HubResult<HubSelectedEditor> SelectEditorForProject(std::span<const HubEditorUiRecord> editors,
                                                                      const Keire::ProjectInspectionResult& inspection,
                                                                      std::string_view preferredInstallationId = {});

    struct HubProjectLaunchResult final
    {
        Keire::ProjectDescriptor Descriptor;
        std::string InstallationId;
        std::uint64_t ProcessId = 0;
        std::optional<HubError> TrackingFailure;
    };

    [[nodiscard]] HubResult<HubProjectLaunchResult>
    LaunchProjectEditor(std::span<const HubEditorUiRecord> editors, EditorProcessTracker& processes,
                        const Keire::ProjectInspectionResult& inspection, std::string_view preferredInstallationId,
                        bool requirePreferred, std::uint64_t nowUnixSeconds);
} // namespace KeireHub
