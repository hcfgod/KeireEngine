#include "KeireClient/Editor/PropertyDrawerRegistry.h"

#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        template <typename T, typename Callback>
        [[nodiscard]] bool DrawValue(Keire::ComponentPropertyValue& value, Callback&& callback)
        {
            auto* typed = std::get_if<T>(&value);
            if (!typed)
                throw std::invalid_argument("Component property metadata does not match its serialized value.");
            return std::forward<Callback>(callback)(*typed);
        }
    } // namespace

    PropertyDrawerRegistry::PropertyDrawerRegistry()
    {
        Register(
            Keire::ComponentPropertyKind::Boolean,
            [](IPropertyEditor& editor, const Keire::ComponentProperty& property, Keire::ComponentPropertyValue& value)
            {
                return DrawValue<bool>(value,
                                       [&](bool& typed) { return editor.EditBoolean(property.DisplayName, typed); });
            });
        Register(
            Keire::ComponentPropertyKind::Integer,
            [](IPropertyEditor& editor, const Keire::ComponentProperty& property, Keire::ComponentPropertyValue& value)
            {
                return DrawValue<std::int64_t>(
                    value,
                    [&](std::int64_t& typed)
                    {
                        if (property.Slider)
                        {
                            if (!property.Minimum || !property.Maximum)
                                throw std::logic_error("Integer sliders require minimum and maximum bounds.");
                            return editor.EditIntegerSlider(property.DisplayName, typed, *property.Minimum,
                                                            *property.Maximum);
                        }
                        return editor.EditInteger(property.DisplayName, typed, property.Step, property.Minimum,
                                                  property.Maximum);
                    });
            });
        Register(
            Keire::ComponentPropertyKind::Scalar,
            [](IPropertyEditor& editor, const Keire::ComponentProperty& property, Keire::ComponentPropertyValue& value)
            {
                return DrawValue<double>(value,
                                         [&](double& typed)
                                         {
                                             if (property.Slider)
                                             {
                                                 if (!property.Minimum || !property.Maximum)
                                                     throw std::logic_error(
                                                         "Scalar sliders require minimum and maximum bounds.");
                                                 return editor.EditScalarSlider(property.DisplayName, typed,
                                                                                *property.Minimum, *property.Maximum);
                                             }
                                             return editor.EditScalar(property.DisplayName, typed, property.Step,
                                                                      property.Minimum, property.Maximum);
                                         });
            });
        Register(
            Keire::ComponentPropertyKind::Text,
            [](IPropertyEditor& editor, const Keire::ComponentProperty& property, Keire::ComponentPropertyValue& value)
            {
                return DrawValue<std::string>(value,
                                              [&](std::string& typed)
                                              {
                                                  return property.TextLines > 1
                                                             ? editor.EditTextMultiline(property.DisplayName, typed,
                                                                                        property.TextLines)
                                                             : editor.EditText(property.DisplayName, typed);
                                              });
            });
        Register(
            Keire::ComponentPropertyKind::Vector2,
            [](IPropertyEditor& editor, const Keire::ComponentProperty& property, Keire::ComponentPropertyValue& value)
            {
                return DrawValue<Keire::Vector2>(
                    value, [&](Keire::Vector2& typed)
                    { return editor.EditVector2(property.DisplayName, typed, property.Step); });
            });
        Register(
            Keire::ComponentPropertyKind::Vector3,
            [](IPropertyEditor& editor, const Keire::ComponentProperty& property, Keire::ComponentPropertyValue& value)
            {
                return DrawValue<Keire::Vector3>(
                    value, [&](Keire::Vector3& typed)
                    { return editor.EditVector3(property.DisplayName, typed, property.Step); });
            });
        Register(
            Keire::ComponentPropertyKind::Vector4,
            [](IPropertyEditor& editor, const Keire::ComponentProperty& property, Keire::ComponentPropertyValue& value)
            {
                return DrawValue<Keire::Vector4>(
                    value, [&](Keire::Vector4& typed)
                    { return editor.EditVector4(property.DisplayName, typed, property.Step); });
            });
        Register(
            Keire::ComponentPropertyKind::Quaternion,
            [](IPropertyEditor& editor, const Keire::ComponentProperty& property, Keire::ComponentPropertyValue& value)
            {
                return DrawValue<Keire::Quaternion>(
                    value, [&](Keire::Quaternion& typed)
                    { return editor.EditQuaternion(property.DisplayName, typed, property.Step); });
            });
        Register(
            Keire::ComponentPropertyKind::Color,
            [](IPropertyEditor& editor, const Keire::ComponentProperty& property, Keire::ComponentPropertyValue& value)
            {
                return DrawValue<Keire::Color>(value, [&](Keire::Color& typed)
                                               { return editor.EditColor(property.DisplayName, typed); });
            });
        Register(
            Keire::ComponentPropertyKind::Asset,
            [](IPropertyEditor& editor, const Keire::ComponentProperty& property, Keire::ComponentPropertyValue& value)
            {
                return DrawValue<Keire::AssetId>(
                    value, [&](Keire::AssetId& typed)
                    { return editor.EditAsset(property.DisplayName, typed, property.ExpectedAssetType); });
            });
        Register(
            Keire::ComponentPropertyKind::Entity,
            [](IPropertyEditor& editor, const Keire::ComponentProperty& property, Keire::ComponentPropertyValue& value)
            {
                return DrawValue<Keire::EntityId>(value, [&](Keire::EntityId& typed)
                                                  { return editor.EditEntity(property.DisplayName, typed); });
            });
        Register(
            Keire::ComponentPropertyKind::Event,
            [](IPropertyEditor& editor, const Keire::ComponentProperty& property, Keire::ComponentPropertyValue& value)
            {
                return DrawValue<Keire::ComponentEventValue>(
                    value, [&](Keire::ComponentEventValue& typed)
                    { return editor.EditEvent(property.DisplayName, typed, property.EventArgumentCount); });
            });
        Register(
            Keire::ComponentPropertyKind::Curve,
            [](IPropertyEditor& editor, const Keire::ComponentProperty& property, Keire::ComponentPropertyValue& value)
            {
                return DrawValue<Keire::Curve1D>(value, [&](Keire::Curve1D& typed)
                                                 { return editor.EditCurve(property.DisplayName, typed); });
            });
        Register(
            Keire::ComponentPropertyKind::Gradient,
            [](IPropertyEditor& editor, const Keire::ComponentProperty& property, Keire::ComponentPropertyValue& value)
            {
                return DrawValue<Keire::ColorGradient>(value, [&](Keire::ColorGradient& typed)
                                                       { return editor.EditGradient(property.DisplayName, typed); });
            });
    }

    void PropertyDrawerRegistry::Register(const Keire::ComponentPropertyKind kind, Drawer drawer)
    {
        if (!drawer)
            throw std::invalid_argument("A property drawer callback is required.");
        m_Drawers.insert_or_assign(kind, std::move(drawer));
    }

    void PropertyDrawerRegistry::RegisterOverride(const Keire::ComponentTypeId component,
                                                  const std::string& propertyKey, Drawer drawer)
    {
        if (!component || propertyKey.empty() || !drawer)
            throw std::invalid_argument("A component, property key, and drawer callback are required.");
        m_Overrides.insert_or_assign(OverrideKey(component, propertyKey), std::move(drawer));
    }

    bool PropertyDrawerRegistry::Draw(IPropertyEditor& editor, const Keire::ComponentTypeId component,
                                      const Keire::ComponentProperty& property,
                                      Keire::ComponentPropertyValue& value) const
    {
        auto scopedProperty = property;
        scopedProperty.DisplayName += "##" + property.Key;
        auto readOnlyCandidate = value;
        auto& editableValue = property.ReadOnly ? readOnlyCandidate : value;
        if (const auto override = m_Overrides.find(OverrideKey(component, property.Key)); override != m_Overrides.end())
            return override->second(editor, scopedProperty, editableValue) && !property.ReadOnly;
        const auto drawer = m_Drawers.find(property.Kind);
        if (drawer == m_Drawers.end())
            throw std::logic_error("No drawer is registered for this component property kind.");
        return drawer->second(editor, scopedProperty, editableValue) && !property.ReadOnly;
    }

    bool PropertyDrawerRegistry::EditComponent(IPropertyEditor& editor,
                                               const Keire::ComponentRegistration& registration,
                                               Keire::Component& component, const Keire::ComponentProperty& property,
                                               const std::function<void()>& beforeCommit) const
    {
        auto original = registration.Serialize(component);
        const auto found = original.find(property.Key);
        if (found == original.end())
            throw std::invalid_argument("The component did not serialize its declared property '" + property.Key +
                                        "'.");
        auto candidate = found->second;
        if (!Draw(editor, registration.Type, property, candidate))
            return false;

        auto proposed = original;
        proposed.insert_or_assign(property.Key, std::move(candidate));
        auto validation = registration.Factory();
        if (!validation)
            throw std::runtime_error("The component factory returned null during property validation.");
        registration.Deserialize(*validation, proposed, registration.SchemaVersion);
        if (beforeCommit)
            beforeCommit();
        try
        {
            registration.Deserialize(component, proposed, registration.SchemaVersion);
        }
        catch (...)
        {
            registration.Deserialize(component, original, registration.SchemaVersion);
            throw;
        }
        return true;
    }

    std::string PropertyDrawerRegistry::OverrideKey(const Keire::ComponentTypeId component,
                                                    const std::string_view property)
    {
        return component.ToString() + ":" + std::string(property);
    }
} // namespace KeireEditor
