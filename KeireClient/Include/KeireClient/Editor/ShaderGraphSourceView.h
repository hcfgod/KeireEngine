#pragma once

#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    struct ShaderGraphSourceLine
    {
        std::size_t Number = 1;
        std::string_view Text;
    };

    /// Returned views borrow the source and remain valid only while its bytes remain unchanged.
    [[nodiscard]] std::vector<ShaderGraphSourceLine>
    ShaderGraphSourcePage(std::string_view source, std::size_t firstLine, std::size_t maximumLines = 80);
    /// Textual identifier navigation; this does not imply a compiler source map for arbitrary graph nodes.
    [[nodiscard]] std::optional<std::size_t> FindShaderGraphSourceSymbol(std::string_view source,
                                                                         std::string_view symbol);
} // namespace KeireEditor
