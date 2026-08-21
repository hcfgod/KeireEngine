#include "KeireInternal/Scripting/ManagedReflection.h"

#include "Keire/Audio/AudioAssets.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4146)
#endif
#include <Coral/Assembly.hpp>
#include <Coral/Attribute.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace Keire::Detail
{
    [[nodiscard]] std::string PathText(const std::filesystem::path& path)
    {
        const auto value = path.generic_u8string();
        return {reinterpret_cast<const char*>(value.data()), value.size()};
    }
    [[nodiscard]] std::string ManagedFieldDisplayName(std::string name)
    {
        const auto fallback = name;
        while (!name.empty() && name.front() == '_')
            name.erase(name.begin());
        std::string result;
        result.reserve(name.size() + 8);
        for (std::size_t index = 0; index < name.size(); ++index)
        {
            const auto character = name[index];
            if (index > 0 && std::isupper(static_cast<unsigned char>(character)) &&
                !std::isupper(static_cast<unsigned char>(name[index - 1])))
            {
                result.push_back(' ');
            }
            result.push_back(character);
        }
        if (!result.empty())
            result.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(result.front())));
        return result.empty() ? fallback : result;
    }

    [[nodiscard]] std::string ManagedTypeName(Coral::Type& type)
    {
        const Coral::ScopedString scopedName(type.GetFullName());
        return static_cast<std::string>(scopedName);
    }

    [[nodiscard]] ManagedInspectorAttributeTypes ResolveManagedInspectorAttributeTypes(Coral::ManagedAssembly& api)
    {
        const auto required = [&api](const std::string_view name) -> const Coral::Type*
        {
            auto& type = api.GetLocalType(name);
            if (!type)
                throw std::runtime_error("Keire.Managed does not expose managed component metadata type '" +
                                         std::string(name) + "'.");
            return std::addressof(type);
        };
        return {.SerializeField = required("Keire.SerializeFieldAttribute"),
                .HideInInspector = required("Keire.HideInInspectorAttribute"),
                .Serializable = required("Keire.SerializableTypeAttribute"),
                .Range = required("Keire.RangeAttribute"),
                .Minimum = required("Keire.MinAttribute"),
                .Maximum = required("Keire.MaxAttribute"),
                .Step = required("Keire.InspectorStepAttribute"),
                .Multiline = required("Keire.MultilineAttribute"),
                .InspectorName = required("Keire.InspectorNameAttribute"),
                .ReadOnly = required("Keire.ReadOnlyInInspectorAttribute"),
                .Header = required("Keire.HeaderAttribute"),
                .Tooltip = required("Keire.TooltipAttribute"),
                .Group = required("Keire.InspectorGroupAttribute")};
    }

    [[nodiscard]] bool ManagedTypeIsEnum(Coral::Type& type)
    {
        auto& baseType = type.GetBaseType();
        return baseType && ManagedTypeName(baseType) == "System.Enum";
    }

    [[nodiscard]] std::optional<ComponentPropertyKind> ManagedFieldKind(Coral::Type& type)
    {
        const auto name = ManagedTypeName(type);
        if (name == "System.Boolean")
            return ComponentPropertyKind::Boolean;
        if (name == "System.SByte" || name == "System.Byte" || name == "System.Int16" || name == "System.UInt16" ||
            name == "System.Int32" || name == "System.UInt32" || name == "System.Int64" || name == "System.UInt64" ||
            name == "System.Char" || ManagedTypeIsEnum(type))
        {
            return ComponentPropertyKind::Integer;
        }
        if (name == "System.Single" || name == "System.Double" || name == "System.Decimal")
            return ComponentPropertyKind::Scalar;
        if (name == "System.String")
            return ComponentPropertyKind::Text;
        if (name == "Keire.Vector2")
            return ComponentPropertyKind::Vector2;
        if (name == "Keire.Vector3")
            return ComponentPropertyKind::Vector3;
        if (name == "Keire.Vector4")
            return ComponentPropertyKind::Vector4;
        if (name == "Keire.Quaternion")
            return ComponentPropertyKind::Quaternion;
        if (name == "Keire.Color")
            return ComponentPropertyKind::Color;
        if (name == "Keire.Entity")
            return ComponentPropertyKind::Entity;
        if (name == "Keire.UiButton" || name == "Keire.UiSlider" || name == "Keire.UiToggle" ||
            name == "Keire.UiInputField" || name == "Keire.UiScrollView")
            return ComponentPropertyKind::Entity;
        if (name == "Keire.KeireEvent" || name.starts_with("Keire.KeireEvent`"))
            return ComponentPropertyKind::Event;
        if (name.starts_with("Keire.AssetReference`1"))
            return ComponentPropertyKind::Asset;
        return std::nullopt;
    }

    [[nodiscard]] std::size_t ManagedEventArgumentCount(const std::string_view name)
    {
        const auto marker = name.find('`');
        if (marker == std::string_view::npos)
            return 0;
        std::size_t result = 0;
        for (auto index = marker + 1; index < name.size() && std::isdigit(static_cast<unsigned char>(name[index]));
             ++index)
        {
            result = result * 10 + static_cast<std::size_t>(name[index] - '0');
        }
        return result;
    }

    [[nodiscard]] std::string ManagedAttributeText(Coral::Attribute& attribute, const std::string_view field)
    {
        const Coral::ScopedString value(attribute.GetFieldValue<Coral::String>(field));
        return static_cast<std::string>(value);
    }

    void ReflectManagedFieldSet(Coral::Type& ownerType, const ManagedInspectorAttributeTypes& attributeTypes,
                                const std::string_view prefix, const std::string_view inheritedGroup,
                                const std::size_t depth, std::vector<std::int32_t>& typeStack,
                                std::vector<ComponentProperty>& result)
    {
        for (auto field : ownerType.GetFields())
        {
            bool serialized = field.GetAccessibility() == Coral::TypeAccessibility::Public;
            bool hidden = false;
            bool readOnly = false;
            bool slider = false;
            bool minimumAttribute = false;
            bool maximumAttribute = false;
            std::optional<double> minimum;
            std::optional<double> maximum;
            std::optional<double> step;
            std::uint32_t textLines = 1;
            std::string displayName;
            std::string header;
            std::string tooltip;
            std::string group(inheritedGroup);
            for (auto attribute : field.GetAttributes())
            {
                if (attributeTypes.SerializeField && attribute.GetType() == *attributeTypes.SerializeField)
                    serialized = true;
                else if (attributeTypes.HideInInspector && attribute.GetType() == *attributeTypes.HideInInspector)
                    hidden = true;
                else if (attributeTypes.Range && attribute.GetType() == *attributeTypes.Range)
                {
                    minimum = attribute.GetFieldValue<double>("Minimum");
                    maximum = attribute.GetFieldValue<double>("Maximum");
                    slider = true;
                }
                else if (attributeTypes.Minimum && attribute.GetType() == *attributeTypes.Minimum)
                {
                    minimum = attribute.GetFieldValue<double>("Minimum");
                    minimumAttribute = true;
                }
                else if (attributeTypes.Maximum && attribute.GetType() == *attributeTypes.Maximum)
                {
                    maximum = attribute.GetFieldValue<double>("Maximum");
                    maximumAttribute = true;
                }
                else if (attributeTypes.Step && attribute.GetType() == *attributeTypes.Step)
                    step = attribute.GetFieldValue<double>("Step");
                else if (attributeTypes.Multiline && attribute.GetType() == *attributeTypes.Multiline)
                    textLines = static_cast<std::uint32_t>(attribute.GetFieldValue<std::int32_t>("Lines"));
                else if (attributeTypes.InspectorName && attribute.GetType() == *attributeTypes.InspectorName)
                    displayName = ManagedAttributeText(attribute, "Name");
                else if (attributeTypes.ReadOnly && attribute.GetType() == *attributeTypes.ReadOnly)
                    readOnly = true;
                else if (attributeTypes.Header && attribute.GetType() == *attributeTypes.Header)
                    header = ManagedAttributeText(attribute, "Value");
                else if (attributeTypes.Tooltip && attribute.GetType() == *attributeTypes.Tooltip)
                    tooltip = ManagedAttributeText(attribute, "Text");
                else if (attributeTypes.Group && attribute.GetType() == *attributeTypes.Group)
                    group = ManagedAttributeText(attribute, "Name");
            }
            if (!serialized || hidden)
                continue;

            const Coral::ScopedString scopedName(field.GetName());
            const auto name = static_cast<std::string>(scopedName);
            const auto key = prefix.empty() ? name : std::string(prefix) + "." + name;
            auto& fieldType = field.GetType();
            if (const auto kind = ManagedFieldKind(fieldType))
            {
                if (std::ranges::find(result, key, &ComponentProperty::Key) != result.end())
                    continue;
                const bool numeric = (*kind == ComponentPropertyKind::Integer && !ManagedTypeIsEnum(fieldType)) ||
                                     *kind == ComponentPropertyKind::Scalar;
                if (slider && (minimumAttribute || maximumAttribute))
                    throw std::runtime_error("Managed Inspector Range cannot be combined with Min or Max: " + key);
                if ((minimum || maximum || step || slider) && !numeric)
                    throw std::runtime_error("Managed Inspector numeric attributes require a numeric field: " + key);
                if (minimum && maximum && *minimum > *maximum)
                    throw std::runtime_error("Managed Inspector bounds are unordered for field: " + key);
                if (slider && (!minimum || !maximum || *minimum >= *maximum))
                    throw std::runtime_error("Managed Inspector sliders require an increasing range for field: " + key);
                if (textLines != 1 && *kind != ComponentPropertyKind::Text)
                    throw std::runtime_error("Managed Inspector multiline fields must be strings: " + key);
                ComponentProperty property;
                property.Key = key;
                property.DisplayName = displayName.empty() ? ManagedFieldDisplayName(name) : std::move(displayName);
                property.Group = std::move(group);
                property.Kind = *kind;
                property.ReadOnly = readOnly;
                property.Minimum = minimum;
                property.Maximum = maximum;
                property.Step = step.value_or(*kind == ComponentPropertyKind::Integer ? 1.0 : 0.1);
                property.Tooltip = std::move(tooltip);
                property.Header = std::move(header);
                property.Slider = slider;
                property.TextLines = textLines;
                if (*kind == ComponentPropertyKind::Event)
                    property.EventArgumentCount = ManagedEventArgumentCount(ManagedTypeName(fieldType));
                if (*kind == ComponentPropertyKind::Asset &&
                    ManagedTypeName(fieldType).find("Keire.AudioClip") != std::string::npos)
                {
                    property.ExpectedAssetType = AudioClipAsset::StaticType();
                }
                result.push_back(std::move(property));
                continue;
            }
            if (!attributeTypes.Serializable || depth >= 4 || fieldType.IsSZArray() ||
                !fieldType.HasAttribute(*attributeTypes.Serializable) ||
                std::ranges::find(typeStack, fieldType.GetTypeId()) != typeStack.end())
            {
                continue;
            }
            typeStack.push_back(fieldType.GetTypeId());
            const auto nestedDisplayName = displayName.empty() ? ManagedFieldDisplayName(name) : std::move(displayName);
            auto nestedGroup = group.empty() ? nestedDisplayName : group;
            if (!group.empty())
            {
                nestedGroup += " / ";
                nestedGroup += nestedDisplayName;
            }
            ReflectManagedFieldSet(fieldType, attributeTypes, key, nestedGroup, depth + 1, typeStack, result);
            typeStack.pop_back();
        }
    }

    [[nodiscard]] std::vector<ComponentProperty>
    ReflectManagedProperties(const Coral::Type& concreteType, const Coral::Type& behaviourType,
                             const ManagedInspectorAttributeTypes& attributeTypes)
    {
        std::vector<ComponentProperty> result;
        std::vector<std::int32_t> typeStack;
        auto* current = const_cast<Coral::Type*>(std::addressof(concreteType));
        while (*current && !(*current == behaviourType))
        {
            typeStack.push_back(current->GetTypeId());
            ReflectManagedFieldSet(*current, attributeTypes, {}, {}, 0, typeStack, result);
            typeStack.pop_back();
            current = std::addressof(current->GetBaseType());
        }
        return result;
    }

    [[nodiscard]] std::vector<ComponentMethod> ReflectManagedMethods(const Coral::Type& concreteType)
    {
        std::vector<ComponentMethod> result;
        for (auto method : concreteType.GetMethods())
        {
            const Coral::ScopedString scopedName(method.GetName());
            const auto name = static_cast<std::string>(scopedName);
            if (ManagedTypeName(method.GetReturnType()) != "System.Void" || name.starts_with("Runtime") ||
                name.starts_with("get_") || name.starts_with("set_") || name == "Awake" || name == "OnEnable" ||
                name == "Start" || name == "FixedUpdate" || name == "Update" || name == "LateUpdate" ||
                name == "OnDisable" || name == "OnDestroy" || name == "OnCollisionEnter" || name == "OnCollisionStay" ||
                name == "OnCollisionExit" || name == "OnTriggerEnter" || name == "OnTriggerStay" ||
                name == "OnTriggerExit" || name == "OnAnimationEvent" || name == "OnAnimatorIk" ||
                name == "OnProceduralMotionEvent" || name == "OnBeforeReload" || name == "OnAfterReload")
            {
                continue;
            }

            ComponentMethod reflected;
            reflected.Name = name;
            for (auto* parameter : method.GetParameterTypes())
            {
                if (!parameter)
                    break;
                reflected.ParameterTypes.push_back(ManagedTypeName(*parameter));
            }
            if (reflected.ParameterTypes.size() == method.GetParameterTypes().size())
            {
                reflected.DisplayName = reflected.Name + "(";
                for (std::size_t index = 0; index < reflected.ParameterTypes.size(); ++index)
                {
                    if (index != 0)
                        reflected.DisplayName += ", ";
                    reflected.DisplayName += reflected.ParameterTypes[index];
                }
                reflected.DisplayName += ")";
                result.push_back(std::move(reflected));
            }
        }
        std::ranges::sort(result, {},
                          [](const ComponentMethod& method) { return std::tie(method.Name, method.ParameterTypes); });
        return result;
    }

    [[nodiscard]] const nlohmann::json* JsonMember(const nlohmann::json& value, const std::string_view primary,
                                                   const std::string_view fallback)
    {
        if (const auto found = value.find(std::string(primary)); found != value.end())
            return std::addressof(*found);
        if (const auto found = value.find(std::string(fallback)); found != value.end())
            return std::addressof(*found);
        return nullptr;
    }

    [[nodiscard]] double JsonNumber(const nlohmann::json& value, const std::string_view primary,
                                    const std::string_view fallback)
    {
        const auto* member = JsonMember(value, primary, fallback);
        if (!member || !member->is_number())
            throw std::invalid_argument("Managed vector state is missing a numeric member.");
        return member->get<double>();
    }

    [[nodiscard]] ComponentPropertyValue DefaultManagedPropertyValue(const ComponentPropertyKind kind)
    {
        switch (kind)
        {
        case ComponentPropertyKind::Boolean:
            return false;
        case ComponentPropertyKind::Integer:
            return std::int64_t{0};
        case ComponentPropertyKind::Scalar:
            return 0.0;
        case ComponentPropertyKind::Text:
            return std::string{};
        case ComponentPropertyKind::Vector2:
            return Vector2{};
        case ComponentPropertyKind::Vector3:
            return Vector3{};
        case ComponentPropertyKind::Vector4:
            return Vector4{};
        case ComponentPropertyKind::Quaternion:
            return Quaternion{};
        case ComponentPropertyKind::Color:
            return Color{};
        case ComponentPropertyKind::Asset:
            return AssetId{};
        case ComponentPropertyKind::Entity:
            return EntityId{};
        case ComponentPropertyKind::Event:
            return ComponentEventValue{};
        case ComponentPropertyKind::Curve:
            return Curve1D{};
        case ComponentPropertyKind::Gradient:
            return ColorGradient{};
        }
        throw std::logic_error("Unsupported managed Inspector property kind.");
    }

    [[nodiscard]] ComponentPropertyValue ReadManagedPropertyValue(const nlohmann::json& value,
                                                                  const ComponentPropertyKind kind)
    {
        const auto readUnsignedInteger = [](const nlohmann::json& source) -> std::optional<std::uint64_t>
        {
            if (source.is_number_unsigned())
                return source.get<std::uint64_t>();
            if (!source.is_number_integer())
                return std::nullopt;
            const auto signedValue = source.get<std::int64_t>();
            return signedValue < 0 ? std::nullopt
                                   : std::optional<std::uint64_t>(static_cast<std::uint64_t>(signedValue));
        };
        const auto readReferenceId = [&readUnsignedInteger](const nlohmann::json& source)
        {
            const auto* nested = JsonMember(source, "Id", "id");
            const auto& id = nested && nested->is_object() ? *nested : source;
            const auto* high = JsonMember(id, "High", "high");
            const auto* low = JsonMember(id, "Low", "low");
            if (!high || !low)
                return AssetId{};
            const auto parsedHigh = readUnsignedInteger(*high);
            const auto parsedLow = readUnsignedInteger(*low);
            return parsedHigh && parsedLow ? AssetId(*parsedHigh, *parsedLow) : AssetId{};
        };
        if (value.is_null())
            return DefaultManagedPropertyValue(kind);
        switch (kind)
        {
        case ComponentPropertyKind::Boolean:
            return value.get<bool>();
        case ComponentPropertyKind::Integer:
            return value.get<std::int64_t>();
        case ComponentPropertyKind::Scalar:
            return value.get<double>();
        case ComponentPropertyKind::Text:
            return value.get<std::string>();
        case ComponentPropertyKind::Vector2:
            return Vector2{static_cast<float>(JsonNumber(value, "X", "x")),
                           static_cast<float>(JsonNumber(value, "Y", "y"))};
        case ComponentPropertyKind::Vector3:
            return Vector3{static_cast<float>(JsonNumber(value, "X", "x")),
                           static_cast<float>(JsonNumber(value, "Y", "y")),
                           static_cast<float>(JsonNumber(value, "Z", "z"))};
        case ComponentPropertyKind::Vector4:
            return Vector4{
                static_cast<float>(JsonNumber(value, "X", "x")), static_cast<float>(JsonNumber(value, "Y", "y")),
                static_cast<float>(JsonNumber(value, "Z", "z")), static_cast<float>(JsonNumber(value, "W", "w"))};
        case ComponentPropertyKind::Quaternion:
            return Quaternion{
                static_cast<float>(JsonNumber(value, "X", "x")), static_cast<float>(JsonNumber(value, "Y", "y")),
                static_cast<float>(JsonNumber(value, "Z", "z")), static_cast<float>(JsonNumber(value, "W", "w"))};
        case ComponentPropertyKind::Color:
            return Color{static_cast<float>(JsonNumber(value, "Red", "red")),
                         static_cast<float>(JsonNumber(value, "Green", "green")),
                         static_cast<float>(JsonNumber(value, "Blue", "blue")),
                         static_cast<float>(JsonNumber(value, "Alpha", "alpha"))};
        case ComponentPropertyKind::Asset:
            return readReferenceId(value);
        case ComponentPropertyKind::Entity:
            return EntityId(readReferenceId(value));
        case ComponentPropertyKind::Event:
        {
            ComponentEventValue result;
            const auto* calls = JsonMember(value, "persistentCalls", "PersistentCalls");
            if (!calls || !calls->is_array())
                return result;
            constexpr std::size_t maximumPersistentListeners = 256;
            for (const auto& call : *calls)
            {
                if (!call.is_object() || result.Listeners.size() >= maximumPersistentListeners)
                    break;
                ComponentEventListener listener;
                if (const auto* enabled = JsonMember(call, "Enabled", "enabled"); enabled && enabled->is_boolean())
                    listener.Enabled = enabled->get<bool>();
                if (const auto* target = JsonMember(call, "Target", "target"); target && target->is_object())
                    listener.Target = EntityId(readReferenceId(*target));
                if (const auto* component = JsonMember(call, "Component", "component");
                    component && component->is_object())
                {
                    listener.Component = ComponentTypeId(readReferenceId(*component));
                }
                if (const auto* method = JsonMember(call, "Method", "method"); method && method->is_string())
                    listener.Method = method->get<std::string>();
                result.Listeners.push_back(std::move(listener));
            }
            return result;
        }
        case ComponentPropertyKind::Curve:
        case ComponentPropertyKind::Gradient:
            throw std::logic_error("Managed fields do not expose native authoring curve values.");
        }
        throw std::logic_error("Unsupported managed Inspector property kind.");
    }

    [[nodiscard]] nlohmann::json WriteManagedPropertyValue(const ComponentPropertyValue& value,
                                                           const ComponentPropertyKind kind)
    {
        switch (kind)
        {
        case ComponentPropertyKind::Boolean:
            return std::get<bool>(value);
        case ComponentPropertyKind::Integer:
            return std::get<std::int64_t>(value);
        case ComponentPropertyKind::Scalar:
            return std::get<double>(value);
        case ComponentPropertyKind::Text:
            return std::get<std::string>(value);
        case ComponentPropertyKind::Vector2:
        {
            const auto vector = std::get<Vector2>(value);
            return {{"X", vector.X}, {"Y", vector.Y}};
        }
        case ComponentPropertyKind::Vector3:
        {
            const auto vector = std::get<Vector3>(value);
            return {{"X", vector.X}, {"Y", vector.Y}, {"Z", vector.Z}};
        }
        case ComponentPropertyKind::Vector4:
        {
            const auto vector = std::get<Vector4>(value);
            return {{"X", vector.X}, {"Y", vector.Y}, {"Z", vector.Z}, {"W", vector.W}};
        }
        case ComponentPropertyKind::Quaternion:
        {
            const auto quaternion = std::get<Quaternion>(value);
            return {{"X", quaternion.X}, {"Y", quaternion.Y}, {"Z", quaternion.Z}, {"W", quaternion.W}};
        }
        case ComponentPropertyKind::Color:
        {
            const auto color = std::get<Color>(value);
            return {{"Red", color.Red}, {"Green", color.Green}, {"Blue", color.Blue}, {"Alpha", color.Alpha}};
        }
        case ComponentPropertyKind::Asset:
        {
            const auto asset = std::get<AssetId>(value);
            return {{"Id", {{"High", asset.High()}, {"Low", asset.Low()}}}};
        }
        case ComponentPropertyKind::Entity:
        {
            const auto entity = std::get<EntityId>(value).Value();
            return {{"Id", {{"High", entity.High()}, {"Low", entity.Low()}}}};
        }
        case ComponentPropertyKind::Event:
        {
            nlohmann::json listeners = nlohmann::json::array();
            for (const auto& listener : std::get<ComponentEventValue>(value).Listeners)
            {
                const auto target = listener.Target.Value();
                const auto component = listener.Component.Value();
                listeners.push_back({{"Enabled", listener.Enabled},
                                     {"Target", {{"Id", {{"High", target.High()}, {"Low", target.Low()}}}}},
                                     {"Component", {{"High", component.High()}, {"Low", component.Low()}}},
                                     {"Method", listener.Method}});
            }
            return {{"persistentCalls", std::move(listeners)}};
        }
        case ComponentPropertyKind::Curve:
        case ComponentPropertyKind::Gradient:
            throw std::logic_error("Managed fields do not expose native authoring curve values.");
        }
        throw std::logic_error("Unsupported managed Inspector property kind.");
    }

    [[nodiscard]] nlohmann::json* ManagedStateField(nlohmann::json& document, const std::string_view name)
    {
        auto* fields = document.contains("Fields")   ? std::addressof(document["Fields"])
                       : document.contains("fields") ? std::addressof(document["fields"])
                                                     : nullptr;
        if (!fields || !fields->is_array())
            return nullptr;
        nlohmann::json* legacyField = nullptr;
        for (auto& field : *fields)
        {
            const auto* fieldName = JsonMember(field, "Name", "name");
            if (!fieldName || !fieldName->is_string() || fieldName->get<std::string>() != name)
                continue;
            const auto* stableId = JsonMember(field, "StableId", "stableId");
            if (stableId && stableId->is_string() && !stableId->get_ref<const std::string&>().empty())
                return std::addressof(field);
            if (!legacyField)
                legacyField = std::addressof(field);
        }
        return legacyField;
    }

    [[nodiscard]] const nlohmann::json* ManagedStateField(const nlohmann::json& document, const std::string_view name)
    {
        const auto* fields = document.contains("Fields")   ? std::addressof(document["Fields"])
                             : document.contains("fields") ? std::addressof(document["fields"])
                                                           : nullptr;
        if (!fields || !fields->is_array())
            return nullptr;
        const nlohmann::json* legacyField = nullptr;
        for (const auto& field : *fields)
        {
            const auto* fieldName = JsonMember(field, "Name", "name");
            if (!fieldName || !fieldName->is_string() || fieldName->get<std::string>() != name)
                continue;
            const auto* stableId = JsonMember(field, "StableId", "stableId");
            if (stableId && stableId->is_string() && !stableId->get_ref<const std::string&>().empty())
                return std::addressof(field);
            if (!legacyField)
                legacyField = std::addressof(field);
        }
        return legacyField;
    }

    [[nodiscard]] std::vector<std::string_view> ManagedPropertyPath(const std::string_view path)
    {
        std::vector<std::string_view> result;
        std::size_t cursor = 0;
        while (cursor <= path.size())
        {
            const auto separator = path.find('.', cursor);
            result.push_back(
                path.substr(cursor, separator == std::string_view::npos ? path.size() - cursor : separator - cursor));
            if (separator == std::string_view::npos)
                break;
            cursor = separator + 1;
        }
        return result;
    }

    [[nodiscard]] const nlohmann::json* ManagedStateValue(const nlohmann::json& document, const std::string_view path)
    {
        const auto segments = ManagedPropertyPath(path);
        if (segments.empty())
            return nullptr;
        const auto* field = ManagedStateField(document, segments.front());
        const nlohmann::json* value = field ? JsonMember(*field, "Value", "value") : nullptr;
        for (std::size_t index = 1; value && index < segments.size(); ++index)
        {
            if (!value->is_object())
                return nullptr;
            const auto found = value->find(std::string(segments[index]));
            value = found == value->end() ? nullptr : std::addressof(*found);
        }
        return value;
    }

    [[nodiscard]] nlohmann::json& EnsureManagedStateValue(nlohmann::json& document, nlohmann::json& fields,
                                                          const std::string_view path)
    {
        const auto segments = ManagedPropertyPath(path);
        if (segments.empty())
            throw std::invalid_argument("Managed Inspector property path is empty.");
        auto* field = ManagedStateField(document, segments.front());
        if (!field)
        {
            fields.push_back({{"StableId", ""},
                              {"Name", std::string(segments.front())},
                              {"Type", ""},
                              {"Aliases", nlohmann::json::array()},
                              {"Value", nullptr}});
            field = std::addressof(fields.back());
        }
        auto* value = std::addressof((*field)["Value"]);
        for (std::size_t index = 1; index < segments.size(); ++index)
        {
            if (!value->is_object())
                *value = nlohmann::json::object();
            value = std::addressof((*value)[std::string(segments[index])]);
        }
        return *value;
    }

    [[nodiscard]] ComponentPropertyBag ProjectManagedState(const std::string& state,
                                                           const std::vector<ComponentProperty>& properties)
    {
        ComponentPropertyBag result{{"managedState", state}};
        auto document = nlohmann::json::parse(state);
        for (const auto& property : properties)
        {
            const auto* value = ManagedStateValue(document, property.Key);
            result.emplace(property.Key, value ? ReadManagedPropertyValue(*value, property.Kind)
                                               : DefaultManagedPropertyValue(property.Kind));
        }
        return result;
    }

    [[nodiscard]] std::string ApplyManagedState(const std::string& state, const ComponentPropertyBag& values,
                                                const std::vector<ComponentProperty>& properties)
    {
        auto document = nlohmann::json::parse(state);
        if (!document.contains("Fields") && !document.contains("fields"))
            document["Fields"] = nlohmann::json::array();
        auto& fields = document.contains("Fields") ? document["Fields"] : document["fields"];
        if (!fields.is_array())
            throw std::invalid_argument("Managed component state fields are not an array.");
        for (const auto& property : properties)
        {
            const auto value = values.find(property.Key);
            if (value == values.end())
                continue;
            EnsureManagedStateValue(document, fields, property.Key) =
                WriteManagedPropertyValue(value->second, property.Kind);
        }
        return document.dump();
    }

    [[nodiscard]] ManagedAssetPropertyDescriptor ParseManagedAssetPropertyDescriptor(const nlohmann::json& source)
    {
        ManagedAssetPropertyDescriptor result;
        result.StableFieldId = AssetId::Parse(source.at("stableFieldId").get<std::string>());
        result.Name = source.at("name").get<std::string>();
        result.DisplayName = source.at("displayName").get<std::string>();
        result.ManagedTypeName = source.at("managedTypeName").get<std::string>();
        const auto kind = source.at("kind").get<std::uint32_t>();
        if (kind > static_cast<std::uint32_t>(ManagedAssetPropertyKind::AssetReference))
            throw std::runtime_error("Managed asset metadata contains an unsupported property kind.");
        result.Kind = static_cast<ManagedAssetPropertyKind>(kind);
        result.ReadOnly = source.value("readOnly", false);
        result.Hidden = source.value("hidden", false);
        if (const auto found = source.find("minimum"); found != source.end())
            result.Minimum = found->get<double>();
        if (const auto found = source.find("maximum"); found != source.end())
            result.Maximum = found->get<double>();
        result.Header = source.value("header", std::string{});
        result.Tooltip = source.value("tooltip", std::string{});
        result.Step = source.value("step", result.Step);
        result.Slider = source.value("slider", false);
        result.TextLines = source.value("textLines", result.TextLines);
        if (const auto found = source.find("expectedAssetType"); found != source.end())
            result.ExpectedAssetType = AssetTypeId(AssetId::Parse(found->get<std::string>()));
        if (const auto found = source.find("expectedManagedType"); found != source.end())
            result.ExpectedManagedType = ManagedTypeId::Parse(found->get<std::string>());
        result.IncludeDerivedAssetTypes = source.value("includeDerivedAssetTypes", true);
        if (const auto found = source.find("children"); found != source.end())
        {
            if (!found->is_array())
                throw std::runtime_error("Managed asset metadata property children are malformed.");
            result.Children.reserve(found->size());
            for (const auto& child : *found)
                result.Children.push_back(ParseManagedAssetPropertyDescriptor(child));
        }
        return result;
    }

    [[nodiscard]] ManagedAssetMetadataResult ParseManagedAssetMetadata(const std::string_view text)
    {
        const auto document = nlohmann::json::parse(text);
        if (document.at("schemaVersion").get<std::uint32_t>() != 1)
            throw std::runtime_error("Keire.Managed returned an unsupported managed asset metadata schema.");
        const auto& types = document.at("types");
        const auto& diagnostics = document.at("diagnostics");
        if (!types.is_array() || !diagnostics.is_array())
            throw std::runtime_error("Keire.Managed returned malformed managed asset metadata.");

        ManagedAssetMetadataResult result;
        result.Types.reserve(types.size());
        for (const auto& source : types)
        {
            ManagedAssetTypeDescriptor descriptor;
            descriptor.StableTypeId = ManagedTypeId::Parse(source.at("stableTypeId").get<std::string>());
            descriptor.FullName = source.at("fullName").get<std::string>();
            descriptor.DisplayName = source.at("displayName").get<std::string>();
            if (const auto found = source.find("baseTypeId"); found != source.end())
                descriptor.BaseTypeId = ManagedTypeId::Parse(found->get<std::string>());
            descriptor.MenuPath = source.value("menuPath", std::string{});
            descriptor.DefaultFileName = source.at("defaultFileName").get<std::string>();
            const auto& properties = source.at("properties");
            if (!properties.is_array())
                throw std::runtime_error("Managed asset metadata properties are malformed.");
            descriptor.Properties.reserve(properties.size());
            for (const auto& property : properties)
                descriptor.Properties.push_back(ParseManagedAssetPropertyDescriptor(property));
            ValidateManagedAssetTypeDescriptor(descriptor);
            result.Types.push_back(std::move(descriptor));
        }
        result.Diagnostics.reserve(diagnostics.size());
        for (const auto& source : diagnostics)
        {
            result.Diagnostics.push_back(
                {source.at("typeName").get<std::string>(), source.at("message").get<std::string>()});
        }

        std::ranges::sort(result.Types, {}, &ManagedAssetTypeDescriptor::FullName);
        std::ranges::sort(result.Diagnostics, {}, [](const ManagedAssetTypeDiagnostic& diagnostic)
                          { return std::tie(diagnostic.TypeName, diagnostic.Message); });
        std::set<ManagedTypeId> stableTypeIds;
        std::set<std::string, std::less<>> fullNames;
        std::set<std::string, std::less<>> menuPaths;
        for (const auto& descriptor : result.Types)
        {
            if (!stableTypeIds.emplace(descriptor.StableTypeId).second ||
                !fullNames.emplace(descriptor.FullName).second)
            {
                throw std::runtime_error("Managed asset metadata contains duplicate type names or stable IDs.");
            }
            if (!descriptor.MenuPath.empty() && !menuPaths.emplace(descriptor.MenuPath).second)
                throw std::runtime_error("Managed asset metadata contains duplicate CreateAssetMenu paths.");
        }
        return result;
    }
} // namespace Keire::Detail
