#pragma once

#include "Keire/Project/Project.h"
#include "Keire/Ui.h"
#include "Keire/Window.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace KeireHub
{
    enum class HubProjectOpenAction : std::uint8_t
    {
        Unavailable,
        OpenWithEditor,
        ReviewUpgrade,
        FindCompatibleEditor
    };

    [[nodiscard]] std::string Utf8Path(const std::filesystem::path& path);
    [[nodiscard]] std::string Lower(std::string value);
    [[nodiscard]] const char* StatusLabel(Keire::ProjectStatus status) noexcept;
    [[nodiscard]] Keire::UiColor StatusColor(Keire::ProjectStatus status) noexcept;
    [[nodiscard]] bool CanOpenOrUpgrade(Keire::ProjectStatus status) noexcept;
    [[nodiscard]] HubProjectOpenAction ResolveProjectOpenAction(Keire::ProjectStatus status,
                                                                bool hasCompatibleEditor) noexcept;
    [[nodiscard]] std::string FormatLastOpened(std::uint64_t seconds);
    [[nodiscard]] std::filesystem::path ResolveEditorExecutable(const std::filesystem::path& hubExecutable);
    [[nodiscard]] std::filesystem::path ResolveAssetToolExecutable(const std::filesystem::path& hubExecutable);
    void ReloadProjectRegistry(Keire::Ref<Keire::ProjectRegistry>& registry);
    void UpdateHubChromeLayout(Keire::Window& window, Keire::LogicalExtent size, bool reserveProductControls = true);
} // namespace KeireHub
