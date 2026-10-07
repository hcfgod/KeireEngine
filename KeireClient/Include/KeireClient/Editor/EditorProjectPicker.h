#pragma once

#include <filesystem>
#include <functional>
#include <span>
#include <string>

namespace KeireEditor
{
    using ProjectPickerLauncher = std::function<bool(const std::filesystem::path&, std::span<const std::string>,
                                                     const std::filesystem::path&, std::string&)>;
    using ProjectPickerRegistration = std::function<std::filesystem::path()>;

    void OpenEditorProjectPicker(const std::filesystem::path& editorExecutable,
                                 const ProjectPickerLauncher& launcher = {},
                                 const ProjectPickerRegistration& registration = {});
} // namespace KeireEditor
