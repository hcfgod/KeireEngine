#pragma once

#include "KeireHubRuntime/EditorInstallationManager.h"

#include <filesystem>

namespace KeireHub::Detail
{
    [[nodiscard]] EditorEntrypointActivity
    ProbeEditorEntrypointActivity(const std::filesystem::path& executable) noexcept;
} // namespace KeireHub::Detail
