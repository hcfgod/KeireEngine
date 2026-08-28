#include "KeireInternal/Assets/TextureImportSettingsInternal.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <variant>

namespace Keire::Detail
{
    namespace
    {
        [[nodiscard]] bool IsLikelyPaletteAtlas(const TextureMipLevel& level)
        {
            const auto width = static_cast<std::size_t>(level.Width);
            const auto height = static_cast<std::size_t>(level.Height);
            if (width < 8U || height < 8U || height > std::numeric_limits<std::size_t>::max() / width)
                return false;
            const auto pixelCount = width * height;
            if (pixelCount > std::numeric_limits<std::size_t>::max() / 4U || level.Pixels.size() != pixelCount * 4U)
            {
                return false;
            }

            constexpr std::size_t MaximumSamples = 65'536;
            constexpr std::size_t MaximumQuantizedColors = 256;
            const auto stride = std::max(pixelCount / MaximumSamples, std::size_t{1});
            std::unordered_set<std::uint32_t> colors;
            colors.reserve(MaximumQuantizedColors + 1U);
            std::size_t sampled = 0;
            for (std::size_t pixel = 0; pixel < pixelCount; pixel += stride)
            {
                const auto offset = pixel * 4U;
                if (std::to_integer<std::uint8_t>(level.Pixels[offset + 3U]) == 0U)
                    continue;
                const auto red = std::to_integer<std::uint8_t>(level.Pixels[offset]) >> 3U;
                const auto green = std::to_integer<std::uint8_t>(level.Pixels[offset + 1U]) >> 3U;
                const auto blue = std::to_integer<std::uint8_t>(level.Pixels[offset + 2U]) >> 3U;
                colors.insert((static_cast<std::uint32_t>(red) << 10U) | (static_cast<std::uint32_t>(green) << 5U) |
                              blue);
                ++sampled;
                if (colors.size() > MaximumQuantizedColors)
                    return false;
            }
            return sampled >= 256U && colors.size() >= 4U && colors.size() * 48U <= sampled;
        }
    } // namespace

    void ApplyAtlasSampling(TextureImportSettings& settings) noexcept
    {
        settings.Mips = TextureMipPolicy::None;
        settings.Sampler.Minimum = TextureFilter::Nearest;
        settings.Sampler.Magnification = TextureFilter::Nearest;
        settings.Sampler.Mip = TextureFilter::Nearest;
        settings.Sampler.AddressU = TextureAddressMode::Clamp;
        settings.Sampler.AddressV = TextureAddressMode::Clamp;
    }

    bool ApplyAutomaticAtlasSampling(TextureImportSettings& settings, std::vector<TextureMipLevel>& mips)
    {
        if (settings.Semantic != TextureSemantic::Color || settings.Mips != TextureMipPolicy::Generate ||
            settings.Sampler.Minimum != TextureFilter::Linear ||
            settings.Sampler.Magnification != TextureFilter::Linear ||
            settings.Sampler.AddressU != TextureAddressMode::Repeat ||
            settings.Sampler.AddressV != TextureAddressMode::Repeat || mips.empty() ||
            !IsLikelyPaletteAtlas(mips.front()))
        {
            return false;
        }
        ApplyAtlasSampling(settings);
        mips.resize(1);
        return true;
    }

    TextureImportSettings NormalizeTextureSettings(TextureImportSettings settings)
    {
        const auto validSemantic =
            settings.Semantic == TextureSemantic::Color || settings.Semantic == TextureSemantic::Data ||
            settings.Semantic == TextureSemantic::Normal || settings.Semantic == TextureSemantic::Environment;
        const auto validColorSpace =
            settings.ColorSpace == TextureColorSpace::Linear || settings.ColorSpace == TextureColorSpace::Srgb;
        const auto validMipPolicy =
            settings.Mips == TextureMipPolicy::None || settings.Mips == TextureMipPolicy::Generate;
        const bool validEnvironmentLayout = settings.EnvironmentLayout >= TextureEnvironmentLayout::Auto &&
                                            settings.EnvironmentLayout <= TextureEnvironmentLayout::VerticalStrip;
        const auto validFilter = [](const TextureFilter filter)
        { return filter == TextureFilter::Nearest || filter == TextureFilter::Linear; };
        const auto validAddress = [](const TextureAddressMode mode)
        {
            return mode == TextureAddressMode::Repeat || mode == TextureAddressMode::Clamp ||
                   mode == TextureAddressMode::Mirror;
        };
        if (!validSemantic || !validColorSpace || !validMipPolicy || !validEnvironmentLayout ||
            !validFilter(settings.Sampler.Minimum) || !validFilter(settings.Sampler.Magnification) ||
            !validFilter(settings.Sampler.Mip) || !validAddress(settings.Sampler.AddressU) ||
            !validAddress(settings.Sampler.AddressV) || !validAddress(settings.Sampler.AddressW))
            throw std::invalid_argument("Texture import settings contain an invalid enum value.");
        if (settings.MaximumSize == 0 || settings.MaximumSize > MaximumTextureDimension ||
            settings.Sampler.Anisotropy == 0 || settings.Sampler.Anisotropy > 16)
            throw std::invalid_argument("Texture import settings contain invalid size or anisotropy limits.");
        if (settings.Semantic != TextureSemantic::Color)
            settings.ColorSpace = TextureColorSpace::Linear;
        if (settings.HighDynamicRange && settings.Semantic != TextureSemantic::Environment)
            throw std::invalid_argument("HDR RGBE storage is reserved for environment textures.");
        return settings;
    }

