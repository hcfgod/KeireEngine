#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    struct ThumbnailRequest;

    namespace Detail
    {
        [[nodiscard]] std::vector<std::byte> MakeDocumentThumbnail(const ThumbnailRequest& request, std::uint32_t width,
                                                                   std::uint32_t height, std::string_view badge = "TX");
        [[nodiscard]] std::vector<std::byte> MakeCodeThumbnail(const ThumbnailRequest& request, std::uint32_t width,
                                                               std::uint32_t height, std::string_view badge = "C#");
        [[nodiscard]] std::vector<std::byte> MakeNodeGraphThumbnail(const ThumbnailRequest& request,
                                                                    std::uint32_t width, std::uint32_t height,
                                                                    std::array<std::uint8_t, 3> background,
                                                                    std::array<std::uint8_t, 3> accent,
                                                                    std::string_view badge);
        [[nodiscard]] std::vector<std::byte> MakeAssemblyThumbnail(const ThumbnailRequest& request, std::uint32_t width,
                                                                   std::uint32_t height);
        [[nodiscard]] std::vector<std::byte> MakeDataThumbnail(const ThumbnailRequest& request, std::uint32_t width,
                                                               std::uint32_t height);
    } // namespace Detail
} // namespace KeireEditor
