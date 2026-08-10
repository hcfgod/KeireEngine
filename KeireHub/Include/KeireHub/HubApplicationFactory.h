#pragma once

#include "KeireHub/HubInstance.h"

#include "Keire/Application.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace KeireHub
{
    enum class HubFolderTarget : std::uint8_t
    {
        None,
        CreateLocation,
        OpenProject,
        LocateEditor,
        EditorInstallLocation,
        DuplicateProject,
        LocateProject
    };

    [[nodiscard]] constexpr bool ShouldHideHubAfterEditorLaunch(const bool keepRunning, const bool trayAvailable,
                                                                const bool processTracked) noexcept
    {
        return keepRunning && trayAvailable && processTracked;
    }

    [[nodiscard]] constexpr bool ShouldRestoreHubAfterEditorExit(const bool hiddenForEditorLaunch,
                                                                 const bool hadTrackedEditors,
                                                                 const bool hasTrackedEditors) noexcept
    {
        return hiddenForEditorLaunch && hadTrackedEditors && !hasTrackedEditors;
    }

    [[nodiscard]] std::unique_ptr<Keire::Application>
    CreateHubApplication(Keire::ApplicationSpecification specification, std::filesystem::path executable, bool smoke,
                         std::optional<HubActivationRequest> pendingStartupActivation,
                         std::shared_ptr<HubInstanceCoordinator> instance);
} // namespace KeireHub
