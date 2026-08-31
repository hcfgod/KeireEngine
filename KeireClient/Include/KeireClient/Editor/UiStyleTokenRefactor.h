#pragma once

#include "Keire/Assets/Asset.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    struct UiStyleTokenRefactorInput final
    {
        Keire::AssetId Asset;
        Keire::AssetTypeId Type;
        std::filesystem::path RelativePath;
    };

    struct UiStyleTokenRefactorOccurrence final
    {
        std::size_t Line = 1;
        std::size_t Column = 1;
        std::string Preview;

        [[nodiscard]] bool operator==(const UiStyleTokenRefactorOccurrence&) const = default;
    };

    struct UiStyleTokenRefactorChange final
    {
        Keire::AssetId Asset;
        std::filesystem::path Path;
        std::filesystem::path RelativePath;
        std::vector<UiStyleTokenRefactorOccurrence> Occurrences;
        std::string BaselineSource;
        std::string CandidateSource;
    };

    struct UiStyleTokenRefactorPreview final
    {
        std::string CurrentName;
        std::string ReplacementName;
        std::vector<UiStyleTokenRefactorChange> Changes;
        std::size_t OccurrenceCount = 0;
    };

    [[nodiscard]] UiStyleTokenRefactorPreview
    BuildUiStyleTokenRefactorPreview(const std::filesystem::path& sourceRoot,
                                     std::span<const UiStyleTokenRefactorInput> inputs, std::string_view currentName,
                                     std::string_view replacementName);
    void ApplyUiStyleTokenRefactor(const UiStyleTokenRefactorPreview& preview);
} // namespace KeireEditor
