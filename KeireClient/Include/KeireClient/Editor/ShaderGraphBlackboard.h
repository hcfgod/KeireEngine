#pragma once

#include "Keire/Rendering/ShaderGraph.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    struct ShaderGraphBlackboardEntry
    {
        std::string Identity;
        std::optional<Keire::AssetId> Node;
        std::string Name;
        std::string Symbol;
        std::string Category;
        std::string Description;
        std::int32_t SortPriority = 0;
        bool Keyword = false;
        bool Exposed = true;
    };

    [[nodiscard]] std::vector<ShaderGraphBlackboardEntry>
    BuildShaderGraphBlackboard(const Keire::ShaderGraphDefinition& definition, std::string_view search = {});
    [[nodiscard]] bool ShaderGraphHasKeywordToken(const Keire::ShaderGraphDefinition& definition,
                                                  std::string_view token);
} // namespace KeireEditor
