#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Animation/RiggingSystem.h"
#include "Keire/Assets/RenderingAssets.h"

#include "KeireInternal/Assets/AssimpProjectIO.h"
#include "KeireInternal/Assets/BuiltinMeshes.h"
#include "KeireInternal/Assets/ImportedMaterialShader.h"
#include "KeireInternal/Assets/TextureAssetImportInternal.h"
#include "KeireInternal/Assets/TextureImportBackend.h"
#include "KeireInternal/Assets/TextureImportSettingsInternal.h"

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/matrix3x3.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Keire
{
    namespace
    {
        [[nodiscard]] std::string Lowercase(std::string value)
        {
            std::ranges::transform(value, value.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return value;
        }

        [[nodiscard]] float Dot(const Vector3 left, const Vector3 right) noexcept
        {
            return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
        }

        [[nodiscard]] Vector3 Normalize(const Vector3 value, const Vector3 fallback) noexcept
        {
            const auto lengthSquared = Dot(value, value);
            if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12F)
                return fallback;
            const auto inverseLength = 1.0F / std::sqrt(lengthSquared);
            return {value.X * inverseLength, value.Y * inverseLength, value.Z * inverseLength};
        }

        [[nodiscard]] TextureMipLevel DownsampleInternal(const TextureMipLevel& source, const bool normalMap)
        {
            TextureMipLevel result;
            result.Width = std::max(source.Width / 2U, 1U);
            result.Height = std::max(source.Height / 2U, 1U);
            result.Pixels.resize(static_cast<std::size_t>(result.Width) * result.Height * 4U);
            for (std::uint32_t y = 0; y < result.Height; ++y)
            {
                for (std::uint32_t x = 0; x < result.Width; ++x)
                {
                    if (normalMap)
                    {
                        Vector3 normal;
                        for (std::uint32_t oy = 0; oy < 2; ++oy)
                        {
                            const auto sourceY = std::min(y * 2U + oy, source.Height - 1U);
                            for (std::uint32_t ox = 0; ox < 2; ++ox)
                            {
                                const auto sourceX = std::min(x * 2U + ox, source.Width - 1U);
                                const auto sourceIndex =
                                    (static_cast<std::size_t>(sourceY) * source.Width + sourceX) * 4U;
                                normal.X +=
                                    static_cast<float>(std::to_integer<std::uint8_t>(source.Pixels[sourceIndex])) /
                                        127.5F -
                                    1.0F;
                                normal.Y +=
                                    static_cast<float>(std::to_integer<std::uint8_t>(source.Pixels[sourceIndex + 1])) /
                                        127.5F -
                                    1.0F;
                                normal.Z +=
                                    static_cast<float>(std::to_integer<std::uint8_t>(source.Pixels[sourceIndex + 2])) /
                                        127.5F -
                                    1.0F;
                            }
                        }
                        normal = Normalize(normal, {0.0F, 0.0F, 1.0F});
                        const auto targetIndex = (static_cast<std::size_t>(y) * result.Width + x) * 4U;
                        result.Pixels[targetIndex] = std::byte(static_cast<std::uint8_t>((normal.X + 1.0F) * 127.5F));
                        result.Pixels[targetIndex + 1] =
                            std::byte(static_cast<std::uint8_t>((normal.Y + 1.0F) * 127.5F));
                        result.Pixels[targetIndex + 2] =
                            std::byte(static_cast<std::uint8_t>((normal.Z + 1.0F) * 127.5F));
                        result.Pixels[targetIndex + 3] = std::byte{255};
                    }
                    else
                    {
                        for (std::uint32_t channel = 0; channel < 4; ++channel)
                        {
                            std::uint32_t total = 0;
                            std::uint32_t samples = 0;
                            for (std::uint32_t oy = 0; oy < 2; ++oy)
                            {
                                const auto sourceY = std::min(y * 2U + oy, source.Height - 1U);
                                for (std::uint32_t ox = 0; ox < 2; ++ox)
                                {
                                    const auto sourceX = std::min(x * 2U + ox, source.Width - 1U);
                                    const auto sourceIndex =
                                        (static_cast<std::size_t>(sourceY) * source.Width + sourceX) * 4U + channel;
                                    total += std::to_integer<std::uint8_t>(source.Pixels[sourceIndex]);
                                    ++samples;
                                }
                            }
                            const auto targetIndex = (static_cast<std::size_t>(y) * result.Width + x) * 4U + channel;
                            result.Pixels[targetIndex] = std::byte((total + samples / 2U) / samples);
                        }
                    }
                }
            }
            return result;
        }

        [[nodiscard]] TextureMipLevel DownsampleRgbe(const TextureMipLevel& source)
        {
            TextureMipLevel result;
            result.Width = std::max(source.Width / 2U, 1U);
            result.Height = std::max(source.Height / 2U, 1U);
            result.Pixels.resize(static_cast<std::size_t>(result.Width) * result.Height * 4U);
            const auto decode = [&source](const std::uint32_t x, const std::uint32_t y)
            {
                const auto index = (static_cast<std::size_t>(y) * source.Width + x) * 4U;
                const auto exponent = std::to_integer<std::uint8_t>(source.Pixels[index + 3U]);
                if (exponent == 0)
                    return Vector3{};
                const float scale = std::ldexp(1.0F, static_cast<int>(exponent) - 136);
                return Vector3{static_cast<float>(std::to_integer<std::uint8_t>(source.Pixels[index])) * scale,
                               static_cast<float>(std::to_integer<std::uint8_t>(source.Pixels[index + 1U])) * scale,
                               static_cast<float>(std::to_integer<std::uint8_t>(source.Pixels[index + 2U])) * scale};
            };
            for (std::uint32_t y = 0; y < result.Height; ++y)
                for (std::uint32_t x = 0; x < result.Width; ++x)
                {
                    Vector3 radiance;
                    for (std::uint32_t offsetY = 0; offsetY < 2; ++offsetY)
                        for (std::uint32_t offsetX = 0; offsetX < 2; ++offsetX)
                        {
                            const auto sample = decode(std::min(x * 2U + offsetX, source.Width - 1U),
                                                       std::min(y * 2U + offsetY, source.Height - 1U));
                            radiance.X += sample.X * 0.25F;
                            radiance.Y += sample.Y * 0.25F;
                            radiance.Z += sample.Z * 0.25F;
                        }
                    const float maximum = std::max({radiance.X, radiance.Y, radiance.Z});
                    if (maximum < 1.0e-32F)
                        continue;
                    int exponent = 0;
                    const float scale = std::frexp(maximum, &exponent) * 256.0F / maximum;
                    const auto index = (static_cast<std::size_t>(y) * result.Width + x) * 4U;
                    result.Pixels[index] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(radiance.X * scale, 0.0F, 255.0F)));
                    result.Pixels[index + 1U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(radiance.Y * scale, 0.0F, 255.0F)));
                    result.Pixels[index + 2U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(radiance.Z * scale, 0.0F, 255.0F)));
                    result.Pixels[index + 3U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(exponent + 128, 0, 255)));
                }
            return result;
        }

        [[nodiscard]] bool IsOpenExr(const std::span<const std::byte> bytes) noexcept
        {
            constexpr std::array magic{std::byte{0x76}, std::byte{0x2f}, std::byte{0x31}, std::byte{0x01}};
            return bytes.size() >= magic.size() && std::ranges::equal(magic, bytes.first(magic.size()));
        }

        [[nodiscard]] std::vector<TextureMipLevel> ImportFloatTexture(Detail::DecodedFloatTexture decoded,
                                                                      const TextureImportSettings& settings)
        {
            if (decoded.Width == 0 || decoded.Height == 0 || decoded.Width > Detail::MaximumTextureDimension ||
                decoded.Height > Detail::MaximumTextureDimension ||
                decoded.Pixels.size() != static_cast<std::size_t>(decoded.Width) * decoded.Height * 4U)
                throw std::invalid_argument("OpenEXR decoder returned invalid RGBA dimensions or storage.");

            TextureMipLevel base;
            base.Width = decoded.Width;
            base.Height = decoded.Height;
            base.Pixels.resize(decoded.Pixels.size());
            if (settings.Semantic == TextureSemantic::Environment)
            {
                for (std::size_t pixel = 0; pixel < decoded.Pixels.size() / 4U; ++pixel)
                {
                    const float red = std::max(decoded.Pixels[pixel * 4U], 0.0F);
                    const float green = std::max(decoded.Pixels[pixel * 4U + 1U], 0.0F);
                    const float blue = std::max(decoded.Pixels[pixel * 4U + 2U], 0.0F);
                    const float maximum = std::max({red, green, blue});
                    if (!std::isfinite(maximum))
                        throw std::invalid_argument("OpenEXR texture contains a non-finite radiance value.");
                    if (maximum < 1.0e-32F)
                        continue;
                    int exponent = 0;
                    const float scale = std::frexp(maximum, &exponent) * 256.0F / maximum;
                    base.Pixels[pixel * 4U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(red * scale, 0.0F, 255.0F)));
                    base.Pixels[pixel * 4U + 1U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(green * scale, 0.0F, 255.0F)));
                    base.Pixels[pixel * 4U + 2U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(blue * scale, 0.0F, 255.0F)));
                    base.Pixels[pixel * 4U + 3U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(exponent + 128, 0, 255)));
                }
                while (base.Width > settings.MaximumSize || base.Height > settings.MaximumSize)
                    base = DownsampleRgbe(base);
                std::vector<TextureMipLevel> result{std::move(base)};
                if (settings.Mips == TextureMipPolicy::Generate)
                    while (result.back().Width > 1 || result.back().Height > 1)
                        result.push_back(DownsampleRgbe(result.back()));
                return result;
            }

            for (std::size_t index = 0; index < decoded.Pixels.size(); ++index)
            {
                const auto value = decoded.Pixels[index];
                if (!std::isfinite(value))
                    throw std::invalid_argument("OpenEXR texture contains a non-finite channel value.");
                base.Pixels[index] =
                    std::byte(static_cast<std::uint8_t>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F));
            }
            if (settings.Semantic == TextureSemantic::Normal && settings.FlipGreen)
                for (std::size_t index = 1; index < base.Pixels.size(); index += 4)
                    base.Pixels[index] = std::byte(255U - std::to_integer<std::uint8_t>(base.Pixels[index]));
            while (base.Width > settings.MaximumSize || base.Height > settings.MaximumSize)
                base = DownsampleInternal(base, settings.Semantic == TextureSemantic::Normal);
            std::vector<TextureMipLevel> result{std::move(base)};
            if (settings.Mips == TextureMipPolicy::Generate)
                while (result.back().Width > 1 || result.back().Height > 1)
                    result.push_back(DownsampleInternal(result.back(), settings.Semantic == TextureSemantic::Normal));
            return result;
        }

        [[nodiscard]] std::vector<TextureMipLevel>
        ImportTextureInternal(const std::span<const std::byte> bytes, const TextureImportSettings& settings,
                              const Detail::TextureDecodeBackend& backend,
                              std::optional<Detail::DecodedFloatTexture> decoded = std::nullopt)
        {
            if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                throw std::invalid_argument("Texture source is empty or exceeds the decoder limit.");
            if (IsOpenExr(bytes))
            {
                if (!decoded)
                {
                    if (!backend)
                        throw std::invalid_argument("OpenEXR decoding is unavailable in this asset-import process.");
                    decoded = backend(bytes);
                }
                return ImportFloatTexture(std::move(*decoded), settings);
            }
            int width = 0;
            int height = 0;
            int channels = 0;
            if (settings.Semantic == TextureSemantic::Environment &&
                stbi_is_hdr_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size())))
            {
                std::unique_ptr<float, decltype(&stbi_image_free)> pixels(
                    stbi_loadf_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                           static_cast<int>(bytes.size()), &width, &height, &channels, 4),
                    stbi_image_free);
                if (!pixels || width <= 0 || height <= 0 || width > static_cast<int>(Detail::MaximumTextureDimension) ||
                    height > static_cast<int>(Detail::MaximumTextureDimension))
                    throw std::invalid_argument(std::string("HDR texture decode failed: ") + stbi_failure_reason());
                TextureMipLevel base;
                base.Width = static_cast<std::uint32_t>(width);
                base.Height = static_cast<std::uint32_t>(height);
                base.Pixels.resize(static_cast<std::size_t>(base.Width) * base.Height * 4U);
                for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(base.Width) * base.Height; ++pixel)
                {
                    const float red = std::max(pixels.get()[pixel * 4U], 0.0F);
                    const float green = std::max(pixels.get()[pixel * 4U + 1U], 0.0F);
                    const float blue = std::max(pixels.get()[pixel * 4U + 2U], 0.0F);
                    const float maximum = std::max({red, green, blue});
                    if (!std::isfinite(maximum))
                        throw std::invalid_argument("HDR texture contains a non-finite radiance value.");
                    if (maximum < 1.0e-32F)
                        continue;
                    int exponent = 0;
                    const float scale = std::frexp(maximum, &exponent) * 256.0F / maximum;
                    base.Pixels[pixel * 4U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(red * scale, 0.0F, 255.0F)));
                    base.Pixels[pixel * 4U + 1U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(green * scale, 0.0F, 255.0F)));
                    base.Pixels[pixel * 4U + 2U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(blue * scale, 0.0F, 255.0F)));
                    base.Pixels[pixel * 4U + 3U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(exponent + 128, 0, 255)));
                }
                while (base.Width > settings.MaximumSize || base.Height > settings.MaximumSize)
                    base = DownsampleRgbe(base);
                std::vector<TextureMipLevel> result{std::move(base)};
                if (settings.Mips == TextureMipPolicy::Generate)
                {
                    while (result.back().Width > 1 || result.back().Height > 1)
                        result.push_back(DownsampleRgbe(result.back()));
                }
                return result;
            }
            std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
                stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size()),
                                      &width, &height, &channels, 4),
                stbi_image_free);
            if (!pixels || width <= 0 || height <= 0 || width > static_cast<int>(Detail::MaximumTextureDimension) ||
                height > static_cast<int>(Detail::MaximumTextureDimension))
                throw std::invalid_argument(std::string("Texture decode failed: ") + stbi_failure_reason());
            TextureMipLevel base;
            base.Width = static_cast<std::uint32_t>(width);
            base.Height = static_cast<std::uint32_t>(height);
            base.Pixels.resize(static_cast<std::size_t>(base.Width) * base.Height * 4U);
            std::memcpy(base.Pixels.data(), pixels.get(), base.Pixels.size());
            if (settings.Semantic == TextureSemantic::Normal && settings.FlipGreen)
            {
                for (std::size_t index = 1; index < base.Pixels.size(); index += 4)
                    base.Pixels[index] = std::byte(255U - std::to_integer<std::uint8_t>(base.Pixels[index]));
            }
            while (base.Width > settings.MaximumSize || base.Height > settings.MaximumSize)
                base = DownsampleInternal(base, settings.Semantic == TextureSemantic::Normal);
            std::vector<TextureMipLevel> result{std::move(base)};
            if (settings.Mips == TextureMipPolicy::Generate)
            {
                while (result.back().Width > 1 || result.back().Height > 1)
                    result.push_back(DownsampleInternal(result.back(), settings.Semantic == TextureSemantic::Normal));
            }
            return result;
        }
    } // namespace

    TextureMipLevel Detail::DownsampleImportedTexture(const TextureMipLevel& source, const bool normalMap)
    {
        return DownsampleInternal(source, normalMap);
    }

    std::vector<TextureMipLevel> Detail::ImportTexturePayload(const std::span<const std::byte> bytes,
                                                              const TextureImportSettings& settings,
                                                              const TextureDecodeBackend& backend,
                                                              std::optional<DecodedFloatTexture> decoded)
    {
        return ImportTextureInternal(bytes, settings, backend, std::move(decoded));
    }

    AssetImporterRegistration Detail::CreateTexture2DAssetImporter(TextureImportSettings settings,
                                                                   TextureDecodeBackend backend)
    {
        settings = Detail::NormalizeTextureSettings(settings);
        AssetImporterRegistration result;
        result.Name = "Keire.Texture2D";
        result.Version = 5;
        result.Type = Texture2DAsset::StaticType();
        result.Extensions = {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr", ".exr"};
        result.Import = [settings, backend](const std::span<const std::byte> bytes)
        {
            auto effective = settings;
            if (stbi_is_hdr_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                        static_cast<int>(bytes.size())) != 0)
            {
                effective.Semantic = TextureSemantic::Environment;
                effective.ColorSpace = TextureColorSpace::Linear;
                effective.Mips = TextureMipPolicy::Generate;
                effective.EnvironmentLayout = TextureEnvironmentLayout::Equirectangular;
                effective.HighDynamicRange = true;
                effective.Sampler.AddressU = TextureAddressMode::Repeat;
                effective.Sampler.AddressV = TextureAddressMode::Clamp;
            }
            return Texture2DAsset::Encode(effective, Detail::ImportTexturePayload(bytes, effective, backend));
        };
        result.ContextualImport = [settings, backend](const AssetImportContext& context,
                                                      const std::span<const std::byte> bytes) -> AssetImportOutput
        {
            auto effective = Detail::ReadTextureSettings(context.MetadataPath, settings);
            if (!context.ImportSettings.empty())
                effective = Detail::ApplyTextureImportSettings(effective, context.ImportSettings);
            const auto extension = Lowercase(context.SourcePath.extension().string());
            std::optional<DecodedFloatTexture> decoded;
            if (extension == ".exr")
            {
                if (!backend)
                    throw std::invalid_argument("OpenEXR decoding is unavailable in this asset-import process.");
                decoded = backend(bytes);
                effective.HighDynamicRange = effective.Semantic == TextureSemantic::Environment;
            }
            if (extension == ".hdr")
            {
                effective.Semantic = TextureSemantic::Environment;
                effective.ColorSpace = TextureColorSpace::Linear;
                effective.Mips = TextureMipPolicy::Generate;
                effective.HighDynamicRange = true;
                effective.Sampler.AddressU = TextureAddressMode::Repeat;
                effective.Sampler.AddressV = TextureAddressMode::Clamp;
                if (effective.EnvironmentLayout == TextureEnvironmentLayout::Auto)
                    effective.EnvironmentLayout = TextureEnvironmentLayout::Equirectangular;
            }
            else if (effective.Semantic == TextureSemantic::Environment &&
                     effective.EnvironmentLayout == TextureEnvironmentLayout::Auto)
            {
                int width = 0;
                int height = 0;
                int channels = 0;
                if (decoded)
                {
                    width = static_cast<int>(decoded->Width);
                    height = static_cast<int>(decoded->Height);
                }
                else if (!stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                                static_cast<int>(bytes.size()), &width, &height, &channels))
                {
                    throw std::invalid_argument(std::string("Environment texture probe failed: ") +
                                                stbi_failure_reason());
                }
                if (width == height * 2)
                    effective.EnvironmentLayout = TextureEnvironmentLayout::Equirectangular;
                else if (width * 3 == height * 4)
                    effective.EnvironmentLayout = TextureEnvironmentLayout::HorizontalCross;
                else if (width * 4 == height * 3)
                    effective.EnvironmentLayout = TextureEnvironmentLayout::VerticalCross;
                else if (width == height * 6)
                    effective.EnvironmentLayout = TextureEnvironmentLayout::HorizontalStrip;
                else if (height == width * 6)
                    effective.EnvironmentLayout = TextureEnvironmentLayout::VerticalStrip;
                else
                    throw std::invalid_argument(
                        "Environment texture must be 2:1 equirectangular, a 4x3/3x4 cross, or a 6x1/1x6 strip.");
            }
            return {Texture2DAsset::Encode(
                effective, Detail::ImportTexturePayload(bytes, effective, backend, std::move(decoded)))};
        };
        result.ImportOptions = Detail::TextureImportOptionDescriptors();
        result.NormalizeImportSettings = [settings](const AssetImportSettings& values)
        { return Detail::NormalizeTextureImportOptionValues(settings, values); };
        result.SuggestImportSettings = Detail::SuggestTextureImportOptionValues;
        result.RestoreCachedOutput = [](const std::span<const std::byte> bytes)
        {
            AssetImportOutput output;
            output.Bytes.assign(bytes.begin(), bytes.end());
            return output;
        };
        return result;
    }

    AssetImporterRegistration CreateTexture2DAssetImporter(TextureImportSettings settings)
    {
        return Detail::CreateTexture2DAssetImporter(std::move(settings), {});
    }

} // namespace Keire