    TextureImportSettings ApplyTextureImportSettings(TextureImportSettings settings, const AssetImportSettings& values)
    {
        const auto choice = [&](const std::string_view key, const std::string& fallback)
        {
            const auto found = values.find(key);
            return found == values.end() ? fallback : std::get<std::string>(found->second);
        };
        const auto semantic = choice("semantic", "color");
        settings.Semantic = semantic == "normal"        ? TextureSemantic::Normal
                            : semantic == "data"        ? TextureSemantic::Data
                            : semantic == "environment" ? TextureSemantic::Environment
                                                        : TextureSemantic::Color;
        settings.ColorSpace =
            choice("colorSpace", "srgb") == "linear" ? TextureColorSpace::Linear : TextureColorSpace::Srgb;
        settings.Mips = choice("mips", "generate") == "none" ? TextureMipPolicy::None : TextureMipPolicy::Generate;
        const auto layout = choice("environmentLayout", "auto");
        settings.EnvironmentLayout = layout == "equirectangular"   ? TextureEnvironmentLayout::Equirectangular
                                     : layout == "horizontalCross" ? TextureEnvironmentLayout::HorizontalCross
                                     : layout == "verticalCross"   ? TextureEnvironmentLayout::VerticalCross
                                     : layout == "horizontalStrip" ? TextureEnvironmentLayout::HorizontalStrip
                                     : layout == "verticalStrip"   ? TextureEnvironmentLayout::VerticalStrip
                                                                   : TextureEnvironmentLayout::Auto;
        if (const auto found = values.find("maximumSize"); found != values.end())
            settings.MaximumSize = static_cast<std::uint32_t>(std::get<std::int64_t>(found->second));
        if (const auto found = values.find("flipGreen"); found != values.end())
            settings.FlipGreen = std::get<bool>(found->second);
        const auto filter = [&](const std::string_view key, const TextureFilter fallback)
        {
            return choice(key, fallback == TextureFilter::Nearest ? "nearest" : "linear") == "nearest"
                       ? TextureFilter::Nearest
                       : TextureFilter::Linear;
        };
        settings.Sampler.Minimum = filter("minFilter", settings.Sampler.Minimum);
        settings.Sampler.Magnification = filter("magFilter", settings.Sampler.Magnification);
        settings.Sampler.Mip = filter("mipFilter", settings.Sampler.Mip);
        const auto address = [&](const std::string_view key, const TextureAddressMode fallback)
        {
            const auto fallbackText = fallback == TextureAddressMode::Clamp    ? "clamp"
                                      : fallback == TextureAddressMode::Mirror ? "mirror"
                                                                               : "repeat";
            const auto value = choice(key, fallbackText);
            return value == "clamp"    ? TextureAddressMode::Clamp
                   : value == "mirror" ? TextureAddressMode::Mirror
                                       : TextureAddressMode::Repeat;
        };
        settings.Sampler.AddressU = address("addressU", settings.Sampler.AddressU);
        settings.Sampler.AddressV = address("addressV", settings.Sampler.AddressV);
        settings.Sampler.AddressW = address("addressW", settings.Sampler.AddressW);
        if (const auto found = values.find("anisotropy"); found != values.end())
            settings.Sampler.Anisotropy = static_cast<std::uint8_t>(std::get<std::int64_t>(found->second));
        return NormalizeTextureSettings(settings);
    }

