#include "KeireInternal/Rendering/RuntimeUiFontAtlasInternal.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <vector>

namespace Keire::RenderBackend
{
    namespace
    {
        constexpr float RasterizedFontSize = 48.0F;
        constexpr float RuntimeUiBaseFontSize = 12.0F;
        constexpr float GlyphMetricScale = RuntimeUiBaseFontSize / RasterizedFontSize;
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

        [[nodiscard]] std::shared_ptr<const RuntimeUiGlyphAtlasCpuData> BuildFallbackAtlas()
        {
            ImFontAtlas source;
            source.Flags |= ImFontAtlasFlags_NoMouseCursors | ImFontAtlasFlags_NoBakedLines;

            constexpr std::array<ImWchar, 3> ranges{RuntimeUiFirstFallbackGlyph, RuntimeUiLastFallbackGlyph, 0};
            ImFontConfig configuration;
            configuration.SizePixels = RasterizedFontSize;
            configuration.GlyphRanges = ranges.data();
            configuration.OversampleH = 2;
            configuration.OversampleV = 2;
            ImFont* font = source.AddFontDefaultVector(&configuration);
            if (font == nullptr)
                throw std::runtime_error("The embedded runtime UI fallback font could not be loaded.");

            ImFontBaked* baked = font->GetFontBaked(RasterizedFontSize);
            if (baked == nullptr)
                throw std::runtime_error("The embedded runtime UI fallback font could not be rasterized.");
            for (std::uint32_t character = RuntimeUiFirstFallbackGlyph; character <= RuntimeUiLastFallbackGlyph;
                 ++character)
                (void)baked->FindGlyphNoFallback(static_cast<ImWchar>(character));

            unsigned char* pixels = nullptr;
            int width = 0;
            int height = 0;
            int bytesPerPixel = 0;
            source.GetTexDataAsRGBA32(&pixels, &width, &height, &bytesPerPixel);
            if (pixels == nullptr || width <= 0 || height <= 0 || bytesPerPixel != 4)
                throw std::runtime_error("The embedded runtime UI fallback font produced an invalid texture atlas.");

            auto result = std::make_shared<RuntimeUiGlyphAtlasCpuData>();
            result->Width = static_cast<std::uint32_t>(width);
            result->Height = static_cast<std::uint32_t>(height);
            result->Pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
            std::memcpy(result->Pixels.data(), pixels, result->Pixels.size());

            baked = font->GetFontBaked(RasterizedFontSize);
            for (std::uint32_t character = RuntimeUiFirstFallbackGlyph; character <= RuntimeUiLastFallbackGlyph;
                 ++character)
            {
                const ImFontGlyph* glyph =
                    baked != nullptr ? baked->FindGlyphNoFallback(static_cast<ImWchar>(character)) : nullptr;
                if (glyph == nullptr)
                    throw std::runtime_error("The embedded runtime UI fallback font omitted a required ASCII glyph.");

                auto& destination = result->Glyphs[character - RuntimeUiFirstFallbackGlyph];
                destination.UvMinimum = {glyph->U0, glyph->V0};
                destination.UvMaximum = {glyph->U1, glyph->V1};
                destination.Offset = {glyph->X0 * GlyphMetricScale, glyph->Y0 * GlyphMetricScale};
                destination.Width = std::max(0.0F, glyph->X1 - glyph->X0) * GlyphMetricScale;
                destination.Height = std::max(0.0F, glyph->Y1 - glyph->Y0) * GlyphMetricScale;
                destination.Advance = std::max(0.0F, glyph->AdvanceX) * GlyphMetricScale;
            }
            return std::shared_ptr<const RuntimeUiGlyphAtlasCpuData>(std::move(result));
        }
    } // namespace

    const std::shared_ptr<const RuntimeUiGlyphAtlasCpuData>& RuntimeUiFallbackGlyphAtlas()
    {
        static const auto atlas = BuildFallbackAtlas();
        return atlas;
    }

