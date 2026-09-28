#pragma once

#include "Keire/Assets/Asset.h"
#include "Keire/Math/Math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace Keire::RenderBackend
{
    inline constexpr AssetId RuntimeUiFallbackFontId{0x4b45495245554946ULL, 0x4f4e540000000001ULL};
    inline constexpr std::uint8_t RuntimeUiFirstFallbackGlyph = 32U;
    inline constexpr std::uint8_t RuntimeUiLastFallbackGlyph = 255U;
    inline constexpr std::uint32_t RuntimeUiCustomFontBaseRasterSize = 48U;
    inline constexpr std::uint32_t RuntimeUiFontAtlasPadding = 16U;

    struct RuntimeUiFontMip final
    {
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::vector<std::byte> Pixels;
    };

    [[nodiscard]] inline std::uint32_t RuntimeUiCustomFontRasterBucket(const float pixelSize) noexcept
    {
        if (!(pixelSize > 48.0F))
            return 48U;
        if (pixelSize <= 96.0F)
            return 96U;
        if (pixelSize <= 192.0F)
            return 192U;
        return 384U;
    }

    struct RuntimeUiGlyph final
    {
        Vector2 UvMinimum;
        Vector2 UvMaximum;
        Vector2 Offset;
        float Width = 0.0F;
        float Height = 0.0F;
        float Advance = 0.0F;
    };

    struct RuntimeUiGlyphAtlasCpuData final
    {
        std::vector<std::byte> Pixels;
        std::array<RuntimeUiGlyph, RuntimeUiLastFallbackGlyph - RuntimeUiFirstFallbackGlyph + 1U> Glyphs{};
        std::unordered_map<std::uint32_t, RuntimeUiGlyph> ShapedGlyphs;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::uint16_t PageIndex = 0;
        std::uint64_t Generation = 1;
    };

    [[nodiscard]] const std::shared_ptr<const RuntimeUiGlyphAtlasCpuData>& RuntimeUiFallbackGlyphAtlas();
    [[nodiscard]] const RuntimeUiGlyph& RuntimeUiFallbackGlyph(std::uint8_t character);
    [[nodiscard]] std::vector<RuntimeUiFontMip> BuildRuntimeUiFontMips(const RuntimeUiGlyphAtlasCpuData& atlas);
    [[nodiscard]] std::shared_ptr<const RuntimeUiGlyphAtlasCpuData>
    BuildRuntimeUiGlyphAtlas(std::span<const std::byte> fontBytes, std::uint32_t collectionIndex,
                             std::span<const std::uint32_t> glyphs, std::uint64_t generation,
                             std::uint32_t rasterSize = RuntimeUiCustomFontBaseRasterSize);
    [[nodiscard]] std::vector<std::shared_ptr<const RuntimeUiGlyphAtlasCpuData>>
    BuildRuntimeUiGlyphAtlasPages(std::span<const std::byte> fontBytes, std::uint32_t collectionIndex,
                                  std::span<const std::uint32_t> glyphs, std::uint64_t generation,
                                  std::uint32_t rasterSize = RuntimeUiCustomFontBaseRasterSize);
    [[nodiscard]] const RuntimeUiGlyph* FindRuntimeUiGlyph(const RuntimeUiGlyphAtlasCpuData& atlas,
                                                           std::uint32_t glyph) noexcept;
    [[nodiscard]] constexpr AssetId RuntimeUiFontBindingId(const AssetId requested) noexcept
    {
        (void)requested;
        return RuntimeUiFallbackFontId;
    }
} // namespace Keire::RenderBackend
