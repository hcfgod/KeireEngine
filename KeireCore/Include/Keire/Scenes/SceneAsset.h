#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    struct SceneVector3
    {
        float X = 0.0F;
        float Y = 0.0F;
        float Z = 0.0F;
        auto operator<=>(const SceneVector3&) const noexcept = default;
    };

    struct SceneQuaternion
    {
        float X = 0.0F;
        float Y = 0.0F;
        float Z = 0.0F;
        float W = 1.0F;
        auto operator<=>(const SceneQuaternion&) const noexcept = default;
    };

    struct SceneTransform
    {
        SceneVector3 Position;
        SceneQuaternion Rotation;
        SceneVector3 Scale{1.0F, 1.0F, 1.0F};
        auto operator<=>(const SceneTransform&) const noexcept = default;
    };

    struct SceneObjectDefinition
    {
        AssetId Id;
        AssetId Parent;
        std::string Name;
        bool Active = true;
        SceneTransform Transform;
    };

    struct SceneDefinition
    {
        std::uint32_t SchemaVersion = 1;
        std::string Name;
        std::vector<SceneObjectDefinition> Objects;
    };

    class KEIRE_API SceneAsset final : public Asset
    {
      public:
        explicit SceneAsset(SceneDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245534345ULL, 0x4e45415353455401ULL));
        }

        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const SceneDefinition& Definition() const noexcept { return m_Definition; }
        [[nodiscard]] const SceneObjectDefinition* FindObject(AssetId id) const noexcept;

        [[nodiscard]] static Ref<SceneAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const SceneDefinition& definition);
        [[nodiscard]] static SceneDefinition EmptyDefinition(std::string name = "Untitled");
        [[nodiscard]] static SceneDefinition SampleDefinition();
        static void Validate(const SceneDefinition& definition);

      private:
        SceneDefinition m_Definition;
        std::size_t m_ResidentBytes = 0;
    };

    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateSceneAssetDecoder();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateSceneAssetImporter();
} // namespace Keire
