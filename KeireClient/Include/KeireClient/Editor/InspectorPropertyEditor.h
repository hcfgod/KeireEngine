#pragma once

#include "KeireClient/Editor/PropertyDrawerRegistry.h"

#include "Keire/Core.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    class AssetPicker;

    class InspectorPropertyEditor final : public IPropertyEditor
    {
      public:
        InspectorPropertyEditor(Keire::UiFrame& ui, std::span<const Keire::AssetSourceRecord> assets,
                                const Keire::Ref<Keire::AssetSystem>& assetSystem,
                                const Keire::Ref<Keire::Scene>& scene, AssetPicker& assetPicker);

        [[nodiscard]] bool EditBoundary() const noexcept;
        bool EditBoolean(std::string_view label, bool& value) override;
        bool EditInteger(std::string_view label, std::int64_t& value, double step, std::optional<double> minimum,
                         std::optional<double> maximum) override;
        bool EditIntegerSlider(std::string_view label, std::int64_t& value, double minimum, double maximum) override;
        bool EditChoice(std::string_view label, std::int64_t& value,
                        std::span<const std::string_view> choices) override;
        bool EditScalar(std::string_view label, double& value, double step, std::optional<double> minimum,
                        std::optional<double> maximum) override;
        bool EditScalarSlider(std::string_view label, double& value, double minimum, double maximum) override;
        bool EditText(std::string_view label, std::string& value) override;
        bool EditTextMultiline(std::string_view label, std::string& value, std::uint32_t visibleLines) override;
        bool EditVector2(std::string_view label, Keire::Vector2& value, double step) override;
        bool EditVector3(std::string_view label, Keire::Vector3& value, double step) override;
        bool EditVector4(std::string_view label, Keire::Vector4& value, double step) override;
        bool EditQuaternion(std::string_view label, Keire::Quaternion& value, double step) override;
        bool EditColor(std::string_view label, Keire::Color& value) override;
        bool EditCurve(std::string_view label, Keire::Curve1D& value) override;
        bool EditGradient(std::string_view label, Keire::ColorGradient& value) override;
        bool EditAsset(std::string_view label, Keire::AssetId& value,
                       std::optional<Keire::AssetTypeId> expectedType) override;
        bool EditTextureAsset(std::string_view label, Keire::AssetId& value,
                              Keire::ShaderTextureSemantic semantic) override;
        bool EditEntity(std::string_view label, Keire::EntityId& value) override;
        bool EditComponentReference(std::string_view label, Keire::ComponentReferenceValue& value,
                                    const Keire::ComponentProperty& property) override;
        bool EditEvent(std::string_view label, Keire::ComponentEventValue& value, std::size_t argumentCount) override;

      private:
        [[nodiscard]] const std::vector<Keire::Entity>& SceneEntities();
        bool Track(bool changed);

        Keire::UiFrame& m_Ui;
        std::span<const Keire::AssetSourceRecord> m_Assets;
        Keire::Ref<Keire::AssetSystem> m_AssetSystem;
        Keire::Ref<Keire::Scene> m_Scene;
        AssetPicker& m_AssetPicker;
        std::optional<std::vector<Keire::Entity>> m_EntityCache;
        bool m_EditBoundary = false;
    };
} // namespace KeireEditor
