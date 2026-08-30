#include "KeireInternal/Rendering/RuntimeUiFontAtlasInternal.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace Keire::RenderBackend
{
    namespace
    {
        constexpr float RasterizedFontSize = 48.0F;
        constexpr float RuntimeUiBaseFontSize = 12.0F;
        constexpr float GlyphMetricScale = RuntimeUiBaseFontSize / RasterizedFontSize;

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
} // namespace Keire::RenderBackend
