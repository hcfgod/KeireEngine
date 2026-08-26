#include "KeireInternal/Assets/TextureImportSettingsInternal.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace Keire::Detail
{
    namespace
    {
        [[nodiscard]] AssetImportOptionDescriptor Choice(std::string key, std::string name, std::string group,
                                                         std::string value, std::vector<std::string> choices)
        {
            return {std::move(key),
                    std::move(name),
                    std::move(group),
                    AssetImportOptionKind::Choice,
                    std::move(value),
                    {},
                    {},
                    1.0,
                    std::move(choices)};
        }

        [[nodiscard]] std::string Lowercase(std::string value)
        {
            std::ranges::transform(value, value.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return value;
        }
    } // namespace

    std::vector<AssetImportOptionDescriptor> TextureImportOptionDescriptors()
    {
        return {
            Choice("semantic", "Semantic", "Texture", "color", {"color", "data", "normal", "environment"}),
            Choice("colorSpace", "Color Space", "Texture", "srgb", {"srgb", "linear"}),
            Choice("mips", "Mip Maps", "Texture", "generate", {"generate", "none"}),
            Choice("environmentLayout", "Environment Layout", "Environment", "auto",
                   {"auto", "equirectangular", "horizontalCross", "verticalCross", "horizontalStrip",
                    "verticalStrip"}),
            {"maximumSize", "Maximum Size", "Texture", AssetImportOptionKind::Integer,
             std::int64_t{MaximumTextureDimension}, 1.0, static_cast<double>(MaximumTextureDimension), 1.0},
            {"flipGreen", "Flip Green Channel", "Texture", AssetImportOptionKind::Boolean, false},
            Choice("minFilter", "Min Filter", "Sampler", "linear", {"linear", "nearest"}),
            Choice("magFilter", "Mag Filter", "Sampler", "linear", {"linear", "nearest"}),
            Choice("mipFilter", "Mip Filter", "Sampler", "linear", {"linear", "nearest"}),
            Choice("addressU", "Address U", "Sampler", "repeat", {"repeat", "clamp", "mirror"}),
            Choice("addressV", "Address V", "Sampler", "repeat", {"repeat", "clamp", "mirror"}),
            Choice("addressW", "Address W", "Sampler", "repeat", {"repeat", "clamp", "mirror"}),
            {"anisotropy", "Anisotropy", "Sampler", AssetImportOptionKind::Integer, std::int64_t{1}, 1.0, 16.0,
             1.0}};
    }

    AssetImportSettings NormalizeTextureImportOptionValues(TextureImportSettings settings,
                                                            const AssetImportSettings& values)
    {
        const auto normalized = ApplyTextureImportSettings(std::move(settings), values);
        auto result = values;
        if (normalized.Semantic != TextureSemantic::Color)
            result["colorSpace"] = std::string("linear");
        return result;
    }

    AssetImportSettings SuggestTextureImportOptionValues(const std::filesystem::path& path,
                                                          const AssetImportSettings& defaults)
    {
        auto result = defaults;
        std::string stem = Lowercase(path.stem().string());
        for (char& value : stem)
            if (!std::isalnum(static_cast<unsigned char>(value)))
                value = ' ';

        const auto containsToken = [&stem](const std::string_view token)
        {
            std::size_t offset = 0;
            while (offset < stem.size())
            {
                offset = stem.find_first_not_of(' ', offset);
                if (offset == std::string::npos)
                    return false;
                const auto end = stem.find(' ', offset);
                if (stem.substr(offset, end - offset) == token)
                    return true;
                offset = end == std::string::npos ? stem.size() : end + 1;
            }
            return false;
        };
        const bool normal =
            containsToken("normal") || containsToken("norm") || containsToken("nrm") || containsToken("nor");
        const bool data = containsToken("metallic") || containsToken("metal") || containsToken("roughness") ||
                          containsToken("rough") || containsToken("occlusion") || containsToken("ao") ||
                          containsToken("orm") || containsToken("rma") || containsToken("mra") ||
                          containsToken("mask") || containsToken("pbr");
        const auto extension = Lowercase(path.extension().string());
        if (extension == ".hdr")
        {
            result["semantic"] = std::string("environment");
            result["colorSpace"] = std::string("linear");
            result["mips"] = std::string("none");
            result["addressV"] = std::string("clamp");
            result["environmentLayout"] = std::string("equirectangular");
        }
        else if (extension == ".exr")
        {
            result["colorSpace"] = std::string("linear");
        }
        else if (normal)
        {
            result["semantic"] = std::string("normal");
            result["colorSpace"] = std::string("linear");
        }
        else if (data)
        {
            result["semantic"] = std::string("data");
            result["colorSpace"] = std::string("linear");
        }
        return result;
    }
} // namespace Keire::Detail
