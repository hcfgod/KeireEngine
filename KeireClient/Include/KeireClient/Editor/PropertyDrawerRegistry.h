#pragma once

#include "Keire/ECS/Component.h"

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
        [[nodiscard]] virtual bool EditChoice(std::string_view label, std::int64_t& value,
                                              std::span<const std::string_view> choices) = 0;
        [[nodiscard]] virtual bool EditScalar(std::string_view label, double& value, double step,
                                              std::optional<double> minimum, std::optional<double> maximum) = 0;
        [[nodiscard]] virtual bool EditText(std::string_view label, std::string& value) = 0;
        [[nodiscard]] virtual bool EditVector2(std::string_view label, Keire::Vector2& value, double step) = 0;
        [[nodiscard]] virtual bool EditVector3(std::string_view label, Keire::Vector3& value, double step) = 0;
        [[nodiscard]] virtual bool EditVector4(std::string_view label, Keire::Vector4& value, double step) = 0;
        [[nodiscard]] virtual bool EditQuaternion(std::string_view label, Keire::Quaternion& value, double step) = 0;
        [[nodiscard]] virtual bool EditColor(std::string_view label, Keire::Color& value) = 0;
        [[nodiscard]] virtual bool EditAsset(std::string_view label, Keire::AssetId& value,
                                             std::optional<Keire::AssetTypeId> expectedType) = 0;
        [[nodiscard]] virtual bool EditEntity(std::string_view label, Keire::EntityId& value) = 0;
    };

    class PropertyDrawerRegistry final
    {
      public:
        using Drawer =
            std::function<bool(IPropertyEditor&, const Keire::ComponentProperty&, Keire::ComponentPropertyValue&)>;

        PropertyDrawerRegistry();

        void Register(Keire::ComponentPropertyKind kind, Drawer drawer);
        void RegisterOverride(Keire::ComponentTypeId component, std::string propertyKey, Drawer drawer);
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
