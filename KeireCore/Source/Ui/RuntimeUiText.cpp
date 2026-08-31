#include "KeireInternal/Ui/RuntimeUiTextInternal.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <fribidi/fribidi.h>
#include <harfbuzz/hb-ot.h>
#include <harfbuzz/hb.h>
#include <unibreak/linebreak.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace Keire::Detail
{
    namespace
    {
        constexpr std::size_t MaximumTextBytes = 1024U * 1024U;
        constexpr std::size_t MaximumCodepoints = 262'144U;
        constexpr std::size_t MaximumFallbackFaces = 16U;
        constexpr float FixedPointScale = 64.0F;

        struct DecodedText final
        {
            std::vector<char32_t> Codepoints;
            std::vector<std::uint32_t> ByteOffsets;
        };

        struct ShapedRun final
        {
            std::vector<RuntimeUiTextGlyph> Glyphs;
            FriBidiLevel Level = 0;
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

        template <typename T, void (*Destroy)(T*)> class OwnedHandle final
        {
          public:
            explicit OwnedHandle(T* value = nullptr) noexcept : m_Value(value) {}
            ~OwnedHandle()
            {
                if (m_Value)
                    Destroy(m_Value);
            }
            OwnedHandle(const OwnedHandle&) = delete;
            OwnedHandle& operator=(const OwnedHandle&) = delete;
            OwnedHandle(OwnedHandle&& other) noexcept : m_Value(std::exchange(other.m_Value, nullptr)) {}
            OwnedHandle& operator=(OwnedHandle&& other) noexcept
            {
                if (this != &other)
                {
                    if (m_Value)
                        Destroy(m_Value);
                    m_Value = std::exchange(other.m_Value, nullptr);
                }
                return *this;
            }
            [[nodiscard]] T* Get() const noexcept { return m_Value; }

          private:
            T* m_Value = nullptr;
        };

        struct LoadedTextFace final
        {
            FreeTypeContext FreeType;
            OwnedHandle<hb_blob_t, hb_blob_destroy> Blob;
            OwnedHandle<hb_face_t, hb_face_destroy> HarfBuzzFace;
            OwnedHandle<hb_font_t, hb_font_destroy> Font;
        };

        [[nodiscard]] DecodedText DecodeUtf8(const std::string_view text)
        {
            if (text.size() > MaximumTextBytes)
                throw std::length_error("Runtime UI text exceeds the 1 MiB shaping limit.");
            DecodedText result;
            result.Codepoints.reserve(std::min(text.size(), MaximumCodepoints));
            result.ByteOffsets.reserve(std::min(text.size(), MaximumCodepoints));
            for (std::size_t index = 0; index < text.size();)
            {
                if (result.Codepoints.size() >= MaximumCodepoints)
                    throw std::length_error("Runtime UI text exceeds the 262,144-codepoint shaping limit.");
                const auto first = static_cast<std::uint8_t>(text[index]);
                std::size_t count = 1;
                char32_t value = first;
                char32_t minimum = 0;
                if ((first & 0xe0U) == 0xc0U)
                {
                    count = 2;
                    value = first & 0x1fU;
                    minimum = 0x80;
                }
                else if ((first & 0xf0U) == 0xe0U)
                {
                    count = 3;
                    value = first & 0x0fU;
                    minimum = 0x800;
                }
                else if ((first & 0xf8U) == 0xf0U)
                {
                    count = 4;
                    value = first & 0x07U;
                    minimum = 0x10000;
                }
                else if (first >= 0x80U)
                {
                    throw std::invalid_argument("Runtime UI text contains invalid UTF-8.");
                }
                if (index + count > text.size())
                    throw std::invalid_argument("Runtime UI text contains truncated UTF-8.");
                for (std::size_t continuation = 1; continuation < count; ++continuation)
                {
                    const auto byte = static_cast<std::uint8_t>(text[index + continuation]);
                    if ((byte & 0xc0U) != 0x80U)
                        throw std::invalid_argument("Runtime UI text contains invalid UTF-8 continuation bytes.");
                    value = value << 6U | (byte & 0x3fU);
                }
                if ((count != 1 && value < minimum) || value > 0x10ffffU || (value >= 0xd800U && value <= 0xdfffU))
                {
                    throw std::invalid_argument("Runtime UI text contains a non-scalar UTF-8 sequence.");
                }
                result.ByteOffsets.push_back(static_cast<std::uint32_t>(index));
                result.Codepoints.push_back(value);
                index += count;
            }
            return result;
        }

        [[nodiscard]] FriBidiParType ParagraphDirection(const RuntimeUiTextDirection direction) noexcept
        {
            if (direction == RuntimeUiTextDirection::LeftToRight)
                return FRIBIDI_PAR_LTR;
            if (direction == RuntimeUiTextDirection::RightToLeft)
                return FRIBIDI_PAR_RTL;
            return FRIBIDI_PAR_ON;
        }

        [[nodiscard]] float FallbackAdvance(const char32_t codepoint,
                                            const RuntimeUiTextLayoutRequest& request) noexcept
        {
            float result = request.FontSize * (codepoint == U' ' || codepoint == U'\t' ? 0.34F : 0.56F);
            result += request.LetterSpacing;
            if (codepoint == U' ' || codepoint == U'\t')
                result += request.WordSpacing;
            return std::max(0.0F, result);
        }

        [[nodiscard]] std::vector<ShapedRun> ShapeParagraph(const std::span<const char32_t> paragraph,
                                                            const std::uint32_t logicalOffset,
                                                            const RuntimeUiTextLayoutRequest& request,
                                                            const std::span<hb_font_t* const> fonts,
                                                            const std::span<FT_Face const> freeTypeFaces)
        {
            if (paragraph.empty())
                return {};
            std::vector<FriBidiChar> characters(paragraph.begin(), paragraph.end());
            std::vector<FriBidiCharType> types(characters.size());
            std::vector<FriBidiBracketType> brackets(characters.size());
            std::vector<FriBidiLevel> levels(characters.size());
            fribidi_get_bidi_types(characters.data(), static_cast<FriBidiStrIndex>(characters.size()), types.data());
            fribidi_get_bracket_types(characters.data(), static_cast<FriBidiStrIndex>(characters.size()), types.data(),
                                      brackets.data());
            auto base = ParagraphDirection(request.Direction);
            if (fribidi_get_par_embedding_levels_ex(types.data(), brackets.data(),
                                                    static_cast<FriBidiStrIndex>(characters.size()), &base,
                                                    levels.data()) == 0)
            {
                throw std::runtime_error("FriBidi could not resolve runtime UI paragraph levels.");
            }

            std::vector<ShapedRun> runs;
            const auto selectFace = [&freeTypeFaces](const char32_t codepoint) noexcept
            {
                for (std::size_t index = 0; index < freeTypeFaces.size(); ++index)
                    if (freeTypeFaces[index] &&
                        FT_Get_Char_Index(freeTypeFaces[index], static_cast<FT_ULong>(codepoint)) != 0U)
                    {
                        return index;
                    }
                return std::size_t{0};
            };
            for (std::size_t levelBegin = 0; levelBegin < paragraph.size();)
            {
                std::size_t levelEnd = levelBegin + 1U;
                while (levelEnd < paragraph.size() && levels[levelEnd] == levels[levelBegin])
                    ++levelEnd;
                for (std::size_t begin = levelBegin; begin < levelEnd;)
                {
                    const auto faceIndex = fonts.empty() ? std::size_t{0} : selectFace(paragraph[begin]);
                    std::size_t end = begin + 1U;
                    while (end < levelEnd && (fonts.empty() || selectFace(paragraph[end]) == faceIndex))
                        ++end;
                    ShapedRun run;
                    run.Level = levels[levelBegin];
                    if (!fonts.empty())
                    {
                        OwnedHandle<hb_buffer_t, hb_buffer_destroy> buffer(hb_buffer_create());
                        if (!buffer.Get())
                            throw std::bad_alloc();
                        hb_buffer_set_direction(buffer.Get(),
                                                (run.Level & 1U) != 0U ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
                        hb_buffer_set_language(buffer.Get(),
                                               hb_language_from_string(request.Language.data(),
                                                                       static_cast<int>(request.Language.size())));
                        hb_buffer_add_utf32(buffer.Get(), reinterpret_cast<const std::uint32_t*>(paragraph.data()),
                                            static_cast<int>(paragraph.size()), static_cast<unsigned int>(begin),
                                            static_cast<int>(end - begin));
                        hb_buffer_guess_segment_properties(buffer.Get());
                        hb_shape(fonts[faceIndex], buffer.Get(), nullptr, 0);
                        unsigned int count = 0;
                        const auto* information = hb_buffer_get_glyph_infos(buffer.Get(), &count);
                        const auto* positions = hb_buffer_get_glyph_positions(buffer.Get(), &count);
                        run.Glyphs.reserve(count);
                        for (unsigned int index = 0; index < count; ++index)
                        {
                            const auto cluster =
                                std::min<std::size_t>(information[index].cluster, paragraph.size() - 1U);
                            const auto codepoint = paragraph[cluster];
                            float advance = static_cast<float>(positions[index].x_advance) / FixedPointScale;
                            if (advance < 0.0F)
                                advance = -advance;
                            advance += request.LetterSpacing;
                            if (codepoint == U' ' || codepoint == U'\t')
                                advance += request.WordSpacing;
                            run.Glyphs.push_back(
                                {.Glyph = information[index].codepoint,
                                 .FaceIndex = static_cast<std::uint16_t>(faceIndex),
                                 .Codepoint = codepoint,
                                 .Cluster = logicalOffset + static_cast<std::uint32_t>(cluster),
                                 .Advance = std::max(0.0F, advance),
                                 .OffsetX = static_cast<float>(positions[index].x_offset) / FixedPointScale,
                                 .OffsetY = -static_cast<float>(positions[index].y_offset) / FixedPointScale,
                                 .RightToLeft = (run.Level & 1U) != 0U});
                        }
                    }
                    else
                    {
                        run.Glyphs.reserve(end - begin);
                        if ((run.Level & 1U) == 0U)
                        {
                            for (std::size_t index = begin; index < end; ++index)
                                run.Glyphs.push_back({.Codepoint = paragraph[index],
                                                      .Cluster = logicalOffset + static_cast<std::uint32_t>(index),
                                                      .Advance = FallbackAdvance(paragraph[index], request)});
                        }
                        else
                        {
                            for (std::size_t index = end; index-- > begin;)
                                run.Glyphs.push_back({.Codepoint = paragraph[index],
                                                      .Cluster = logicalOffset + static_cast<std::uint32_t>(index),
                                                      .Advance = FallbackAdvance(paragraph[index], request),
                                                      .RightToLeft = true});
                        }
                    }
                    runs.push_back(std::move(run));
                    begin = end;
                }
                levelBegin = levelEnd;
            }

            FriBidiLevel maximum = 0;
            FriBidiLevel lowestOdd = (std::numeric_limits<FriBidiLevel>::max)();
            for (const auto& run : runs)
            {
                maximum = std::max(maximum, run.Level);
                if ((run.Level & 1U) != 0U)
                    lowestOdd = std::min(lowestOdd, run.Level);
            }
            if (lowestOdd != (std::numeric_limits<FriBidiLevel>::max)())
                for (auto level = maximum; level >= lowestOdd; --level)
                {
                    for (std::size_t begin = 0; begin < runs.size();)
                    {
                        while (begin < runs.size() && runs[begin].Level < level)
                            ++begin;
                        auto end = begin;
                        while (end < runs.size() && runs[end].Level >= level)
                            ++end;
                        std::reverse(runs.begin() + static_cast<std::ptrdiff_t>(begin),
                                     runs.begin() + static_cast<std::ptrdiff_t>(end));
                        begin = end;
                    }
                    if (level == 0)
                        break;
                }
            return runs;
        }

        [[nodiscard]] std::uint64_t HashBytes(const std::span<const std::byte> bytes) noexcept
        {
            std::uint64_t result = 1469598103934665603ULL;
            for (const auto value : bytes)
            {
                result ^= static_cast<std::uint8_t>(value);
                result *= 1099511628211ULL;
            }
            return result;
        }

        template <typename T> void HashCombine(std::size_t& seed, const T& value) noexcept
        {
            seed ^= std::hash<T>{}(value) + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U);
        }

        struct CacheKey final
        {
            std::uint64_t FontGeneration = 0;
            std::uint64_t FontHash = 0;
            std::string Text;
            std::string Language;
            RuntimeUiTextDirection Direction = RuntimeUiTextDirection::Automatic;
            RuntimeUiTextWrap Wrap = RuntimeUiTextWrap::Normal;
            RuntimeUiTextOverflow Overflow = RuntimeUiTextOverflow::Clip;
            float FontSize = 0.0F;
            float AvailableWidth = 0.0F;
            float LineHeight = 0.0F;
            float LetterSpacing = 0.0F;
            float WordSpacing = 0.0F;
            std::uint16_t MaximumLines = 0;
            std::uint16_t Weight = 400;
            RuntimeUiFontSlant Slant = RuntimeUiFontSlant::Normal;

            [[nodiscard]] bool operator==(const CacheKey&) const = default;
        };

        struct CacheKeyHash final
        {
            [[nodiscard]] std::size_t operator()(const CacheKey& key) const noexcept
            {
                std::size_t result = 0;
                HashCombine(result, key.FontGeneration);
                HashCombine(result, key.FontHash);
                HashCombine(result, key.Text);
                HashCombine(result, key.Language);
                HashCombine(result, static_cast<std::uint8_t>(key.Direction));
                HashCombine(result, static_cast<std::uint8_t>(key.Wrap));
                HashCombine(result, static_cast<std::uint8_t>(key.Overflow));
                HashCombine(result, std::bit_cast<std::uint32_t>(key.FontSize));
                HashCombine(result, std::bit_cast<std::uint32_t>(key.AvailableWidth));
                HashCombine(result, std::bit_cast<std::uint32_t>(key.LineHeight));
                HashCombine(result, std::bit_cast<std::uint32_t>(key.LetterSpacing));
                HashCombine(result, std::bit_cast<std::uint32_t>(key.WordSpacing));
                HashCombine(result, key.MaximumLines);
                HashCombine(result, key.Weight);
                HashCombine(result, static_cast<std::uint8_t>(key.Slant));
                return result;
            }
        };

        [[nodiscard]] CacheKey MakeKey(const RuntimeUiTextLayoutRequest& request)
        {
            std::uint64_t generation = request.FontGeneration;
            std::uint64_t hash = request.FontGeneration == 0U ? HashBytes(request.FontBytes) : 0U;
            const auto combine = [](std::uint64_t& destination, const std::uint64_t value) noexcept
            { destination ^= value + 0x9e3779b97f4a7c15ULL + (destination << 6U) + (destination >> 2U); };
            combine(generation, request.CollectionIndex);
            for (const auto& face : request.FallbackFaces)
            {
                combine(generation, face.FontGeneration);
                combine(generation, face.CollectionIndex);
                if (face.FontGeneration == 0U)
                    combine(hash, HashBytes(face.FontBytes));
            }
            return {.FontGeneration = generation,
                    // Asset generations already provide an immutable content identity. Avoid hashing multi-megabyte
                    // font faces on every lookup; raw callers without generations retain content hashing.
                    .FontHash = hash,
                    .Text = std::string(request.Text),
                    .Language = std::string(request.Language),
                    .Direction = request.Direction,
                    .Wrap = request.Wrap,
                    .Overflow = request.Overflow,
                    .FontSize = request.FontSize,
                    .AvailableWidth = request.AvailableWidth,
                    .LineHeight = request.AuthoredLineHeight,
                    .LetterSpacing = request.LetterSpacing,
                    .WordSpacing = request.WordSpacing,
                    .MaximumLines = request.MaximumLines,
                    .Weight = request.Weight,
                    .Slant = request.Slant};
        }
    } // namespace

    std::size_t CountRuntimeUiMissingGlyphs(const std::span<const std::byte> fontBytes,
                                            const std::uint32_t collectionIndex, const std::string_view text)
    {
        if (fontBytes.empty())
            return DecodeUtf8(text).Codepoints.size();
        FreeTypeContext freeType;
        if (FT_Init_FreeType(&freeType.Library) != 0 ||
            fontBytes.size() > static_cast<std::size_t>((std::numeric_limits<FT_Long>::max)()) ||
            FT_New_Memory_Face(freeType.Library, reinterpret_cast<const FT_Byte*>(fontBytes.data()),
                               static_cast<FT_Long>(fontBytes.size()), collectionIndex, &freeType.Face) != 0)
        {
            throw std::runtime_error("FreeType could not inspect the runtime UI font face.");
        }
        std::size_t missing = 0;
        for (const auto codepoint : DecodeUtf8(text).Codepoints)
            if (codepoint != U'\n' && codepoint != U'\r' && codepoint != U'\t' &&
                FT_Get_Char_Index(freeType.Face, static_cast<FT_ULong>(codepoint)) == 0U)
            {
                ++missing;
            }
        return missing;
    }

    RuntimeUiTextLayout BuildRuntimeUiTextLayout(const RuntimeUiTextLayoutRequest& request)
    {
        if (!std::isfinite(request.FontSize) || request.FontSize <= 0.0F || request.FontSize > 4096.0F ||
            !std::isfinite(request.AvailableWidth) || request.AvailableWidth < 0.0F ||
            !std::isfinite(request.AuthoredLineHeight) || request.AuthoredLineHeight < 0.0F ||
            !std::isfinite(request.LetterSpacing) || !std::isfinite(request.WordSpacing) || request.Language.empty() ||
            request.Language.size() > 64U)
        {
            throw std::invalid_argument("Runtime UI text layout parameters are invalid.");
        }
        const auto decoded = DecodeUtf8(request.Text);
        RuntimeUiTextLayout result;
        result.UsedFontFallback = request.FontBytes.empty();
        std::vector<std::unique_ptr<LoadedTextFace>> loadedFaces;
        std::vector<hb_font_t*> fonts;
        std::vector<FT_Face> freeTypeFaces;
        if (!request.FontBytes.empty())
        {
            std::vector<RuntimeUiTextFace> faceRequests;
            faceRequests.reserve(1U + std::min(request.FallbackFaces.size(), MaximumFallbackFaces - 1U));
            faceRequests.push_back({request.FontBytes, request.FontGeneration, request.CollectionIndex});
            for (const auto& fallback : request.FallbackFaces)
                if (!fallback.FontBytes.empty() && faceRequests.size() < MaximumFallbackFaces)
                    faceRequests.push_back(fallback);
            loadedFaces.reserve(faceRequests.size());
            fonts.reserve(faceRequests.size());
            freeTypeFaces.reserve(faceRequests.size());
            for (const auto& faceRequest : faceRequests)
            {
                auto loaded = std::make_unique<LoadedTextFace>();
                if (FT_Init_FreeType(&loaded->FreeType.Library) != 0)
                    throw std::runtime_error("FreeType could not initialize for runtime UI shaping.");
                if (faceRequest.FontBytes.size() > static_cast<std::size_t>((std::numeric_limits<FT_Long>::max)()) ||
                    faceRequest.FontBytes.size() > (std::numeric_limits<unsigned int>::max)() ||
                    FT_New_Memory_Face(loaded->FreeType.Library,
                                       reinterpret_cast<const FT_Byte*>(faceRequest.FontBytes.data()),
                                       static_cast<FT_Long>(faceRequest.FontBytes.size()), faceRequest.CollectionIndex,
                                       &loaded->FreeType.Face) != 0)
                {
                    throw std::runtime_error("FreeType rejected a selected runtime UI font face or collection index.");
                }
                (void)FT_Set_Pixel_Sizes(loaded->FreeType.Face, 0, static_cast<FT_UInt>(std::ceil(request.FontSize)));
                loaded->Blob = OwnedHandle<hb_blob_t, hb_blob_destroy>(
                    hb_blob_create(reinterpret_cast<const char*>(faceRequest.FontBytes.data()),
                                   static_cast<unsigned int>(faceRequest.FontBytes.size()), HB_MEMORY_MODE_READONLY,
                                   nullptr, nullptr));
                loaded->HarfBuzzFace = OwnedHandle<hb_face_t, hb_face_destroy>(
                    hb_face_create(loaded->Blob.Get(), faceRequest.CollectionIndex));
                loaded->Font = OwnedHandle<hb_font_t, hb_font_destroy>(hb_font_create(loaded->HarfBuzzFace.Get()));
                if (!loaded->Blob.Get() || !loaded->HarfBuzzFace.Get() || !loaded->Font.Get())
                    throw std::runtime_error("HarfBuzz could not create a selected runtime UI font.");
                hb_ot_font_set_funcs(loaded->Font.Get());
                const auto scale = static_cast<int>(std::lround(request.FontSize * FixedPointScale));
                hb_font_set_scale(loaded->Font.Get(), scale, scale);
                std::array<hb_variation_t, 2> variations{};
                unsigned int variationCount = 0;
                variations[variationCount++] = {HB_TAG('w', 'g', 'h', 't'), static_cast<float>(request.Weight)};
                if (request.Slant != RuntimeUiFontSlant::Normal)
                    variations[variationCount++] = {HB_TAG('s', 'l', 'n', 't'), -12.0F};
                hb_font_set_variations(loaded->Font.Get(), variations.data(), variationCount);
                fonts.push_back(loaded->Font.Get());
                freeTypeFaces.push_back(loaded->FreeType.Face);
                loadedFaces.push_back(std::move(loaded));
            }
            if (freeTypeFaces.front()->size)
            {
                result.Ascender = static_cast<float>(freeTypeFaces.front()->size->metrics.ascender) / 64.0F;
                result.Descender = -static_cast<float>(freeTypeFaces.front()->size->metrics.descender) / 64.0F;
                result.LineHeight = static_cast<float>(freeTypeFaces.front()->size->metrics.height) / 64.0F;
            }

            std::size_t paragraphBegin = 0;
            while (paragraphBegin <= decoded.Codepoints.size())
            {
                auto paragraphEnd = paragraphBegin;
                while (paragraphEnd < decoded.Codepoints.size() && decoded.Codepoints[paragraphEnd] != U'\n')
                    ++paragraphEnd;
                auto runs =
                    ShapeParagraph(std::span(decoded.Codepoints).subspan(paragraphBegin, paragraphEnd - paragraphBegin),
                                   static_cast<std::uint32_t>(paragraphBegin), request, fonts, freeTypeFaces);
                for (auto& run : runs)
                    result.Glyphs.insert(result.Glyphs.end(), std::make_move_iterator(run.Glyphs.begin()),
                                         std::make_move_iterator(run.Glyphs.end()));
                if (paragraphEnd == decoded.Codepoints.size())
                    break;
                result.Glyphs.push_back({.Codepoint = U'\n', .Cluster = static_cast<std::uint32_t>(paragraphEnd)});
                paragraphBegin = paragraphEnd + 1U;
            }
            result.UsedFontFallback = std::ranges::any_of(
                result.Glyphs, [](const auto& glyph) { return glyph.Codepoint != U'\n' && glyph.FaceIndex != 0U; });
        }
        else
        {
            result.Ascender = request.FontSize;
            result.Descender = request.FontSize * 0.25F;
            result.LineHeight = request.FontSize * 1.35F;
            std::size_t paragraphBegin = 0;
            while (paragraphBegin <= decoded.Codepoints.size())
            {
                auto paragraphEnd = paragraphBegin;
                while (paragraphEnd < decoded.Codepoints.size() && decoded.Codepoints[paragraphEnd] != U'\n')
                    ++paragraphEnd;
                auto runs =
                    ShapeParagraph(std::span(decoded.Codepoints).subspan(paragraphBegin, paragraphEnd - paragraphBegin),
                                   static_cast<std::uint32_t>(paragraphBegin), request, {}, {});
                for (auto& run : runs)
                    result.Glyphs.insert(result.Glyphs.end(), std::make_move_iterator(run.Glyphs.begin()),
                                         std::make_move_iterator(run.Glyphs.end()));
                if (paragraphEnd == decoded.Codepoints.size())
                    break;
                result.Glyphs.push_back({.Codepoint = U'\n', .Cluster = static_cast<std::uint32_t>(paragraphEnd)});
                paragraphBegin = paragraphEnd + 1U;
            }
        }
        result.LineHeight = request.AuthoredLineHeight > 0.0F ? request.AuthoredLineHeight : result.LineHeight;
        if (result.LineHeight <= 0.0F)
            result.LineHeight = request.FontSize * 1.35F;

        std::vector<char> breaks(decoded.Codepoints.size(), LINEBREAK_NOBREAK);
        if (!decoded.Codepoints.empty())
        {
            const std::string language(request.Language);
            set_linebreaks_utf32(reinterpret_cast<const utf32_t*>(decoded.Codepoints.data()), decoded.Codepoints.size(),
                                 language.c_str(), breaks.data());
        }
        const float available = request.Wrap == RuntimeUiTextWrap::NoWrap || request.AvailableWidth <= 0.0F
                                    ? (std::numeric_limits<float>::max)()
                                    : request.AvailableWidth;
        std::size_t lineFirst = 0;
        float lineWidth = 0.0F;
        std::optional<std::size_t> lastBreak;
        float widthAtBreak = 0.0F;
        const auto commitLine = [&](const std::size_t end, const float width)
        {
            result.Lines.push_back({lineFirst, end - lineFirst, width});
            result.Width = std::max(result.Width, width);
        };
        for (std::size_t index = 0; index < result.Glyphs.size(); ++index)
        {
            auto& glyph = result.Glyphs[index];
            if (glyph.Codepoint == U'\n')
            {
                commitLine(index, lineWidth);
                lineFirst = index + 1U;
                lineWidth = 0.0F;
                lastBreak.reset();
                continue;
            }
            const bool breakable = glyph.Cluster < breaks.size() && (breaks[glyph.Cluster] == LINEBREAK_ALLOWBREAK ||
                                                                     breaks[glyph.Cluster] == LINEBREAK_MUSTBREAK);
            if (breakable)
            {
                lastBreak = index + 1U;
                widthAtBreak = lineWidth + glyph.Advance;
            }
            if (lineWidth + glyph.Advance > available && index > lineFirst)
            {
                const auto end = lastBreak && *lastBreak > lineFirst ? *lastBreak : index;
                commitLine(end, lastBreak && *lastBreak > lineFirst ? widthAtBreak : lineWidth);
                lineFirst = end;
                lineWidth = 0.0F;
                lastBreak.reset();
                for (std::size_t replay = lineFirst; replay < index; ++replay)
                    lineWidth += result.Glyphs[replay].Advance;
            }
            lineWidth += glyph.Advance;
        }
        if (lineFirst <= result.Glyphs.size())
            commitLine(result.Glyphs.size(), lineWidth);

        if (request.MaximumLines != 0 && result.Lines.size() > request.MaximumLines)
        {
            result.Truncated = true;
            result.Lines.resize(request.MaximumLines);
            const auto retained = result.Lines.back().FirstGlyph + result.Lines.back().GlyphCount;
            result.Glyphs.resize(retained);
            if (request.Overflow == RuntimeUiTextOverflow::Ellipsis && !result.Glyphs.empty())
            {
                auto& last = result.Glyphs.back();
                last.Codepoint = U'\u2026';
                if (!fonts.empty())
                {
                    std::size_t faceIndex = 0;
                    for (; faceIndex < freeTypeFaces.size(); ++faceIndex)
                        if (FT_Get_Char_Index(freeTypeFaces[faceIndex], static_cast<FT_ULong>(last.Codepoint)) != 0U)
                            break;
                    if (faceIndex == freeTypeFaces.size())
                        faceIndex = 0;
                    hb_codepoint_t glyph = 0;
                    if (hb_font_get_nominal_glyph(fonts[faceIndex], static_cast<hb_codepoint_t>(last.Codepoint),
                                                  &glyph) == 0)
                    {
                        last.Codepoint = U'.';
                        for (faceIndex = 0; faceIndex < freeTypeFaces.size(); ++faceIndex)
                            if (hb_font_get_nominal_glyph(fonts[faceIndex], static_cast<hb_codepoint_t>(last.Codepoint),
                                                          &glyph) != 0)
                                break;
                        if (faceIndex == freeTypeFaces.size())
                            faceIndex = 0;
                    }
                    last.Glyph = glyph;
                    last.FaceIndex = static_cast<std::uint16_t>(faceIndex);
                    last.Advance =
                        static_cast<float>(hb_font_get_glyph_h_advance(fonts[faceIndex], glyph)) / FixedPointScale +
                        request.LetterSpacing;
                }
                else
                {
                    last.Glyph = 0;
                    last.Advance = FallbackAdvance(last.Codepoint, request);
                }
            }
        }
        result.Width = 0.0F;
        for (std::size_t lineIndex = 0; lineIndex < result.Lines.size(); ++lineIndex)
        {
            auto& line = result.Lines[lineIndex];
            float position = 0.0F;
            const auto end = std::min(result.Glyphs.size(), line.FirstGlyph + line.GlyphCount);
            for (std::size_t index = line.FirstGlyph; index < end; ++index)
            {
                result.Glyphs[index].X = position;
                result.Glyphs[index].Y = static_cast<float>(lineIndex) * result.LineHeight;
                position += result.Glyphs[index].Advance;
            }
            line.Width = position;
            result.Width = std::max(result.Width, line.Width);
        }
        result.Height = static_cast<float>(result.Lines.size()) * result.LineHeight;
        return result;
    }

    class RuntimeUiTextLayoutCache::Impl final
    {
      public:
        struct Entry final
        {
            CacheKey Key;
            std::shared_ptr<const RuntimeUiTextLayout> Layout;
            std::uint64_t LastUse = 0;
        };

        explicit Impl(const std::size_t maximumEntries, const std::size_t maximumGlyphs)
            : MaximumEntries(std::max<std::size_t>(1U, maximumEntries)),
              MaximumGlyphs(std::max<std::size_t>(1U, maximumGlyphs))
        {
        }

        std::vector<Entry> Entries;
        std::size_t MaximumEntries = 0;
        std::size_t MaximumGlyphs = 0;
        std::size_t Glyphs = 0;
        std::uint64_t Clock = 0;
        RuntimeUiTextCacheStatistics Statistics;
    };

    RuntimeUiTextLayoutCache::RuntimeUiTextLayoutCache(const std::size_t maximumEntries,
                                                       const std::size_t maximumGlyphs)
        : m_Impl(std::make_unique<Impl>(maximumEntries, maximumGlyphs))
    {
    }

    RuntimeUiTextLayoutCache::~RuntimeUiTextLayoutCache() = default;
    RuntimeUiTextLayoutCache::RuntimeUiTextLayoutCache(RuntimeUiTextLayoutCache&&) noexcept = default;
    RuntimeUiTextLayoutCache& RuntimeUiTextLayoutCache::operator=(RuntimeUiTextLayoutCache&&) noexcept = default;

    std::shared_ptr<const RuntimeUiTextLayout>
    RuntimeUiTextLayoutCache::Resolve(const RuntimeUiTextLayoutRequest& request)
    {
        auto key = MakeKey(request);
        const auto found = std::ranges::find(m_Impl->Entries, key, &Impl::Entry::Key);
        if (found != m_Impl->Entries.end())
        {
            found->LastUse = ++m_Impl->Clock;
            ++m_Impl->Statistics.Hits;
            return found->Layout;
        }
        ++m_Impl->Statistics.Misses;
        auto layout = std::make_shared<const RuntimeUiTextLayout>(BuildRuntimeUiTextLayout(request));
        while (!m_Impl->Entries.empty() && (m_Impl->Entries.size() >= m_Impl->MaximumEntries ||
                                            m_Impl->Glyphs + layout->Glyphs.size() > m_Impl->MaximumGlyphs))
        {
            const auto eviction = std::ranges::min_element(m_Impl->Entries, {}, &Impl::Entry::LastUse);
            m_Impl->Glyphs -= eviction->Layout->Glyphs.size();
            m_Impl->Entries.erase(eviction);
            ++m_Impl->Statistics.Evictions;
        }
        if (layout->Glyphs.size() <= m_Impl->MaximumGlyphs)
        {
            m_Impl->Glyphs += layout->Glyphs.size();
            m_Impl->Entries.push_back({std::move(key), layout, ++m_Impl->Clock});
        }
        m_Impl->Statistics.Entries = m_Impl->Entries.size();
        m_Impl->Statistics.Glyphs = m_Impl->Glyphs;
        return layout;
    }

    void RuntimeUiTextLayoutCache::InvalidateFontGeneration(const std::uint64_t generation) noexcept
    {
        for (auto iterator = m_Impl->Entries.begin(); iterator != m_Impl->Entries.end();)
        {
            if (iterator->Key.FontGeneration != generation)
            {
                m_Impl->Glyphs -= iterator->Layout->Glyphs.size();
                iterator = m_Impl->Entries.erase(iterator);
            }
            else
                ++iterator;
        }
        m_Impl->Statistics.Entries = m_Impl->Entries.size();
        m_Impl->Statistics.Glyphs = m_Impl->Glyphs;
    }

    void RuntimeUiTextLayoutCache::Clear() noexcept
    {
        m_Impl->Entries.clear();
        m_Impl->Glyphs = 0;
        m_Impl->Statistics.Entries = 0;
        m_Impl->Statistics.Glyphs = 0;
    }

    RuntimeUiTextCacheStatistics RuntimeUiTextLayoutCache::Statistics() const noexcept { return m_Impl->Statistics; }
} // namespace Keire::Detail
