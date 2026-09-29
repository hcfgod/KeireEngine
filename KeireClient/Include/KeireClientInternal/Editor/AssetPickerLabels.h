#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace KeireEditor::Detail
{
    struct AssetPickerLabels final
    {
        std::string Preview;
        std::string Full;
    };

    [[nodiscard]] inline AssetPickerLabels MakeAssetPickerLabels(const std::filesystem::path& path,
                                                                 const std::string_view generatedName = {})
    {
        const auto full = path.generic_string();
        const auto file = path.filename().generic_string();
        if (generatedName.empty())
            return {file.empty() ? full : file, full};
        return {std::string(generatedName) + " (" + file + ")", full + " / " + std::string(generatedName)};
    }
} // namespace KeireEditor::Detail