    TextureImportSettings ReadTextureSettings(const std::filesystem::path& metadataPath, TextureImportSettings settings)
    {
        if (metadataPath.empty() || !std::filesystem::is_regular_file(metadataPath))
            return NormalizeTextureSettings(settings);
        std::ifstream stream(metadataPath, std::ios::binary);
        nlohmann::json metadata;
        stream >> metadata;
        if (!stream || !metadata.is_object())
            throw std::invalid_argument("Texture metadata is not valid JSON.");
        const auto found = metadata.find("textureImportSettings");
        if (found == metadata.end())
            return NormalizeTextureSettings(settings);
        if (!found->is_object())
            throw std::invalid_argument("textureImportSettings must be an object.");
        const auto& values = *found;
        if (values.contains("semantic"))
        {
            const auto semantic = values.at("semantic").get<std::string>();
            if (semantic == "color")
                settings.Semantic = TextureSemantic::Color;
            else if (semantic == "data")
                settings.Semantic = TextureSemantic::Data;
            else if (semantic == "normal")
                settings.Semantic = TextureSemantic::Normal;
            else if (semantic == "environment")
                settings.Semantic = TextureSemantic::Environment;
            else
                throw std::invalid_argument("Texture semantic must be color, data, normal, or environment.");
        }
        if (values.contains("colorSpace"))
        {
            const auto colorSpace = values.at("colorSpace").get<std::string>();
            if (colorSpace == "linear")
                settings.ColorSpace = TextureColorSpace::Linear;
            else if (colorSpace == "srgb")
                settings.ColorSpace = TextureColorSpace::Srgb;
            else
                throw std::invalid_argument("Texture colorSpace must be linear or srgb.");
        }
        if (values.contains("mips"))
        {
            const auto mips = values.at("mips").get<std::string>();
            if (mips == "none")
                settings.Mips = TextureMipPolicy::None;
            else if (mips == "generate")
                settings.Mips = TextureMipPolicy::Generate;
            else
                throw std::invalid_argument("Texture mips must be none or generate.");
        }
        if (values.contains("environmentLayout"))
        {
            const auto layout = values.at("environmentLayout").get<std::string>();
            settings.EnvironmentLayout = layout == "equirectangular"   ? TextureEnvironmentLayout::Equirectangular
                                         : layout == "horizontalCross" ? TextureEnvironmentLayout::HorizontalCross
                                         : layout == "verticalCross"   ? TextureEnvironmentLayout::VerticalCross
                                         : layout == "horizontalStrip" ? TextureEnvironmentLayout::HorizontalStrip
                                         : layout == "verticalStrip"   ? TextureEnvironmentLayout::VerticalStrip
                                                                       : TextureEnvironmentLayout::Auto;
        }
        settings.MaximumSize = values.value("maximumSize", settings.MaximumSize);
        settings.FlipGreen = values.value("flipGreen", settings.FlipGreen);
        if (const auto sampler = values.find("sampler"); sampler != values.end())
        {
            if (!sampler->is_object())
                throw std::invalid_argument("Texture sampler settings must be an object.");
            const auto filter = [](const std::string& value)
            {
                if (value == "nearest")
                    return TextureFilter::Nearest;
                if (value == "linear")
                    return TextureFilter::Linear;
                throw std::invalid_argument("Texture filter must be nearest or linear.");
            };
            const auto address = [](const std::string& value)
            {
                if (value == "repeat")
                    return TextureAddressMode::Repeat;
                if (value == "clamp")
                    return TextureAddressMode::Clamp;
                if (value == "mirror")
                    return TextureAddressMode::Mirror;
                throw std::invalid_argument("Texture address mode must be repeat, clamp, or mirror.");
            };
            if (sampler->contains("min"))
                settings.Sampler.Minimum = filter(sampler->at("min").get<std::string>());
            if (sampler->contains("mag"))
                settings.Sampler.Magnification = filter(sampler->at("mag").get<std::string>());
            if (sampler->contains("mip"))
                settings.Sampler.Mip = filter(sampler->at("mip").get<std::string>());
            if (sampler->contains("addressU"))
                settings.Sampler.AddressU = address(sampler->at("addressU").get<std::string>());
            if (sampler->contains("addressV"))
                settings.Sampler.AddressV = address(sampler->at("addressV").get<std::string>());
            if (sampler->contains("addressW"))
                settings.Sampler.AddressW = address(sampler->at("addressW").get<std::string>());
            if (sampler->contains("anisotropy"))
            {
                const auto anisotropy = sampler->at("anisotropy").get<unsigned int>();
                if (anisotropy > std::numeric_limits<std::uint8_t>::max())
                    throw std::invalid_argument("Texture anisotropy exceeds its encoded range.");
                settings.Sampler.Anisotropy = static_cast<std::uint8_t>(anisotropy);
            }
        }
        return NormalizeTextureSettings(settings);
    }
} // namespace Keire::Detail
