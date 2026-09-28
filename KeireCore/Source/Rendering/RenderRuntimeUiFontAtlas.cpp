#include "KeireInternal/Rendering/RuntimeUiFontAtlasInternal.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <vector>

namespace Keire::RenderBackend
{
    namespace
    {
        constexpr std::uint32_t CustomAtlasWidth = 1024U;
        constexpr std::uint32_t MaximumCustomAtlasHeight = 2048U;
        constexpr std::size_t MaximumCustomGlyphs = 4096U;
        constexpr std::size_t MaximumCustomAtlasPages = 8U;

        struct RasterizedGlyph final
        {
            std::uint32_t Glyph = 0;
            std::vector<std::byte> Alpha;
            std::uint32_t Width = 0;
            std::uint32_t Height = 0;
            std::int32_t Left = 0;
            std::int32_t Top = 0;
            float Advance = 0.0F;
            std::uint32_t X = 0;
            std::uint32_t Y = 0;
            std::uint16_t PageIndex = 0;
        };

        struct FreeTypeContext final
        {
            ~FreeTypeContext()
            {
                if (Face)
                    FT_Done_Face(Face);
                if (Library)
                    FT_Done_FreeType(Library);
            }

            FT_Library Library = nullptr;
            FT_Face Face = nullptr;
        };

        [[nodiscard]] std::uint32_t NextPowerOfTwo(std::uint32_t value) noexcept
        {
            value = std::max(1U, value);
            --value;
            value |= value >> 1U;
            value |= value >> 2U;
            value |= value >> 4U;
            value |= value >> 8U;
            value |= value >> 16U;
            return value + 1U;
        }

    } // namespace

