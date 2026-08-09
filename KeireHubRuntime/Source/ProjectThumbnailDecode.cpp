#include <KeireHubRuntimeInternal/ProjectThumbnailDecode.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#define STB_IMAGE_STATIC
#define STBI_FAILURE_USERMSG
#define STBI_MAX_DIMENSIONS 4096
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_RESIZE_STATIC
#define STBIR_NO_SIMD
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>

namespace KeireHub::Detail
{
    namespace
    {
        constexpr std::uint64_t MaximumSourcePixels = 16ULL * 1024ULL * 1024ULL;
        constexpr int RgbaChannels = 4;

        struct StbiImageDeleter final
        {
            void operator()(unsigned char* pixels) const noexcept { stbi_image_free(pixels); }
        };

        [[nodiscard]] std::string DecodeFailure()
        {
            if (const auto* reason = stbi_failure_reason(); reason && *reason != '\0')
                return reason;
            return "The PNG decoder did not provide failure details.";
        }
    } // namespace

    std::optional<ProjectThumbnailImage> DecodeProjectThumbnail(const std::filesystem::path& path,
                                                                const std::uint64_t encodedSize,
                                                                std::string& details) noexcept
    {
        try
        {
            if (encodedSize == 0 || encodedSize > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ||
                encodedSize > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
            {
                details = "The encoded thumbnail size cannot be decoded safely.";
                return std::nullopt;
            }

            std::vector<unsigned char> encoded(static_cast<std::size_t>(encodedSize));
            std::ifstream stream(path, std::ios::binary);
            stream.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
            if (!stream || stream.peek() != std::ifstream::traits_type::eof())
            {
                details = "The thumbnail changed or could not be read completely.";
                return std::nullopt;
            }

            int sourceWidth = 0;
            int sourceHeight = 0;
            int sourceChannels = 0;
            if (stbi_info_from_memory(encoded.data(), static_cast<int>(encoded.size()), &sourceWidth, &sourceHeight,
                                      &sourceChannels) == 0 ||
                sourceWidth <= 0 || sourceHeight <= 0 ||
                static_cast<std::uint64_t>(sourceWidth) * static_cast<std::uint64_t>(sourceHeight) >
                    MaximumSourcePixels)
            {
                details = DecodeFailure();
                return std::nullopt;
            }

            std::unique_ptr<unsigned char, StbiImageDeleter> decoded(
                stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &sourceWidth, &sourceHeight,
                                      &sourceChannels, RgbaChannels));
            if (!decoded)
            {
                details = DecodeFailure();
                return std::nullopt;
            }

            int cropWidth = sourceWidth;
            int cropHeight = sourceHeight;
            const auto sourceAspect = static_cast<std::int64_t>(sourceWidth) * ProjectThumbnailImage::PixelHeight;
            const auto targetAspect = static_cast<std::int64_t>(sourceHeight) * ProjectThumbnailImage::PixelWidth;
            if (sourceAspect > targetAspect)
            {
                cropWidth = static_cast<int>(static_cast<std::int64_t>(sourceHeight) *
                                             ProjectThumbnailImage::PixelWidth / ProjectThumbnailImage::PixelHeight);
            }
            else if (sourceAspect < targetAspect)
            {
                cropHeight = static_cast<int>(static_cast<std::int64_t>(sourceWidth) *
                                              ProjectThumbnailImage::PixelHeight / ProjectThumbnailImage::PixelWidth);
            }
            cropWidth = std::clamp(cropWidth, 1, sourceWidth);
            cropHeight = std::clamp(cropHeight, 1, sourceHeight);
            const int cropX = (sourceWidth - cropWidth) / 2;
            const int cropY = (sourceHeight - cropHeight) / 2;
            const auto* cropped =
                decoded.get() + (static_cast<std::size_t>(cropY) * static_cast<std::size_t>(sourceWidth) +
                                 static_cast<std::size_t>(cropX)) *
                                    RgbaChannels;

            constexpr std::size_t outputBytes = static_cast<std::size_t>(ProjectThumbnailImage::PixelWidth) *
                                                ProjectThumbnailImage::PixelHeight * RgbaChannels;
            std::vector<std::byte> output(outputBytes);
            if (!stbir_resize_uint8_srgb(cropped, cropWidth, cropHeight, sourceWidth * RgbaChannels,
                                         reinterpret_cast<unsigned char*>(output.data()),
                                         static_cast<int>(ProjectThumbnailImage::PixelWidth),
                                         static_cast<int>(ProjectThumbnailImage::PixelHeight), 0, STBIR_RGBA))
            {
                details = "The thumbnail could not be normalized for display.";
                return std::nullopt;
            }

            details.clear();
            return ProjectThumbnailImage{.Width = ProjectThumbnailImage::PixelWidth,
                                         .Height = ProjectThumbnailImage::PixelHeight,
                                         .RgbaPixels =
                                             std::make_shared<const std::vector<std::byte>>(std::move(output))};
        }
        catch (const std::exception& error)
        {
            details = error.what();
            return std::nullopt;
        }
        catch (...)
        {
            details = "An unknown thumbnail decoding failure occurred.";
            return std::nullopt;
        }
    }
} // namespace KeireHub::Detail

namespace KeireHub
{
    std::optional<ProjectThumbnailImage>
    DecodeHubPngImage(const std::filesystem::path& path, const std::uint64_t encodedSize, std::string& details) noexcept
    {
        return Detail::DecodeProjectThumbnail(path, encodedSize, details);
    }
} // namespace KeireHub
