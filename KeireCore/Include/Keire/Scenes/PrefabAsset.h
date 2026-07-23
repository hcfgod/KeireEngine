#pragma once

#include "Keire/Api.h"
#include "Keire/Scenes/SceneAsset.h"

#include <functional>

namespace Keire
{
    struct PrefabDefinition
    {
        std::uint32_t SchemaVersion = 1;
        AssetId BasePrefab;
        SceneDefinition Template;
    };

    class KEIRE_API PrefabAsset final : public Asset
    {
      public:
        explicit PrefabAsset(PrefabDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b45495245505245ULL, 0x4641424153535401ULL));
        }

        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const PrefabDefinition& Definition() const noexcept { return m_Definition; }

        [[nodiscard]] static Ref<PrefabAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const PrefabDefinition& definition);
        static void Validate(const PrefabDefinition& definition);

      private:
        PrefabDefinition m_Definition;
        std::size_t m_ResidentBytes = 0;
    };

    using PrefabResolver = std::function<Ref<PrefabAsset>(AssetId)>;

    [[nodiscard]] KEIRE_API SceneDefinition ComposePrefab(AssetId prefab, const PrefabResolver& resolver);
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreatePrefabAssetDecoder();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreatePrefabAssetImporter();
} // namespace Keire
