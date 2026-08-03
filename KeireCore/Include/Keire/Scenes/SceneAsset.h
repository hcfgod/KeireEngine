#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/ECS/Component.h"
#include "Keire/ECS/EntityLayer.h"
#include "Keire/Math/Math.h"
#include "Keire/Rendering/Lighting.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    inline constexpr std::uint32_t CurrentSceneSchemaVersion = 5;

    using SceneVector3 = Vector3;
    using SceneQuaternion = Quaternion;

    struct SceneTransform
    {
        SceneVector3 Position;
        SceneQuaternion Rotation;
        SceneVector3 Scale{1.0F, 1.0F, 1.0F};
        auto operator<=>(const SceneTransform&) const noexcept = default;
    };

    struct SceneComponentDefinition
    {
        ComponentTypeId Type;
        std::uint32_t SchemaVersion = 1;
        bool Enabled = true;
        std::string Data;
    };

    struct SceneObjectDefinition
    {
        AssetId Id;
        AssetId Parent;
        std::string Name;
        bool Active = true;
        SceneTransform Transform;
        std::vector<SceneComponentDefinition> Components;
        std::uint32_t Layer = 0;
    };

    enum class PrefabOverrideKind : std::uint8_t
    {
        RenameObject,
        SetObjectActive,
        SetObjectTransform,
        SetComponentProperty,
        AddComponent,
        RemoveComponent,
        AddObject,
        RemoveObject,
        SetObjectLayer
    };

    struct PrefabOverrideDefinition
    {
        PrefabOverrideKind Kind = PrefabOverrideKind::SetComponentProperty;
        AssetId Object;
        ComponentTypeId Component;
        std::string Property;
        ComponentPropertyValue Value = false;
        std::string Name;
        bool Active = true;
        std::uint32_t Layer = 0;
        SceneTransform Transform;
        std::optional<SceneComponentDefinition> AddedComponent;
        std::optional<SceneObjectDefinition> AddedObject;
    };

    struct PrefabObjectMapping
    {
        AssetId Source;
        AssetId Instance;
    };

    struct PrefabInstanceDefinition
    {
        AssetId Prefab;
        AssetId Root;
        std::vector<PrefabObjectMapping> Objects;
        std::vector<PrefabOverrideDefinition> Overrides;
    };

    struct SceneDefinition
    {
        std::uint32_t SchemaVersion = CurrentSceneSchemaVersion;
        std::string Name;
        std::vector<SceneObjectDefinition> Objects;
        std::vector<PrefabInstanceDefinition> PrefabInstances;
        std::vector<PrefabOverrideDefinition> PrefabOverrides;
        LightingBakeSettings Lighting;
        AssetId BakedLighting;
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
        [[nodiscard]] static SceneDefinition SampleDefinition(AssetId material);
        static void Validate(const SceneDefinition& definition);

      private:
        SceneDefinition m_Definition;
        std::size_t m_ResidentBytes = 0;
    };

    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateSceneAssetDecoder();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateSceneAssetImporter();
} // namespace Keire
