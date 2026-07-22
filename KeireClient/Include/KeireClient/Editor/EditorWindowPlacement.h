#pragma once

#include "Keire/Window.h"

#include <filesystem>
#include <optional>
#include <span>

namespace KeireEditor
{
    struct EditorWindowPlacement
    {
        Keire::WindowPosition Position{80, 80};
        Keire::LogicalExtent WindowedSize{1280, 720};
        Keire::WindowMode Mode = Keire::WindowMode::Windowed;
        bool Maximized = false;
    };

    [[nodiscard]] std::optional<EditorWindowPlacement>
    LoadEditorWindowPlacement(const std::filesystem::path& path) noexcept;
    [[nodiscard]] bool SaveEditorWindowPlacement(const std::filesystem::path& path,
                                                 const EditorWindowPlacement& placement) noexcept;
    void PrepareEditorWindow(const EditorWindowPlacement& placement, Keire::WindowSpecification& specification);
    [[nodiscard]] EditorWindowPlacement
    CorrectEditorWindowPlacement(const EditorWindowPlacement& placement,
                                 std::span<const Keire::DisplayInformation> displays,
                                 Keire::LogicalExtent minimumSize = {640, 480});
} // namespace KeireEditor
