#pragma once

#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Component.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace KeireEditor
{
    class IPropertyEditor
    {
      public:
        virtual ~IPropertyEditor() = default;

        [[nodiscard]] virtual bool EditBoolean(std::string_view label, bool& value) = 0;
        [[nodiscard]] virtual bool EditInteger(std::string_view label, std::int64_t& value, double step,
                                               std::optional<double> minimum, std::optional<double> maximum) = 0;
        [[nodiscard]] virtual bool EditIntegerSlider(std::string_view label, std::int64_t& value, double minimum,
                                                     double maximum)
        {
            return EditInteger(label, value, 1.0, minimum, maximum);
        }
        [[nodiscard]] virtual bool EditChoice(std::string_view label, std::int64_t& value,
                                              std::span<const std::string_view> choices) = 0;
        [[nodiscard]] virtual bool EditScalar(std::string_view label, double& value, double step,
                                              std::optional<double> minimum, std::optional<double> maximum) = 0;
        [[nodiscard]] virtual bool EditScalarSlider(std::string_view label, double& value, double minimum,
                                                    double maximum)
        {
            return EditScalar(label, value, 0.1, minimum, maximum);
        }
        [[nodiscard]] virtual bool EditText(std::string_view label, std::string& value) = 0;
        [[nodiscard]] virtual bool EditTextMultiline(std::string_view label, std::string& value,
                                                     std::uint32_t visibleLines)
        {
            (void)visibleLines;
            return EditText(label, value);
        }
        [[nodiscard]] virtual bool EditVector2(std::string_view label, Keire::Vector2& value, double step) = 0;
        [[nodiscard]] virtual bool EditVector3(std::string_view label, Keire::Vector3& value, double step) = 0;
        [[nodiscard]] virtual bool EditVector4(std::string_view label, Keire::Vector4& value, double step) = 0;
        [[nodiscard]] virtual bool EditQuaternion(std::string_view label, Keire::Quaternion& value, double step) = 0;
        [[nodiscard]] virtual bool EditColor(std::string_view label, Keire::Color& value) = 0;
        [[nodiscard]] virtual bool EditCurve(std::string_view label, Keire::Curve1D& value)
        {
            (void)label;
            (void)value;
            return false;
        }
        [[nodiscard]] virtual bool EditGradient(std::string_view label, Keire::ColorGradient& value)
        {
            (void)label;
            (void)value;
            return false;
        }
        [[nodiscard]] virtual bool EditAsset(std::string_view label, Keire::AssetId& value,
                                             std::optional<Keire::AssetTypeId> expectedType) = 0;
        [[nodiscard]] virtual bool EditTextureAsset(std::string_view label, Keire::AssetId& value,
                                                    Keire::ShaderTextureSemantic semantic)
        {
            (void)semantic;
            return EditAsset(label, value, Keire::Texture2DAsset::StaticType());
        }
        [[nodiscard]] virtual bool EditEntity(std::string_view label, Keire::EntityId& value) = 0;
        [[nodiscard]] virtual bool EditEvent(std::string_view label, Keire::ComponentEventValue& value,
                                             std::size_t argumentCount) = 0;
    };

    class PropertyDrawerRegistry final
    {
      public:
        using Drawer =
            std::function<bool(IPropertyEditor&, const Keire::ComponentProperty&, Keire::ComponentPropertyValue&)>;

        PropertyDrawerRegistry();

        void Register(Keire::ComponentPropertyKind kind, Drawer drawer);
        void RegisterOverride(Keire::ComponentTypeId component, const std::string& propertyKey, Drawer drawer);
        [[nodiscard]] bool Draw(IPropertyEditor& editor, Keire::ComponentTypeId component,
                                const Keire::ComponentProperty& property, Keire::ComponentPropertyValue& value) const;
        [[nodiscard]] bool EditComponent(IPropertyEditor& editor, const Keire::ComponentRegistration& registration,
                                         Keire::Component& component, const Keire::ComponentProperty& property,
                                         const std::function<void()>& beforeCommit = {}) const;

      private:
        [[nodiscard]] static std::string OverrideKey(Keire::ComponentTypeId component, std::string_view property);

        std::map<Keire::ComponentPropertyKind, Drawer> m_Drawers;
        std::map<std::string, Drawer, std::less<>> m_Overrides;
    };
} // namespace KeireEditor
