#pragma once

#include "Keire/Scripting/ScriptSystem.h"

#include <map>
#include <memory>

namespace Keire
{
    class AssetSystem;
    class Scene;
} // namespace Keire

namespace Keire::Detail
{
    class ManagedMaterialParameterStore final
    {
      public:
        ManagedMaterialParameterStore();
        ~ManagedMaterialParameterStore();

        ManagedMaterialParameterStore(const ManagedMaterialParameterStore&) = delete;
        ManagedMaterialParameterStore& operator=(const ManagedMaterialParameterStore&) = delete;

        [[nodiscard]] bool Ready(const Ref<AssetSystem>& assets, AssetId collection);
        [[nodiscard]] bool Set(const Ref<AssetSystem>& assets, AssetId collection, std::string_view name,
                               MaterialPropertyValue value);
        [[nodiscard]] bool Reset(const Ref<AssetSystem>& assets, AssetId collection, std::string_view name);
        [[nodiscard]] bool Clear(const Ref<AssetSystem>& assets, AssetId collection);
        [[nodiscard]] std::map<std::string, MaterialPropertyValue, std::less<>> Snapshot();
        void Close() noexcept;

      private:
        class Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    class ManagedRuntimeSceneServices : public IScriptRuntimeServices
    {
      public:
        ~ManagedRuntimeSceneServices() override = default;

        [[nodiscard]] std::optional<float>
        ReadManagedRenderingScalar(AssetId entity, ManagedRenderingComponent component,
                                   ManagedRenderingScalarProperty property) noexcept override;
        [[nodiscard]] bool SetManagedRenderingScalar(AssetId entity, ManagedRenderingComponent component,
                                                     ManagedRenderingScalarProperty property,
                                                     float value) noexcept override;
        [[nodiscard]] std::optional<std::int32_t>
        ReadManagedRenderingInteger(AssetId entity, ManagedRenderingComponent component,
                                    ManagedRenderingIntegerProperty property) noexcept override;
        [[nodiscard]] bool SetManagedRenderingInteger(AssetId entity, ManagedRenderingComponent component,
                                                      ManagedRenderingIntegerProperty property,
                                                      std::int32_t value) noexcept override;
        [[nodiscard]] std::optional<bool>
        ReadManagedRenderingFlag(AssetId entity, ManagedRenderingComponent component,
                                 ManagedRenderingFlagProperty property) noexcept override;
        [[nodiscard]] bool SetManagedRenderingFlag(AssetId entity, ManagedRenderingComponent component,
                                                   ManagedRenderingFlagProperty property, bool value) noexcept override;
        [[nodiscard]] std::optional<Vector2>
        ReadManagedRenderingVector(AssetId entity, ManagedRenderingComponent component,
                                   ManagedRenderingVectorProperty property) noexcept override;
        [[nodiscard]] bool SetManagedRenderingVector(AssetId entity, ManagedRenderingComponent component,
                                                     ManagedRenderingVectorProperty property,
                                                     Vector2 value) noexcept override;
        [[nodiscard]] std::optional<Color>
        ReadManagedRenderingColor(AssetId entity, ManagedRenderingComponent component,
                                  ManagedRenderingColorProperty property) noexcept override;
        [[nodiscard]] bool SetManagedRenderingColor(AssetId entity, ManagedRenderingComponent component,
                                                    ManagedRenderingColorProperty property,
                                                    Color value) noexcept override;
        [[nodiscard]] std::optional<AssetId>
        ReadManagedRenderingAsset(AssetId entity, ManagedRenderingComponent component,
                                  ManagedRenderingAssetProperty property) noexcept override;
        [[nodiscard]] bool SetManagedRenderingAsset(AssetId entity, ManagedRenderingComponent component,
                                                    ManagedRenderingAssetProperty property,
                                                    AssetId value) noexcept override;
        [[nodiscard]] std::optional<std::vector<AssetId>>
        ReadManagedRendererMaterials(AssetId entity) noexcept override;
        [[nodiscard]] bool SetManagedRendererMaterials(AssetId entity,
                                                       std::span<const AssetId> materials) noexcept override;
        [[nodiscard]] bool SetManagedMaterialProperty(AssetId entity, std::string_view name,
                                                      MaterialPropertyValue value) noexcept override;
        [[nodiscard]] bool ResetManagedMaterialProperty(AssetId entity, std::string_view name) noexcept override;
        [[nodiscard]] bool ClearManagedMaterialProperties(AssetId entity) noexcept override;
        [[nodiscard]] bool SetManagedMaterialInstanceProperty(AssetId entity, std::size_t slot, std::string_view name,
                                                              MaterialPropertyValue value) noexcept override;
        [[nodiscard]] bool ResetManagedMaterialInstanceProperty(AssetId entity, std::size_t slot,
                                                                std::string_view name) noexcept override;
        [[nodiscard]] bool ClearManagedMaterialInstanceProperties(AssetId entity, std::size_t slot) noexcept override;

