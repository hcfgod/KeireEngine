#pragma once

#include "Keire/Assets/Asset.h"

#include <cstdint>
#include <filesystem>

namespace KeireEditor
{
    struct EditorSessionState final
    {
        Keire::AssetId LastScene;
        bool MaximizeGameOnPlay = false;
        std::uint8_t PresentMode = 255; // Unset preserves the application default.

        [[nodiscard]] bool operator==(const EditorSessionState&) const = default;
    };

    [[nodiscard]] EditorSessionState LoadEditorSessionState(const std::filesystem::path& path) noexcept;
    [[nodiscard]] bool SaveEditorSessionState(const std::filesystem::path& path,
                                              const EditorSessionState& state) noexcept;
    [[nodiscard]] bool SaveEditorSessionPresentMode(const std::filesystem::path& path, std::uint8_t mode) noexcept;
    // Preference updates preserve the last opened scene even after the live document has closed.
    [[nodiscard]] bool SaveEditorSessionViewPreference(const std::filesystem::path& path,
                                                       bool maximizeGameOnPlay) noexcept;
} // namespace KeireEditor