    const RuntimeUiGlyph& RuntimeUiFallbackGlyph(const std::uint8_t character) noexcept
    {
        constexpr auto fallback = static_cast<std::uint8_t>('?');
        const auto resolved =
            character >= RuntimeUiFirstFallbackGlyph && character <= RuntimeUiLastFallbackGlyph ? character : fallback;
        return RuntimeUiFallbackGlyphAtlas()->Glyphs[resolved - RuntimeUiFirstFallbackGlyph];
    }

    std::shared_ptr<const RuntimeUiGlyphAtlasCpuData>
    BuildRuntimeUiGlyphAtlas(const std::span<const std::byte> fontBytes, const std::uint32_t collectionIndex,
                             const std::span<const std::uint32_t> glyphs, const std::uint64_t generation)
    {
        auto pages = BuildRuntimeUiGlyphAtlasPages(fontBytes, collectionIndex, glyphs, generation);
        if (pages.size() != 1U)
        {
            throw std::length_error(
                "Runtime UI glyphs require multiple atlas pages; use BuildRuntimeUiGlyphAtlasPages.");
        }
        return std::move(pages.front());
    }

    std::vector<std::shared_ptr<const RuntimeUiGlyphAtlasCpuData>>
    BuildRuntimeUiGlyphAtlasPages(const std::span<const std::byte> fontBytes, const std::uint32_t collectionIndex,
                                  const std::span<const std::uint32_t> glyphs, const std::uint64_t generation)
    {
        if (fontBytes.empty() || glyphs.empty() || glyphs.size() > MaximumCustomGlyphs || generation == 0)
            throw std::invalid_argument("Runtime UI custom font atlas request is empty or exceeds its bounds.");
        FreeTypeContext freeType;
        if (FT_Init_FreeType(&freeType.Library) != 0 ||
            fontBytes.size() > static_cast<std::size_t>((std::numeric_limits<FT_Long>::max)()) ||
            FT_New_Memory_Face(freeType.Library, reinterpret_cast<const FT_Byte*>(fontBytes.data()),
                               static_cast<FT_Long>(fontBytes.size()), collectionIndex, &freeType.Face) != 0)
        {
            throw std::runtime_error("FreeType could not open the runtime UI font atlas face.");
        }
        if (FT_Set_Pixel_Sizes(freeType.Face, 0, static_cast<FT_UInt>(RasterizedFontSize)) != 0)
            throw std::runtime_error("FreeType could not select the runtime UI atlas raster size.");

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
            std::uint32_t X = 1U;
            std::uint32_t Y = 1U;
            std::uint32_t RowHeight = 0U;
        };
        std::vector<PageCursor> cursors(1U);
        for (auto& glyph : rasterized)
        {
            if (glyph.Width + 2U > CustomAtlasWidth)
                throw std::length_error("A runtime UI glyph exceeds the atlas page width.");
            auto* cursor = &cursors.back();
            if (cursor->X + glyph.Width + 1U > CustomAtlasWidth)
            {
                cursor->X = 1U;
                cursor->Y += cursor->RowHeight + 1U;
                cursor->RowHeight = 0U;
            }
            if (cursor->Y + glyph.Height + 1U > MaximumCustomAtlasHeight)
            {
                if (cursors.size() >= MaximumCustomAtlasPages)
                    throw std::length_error("Runtime UI glyphs exceed the bounded eight-page atlas budget.");
                cursors.push_back({});
                cursor = &cursors.back();
            }
            glyph.X = cursor->X;
            glyph.Y = cursor->Y;
            glyph.PageIndex = static_cast<std::uint16_t>(cursors.size() - 1U);
            cursor->X += glyph.Width + 1U;
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
            page->Height = std::min(MaximumCustomAtlasHeight, NextPowerOfTwo(cursor.Y + cursor.RowHeight + 1U));
            page->PageIndex = static_cast<std::uint16_t>(pageIndex);
            page->Generation = generation;
            page->Pixels.assign(static_cast<std::size_t>(page->Width) * page->Height * 4U, std::byte{});
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
                    .Offset = {static_cast<float>(glyph.Left), -static_cast<float>(glyph.Top)},
                    .Width = static_cast<float>(glyph.Width),
                    .Height = static_cast<float>(glyph.Height),
                    .Advance = glyph.Advance});
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
