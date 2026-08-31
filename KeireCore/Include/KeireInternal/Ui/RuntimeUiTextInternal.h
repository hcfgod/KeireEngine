#pragma once

#include "Keire/Ui/RuntimeUi.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire::Detail
{
    struct RuntimeUiTextGlyph final
    {
        std::uint32_t Glyph = 0;
        std::uint16_t FaceIndex = 0;
        char32_t Codepoint = U'\0';
        std::uint32_t Cluster = 0;
        float X = 0.0F;
        float Y = 0.0F;
        float Advance = 0.0F;
        float OffsetX = 0.0F;
        float OffsetY = 0.0F;
        bool RightToLeft = false;

        [[nodiscard]] bool operator==(const RuntimeUiTextGlyph&) const = default;
    };

    struct RuntimeUiTextFace final
    {
        std::span<const std::byte> FontBytes;
        std::uint64_t FontGeneration = 0;
        std::uint32_t CollectionIndex = 0;
    };

    struct RuntimeUiTextLine final
    {
        std::size_t FirstGlyph = 0;
        std::size_t GlyphCount = 0;
        float Width = 0.0F;

        [[nodiscard]] bool operator==(const RuntimeUiTextLine&) const = default;
    };

    struct RuntimeUiTextLayout final
    {
        std::vector<RuntimeUiTextGlyph> Glyphs;
        std::vector<RuntimeUiTextLine> Lines;
        float Width = 0.0F;
        float Height = 0.0F;
        float Ascender = 0.0F;
        float Descender = 0.0F;
        float LineHeight = 0.0F;
        bool Truncated = false;
        bool UsedFontFallback = false;

        [[nodiscard]] bool operator==(const RuntimeUiTextLayout&) const = default;
    };

    struct RuntimeUiTextLayoutRequest final
    {
        std::span<const std::byte> FontBytes;
        std::uint64_t FontGeneration = 0;
        std::uint32_t CollectionIndex = 0;
        /// Ordered fallback faces. Face zero is always the primary FontBytes/CollectionIndex pair above.
        std::span<const RuntimeUiTextFace> FallbackFaces;
        std::string_view Text;
        std::string_view Language = "und";
        RuntimeUiTextDirection Direction = RuntimeUiTextDirection::Automatic;
        RuntimeUiTextWrap Wrap = RuntimeUiTextWrap::Normal;
        RuntimeUiTextOverflow Overflow = RuntimeUiTextOverflow::Clip;
        float FontSize = 16.0F;
        float AvailableWidth = 0.0F;
        float AuthoredLineHeight = 0.0F;
        float LetterSpacing = 0.0F;
        float WordSpacing = 0.0F;
        std::uint16_t MaximumLines = 0;
        std::uint16_t Weight = 400;
        RuntimeUiFontSlant Slant = RuntimeUiFontSlant::Normal;
    };

    struct RuntimeUiTextCacheStatistics final
    {
        std::size_t Entries = 0;
        std::size_t Glyphs = 0;
        std::uint64_t Hits = 0;
        std::uint64_t Misses = 0;
        std::uint64_t Evictions = 0;
    };

    [[nodiscard]] RuntimeUiTextLayout BuildRuntimeUiTextLayout(const RuntimeUiTextLayoutRequest& request);
    [[nodiscard]] std::size_t CountRuntimeUiMissingGlyphs(std::span<const std::byte> fontBytes,
                                                          std::uint32_t collectionIndex, std::string_view text);

    class RuntimeUiTextLayoutCache final
    {
      public:
        explicit RuntimeUiTextLayoutCache(std::size_t maximumEntries = 256U, std::size_t maximumGlyphs = 65'536U);
        ~RuntimeUiTextLayoutCache();

        RuntimeUiTextLayoutCache(const RuntimeUiTextLayoutCache&) = delete;
        RuntimeUiTextLayoutCache& operator=(const RuntimeUiTextLayoutCache&) = delete;
        RuntimeUiTextLayoutCache(RuntimeUiTextLayoutCache&&) noexcept;
        RuntimeUiTextLayoutCache& operator=(RuntimeUiTextLayoutCache&&) noexcept;

        [[nodiscard]] std::shared_ptr<const RuntimeUiTextLayout> Resolve(const RuntimeUiTextLayoutRequest& request);
        void InvalidateFontGeneration(std::uint64_t generation) noexcept;
        void Clear() noexcept;
        [[nodiscard]] RuntimeUiTextCacheStatistics Statistics() const noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };
} // namespace Keire::Detail