    std::vector<RuntimeUiFontMip> BuildRuntimeUiFontMips(const RuntimeUiGlyphAtlasCpuData& atlas)
    {
        if (!std::has_single_bit(atlas.Width) || !std::has_single_bit(atlas.Height) || atlas.Width > 16'384U ||
            atlas.Height > 16'384U || atlas.Pixels.size() != static_cast<std::size_t>(atlas.Width) * atlas.Height * 4U)
            throw std::invalid_argument("Runtime UI font mip source dimensions are invalid.");
        std::vector<RuntimeUiFontMip> levels{{atlas.Width, atlas.Height, atlas.Pixels}};
        // Stop at 1/8 resolution: the atlas gutter isolates adjacent glyphs through this footprint.
        while (levels.size() < 4U && levels.back().Width > 1U && levels.back().Height > 1U)
        {
            const auto& previous = levels.back();
            RuntimeUiFontMip mip{previous.Width / 2U, previous.Height / 2U, {}};
            mip.Pixels.assign(static_cast<std::size_t>(mip.Width) * mip.Height * 4U, std::byte{0xff});
            for (std::uint32_t y = 0; y < mip.Height; ++y)
                for (std::uint32_t x = 0; x < mip.Width; ++x)
                {
                    std::uint32_t alpha = 0;
                    for (std::uint32_t row = 0; row < 2U; ++row)
                        for (std::uint32_t column = 0; column < 2U; ++column)
                            alpha += std::to_integer<std::uint32_t>(
                                previous.Pixels[((static_cast<std::size_t>(y) * 2U + row) * previous.Width + x * 2U +
                                                 column) *
                                                    4U +
                                                3U]);
                    mip.Pixels[(static_cast<std::size_t>(y) * mip.Width + x) * 4U + 3U] =
                        static_cast<std::byte>((alpha + 2U) / 4U);
                }
            levels.push_back(std::move(mip));
        }
        return levels;
    }

    std::shared_ptr<const RuntimeUiGlyphAtlasCpuData>
    BuildRuntimeUiGlyphAtlas(const std::span<const std::byte> fontBytes, const std::uint32_t collectionIndex,
                             const std::span<const std::uint32_t> glyphs, const std::uint64_t generation,
                             const std::uint32_t rasterSize)
    {
        auto pages = BuildRuntimeUiGlyphAtlasPages(fontBytes, collectionIndex, glyphs, generation, rasterSize);
        if (pages.size() != 1U)
        {
            throw std::length_error(
                "Runtime UI glyphs require multiple atlas pages; use BuildRuntimeUiGlyphAtlasPages.");
        }
        return std::move(pages.front());
    }

    std::vector<std::shared_ptr<const RuntimeUiGlyphAtlasCpuData>>
    BuildRuntimeUiGlyphAtlasPages(const std::span<const std::byte> fontBytes, const std::uint32_t collectionIndex,
                                  const std::span<const std::uint32_t> glyphs, const std::uint64_t generation,
                                  const std::uint32_t rasterSize)
    {
        if (fontBytes.empty() || glyphs.empty() || glyphs.size() > MaximumCustomGlyphs || generation == 0 ||
            (rasterSize != 48U && rasterSize != 96U && rasterSize != 192U && rasterSize != 384U))
            throw std::invalid_argument("Runtime UI custom font atlas request is empty or exceeds its bounds.");
        FreeTypeContext freeType;
        if (FT_Init_FreeType(&freeType.Library) != 0 ||
            fontBytes.size() > static_cast<std::size_t>((std::numeric_limits<FT_Long>::max)()) ||
            FT_New_Memory_Face(freeType.Library, reinterpret_cast<const FT_Byte*>(fontBytes.data()),
                               static_cast<FT_Long>(fontBytes.size()), collectionIndex, &freeType.Face) != 0)
        {
            throw std::runtime_error("FreeType could not open the runtime UI font atlas face.");
        }
        if (FT_Set_Pixel_Sizes(freeType.Face, 0, static_cast<FT_UInt>(rasterSize)) != 0)
            throw std::runtime_error("FreeType could not select the runtime UI atlas raster size.");
        const float metricScale =
            static_cast<float>(RuntimeUiCustomFontBaseRasterSize) / static_cast<float>(rasterSize);

        std::vector<std::uint32_t> unique(glyphs.begin(), glyphs.end());
        std::ranges::sort(unique);
        const auto duplicate = std::ranges::unique(unique);
        unique.erase(duplicate.begin(), duplicate.end());
        if (unique.size() > MaximumCustomGlyphs)
            throw std::length_error("Runtime UI custom font atlas exceeds 4,096 glyphs.");

        std::vector<RasterizedGlyph> rasterized;
        rasterized.reserve(unique.size());
        for (const auto glyph : unique)
        {
            if (FT_Load_Glyph(freeType.Face, glyph, FT_LOAD_DEFAULT) != 0 ||
                FT_Render_Glyph(freeType.Face->glyph, FT_RENDER_MODE_NORMAL) != 0)
            {
                continue;
            }
            const auto& bitmap = freeType.Face->glyph->bitmap;
            RasterizedGlyph entry;
            entry.Glyph = glyph;
            entry.Width = bitmap.width;
            entry.Height = bitmap.rows;
            entry.Left = freeType.Face->glyph->bitmap_left;
            entry.Top = freeType.Face->glyph->bitmap_top;
            entry.Advance = static_cast<float>(freeType.Face->glyph->advance.x) / 64.0F;
            entry.Alpha.resize(static_cast<std::size_t>(entry.Width) * entry.Height);
            for (std::uint32_t row = 0; row < entry.Height; ++row)
            {
                const auto sourceRow = bitmap.pitch >= 0 ? row : entry.Height - row - 1U;
                const auto* source = bitmap.buffer + static_cast<std::ptrdiff_t>(sourceRow) * std::abs(bitmap.pitch);
                for (std::uint32_t column = 0; column < entry.Width; ++column)
                {
                    const auto alpha = bitmap.pixel_mode == FT_PIXEL_MODE_MONO
                                           ? ((source[column / 8U] & (0x80U >> (column % 8U))) != 0U ? 255U : 0U)
                                           : source[column];
                    entry.Alpha[static_cast<std::size_t>(row) * entry.Width + column] = static_cast<std::byte>(alpha);
                }
            }
            rasterized.push_back(std::move(entry));
        }

        struct PageCursor final
        {
            std::uint32_t X = RuntimeUiFontAtlasPadding;
            std::uint32_t Y = RuntimeUiFontAtlasPadding;
            std::uint32_t RowHeight = 0U;
        };
        std::vector<PageCursor> cursors(1U);
        for (auto& glyph : rasterized)
        {
            if (glyph.Width + 2U * RuntimeUiFontAtlasPadding > CustomAtlasWidth)
                throw std::length_error("A runtime UI glyph exceeds the atlas page width.");
            if (glyph.Height + 2U * RuntimeUiFontAtlasPadding > MaximumCustomAtlasHeight)
                throw std::length_error("A runtime UI glyph exceeds the atlas page height.");
            auto* cursor = &cursors.back();
            if (cursor->X + glyph.Width + RuntimeUiFontAtlasPadding > CustomAtlasWidth)
            {
                cursor->X = RuntimeUiFontAtlasPadding;
                cursor->Y += cursor->RowHeight + RuntimeUiFontAtlasPadding;
                cursor->RowHeight = 0U;
            }
            if (cursor->Y + glyph.Height + RuntimeUiFontAtlasPadding > MaximumCustomAtlasHeight)
            {
                if (cursors.size() >= MaximumCustomAtlasPages)
                    throw std::length_error("Runtime UI glyphs exceed the bounded eight-page atlas budget.");
                cursors.push_back({});
                cursor = &cursors.back();
            }
            glyph.X = cursor->X;
            glyph.Y = cursor->Y;
            glyph.PageIndex = static_cast<std::uint16_t>(cursors.size() - 1U);
            cursor->X += glyph.Width + RuntimeUiFontAtlasPadding;
            cursor->RowHeight = std::max(cursor->RowHeight, glyph.Height);
        }
        if (rasterized.empty())
            throw std::runtime_error("Runtime UI font atlas rasterization produced no glyphs.");

        std::vector<std::shared_ptr<RuntimeUiGlyphAtlasCpuData>> mutablePages;
        mutablePages.reserve(cursors.size());
        for (std::size_t pageIndex = 0; pageIndex < cursors.size(); ++pageIndex)
        {
            const auto& cursor = cursors[pageIndex];
            auto page = std::make_shared<RuntimeUiGlyphAtlasCpuData>();
            page->Width = CustomAtlasWidth;
            page->Height = std::min(MaximumCustomAtlasHeight,
                                    NextPowerOfTwo(cursor.Y + cursor.RowHeight + RuntimeUiFontAtlasPadding));
            page->PageIndex = static_cast<std::uint16_t>(pageIndex);
            page->Generation = generation;
            page->Pixels.assign(static_cast<std::size_t>(page->Width) * page->Height * 4U, std::byte{});
            // Straight-alpha filtering must interpolate coverage, not darken glyph edges into black padding.
            for (std::size_t pixel = 0; pixel < page->Pixels.size(); pixel += 4U)
                std::fill_n(page->Pixels.begin() + static_cast<std::ptrdiff_t>(pixel), 3U, std::byte{0xff});
            mutablePages.push_back(std::move(page));
        }
        for (const auto& glyph : rasterized)
        {
            auto& page = *mutablePages[glyph.PageIndex];
            for (std::uint32_t row = 0; row < glyph.Height; ++row)
                for (std::uint32_t column = 0; column < glyph.Width; ++column)
                {
                    const auto source = glyph.Alpha[static_cast<std::size_t>(row) * glyph.Width + column];
                    const auto destination =
                        (static_cast<std::size_t>(glyph.Y + row) * page.Width + glyph.X + column) * 4U;
                    page.Pixels[destination] = std::byte{0xff};
                    page.Pixels[destination + 1U] = std::byte{0xff};
                    page.Pixels[destination + 2U] = std::byte{0xff};
                    page.Pixels[destination + 3U] = source;
                }
            page.ShapedGlyphs.emplace(
                glyph.Glyph,
                RuntimeUiGlyph{
                    .UvMinimum = {static_cast<float>(glyph.X) / static_cast<float>(page.Width),
                                  static_cast<float>(glyph.Y) / static_cast<float>(page.Height)},
                    .UvMaximum = {static_cast<float>(glyph.X + glyph.Width) / static_cast<float>(page.Width),
                                  static_cast<float>(glyph.Y + glyph.Height) / static_cast<float>(page.Height)},
                    .Offset = {static_cast<float>(glyph.Left) * metricScale,
                               -static_cast<float>(glyph.Top) * metricScale},
                    .Width = static_cast<float>(glyph.Width) * metricScale,
                    .Height = static_cast<float>(glyph.Height) * metricScale,
                    .Advance = glyph.Advance * metricScale});
        }
        std::vector<std::shared_ptr<const RuntimeUiGlyphAtlasCpuData>> result;
        result.reserve(mutablePages.size());
        for (auto& page : mutablePages)
            result.push_back(std::move(page));
        return result;
    }

    const RuntimeUiGlyph* FindRuntimeUiGlyph(const RuntimeUiGlyphAtlasCpuData& atlas,
                                             const std::uint32_t glyph) noexcept
    {
        const auto found = atlas.ShapedGlyphs.find(glyph);
        return found == atlas.ShapedGlyphs.end() ? nullptr : &found->second;
    }
} // namespace Keire::RenderBackend