      protected:
        [[nodiscard]] virtual Ref<Scene> ManagedRuntimeScene() const noexcept = 0;
    };

    [[nodiscard]] std::optional<float> ReadManagedRenderingScalar(const Ref<Scene>& scene, AssetId entity,
                                                                  ManagedRenderingComponent component,
                                                                  ManagedRenderingScalarProperty property) noexcept;
    [[nodiscard]] bool SetManagedRenderingScalar(const Ref<Scene>& scene, AssetId entity,
                                                 ManagedRenderingComponent component,
                                                 ManagedRenderingScalarProperty property, float value) noexcept;
    [[nodiscard]] std::optional<std::int32_t>
    ReadManagedRenderingInteger(const Ref<Scene>& scene, AssetId entity, ManagedRenderingComponent component,
                                ManagedRenderingIntegerProperty property) noexcept;
    [[nodiscard]] bool SetManagedRenderingInteger(const Ref<Scene>& scene, AssetId entity,
                                                  ManagedRenderingComponent component,
                                                  ManagedRenderingIntegerProperty property,
                                                  std::int32_t value) noexcept;
    [[nodiscard]] std::optional<bool> ReadManagedRenderingFlag(const Ref<Scene>& scene, AssetId entity,
                                                               ManagedRenderingComponent component,
                                                               ManagedRenderingFlagProperty property) noexcept;
    [[nodiscard]] bool SetManagedRenderingFlag(const Ref<Scene>& scene, AssetId entity,
                                               ManagedRenderingComponent component,
                                               ManagedRenderingFlagProperty property, bool value) noexcept;
    [[nodiscard]] std::optional<Vector2> ReadManagedRenderingVector(const Ref<Scene>& scene, AssetId entity,
                                                                    ManagedRenderingComponent component,
                                                                    ManagedRenderingVectorProperty property) noexcept;
    [[nodiscard]] bool SetManagedRenderingVector(const Ref<Scene>& scene, AssetId entity,
                                                 ManagedRenderingComponent component,
                                                 ManagedRenderingVectorProperty property, Vector2 value) noexcept;
    [[nodiscard]] std::optional<Color> ReadManagedRenderingColor(const Ref<Scene>& scene, AssetId entity,
                                                                 ManagedRenderingComponent component,
                                                                 ManagedRenderingColorProperty property) noexcept;
    [[nodiscard]] bool SetManagedRenderingColor(const Ref<Scene>& scene, AssetId entity,
                                                ManagedRenderingComponent component,
                                                ManagedRenderingColorProperty property, Color value) noexcept;
    [[nodiscard]] std::optional<AssetId> ReadManagedRenderingAsset(const Ref<Scene>& scene, AssetId entity,
                                                                   ManagedRenderingComponent component,
                                                                   ManagedRenderingAssetProperty property) noexcept;
    [[nodiscard]] bool SetManagedRenderingAsset(const Ref<Scene>& scene, AssetId entity,
                                                ManagedRenderingComponent component,
                                                ManagedRenderingAssetProperty property, AssetId value) noexcept;
    [[nodiscard]] std::optional<std::vector<AssetId>> ReadManagedRendererMaterials(const Ref<Scene>& scene,
                                                                                   AssetId entity) noexcept;
    [[nodiscard]] bool SetManagedRendererMaterials(const Ref<Scene>& scene, AssetId entity,
                                                   std::span<const AssetId> materials) noexcept;
    [[nodiscard]] bool SetManagedMaterialProperty(const Ref<Scene>& scene, AssetId entity, std::string_view name,
                                                  MaterialPropertyValue value) noexcept;
    [[nodiscard]] bool ResetManagedMaterialProperty(const Ref<Scene>& scene, AssetId entity,
                                                    std::string_view name) noexcept;
    [[nodiscard]] bool ClearManagedMaterialProperties(const Ref<Scene>& scene, AssetId entity) noexcept;
    [[nodiscard]] bool SetManagedMaterialInstanceProperty(const Ref<Scene>& scene, AssetId entity, std::size_t slot,
                                                          std::string_view name, MaterialPropertyValue value) noexcept;
    [[nodiscard]] bool ResetManagedMaterialInstanceProperty(const Ref<Scene>& scene, AssetId entity, std::size_t slot,
                                                            std::string_view name) noexcept;
    [[nodiscard]] bool ClearManagedMaterialInstanceProperties(const Ref<Scene>& scene, AssetId entity,
                                                              std::size_t slot) noexcept;
} // namespace Keire::Detail
