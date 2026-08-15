#pragma once

#include "KeireHubRuntime/EditorInstallationManager.h"

#include <cstdint>
#include <filesystem>

namespace KeireHub::Detail
{
    [[nodiscard]] EditorEntrypointActivity
    ProbeEditorEntrypointActivity(const std::filesystem::path& executable) noexcept;
    [[nodiscard]] EditorEntrypointActivity ProbeEditorProcessActivity(std::uint64_t processId,
                                                                      const std::filesystem::path& executable) noexcept;
} // namespace KeireHub::Detail
