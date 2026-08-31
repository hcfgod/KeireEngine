#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace Keire
{
    enum class UiFontStyle : std::uint8_t
    {
        Normal,
        Italic,
        Oblique
    };

    struct UiFontFaceReference
    {
        AssetId Face;
        std::uint16_t Weight = 400;
        UiFontStyle Style = UiFontStyle::Normal;
        std::uint16_t CollectionIndex = 0;

        [[nodiscard]] bool operator==(const UiFontFaceReference&) const = default;
    };

    struct UiFontFamilyDefinition
    {
        std::uint32_t SchemaVersion = 1;
        std::string Name;
        std::vector<UiFontFaceReference> Faces;
        std::vector<AssetId> FallbackFamilies;

        [[nodiscard]] bool operator==(const UiFontFamilyDefinition&) const = default;
    };

    class KEIRE_API UiFontFaceAsset final : public Asset
    {
      public:
        explicit UiFontFaceAsset(std::vector<std::byte> bytes = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245554946ULL, 0x4f4e544641434501ULL));
        }

        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override { return sizeof(*this) + m_Bytes.size(); }
        [[nodiscard]] std::span<const std::byte> Bytes() const noexcept { return m_Bytes; }

        static void Validate(std::span<const std::byte> bytes);

      private:
        std::vector<std::byte> m_Bytes;
    };

    class KEIRE_API UiFontFamilyAsset final : public Asset
    {
      public:
        explicit UiFontFamilyAsset(UiFontFamilyDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245554946ULL, 0x4f4e5446414d0001ULL));
        }

        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const UiFontFamilyDefinition& Definition() const noexcept { return m_Definition; }

        [[nodiscard]] static Ref<UiFontFamilyAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const UiFontFamilyDefinition& definition);
        static void Validate(const UiFontFamilyDefinition& definition);

      private:
        UiFontFamilyDefinition m_Definition;
    };

    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateUiFontFaceAssetImporter();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateUiFontFamilyAssetImporter();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateUiFontFaceAssetDecoder();
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateUiFontFamilyAssetDecoder();
} // namespace Keire
