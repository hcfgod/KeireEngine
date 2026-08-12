#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace KeireEditor
{
    struct ExternalEditorProfile
    {
        std::string Id;
        std::string DisplayName;
        std::filesystem::path Executable;
        bool Installed = false;
        bool SystemDefault = false;
    };

    [[nodiscard]] std::vector<ExternalEditorProfile> DiscoverExternalEditorProfiles();
} // namespace KeireEditor
