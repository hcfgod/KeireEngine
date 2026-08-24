#include "KeireClient/Editor/ThumbnailArtwork.h"

#include "KeireClient/Editor/ThumbnailService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor::Detail
{
    namespace
    {
        void PutPixel(std::vector<std::byte>& pixels, const std::uint32_t width, const std::uint32_t x,
                      const std::uint32_t y, const std::array<std::uint8_t, 3> color)
        {
            const auto offset = (static_cast<std::size_t>(y) * width + x) * 4U;
            pixels[offset] = static_cast<std::byte>(color[0]);
            pixels[offset + 1] = static_cast<std::byte>(color[1]);
            pixels[offset + 2] = static_cast<std::byte>(color[2]);
            pixels[offset + 3] = std::byte{255};
        }

        void DrawLine(std::vector<std::byte>& pixels, const std::uint32_t width, const std::uint32_t height, int x0,
                      int y0, const int x1, const int y1, const std::array<std::uint8_t, 3> color)
        {
            const int dx = std::abs(x1 - x0);
            const int sx = x0 < x1 ? 1 : -1;
            const int dy = -std::abs(y1 - y0);
            const int sy = y0 < y1 ? 1 : -1;
            int error = dx + dy;
            for (;;)
            {
                if (x0 >= 0 && y0 >= 0 && x0 < static_cast<int>(width) && y0 < static_cast<int>(height))
                    PutPixel(pixels, width, static_cast<std::uint32_t>(x0), static_cast<std::uint32_t>(y0), color);
                if (x0 == x1 && y0 == y1)
                    break;
                const int doubled = error * 2;
                if (doubled >= dy)
                {
                    error += dy;
                    x0 += sx;
                }
                if (doubled <= dx)
                {
                    error += dx;
                    y0 += sy;
                }
            }
        }

        void FillRectangle(std::vector<std::byte>& pixels, const std::uint32_t width, const std::uint32_t height,
                           const int left, const int top, const int right, const int bottom,
                           const std::array<std::uint8_t, 3> color)
        {
            for (int y = std::max(top, 0); y < std::min(bottom, static_cast<int>(height)); ++y)
                for (int x = std::max(left, 0); x < std::min(right, static_cast<int>(width)); ++x)
                    PutPixel(pixels, width, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y), color);
        }

        [[nodiscard]] std::array<std::uint8_t, 5> GlyphRows(const char glyph) noexcept
        {
            switch (glyph)
            {
            case '#':
                return {0b101, 0b111, 0b101, 0b111, 0b101};
            case 'A':
                return {0b010, 0b101, 0b111, 0b101, 0b101};
            case 'C':
                return {0b111, 0b100, 0b100, 0b100, 0b111};
            case 'D':
                return {0b110, 0b101, 0b101, 0b101, 0b110};
            case 'F':
                return {0b111, 0b100, 0b110, 0b100, 0b100};
            case 'G':
                return {0b111, 0b100, 0b101, 0b101, 0b111};
            case 'L':
                return {0b100, 0b100, 0b100, 0b100, 0b111};
            case 'M':
                return {0b101, 0b111, 0b111, 0b101, 0b101};
            case 'P':
                return {0b110, 0b101, 0b110, 0b100, 0b100};
            case 'R':
                return {0b110, 0b101, 0b110, 0b101, 0b101};
            case 'S':
                return {0b111, 0b100, 0b111, 0b001, 0b111};
            case 'T':
                return {0b111, 0b010, 0b010, 0b010, 0b010};
            case 'U':
                return {0b101, 0b101, 0b101, 0b101, 0b111};
            case 'X':
                return {0b101, 0b101, 0b010, 0b101, 0b101};
            default:
                return {};
            }
        }

        void ApplyBadge(std::vector<std::byte>& pixels, const std::uint32_t width, const std::uint32_t height,
                        const std::string_view text)
        {
            if (width < 24U || height < 16U || text.size() != 2U)
                return;
            constexpr std::uint32_t badgeWidth = 22;
            constexpr std::uint32_t badgeHeight = 14;
            const auto left = width - badgeWidth - 3U;
            const auto top = height - badgeHeight - 3U;
            FillRectangle(pixels, width, height, static_cast<int>(left), static_cast<int>(top),
                          static_cast<int>(left + badgeWidth), static_cast<int>(top + badgeHeight), {20, 25, 34});
            DrawLine(pixels, width, height, static_cast<int>(left), static_cast<int>(top),
                     static_cast<int>(left + badgeWidth - 1U), static_cast<int>(top), {229, 178, 65});
            DrawLine(pixels, width, height, static_cast<int>(left), static_cast<int>(top + badgeHeight - 1U),
                     static_cast<int>(left + badgeWidth - 1U), static_cast<int>(top + badgeHeight - 1U),
                     {229, 178, 65});
            for (std::size_t character = 0; character < text.size(); ++character)
            {
                const auto rows = GlyphRows(text[character]);
                const auto glyphLeft = left + 4U + static_cast<std::uint32_t>(character) * 8U;
                for (std::uint32_t row = 0; row < rows.size(); ++row)
                    for (std::uint32_t column = 0; column < 3U; ++column)
                        if ((rows[row] & (1U << (2U - column))) != 0U)
                            for (std::uint32_t pixelY = 0; pixelY < 2U; ++pixelY)
                                for (std::uint32_t pixelX = 0; pixelX < 2U; ++pixelX)
                                    PutPixel(pixels, width, glyphLeft + column * 2U + pixelX,
                                             top + 2U + row * 2U + pixelY, {248, 225, 151});
            }
        }

        [[nodiscard]] std::vector<std::byte> MakeCanvas(const std::uint32_t width, const std::uint32_t height,
                                                        const std::array<std::uint8_t, 3> background,
                                                        const std::array<std::uint8_t, 3> accent)
        {
            std::vector<std::byte> result(static_cast<std::size_t>(width) * height * 4U);
            for (std::uint32_t y = 0; y < height; ++y)
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    const auto lift = static_cast<std::uint8_t>((10U * y + 5U * x) / std::max(width + height, 1U));
                    const bool edge = x < 2U || y < 2U || x + 2U >= width || y + 2U >= height;
                    PutPixel(
                        result, width, x, y,
                        edge ? std::array<std::uint8_t, 3>{static_cast<std::uint8_t>((background[0] + accent[0]) / 2U),
                                                           static_cast<std::uint8_t>((background[1] + accent[1]) / 2U),
                                                           static_cast<std::uint8_t>((background[2] + accent[2]) / 2U)}
                             : std::array<std::uint8_t, 3>{
                                   static_cast<std::uint8_t>(std::min<unsigned>(background[0] + lift, 255U)),
                                   static_cast<std::uint8_t>(std::min<unsigned>(background[1] + lift, 255U)),
                                   static_cast<std::uint8_t>(std::min<unsigned>(background[2] + lift, 255U))});
                }
            return result;
        }

        void ApplyMissingMark(std::vector<std::byte>& pixels, const std::uint32_t width, const std::uint32_t height,
                              const bool missing)
        {
            if (!missing)
                return;
            const auto inset = static_cast<int>(std::max(7U, std::min(width, height) / 10U));
            DrawLine(pixels, width, height, inset, inset, static_cast<int>(width) - inset - 1,
                     static_cast<int>(height) - inset - 1, {239, 74, 88});
            DrawLine(pixels, width, height, inset + 1, inset, static_cast<int>(width) - inset,
                     static_cast<int>(height) - inset - 1, {239, 74, 88});
            DrawLine(pixels, width, height, static_cast<int>(width) - inset - 1, inset, inset,
                     static_cast<int>(height) - inset - 1, {239, 74, 88});
            DrawLine(pixels, width, height, static_cast<int>(width) - inset, inset, inset + 1,
                     static_cast<int>(height) - inset - 1, {239, 74, 88});
        }
    } // namespace

    std::vector<std::byte> MakeDocumentThumbnail(const ThumbnailRequest& request, const std::uint32_t width,
                                                 const std::uint32_t height, const std::string_view badge)
    {
        constexpr std::array background{std::uint8_t{31}, std::uint8_t{38}, std::uint8_t{49}};
        constexpr std::array accent{std::uint8_t{103}, std::uint8_t{167}, std::uint8_t{224}};
        auto result = MakeCanvas(width, height, background, accent);
        if (width < 40U || height < 40U)
            return result;

        const int left = static_cast<int>(width / 4U);
        const int top = static_cast<int>(height / 7U);
        const int right = static_cast<int>(width - width / 4U);
        const int bottom = static_cast<int>(height - height / 7U);
        const int fold = std::max(8, static_cast<int>(width / 8U));
        FillRectangle(result, width, height, left + 2, top + 3, right + 2, bottom + 3, {17, 23, 31});
        FillRectangle(result, width, height, left, top, right, bottom, {218, 227, 238});
        FillRectangle(result, width, height, left, top, left + 4, bottom, accent);
        FillRectangle(result, width, height, right - fold, top, right, top + fold, {143, 165, 188});
        DrawLine(result, width, height, right - fold, top, right - fold, top + fold, {93, 119, 145});
        DrawLine(result, width, height, right - fold, top + fold, right, top + fold, {93, 119, 145});
        for (int line = 0; line < 5; ++line)
        {
            const int y = top + fold + 8 + line * std::max(5, static_cast<int>(height / 14U));
            const int lineRight = right - 7 - (line == 1 || line == 4 ? 10 : 0);
            DrawLine(result, width, height, left + 10, y, lineRight, y, {100, 119, 140});
            if (line == 2)
                DrawLine(result, width, height, left + 10, y + 1, lineRight - 7, y + 1, {100, 119, 140});
        }
        ApplyBadge(result, width, height, badge);
        ApplyMissingMark(result, width, height, request.Missing);
        return result;
    }

    std::vector<std::byte> MakeCodeThumbnail(const ThumbnailRequest& request, const std::uint32_t width,
                                             const std::uint32_t height, const std::string_view badge)
    {
        constexpr std::array background{std::uint8_t{25}, std::uint8_t{39}, std::uint8_t{54}};
        constexpr std::array accent{std::uint8_t{67}, std::uint8_t{174}, std::uint8_t{232}};
        auto result = MakeCanvas(width, height, background, accent);
        if (width < 40U || height < 40U)
            return result;

        const int left = static_cast<int>(width / 7U);
        const int top = static_cast<int>(height / 6U);
        const int right = static_cast<int>(width - width / 7U);
        const int bottom = static_cast<int>(height - height / 6U);
        FillRectangle(result, width, height, left + 2, top + 3, right + 2, bottom + 3, {12, 20, 30});
        FillRectangle(result, width, height, left, top, right, bottom, {25, 36, 49});
        FillRectangle(result, width, height, left, top, left + 4, bottom, accent);
        constexpr std::array punctuation{std::uint8_t{125}, std::uint8_t{211}, std::uint8_t{246}};
        DrawLine(result, width, height, left + 17, top + 14, left + 11, top + 20, punctuation);
        DrawLine(result, width, height, left + 11, top + 20, left + 17, top + 26, punctuation);
        DrawLine(result, width, height, right - 17, top + 14, right - 11, top + 20, punctuation);
        DrawLine(result, width, height, right - 11, top + 20, right - 17, top + 26, punctuation);
        FillRectangle(result, width, height, left + 25, top + 12, right - 25, top + 15, {224, 173, 94});
        FillRectangle(result, width, height, left + 25, top + 22, right - 32, top + 25, {116, 206, 167});
        FillRectangle(result, width, height, left + 18, top + 36, right - 21, top + 39, {133, 151, 177});
        FillRectangle(result, width, height, left + 25, top + 46, right - 31, top + 49, {187, 136, 224});
        ApplyBadge(result, width, height, badge);
        ApplyMissingMark(result, width, height, request.Missing);
        return result;
    }

    std::vector<std::byte> MakeNodeGraphThumbnail(const ThumbnailRequest& request, const std::uint32_t width,
                                                  const std::uint32_t height,
                                                  const std::array<std::uint8_t, 3> background,
                                                  const std::array<std::uint8_t, 3> accent,
                                                  const std::string_view badge)
    {
        auto result = MakeCanvas(width, height, background, accent);
        if (width < 40U || height < 40U)
            return result;
        const std::array centers{std::pair{static_cast<int>(width / 4U), static_cast<int>(height / 3U)},
                                 std::pair{static_cast<int>(width / 2U), static_cast<int>(height * 2U / 3U)},
                                 std::pair{static_cast<int>(width * 3U / 4U), static_cast<int>(height / 3U)}};
        DrawLine(result, width, height, centers[0].first, centers[0].second, centers[1].first, centers[1].second,
                 {111, 126, 153});
        DrawLine(result, width, height, centers[1].first, centers[1].second, centers[2].first, centers[2].second,
                 {111, 126, 153});
        for (const auto [x, y] : centers)
        {
            FillRectangle(result, width, height, x - 9, y - 7, x + 10, y + 8, {20, 26, 37});
            FillRectangle(result, width, height, x - 7, y - 5, x + 8, y + 6, accent);
            FillRectangle(result, width, height, x - 4, y - 2, x + 5, y + 3, {30, 37, 50});
        }
        ApplyBadge(result, width, height, badge);
        ApplyMissingMark(result, width, height, request.Missing);
        return result;
    }

    std::vector<std::byte> MakeAssemblyThumbnail(const ThumbnailRequest& request, const std::uint32_t width,
                                                 const std::uint32_t height)
    {
        auto result = MakeNodeGraphThumbnail(request, width, height, {29, 36, 50}, {96, 153, 232}, "AS");
        if (width >= 40U && height >= 40U)
        {
            const int centerX = static_cast<int>(width / 2U);
            const int centerY = static_cast<int>(height / 2U);
            FillRectangle(result, width, height, centerX - 12, centerY - 9, centerX + 13, centerY + 10,
                          {218, 226, 240});
            FillRectangle(result, width, height, centerX - 8, centerY - 5, centerX + 9, centerY + 6, {54, 75, 108});
        }
        return result;
    }

    std::vector<std::byte> MakeDataThumbnail(const ThumbnailRequest& request, const std::uint32_t width,
                                             const std::uint32_t height)
    {
        auto result = MakeCanvas(width, height, {40, 30, 49}, {185, 112, 225});
        if (width >= 40U && height >= 40U)
        {
            const int left = static_cast<int>(width / 5U);
            const int right = static_cast<int>(width - width / 5U);
            const int top = static_cast<int>(height / 5U);
            const int rowHeight = std::max(8, static_cast<int>(height / 7U));
            for (int row = 0; row < 4; ++row)
            {
                const int y = top + row * rowHeight;
                FillRectangle(result, width, height, left, y, right, y + rowHeight - 3,
                              row % 2 == 0 ? std::array<std::uint8_t, 3>{72, 51, 88}
                                           : std::array<std::uint8_t, 3>{57, 43, 72});
                FillRectangle(result, width, height, left + 4, y + 3, left + 9, y + rowHeight - 6, {203, 145, 233});
                FillRectangle(result, width, height, left + 14, y + 3, right - 5, y + rowHeight - 6, {130, 101, 151});
            }
        }
        ApplyBadge(result, width, height, "DT");
        ApplyMissingMark(result, width, height, request.Missing);
        return result;
    }
} // namespace KeireEditor::Detail
