#include "KeireInternal/Scripting/ManagedReflection.h"

#include "Keire/Scripting/ManagedDataAsset.h"

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
                .SerializeReference = required("Keire.SerializeReferenceAttribute"),
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
                .Group = required("Keire.InspectorGroupAttribute"),
                .StableComponentId = required("Keire.StableComponentIdAttribute"),
                .StableAssetTypeId = required("Keire.StableAssetTypeIdAttribute"),
                .StableSerializedTypeId = required("Keire.StableSerializedTypeIdAttribute")};
    }

    [[nodiscard]] bool ManagedTypeDerivesFrom(Coral::Type& type, const std::string_view expected)
    {
        auto* current = std::addressof(type);
        while (*current)
        {
            const auto name = ManagedTypeName(*current);
            if (name == expected)
                return true;
            if (name == "System.Object")
                return false;
            current = std::addressof(current->GetBaseType());
        }
        return false;
    }

    [[nodiscard]] ComponentTypeId ManagedComponentType(Coral::Type& type,
                                                       const ManagedInspectorAttributeTypes& attributeTypes)
    {
        if (!attributeTypes.StableComponentId)
            return {};
        for (auto attribute : type.GetAttributes())
        {
            if (attribute.GetType() == *attributeTypes.StableComponentId)
            {
                return ComponentTypeId(AssetId(attribute.GetFieldValue<std::uint64_t>("High"),
                                               attribute.GetFieldValue<std::uint64_t>("Low")));
            }
        }
        return {};
    }

    [[nodiscard]] AssetTypeId ManagedAssetType(Coral::Type& type, const ManagedInspectorAttributeTypes& attributeTypes)
    {
        if (!attributeTypes.StableAssetTypeId)
            return {};
        for (auto attribute : type.GetAttributes())
        {
            if (attribute.GetType() == *attributeTypes.StableAssetTypeId)
            {
                return AssetTypeId(AssetId(attribute.GetFieldValue<std::uint64_t>("High"),
                                           attribute.GetFieldValue<std::uint64_t>("Low")));
            }
        }
        return {};
    }

    [[nodiscard]] bool ManagedTypeIsEnum(Coral::Type& type)
    {
        auto& baseType = type.GetBaseType();
        return baseType && ManagedTypeName(baseType) == "System.Enum";
    }

    [[nodiscard]] bool ManagedTypeIsInterface(Coral::Type& type)
    {
        const auto name = ManagedTypeName(type);
        return name != "System.Object" && !type.GetBaseType() && type.GetManagedType() == Coral::ManagedType::Unknown;
    }

    [[nodiscard]] bool ManagedTypeHasAttribute(Coral::Type& type, const std::string_view expected)
    {
        return std::ranges::any_of(type.GetAttributes(), [expected](auto attribute)
                                   { return ManagedTypeName(attribute.GetType()) == expected; });
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
        if (name == "Keire.Entity" || ManagedTypeDerivesFrom(type, "Keire.Component") || ManagedTypeIsInterface(type))
            return ComponentPropertyKind::Entity;
        if (name == "Keire.UiButton" || name == "Keire.UiSlider" || name == "Keire.UiToggle" ||
            name == "Keire.UiInputField" || name == "Keire.UiScrollView")
            return ComponentPropertyKind::Entity;
        if (name == "Keire.KeireEvent" || name.starts_with("Keire.KeireEvent`"))
            return ComponentPropertyKind::Event;
        if (ManagedTypeDerivesFrom(type, "Keire.Asset"))
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
            bool serializeReference = false;
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
                else if (attributeTypes.SerializeReference && attribute.GetType() == *attributeTypes.SerializeReference)
                {
                    serialized = true;
                    serializeReference = true;
                }
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
            if (serializeReference)
                continue;
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
                property.DeclaredManagedType = ManagedTypeName(fieldType);
                if (*kind == ComponentPropertyKind::Entity)
                {
                    if (property.DeclaredManagedType == "Keire.Entity")
                        property.ReferenceKind = ManagedReferenceKind::Entity;
                    else if (ManagedTypeDerivesFrom(fieldType, "Keire.Behaviour"))
                        property.ReferenceKind = ManagedReferenceKind::Behaviour;
                    else if (ManagedTypeDerivesFrom(fieldType, "Keire.Component"))
                        property.ReferenceKind = ManagedReferenceKind::Component;
                    else if (ManagedTypeIsInterface(fieldType))
                        property.ReferenceKind = ManagedReferenceKind::Behaviour;
                }
                else if (*kind == ComponentPropertyKind::Asset)
                {
                    if (ManagedTypeDerivesFrom(fieldType, "Keire.ScriptableObject"))
                        property.ReferenceKind = ManagedReferenceKind::ScriptableObject;
                    else if (property.DeclaredManagedType == "Keire.Prefab")
                        property.ReferenceKind = ManagedReferenceKind::Prefab;
                    else if (property.DeclaredManagedType == "Keire.SceneAsset")
                        property.ReferenceKind = ManagedReferenceKind::SceneAsset;
                    else
                        property.ReferenceKind = ManagedReferenceKind::Asset;
                }
                if (property.ReferenceKind == ManagedReferenceKind::Component ||
                    property.ReferenceKind == ManagedReferenceKind::Behaviour)
                {
                    if (const auto componentType = ManagedComponentType(fieldType, attributeTypes))
                        property.CompatibleComponentTypes.push_back(componentType);
                }
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
                if (*kind == ComponentPropertyKind::Asset)
                {
                    if (property.ReferenceKind == ManagedReferenceKind::ScriptableObject)
                        property.ExpectedAssetType = ManagedDataAsset::StaticType();
                    else if (const auto assetType = ManagedAssetType(fieldType, attributeTypes))
                        property.ExpectedAssetType = assetType;
                }
                result.push_back(std::move(property));
                continue;
            }
            const bool serializable =
                (attributeTypes.Serializable && fieldType.HasAttribute(*attributeTypes.Serializable)) ||
                ManagedTypeHasAttribute(fieldType, "System.SerializableAttribute");
            const auto fieldTypeName = ManagedTypeName(fieldType);
            if (fieldType.IsSZArray() || fieldTypeName.starts_with("System.Collections.Generic.List`") ||
                fieldTypeName.starts_with("System.Collections.Generic.Dictionary`"))
            {
                // Composite fields are described by the managed v3 metadata pass so their exact nested shape is
                // available to the shared collection/reference Inspector.
                continue;
            }
            if (depth >= 4 || !serializable || std::ranges::find(typeStack, fieldType.GetTypeId()) != typeStack.end())
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
        std::set<std::string, std::less<>> engineMethodNames;
        auto* current = std::addressof(const_cast<Coral::Type&>(concreteType).GetBaseType());
        while (*current && ManagedTypeName(*current) != "Keire.Behaviour")
            current = std::addressof(current->GetBaseType());
        if (*current)
        {
            for (auto method : current->GetMethods())
            {
                const Coral::ScopedString scopedName(method.GetName());
                engineMethodNames.emplace(static_cast<std::string>(scopedName));
            }
        }
        for (auto method : concreteType.GetMethods())
        {
            const Coral::ScopedString scopedName(method.GetName());
            const auto name = static_cast<std::string>(scopedName);
            if (engineMethodNames.contains(name))
                continue;
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

    [[nodiscard]] ComponentPropertyValue
    DefaultManagedPropertyValue(const ComponentPropertyKind kind,
                                const ManagedReferenceKind referenceKind = ManagedReferenceKind::None)
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
            return referenceKind == ManagedReferenceKind::Component || referenceKind == ManagedReferenceKind::Behaviour
                       ? ComponentPropertyValue(ComponentReferenceValue{})
                       : ComponentPropertyValue(EntityId{});
        case ComponentPropertyKind::Event:
            return ComponentEventValue{};
        case ComponentPropertyKind::Curve:
            return Curve1D{};
        case ComponentPropertyKind::Gradient:
            return ColorGradient{};
        case ComponentPropertyKind::ManagedReferenceGraph:
            return std::string(R"({"Version":1,"Root":{"Reference":0,"Scalar":null},"Objects":[]})");
        }
        throw std::logic_error("Unsupported managed Inspector property kind.");
    }

    [[nodiscard]] ComponentPropertyValue
    ReadManagedPropertyValue(const nlohmann::json& value, const ComponentPropertyKind kind,
                             const ManagedReferenceKind referenceKind = ManagedReferenceKind::None)
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
        std::function<AssetId(const nlohmann::json&)> readReferenceId;
        readReferenceId = [&readUnsignedInteger, &readReferenceId](const nlohmann::json& source) -> AssetId
        {
            if (source.is_null())
                return {};
            for (const auto name : {"entity", "Entity", "asset", "Asset"})
            {
                const auto nestedReference = source.find(name);
                if (nestedReference != source.end() && nestedReference->is_object())
                    return readReferenceId(*nestedReference);
            }
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
            return DefaultManagedPropertyValue(kind, referenceKind);
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
            if (referenceKind == ManagedReferenceKind::Component || referenceKind == ManagedReferenceKind::Behaviour)
            {
                const auto* component = JsonMember(value, "component", "Component");
                return ComponentReferenceValue{EntityId(readReferenceId(value)),
                                               component ? ComponentTypeId(readReferenceId(*component))
                                                         : ComponentTypeId{}};
            }
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
        case ComponentPropertyKind::ManagedReferenceGraph:
            return value.dump();
        }
        throw std::logic_error("Unsupported managed Inspector property kind.");
    }

    [[nodiscard]] nlohmann::json WriteManagedPropertyValue(const ComponentPropertyValue& value,
                                                           const ComponentProperty& property)
    {
        const auto kind = property.Kind;
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
            if (!asset)
                return nullptr;
            return {{"$ref", "asset"},
                    {"asset", {{"High", asset.High()}, {"Low", asset.Low()}}},
                    {"type", property.DeclaredManagedType}};
        }
        case ComponentPropertyKind::Entity:
        {
            const bool componentReference = property.ReferenceKind == ManagedReferenceKind::Component ||
                                            property.ReferenceKind == ManagedReferenceKind::Behaviour;
            const auto entity = componentReference ? std::get<ComponentReferenceValue>(value).Entity.Value()
                                                   : std::get<EntityId>(value).Value();
            if (!entity)
                return nullptr;
            const auto entityReference =
                nlohmann::json{{"$ref", "entity"}, {"entity", {{"High", entity.High()}, {"Low", entity.Low()}}}};
            if (property.ReferenceKind != ManagedReferenceKind::Component &&
                property.ReferenceKind != ManagedReferenceKind::Behaviour)
            {
                return entityReference;
            }
            const auto selected = std::get<ComponentReferenceValue>(value).Component;
            nlohmann::json component = nullptr;
            if (selected)
            {
                const auto type = selected.Value();
                component = {{"High", type.High()}, {"Low", type.Low()}};
            }
            return {{"$ref", "component"},
                    {"entity", std::move(entityReference)},
                    {"component", std::move(component)},
                    {"type", property.DeclaredManagedType}};
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
        case ComponentPropertyKind::ManagedReferenceGraph:
            return nlohmann::json::parse(std::get<std::string>(value));
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

    [[nodiscard]] std::string ManagedStateGraphRootKey(const nlohmann::json& field, const std::string_view propertyKey)
    {
        if (const auto* root = JsonMember(field, "ReferenceGraphRoot", "referenceGraphRoot"))
        {
            if (!root->is_string() || root->get_ref<const std::string&>().empty())
                throw std::invalid_argument("Managed reference-graph root keys must be non-empty strings.");
            return root->get<std::string>();
        }
        if (const auto* stableId = JsonMember(field, "StableId", "stableId");
            stableId && stableId->is_string() && !stableId->get_ref<const std::string&>().empty())
        {
            auto value = stableId->get<std::string>();
            std::ranges::transform(value, value.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return "id:" + value;
        }
        return "field:" + std::string(propertyKey);
    }

    [[nodiscard]] ManagedReferenceGraph ManagedStateSharedGraph(const nlohmann::json& document)
    {
        const auto* encoded = JsonMember(document, "ReferenceGraph", "referenceGraph");
        if (!encoded)
            return {.Version = 2};
        if (!encoded->is_object())
            throw std::invalid_argument("Managed state shared reference graph must be an object.");
        auto graph = DecodeManagedReferenceGraph(encoded->dump());
        if (graph.Version != 2)
            throw std::invalid_argument("Managed state shared reference graph must use version 2.");
        ValidateManagedReferenceGraphDocument(graph);
        return graph;
    }

    void SetManagedStateSharedGraph(nlohmann::json& document, const ManagedReferenceGraph& graph)
    {
        auto encoded = nlohmann::json::parse(EncodeManagedReferenceGraph(graph));
        if (!document.contains("ReferenceGraph") && document.contains("referenceGraph"))
            document["referenceGraph"] = std::move(encoded);
        else
            document["ReferenceGraph"] = std::move(encoded);
    }

    [[nodiscard]] ComponentPropertyBag ProjectManagedState(const std::string& state,
                                                           const std::vector<ComponentProperty>& properties)
    {
        ComponentPropertyBag result{{"managedState", state}};
        auto document = nlohmann::json::parse(state);
        for (const auto& property : properties)
        {
            const auto* value = ManagedStateValue(document, property.Key);
            auto projected = DefaultManagedPropertyValue(property.Kind, property.ReferenceKind);
            if (property.Kind == ComponentPropertyKind::ManagedReferenceGraph)
            {
                if (!property.ReferenceGraph)
                {
                    throw std::logic_error("Managed graph property metadata is missing its graph descriptor.");
                }
                const auto segments = ManagedPropertyPath(property.Key);
                const auto* field = segments.empty() ? nullptr : ManagedStateField(document, segments.front());
                const auto* marker = field ? JsonMember(*field, "ReferenceGraph", "referenceGraph") : nullptr;
                if (property.ReferenceGraph->Root.ReferenceGraph)
                {
                    if (field && (!marker || !marker->is_boolean() || !marker->get<bool>()))
                    {
                        throw std::invalid_argument("KEIRE-MANAGED-SERIALIZATION-0003: Managed Behaviour field '" +
                                                    property.Key + "' is missing its reference-graph marker.");
                    }
                    const auto* rootKey =
                        field ? JsonMember(*field, "ReferenceGraphRoot", "referenceGraphRoot") : nullptr;
                    ManagedReferenceGraph graph;
                    if (rootKey)
                    {
                        if (!rootKey->is_string() || rootKey->get_ref<const std::string&>().empty())
                            throw std::invalid_argument("Managed Behaviour field '" + property.Key +
                                                        "' has an invalid reference-graph root key.");
                        const auto* shared = JsonMember(document, "ReferenceGraph", "referenceGraph");
                        if (!shared || !shared->is_object())
                            throw std::invalid_argument("Managed Behaviour field '" + property.Key +
                                                        "' references a missing shared object table.");
                        graph = ExtractManagedReferenceGraphRoot(DecodeManagedReferenceGraph(shared->dump()),
                                                                 rootKey->get_ref<const std::string&>());
                    }
                    else
                    {
                        projected = value ? ReadManagedPropertyValue(*value, property.Kind, property.ReferenceKind)
                                          : DefaultManagedPropertyValue(property.Kind, property.ReferenceKind);
                        graph = DecodeManagedReferenceGraph(std::get<std::string>(projected));
                    }
                    ValidateManagedReferenceGraph(graph, property.ReferenceGraph->Root, property.ReferenceGraph->Types);
                    projected = EncodeManagedReferenceGraph(graph);
                }
                else
                {
                    if (marker && (!marker->is_boolean() || marker->get<bool>()))
                    {
                        throw std::invalid_argument(
                            "KEIRE-MANAGED-SERIALIZATION-0003: Managed Behaviour collection field '" + property.Key +
                            "' declared as '" + property.DeclaredManagedType +
                            "' is incorrectly marked as a reference graph.");
                    }
                    try
                    {
                        const auto kind = property.ReferenceGraph->Root.Kind;
                        const auto fallback = kind == ManagedAssetPropertyKind::Array ||
                                                      kind == ManagedAssetPropertyKind::List ||
                                                      kind == ManagedAssetPropertyKind::Dictionary
                                                  ? "[]"
                                                  : "null";
                        const auto decoded =
                            DecodeManagedAssetValue(value ? value->dump() : fallback, property.ReferenceGraph->Root);
                        projected = EncodeManagedAssetValue(decoded, property.ReferenceGraph->Root);
                    }
                    catch (const std::exception& error)
                    {
                        throw std::invalid_argument(
                            "KEIRE-MANAGED-SERIALIZATION-0003: Managed Behaviour collection field '" + property.Key +
                            "' declared as '" + property.DeclaredManagedType + "' is invalid: " + error.what());
                    }
                }
            }
            else
            {
                projected = value ? ReadManagedPropertyValue(*value, property.Kind, property.ReferenceKind)
                                  : DefaultManagedPropertyValue(property.Kind, property.ReferenceKind);
            }
            result.emplace(property.Key, std::move(projected));
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
            if (property.Kind == ComponentPropertyKind::ManagedReferenceGraph)
            {
                if (!property.ReferenceGraph)
                    throw std::logic_error("Managed graph property metadata is missing its graph descriptor.");
                const auto& encoded = std::get<std::string>(value->second);
                std::string canonical;
                if (property.ReferenceGraph->Root.ReferenceGraph)
                {
                    const auto graph = DecodeManagedReferenceGraph(encoded);
                    ValidateManagedReferenceGraph(graph, property.ReferenceGraph->Root, property.ReferenceGraph->Types);
                    const auto segments = ManagedPropertyPath(property.Key);
                    if (segments.empty())
                        throw std::logic_error("Managed graph property path is empty.");
                    (void)EnsureManagedStateValue(document, fields, property.Key);
                    auto* field = ManagedStateField(document, segments.front());
                    if (!field)
                        throw std::logic_error("Managed graph state field could not be materialized.");
                    const auto rootKey = ManagedStateGraphRootKey(*field, property.Key);
                    auto shared = ManagedStateSharedGraph(document);
                    UpdateManagedReferenceGraphRoot(shared, rootKey, graph);
                    SetManagedStateSharedGraph(document, shared);
                    field->erase("Value");
                    field->erase("value");
                    (*field)["ReferenceGraph"] = true;
                    (*field)["ReferenceGraphRoot"] = rootKey;
                    field->erase("referenceGraph");
                    field->erase("referenceGraphRoot");
                    continue;
                }
                else
                {
                    try
                    {
                        const auto decoded = DecodeManagedAssetValue(encoded, property.ReferenceGraph->Root);
                        canonical = EncodeManagedAssetValue(decoded, property.ReferenceGraph->Root);
                    }
                    catch (const std::exception& error)
                    {
                        throw std::invalid_argument(
                            "KEIRE-MANAGED-SERIALIZATION-0003: Managed Behaviour collection field '" + property.Key +
                            "' declared as '" + property.DeclaredManagedType + "' is invalid: " + error.what());
                    }
                }
                EnsureManagedStateValue(document, fields, property.Key) = nlohmann::json::parse(canonical);
                const auto segments = ManagedPropertyPath(property.Key);
                if (segments.empty())
                    throw std::logic_error("Managed graph property path is empty.");
                auto* field = ManagedStateField(document, segments.front());
                if (!field)
                    throw std::logic_error("Managed graph state field could not be materialized.");
                if (property.ReferenceGraph->Root.ReferenceGraph)
                    (*field)["ReferenceGraph"] = true;
                else
                {
                    field->erase("ReferenceGraph");
                    field->erase("referenceGraph");
                    field->erase("ReferenceGraphRoot");
                    field->erase("referenceGraphRoot");
                }
            }
            else
            {
                EnsureManagedStateValue(document, fields, property.Key) =
                    WriteManagedPropertyValue(value->second, property);
            }
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
        if (kind > static_cast<std::uint32_t>(ManagedAssetPropertyKind::Dictionary))
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
        result.ReferenceGraph = source.value("referenceGraph", false);
        if (const auto found = source.find("expectedAssetType"); found != source.end())
            result.ExpectedAssetType = AssetTypeId(AssetId::Parse(found->get<std::string>()));
        if (const auto found = source.find("expectedManagedType"); found != source.end())
            result.ExpectedManagedType = ManagedTypeId::Parse(found->get<std::string>());
        result.IncludeDerivedAssetTypes = source.value("includeDerivedAssetTypes", true);
        if (const auto found = source.find("referenceTypeChoices"); found != source.end())
        {
            if (!found->is_array())
                throw std::runtime_error("Managed asset metadata reference type choices are malformed.");
            result.ReferenceTypeChoices.reserve(found->size());
            for (const auto& type : *found)
                result.ReferenceTypeChoices.push_back(ManagedTypeId::Parse(type.get<std::string>()));
        }
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

    [[nodiscard]] std::vector<ManagedAssetReferenceTypeDescriptor>
    ParseManagedReferenceTypes(const nlohmann::json& source)
    {
        const auto referenceTypes = source.find("referenceTypes");
        if (referenceTypes == source.end())
            return {};
        if (!referenceTypes->is_array())
            throw std::runtime_error("Managed asset metadata reference types are malformed.");
        std::vector<ManagedAssetReferenceTypeDescriptor> result;
        result.reserve(referenceTypes->size());
        for (const auto& encoded : *referenceTypes)
        {
            ManagedAssetReferenceTypeDescriptor referenceType;
            referenceType.StableTypeId = ManagedTypeId::Parse(encoded.at("stableTypeId").get<std::string>());
            referenceType.FullName = encoded.at("fullName").get<std::string>();
            referenceType.DisplayName = encoded.at("displayName").get<std::string>();
            const auto& fields = encoded.at("properties");
            if (!fields.is_array())
                throw std::runtime_error("Managed asset metadata reference type fields are malformed.");
            referenceType.Properties.reserve(fields.size());
            for (const auto& field : fields)
                referenceType.Properties.push_back(ParseManagedAssetPropertyDescriptor(field));
            result.push_back(std::move(referenceType));
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
            descriptor.ReferenceTypes = ParseManagedReferenceTypes(source);
            ValidateManagedAssetTypeDescriptor(descriptor);
            result.Types.push_back(std::move(descriptor));
        }
        if (const auto behaviours = document.find("behaviours"); behaviours != document.end())
        {
            if (!behaviours->is_array())
                throw std::runtime_error("Managed Behaviour graph metadata is malformed.");
            result.Behaviours.reserve(behaviours->size());
            for (const auto& source : *behaviours)
            {
                ManagedAssetMetadataResult::BehaviourGraph behaviour;
                behaviour.FullName = source.at("fullName").get<std::string>();
                const auto& properties = source.at("properties");
                if (!properties.is_array())
                    throw std::runtime_error("Managed Behaviour graph fields are malformed.");
                const auto referenceTypes = ParseManagedReferenceTypes(source);
                behaviour.Fields.reserve(properties.size());
                for (const auto& property : properties)
                {
                    ManagedReferenceGraphDescriptor graph;
                    graph.Root = ParseManagedAssetPropertyDescriptor(property);
                    graph.Types = referenceTypes;
                    behaviour.Fields.push_back(std::move(graph));
                }
                result.Behaviours.push_back(std::move(behaviour));
            }
        }
        result.Diagnostics.reserve(diagnostics.size());
        for (const auto& source : diagnostics)
        {
            result.Diagnostics.push_back({.TypeName = source.at("typeName").get<std::string>(),
                                          .Message = source.at("message").get<std::string>(),
                                          .Code = source.value("code", std::string{}),
                                          .Phase = source.value("phase", std::string{}),
                                          .Owner = source.value("owner", std::string{}),
                                          .RootField = source.value("rootField", std::string{}),
                                          .FieldPath = source.value("fieldPath", std::string{}),
                                          .DeclaredType = source.value("declaredType", std::string{}),
                                          .RuntimeType = source.value("runtimeType", std::string{}),
                                          .SerializedTypeId = source.value("serializedTypeId", std::string{}),
                                          .ObjectId = source.contains("objectId") && !source.at("objectId").is_null()
                                                          ? std::optional(source.at("objectId").get<std::uint32_t>())
                                                          : std::nullopt});
        }

        std::ranges::sort(result.Types, {}, &ManagedAssetTypeDescriptor::FullName);
        std::ranges::sort(result.Behaviours, {}, &ManagedAssetMetadataResult::BehaviourGraph::FullName);
        std::ranges::sort(
            result.Diagnostics, {}, [](const ManagedAssetTypeDiagnostic& diagnostic)
            { return std::tie(diagnostic.TypeName, diagnostic.FieldPath, diagnostic.Code, diagnostic.Message); });
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
