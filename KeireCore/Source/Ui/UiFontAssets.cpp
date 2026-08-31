#include "Keire/Ui/UiFontAssets.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <ranges>
#include <stdexcept>
#include <unordered_set>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::size_t MaximumFontBytes = 64ULL * 1024U * 1024U;
        constexpr std::size_t MaximumFamilyBytes = 1ULL * 1024U * 1024U;
        constexpr std::size_t MaximumFaces = 64;
        constexpr std::size_t MaximumFallbackFamilies = 16;

        [[nodiscard]] std::string_view FontStyleName(const UiFontStyle style) noexcept
        {
            switch (style)
            {
            case UiFontStyle::Normal:
                return "normal";
            case UiFontStyle::Italic:
                return "italic";
            case UiFontStyle::Oblique:
                return "oblique";
            }
            return "normal";
        }

        [[nodiscard]] UiFontStyle ParseFontStyle(const std::string_view value)
        {
            if (value == "normal")
                return UiFontStyle::Normal;
            if (value == "italic")
                return UiFontStyle::Italic;
            if (value == "oblique")
                return UiFontStyle::Oblique;
            throw std::runtime_error("UI font family contains an unsupported font style.");
        }

        [[nodiscard]] std::uint32_t BigEndianTag(const std::span<const std::byte> bytes) noexcept
        {
            return static_cast<std::uint32_t>(bytes[0]) << 24U | static_cast<std::uint32_t>(bytes[1]) << 16U |
                   static_cast<std::uint32_t>(bytes[2]) << 8U | static_cast<std::uint32_t>(bytes[3]);
        }

        [[nodiscard]] std::vector<std::byte> JsonBytes(const Json& value)
        {
            const auto text = value.dump(2) + '\n';
            std::vector<std::byte> result(text.size());
            std::memcpy(result.data(), text.data(), text.size());
            return result;
        }
    } // namespace

    UiFontFaceAsset::UiFontFaceAsset(std::vector<std::byte> bytes) : m_Bytes(std::move(bytes))
    {
        if (!m_Bytes.empty())
            Validate(m_Bytes);
    }

    void UiFontFaceAsset::Validate(const std::span<const std::byte> bytes)
    {
        if (bytes.size() < 12 || bytes.size() > MaximumFontBytes)
            throw std::invalid_argument("UI font face is empty, truncated, or exceeds the 64 MiB safety limit.");
        const auto signature = BigEndianTag(bytes);
        constexpr std::uint32_t TrueType = 0x00010000U;
        constexpr std::uint32_t OpenType = 0x4f54544fU;
        constexpr std::uint32_t Collection = 0x74746366U;
        constexpr std::uint32_t AppleTrueType = 0x74727565U;
        if (signature != TrueType && signature != OpenType && signature != Collection && signature != AppleTrueType)
            throw std::invalid_argument(
                "UI font face does not contain a supported TrueType, OpenType, or collection signature.");
    }

    UiFontFamilyAsset::UiFontFamilyAsset(UiFontFamilyDefinition definition) : m_Definition(std::move(definition))
    {
        if (!m_Definition.Name.empty() || !m_Definition.Faces.empty() || !m_Definition.FallbackFamilies.empty())
            Validate(m_Definition);
    }

    std::size_t UiFontFamilyAsset::ResidentBytes() const noexcept
    {
        return sizeof(*this) + m_Definition.Name.size() + m_Definition.Faces.size() * sizeof(UiFontFaceReference) +
               m_Definition.FallbackFamilies.size() * sizeof(AssetId);
    }

    void UiFontFamilyAsset::Validate(const UiFontFamilyDefinition& definition)
    {
        if (definition.SchemaVersion != 1 || definition.Name.empty() || definition.Name.size() > 256 ||
            definition.Faces.empty() || definition.Faces.size() > MaximumFaces ||
            definition.FallbackFamilies.size() > MaximumFallbackFamilies)
            throw std::invalid_argument("UI font family has an invalid schema, name, face count, or fallback count.");
        std::unordered_set<AssetId> faces;
        for (const auto& face : definition.Faces)
        {
            if (!face.Face || face.Weight < 1 || face.Weight > 1000 || face.Style > UiFontStyle::Oblique ||
                !faces.insert(face.Face).second)
                throw std::invalid_argument("UI font family contains an invalid or duplicate face mapping.");
        }
        std::unordered_set<AssetId> fallbacks;
        for (const auto fallback : definition.FallbackFamilies)
            if (!fallback || !fallbacks.insert(fallback).second)
                throw std::invalid_argument("UI font family contains an invalid or duplicate fallback family.");
    }

    Ref<UiFontFamilyAsset> UiFontFamilyAsset::Decode(const std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() > MaximumFamilyBytes)
            throw std::runtime_error("UI font family is empty or exceeds the 1 MiB safety limit.");
        try
        {
            const auto source = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                            reinterpret_cast<const char*>(bytes.data() + bytes.size()));
            UiFontFamilyDefinition definition;
            definition.SchemaVersion = source.value("schemaVersion", 0U);
            definition.Name = source.at("name").get<std::string>();
            for (const auto& face : source.at("faces"))
                definition.Faces.push_back({.Face = AssetId::Parse(face.at("face").get<std::string>()),
                                            .Weight = face.value("weight", std::uint16_t{400}),
                                            .Style = ParseFontStyle(face.value("style", std::string("normal"))),
                                            .CollectionIndex = face.value("collectionIndex", std::uint16_t{})});
            for (const auto& fallback : source.value("fallbackFamilies", Json::array()))
                definition.FallbackFamilies.push_back(AssetId::Parse(fallback.get<std::string>()));
            return CreateRef<UiFontFamilyAsset>(std::move(definition));
        }
        catch (const Json::exception& error)
        {
            throw std::runtime_error(std::string("UI font family JSON is malformed: ") + error.what());
        }
    }

    std::vector<std::byte> UiFontFamilyAsset::Encode(const UiFontFamilyDefinition& definition)
    {
        Validate(definition);
        Json faces = Json::array();
        for (const auto& face : definition.Faces)
            faces.push_back({{"face", face.Face.ToString()},
                             {"weight", face.Weight},
                             {"style", FontStyleName(face.Style)},
                             {"collectionIndex", face.CollectionIndex}});
        Json fallbacks = Json::array();
        for (const auto fallback : definition.FallbackFamilies)
            fallbacks.push_back(fallback.ToString());
        return JsonBytes({{"schemaVersion", definition.SchemaVersion},
                          {"name", definition.Name},
                          {"faces", std::move(faces)},
                          {"fallbackFamilies", std::move(fallbacks)}});
    }

    AssetImporterRegistration CreateUiFontFaceAssetImporter()
    {
        return {"Keire.UiFontFace",
                1,
                UiFontFaceAsset::StaticType(),
                {".ttf", ".otf", ".ttc"},
                [](const std::span<const std::byte> bytes)
                {
                    UiFontFaceAsset::Validate(bytes);
                    return std::vector(bytes.begin(), bytes.end());
                }};
    }

    AssetImporterRegistration CreateUiFontFamilyAssetImporter()
    {
        AssetImporterRegistration result{"Keire.UiFontFamily", 1, UiFontFamilyAsset::StaticType(), {".keirefont"}};
        result.ContextualImport = [](const AssetImportContext&, const std::span<const std::byte> bytes)
        {
            const auto family = UiFontFamilyAsset::Decode(bytes);
            AssetImportOutput output;
            output.Bytes = UiFontFamilyAsset::Encode(family->Definition());
            for (const auto& face : family->Definition().Faces)
                output.AssetDependencies.push_back(face.Face);
            output.AssetDependencies.insert(output.AssetDependencies.end(),
                                            family->Definition().FallbackFamilies.begin(),
                                            family->Definition().FallbackFamilies.end());
            std::ranges::sort(output.AssetDependencies);
            const auto unique = std::ranges::unique(output.AssetDependencies);
            output.AssetDependencies.erase(unique.begin(), unique.end());
            return output;
        };
        return result;
    }

    AssetDecoderRegistration CreateUiFontFaceAssetDecoder()
    {
        return {UiFontFaceAsset::StaticType(), CreateRef<UiFontFaceAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset>
                { return CreateRef<UiFontFaceAsset>(std::vector(bytes.begin(), bytes.end())); }};
    }

    AssetDecoderRegistration CreateUiFontFamilyAssetDecoder()
    {
        return {UiFontFamilyAsset::StaticType(), CreateRef<UiFontFamilyAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return UiFontFamilyAsset::Decode(bytes); }};
    }
} // namespace Keire
