#include "Keire/Scripting/ManagedDataAsset.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t MaximumDocumentBytes = 16U * 1024U * 1024U;
        constexpr std::size_t MaximumFieldCount = 16U * 1024U;
        constexpr std::size_t MaximumDependencyCount = 16U * 1024U;
        constexpr std::size_t MaximumAliasesPerField = 128;
        constexpr std::size_t MaximumTextBytes = 2048;
        constexpr std::size_t MaximumValueNodes = 128U * 1024U;
        constexpr std::size_t MaximumValueDepth = 32;
        constexpr std::size_t MaximumTypeCatalogBytes = 16U * 1024U * 1024U;

        [[nodiscard]] bool HasVisibleText(const std::string_view value) noexcept
        {
            if (value.empty() || value.size() > MaximumTextBytes)
                return false;
            bool visible = false;
            for (const unsigned char character : value)
            {
                if (character < 0x20U)
                    return false;
                visible = visible || character > 0x20U;
            }
            return visible;
        }

        void ValidateJsonValue(const Json& value, const std::size_t depth, std::size_t& nodes)
        {
            if (depth > MaximumValueDepth || ++nodes > MaximumValueNodes)
                throw std::invalid_argument("Managed data field value exceeds the nesting or node limit.");
            if (value.is_discarded() || value.is_binary())
                throw std::invalid_argument("Managed data field value contains an unsupported JSON value.");
            if (value.is_array())
            {
                for (const auto& element : value)
                    ValidateJsonValue(element, depth + 1, nodes);
            }
            else if (value.is_object())
            {
                for (const auto& [name, element] : value.items())
                {
                    if (!HasVisibleText(name))
                        throw std::invalid_argument("Managed data field value contains an invalid object member name.");
                    ValidateJsonValue(element, depth + 1, nodes);
                }
            }
        }

        [[nodiscard]] Json ParseFieldValue(const std::string_view text)
        {
            if (text.empty() || text.size() > MaximumDocumentBytes)
                throw std::invalid_argument("Managed data field value is empty or exceeds the size limit.");
            try
            {
                const auto value = Json::parse(text.begin(), text.end());
                std::size_t nodes = 0;
                ValidateJsonValue(value, 0, nodes);
                return value;
            }
            catch (const Json::exception& exception)
            {
                throw std::invalid_argument(std::string("Managed data field value is malformed: ") + exception.what());
            }
        }

        [[nodiscard]] bool FieldLess(const ManagedDataFieldState& left, const ManagedDataFieldState& right) noexcept
        {
            return left.StableFieldId < right.StableFieldId;
        }

        [[nodiscard]] bool DependencyLess(const ManagedDataAssetDependency& left,
                                          const ManagedDataAssetDependency& right) noexcept
        {
            return left.Asset < right.Asset;
        }

        [[nodiscard]] bool IsNumericProperty(const ManagedAssetPropertyKind kind) noexcept
        {
            return kind == ManagedAssetPropertyKind::Integer || kind == ManagedAssetPropertyKind::UnsignedInteger ||
                   kind == ManagedAssetPropertyKind::Scalar;
        }

        [[nodiscard]] bool IsContainerProperty(const ManagedAssetPropertyKind kind) noexcept
        {
            return kind == ManagedAssetPropertyKind::SerializableObject || kind == ManagedAssetPropertyKind::Array ||
                   kind == ManagedAssetPropertyKind::List;
        }

        [[nodiscard]] bool IsValidMenuPath(const std::string_view value) noexcept
        {
            if (value.empty())
                return true;
            if (value.front() == '/' || value.back() == '/' || value.find("//") != std::string_view::npos)
                return false;
            std::size_t start = 0;
            while (start < value.size())
            {
                const auto end = value.find('/', start);
                const auto segment =
                    value.substr(start, end == std::string_view::npos ? value.size() - start : end - start);
                if (!HasVisibleText(segment) || segment == "." || segment == "..")
                    return false;
                if (end == std::string_view::npos)
                    break;
                start = end + 1;
            }
            return true;
        }

        void ValidateProperty(const ManagedAssetPropertyDescriptor& property, const std::size_t depth,
                              std::size_t& count)
        {
            if (depth > MaximumValueDepth || ++count > MaximumFieldCount)
                throw std::invalid_argument("Managed asset property tree exceeds the depth or property-count limit.");
            if (!property.StableFieldId)
                throw std::invalid_argument("Managed asset properties require non-empty stable field IDs.");
            if (property.Kind > ManagedAssetPropertyKind::AssetReference)
                throw std::invalid_argument("Managed asset property uses an unsupported property kind.");
            if (!HasVisibleText(property.Name) || !HasVisibleText(property.DisplayName) ||
                !HasVisibleText(property.ManagedTypeName))
            {
                throw std::invalid_argument("Managed asset property names and managed types must be non-empty.");
            }

            if (property.Minimum.has_value() != property.Maximum.has_value() ||
                (property.Minimum && (!IsNumericProperty(property.Kind) || !std::isfinite(*property.Minimum) ||
                                      !std::isfinite(*property.Maximum) || *property.Minimum > *property.Maximum)))
            {
                throw std::invalid_argument("Managed asset property ranges must be finite, ordered, and numeric.");
            }
            if ((!property.Header.empty() && !HasVisibleText(property.Header)) ||
                (!property.Tooltip.empty() && !HasVisibleText(property.Tooltip)))
            {
                throw std::invalid_argument("Managed asset property headers and tooltips contain invalid text.");
            }

            if (property.Kind == ManagedAssetPropertyKind::AssetReference)
            {
                if (!property.ExpectedAssetType || !*property.ExpectedAssetType)
                    throw std::invalid_argument("Managed asset references require an expected native asset type.");
                if (property.ExpectedManagedType && !static_cast<bool>(*property.ExpectedManagedType))
                    throw std::invalid_argument("Managed asset references cannot use an empty managed type ID.");
                if (property.ExpectedManagedType && *property.ExpectedAssetType != ManagedDataAsset::StaticType())
                {
                    throw std::invalid_argument(
                        "A managed reference type may only constrain ManagedDataAsset references.");
                }
            }
            else if (property.ExpectedAssetType || property.ExpectedManagedType)
            {
                throw std::invalid_argument("Only managed asset-reference properties may declare asset constraints.");
            }

            if (!IsContainerProperty(property.Kind) && !property.Children.empty())
                throw std::invalid_argument("Only object, array, and list properties may contain child descriptors.");
            if ((property.Kind == ManagedAssetPropertyKind::Array || property.Kind == ManagedAssetPropertyKind::List) &&
                property.Children.size() != 1)
            {
                throw std::invalid_argument("Managed array and list descriptors require exactly one element child.");
            }

            std::set<std::string, std::less<>> childNames;
            std::set<AssetId> childStableIds;
            for (const auto& child : property.Children)
            {
                if (!childNames.emplace(child.Name).second || !childStableIds.emplace(child.StableFieldId).second)
                    throw std::invalid_argument("Managed asset properties require unique sibling names and IDs.");
                ValidateProperty(child, depth + 1, count);
            }
        }

        [[nodiscard]] Json EncodePropertyDescriptor(const ManagedAssetPropertyDescriptor& property)
        {
            Json result{{"stableFieldId", property.StableFieldId.ToString()},
                        {"name", property.Name},
                        {"displayName", property.DisplayName},
                        {"managedTypeName", property.ManagedTypeName},
                        {"kind", static_cast<std::uint8_t>(property.Kind)},
                        {"readOnly", property.ReadOnly},
                        {"hidden", property.Hidden},
                        {"header", property.Header},
                        {"tooltip", property.Tooltip},
                        {"includeDerivedAssetTypes", property.IncludeDerivedAssetTypes}};
            if (property.Minimum)
                result["minimum"] = *property.Minimum;
            if (property.Maximum)
                result["maximum"] = *property.Maximum;
            if (property.ExpectedAssetType)
                result["expectedAssetType"] = property.ExpectedAssetType->ToString();
            if (property.ExpectedManagedType)
                result["expectedManagedType"] = property.ExpectedManagedType->ToString();
            result["children"] = Json::array();
            for (const auto& child : property.Children)
                result["children"].push_back(EncodePropertyDescriptor(child));
            return result;
        }

        [[nodiscard]] ManagedAssetPropertyDescriptor DecodePropertyDescriptor(const Json& source)
        {
            if (!source.is_object())
                throw std::invalid_argument("Managed type catalog property entries must be objects.");
            ManagedAssetPropertyDescriptor result;
            result.StableFieldId = AssetId::Parse(source.at("stableFieldId").get<std::string>());
            result.Name = source.at("name").get<std::string>();
            result.DisplayName = source.at("displayName").get<std::string>();
            result.ManagedTypeName = source.at("managedTypeName").get<std::string>();
            const auto kind = source.at("kind").get<std::uint32_t>();
            if (kind > static_cast<std::uint32_t>(ManagedAssetPropertyKind::AssetReference))
                throw std::invalid_argument("Managed type catalog contains an unsupported property kind.");
            result.Kind = static_cast<ManagedAssetPropertyKind>(kind);
            result.ReadOnly = source.value("readOnly", false);
            result.Hidden = source.value("hidden", false);
            if (const auto found = source.find("minimum"); found != source.end())
                result.Minimum = found->get<double>();
            if (const auto found = source.find("maximum"); found != source.end())
                result.Maximum = found->get<double>();
            result.Header = source.value("header", std::string{});
            result.Tooltip = source.value("tooltip", std::string{});
            if (const auto found = source.find("expectedAssetType"); found != source.end())
                result.ExpectedAssetType = AssetTypeId::Parse(found->get<std::string>());
            if (const auto found = source.find("expectedManagedType"); found != source.end())
                result.ExpectedManagedType = ManagedTypeId::Parse(found->get<std::string>());
            result.IncludeDerivedAssetTypes = source.value("includeDerivedAssetTypes", true);
            const auto& children = source.at("children");
            if (!children.is_array())
                throw std::invalid_argument("Managed type catalog property children must be an array.");
            result.Children.reserve(children.size());
            for (const auto& child : children)
                result.Children.push_back(DecodePropertyDescriptor(child));
            return result;
        }

        void ValidateTypeCatalog(const std::span<const ManagedAssetTypeDescriptor> descriptors)
        {
            std::set<ManagedTypeId> stableTypeIds;
            std::set<std::string, std::less<>> fullNames;
            std::set<std::string, std::less<>> menuPaths;
            for (const auto& descriptor : descriptors)
            {
                ValidateManagedAssetTypeDescriptor(descriptor);
                if (!stableTypeIds.emplace(descriptor.StableTypeId).second ||
                    !fullNames.emplace(descriptor.FullName).second)
                {
                    throw std::invalid_argument("Managed type catalog contains duplicate type names or stable IDs.");
                }
                if (!descriptor.MenuPath.empty() && !menuPaths.emplace(descriptor.MenuPath).second)
                    throw std::invalid_argument("Managed type catalog contains duplicate CreateAssetMenu paths.");
            }
            for (const auto& descriptor : descriptors)
            {
                auto current = descriptor.BaseTypeId;
                std::set<ManagedTypeId> visited;
                while (current)
                {
                    if (!visited.emplace(*current).second || *current == descriptor.StableTypeId)
                        throw std::invalid_argument("Managed type catalog contains an inheritance cycle.");
                    const auto found =
                        std::ranges::find(descriptors, *current, &ManagedAssetTypeDescriptor::StableTypeId);
                    current = found == descriptors.end() ? std::nullopt : found->BaseTypeId;
                }
            }
        }

        [[nodiscard]] const Json* FindMember(const Json& value, const std::string_view primary,
                                             const std::string_view fallback)
        {
            if (const auto found = value.find(std::string(primary)); found != value.end())
                return std::addressof(*found);
            if (const auto found = value.find(std::string(fallback)); found != value.end())
                return std::addressof(*found);
            return nullptr;
        }

        [[nodiscard]] std::uint64_t ReadUnsigned(const Json& value)
        {
            if (value.is_number_unsigned())
                return value.get<std::uint64_t>();
            if (value.is_number_integer())
            {
                const auto signedValue = value.get<std::int64_t>();
                if (signedValue >= 0)
                    return static_cast<std::uint64_t>(signedValue);
            }
            throw std::invalid_argument("Managed data contains a value that is not an unsigned integer.");
        }

        void ValidateSignedInteger(const Json& value)
        {
            if (value.is_number_integer())
            {
                (void)value.get<std::int64_t>();
                return;
            }
            if (value.is_number_unsigned() &&
                value.get<std::uint64_t>() <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            {
                return;
            }
            throw std::invalid_argument("Managed data contains a value that is not a signed integer.");
        }

        [[nodiscard]] double ReadFiniteNumber(const Json& value)
        {
            if (!value.is_number())
                throw std::invalid_argument("Managed data contains a value that is not numeric.");
            const auto number = value.get<double>();
            if (!std::isfinite(number))
                throw std::invalid_argument("Managed data contains a non-finite number.");
            return number;
        }

        [[nodiscard]] AssetId ReadAssetReference(const Json& value)
        {
            if (value.is_null())
                return {};
            if (!value.is_object())
                throw std::invalid_argument("Managed data asset reference must be an object or null.");
            const auto* nested = FindMember(value, "Id", "id");
            const auto& id = nested ? *nested : value;
            if (!id.is_object())
                throw std::invalid_argument("Managed data asset reference ID must be an object.");
            const auto* high = FindMember(id, "High", "high");
            const auto* low = FindMember(id, "Low", "low");
            if (!high || !low)
                throw std::invalid_argument("Managed data asset reference is missing its stable ID.");
            return AssetId(ReadUnsigned(*high), ReadUnsigned(*low));
        }

        void AddExpectedDependency(std::map<AssetId, ManagedDataAssetDependency>& dependencies,
                                   std::map<AssetId, bool>& includeDerivedByAsset, const AssetId referenced,
                                   const ManagedAssetPropertyDescriptor& property)
        {
            if (!referenced)
                return;
            ManagedDataAssetDependency expected{referenced, *property.ExpectedAssetType, property.ExpectedManagedType};
            const auto [found, inserted] = dependencies.emplace(referenced, expected);
            if (!inserted && !(found->second == expected))
                throw std::invalid_argument("Managed data references one asset through incompatible typed references.");
            if (property.ExpectedManagedType)
            {
                const auto [derived, derivedInserted] =
                    includeDerivedByAsset.emplace(referenced, property.IncludeDerivedAssetTypes);
                if (!derivedInserted && derived->second != property.IncludeDerivedAssetTypes)
                {
                    throw std::invalid_argument(
                        "Managed data references one asset through conflicting derived-type constraints.");
                }
            }
        }

        void ValidateValueForProperty(const Json& value, const ManagedAssetPropertyDescriptor& property,
                                      std::map<AssetId, ManagedDataAssetDependency>& dependencies,
                                      std::map<AssetId, bool>& includeDerivedByAsset, const std::size_t depth)
        {
            if (depth > MaximumValueDepth)
                throw std::invalid_argument("Managed data semantic validation exceeded the supported nesting depth.");
            switch (property.Kind)
            {
            case ManagedAssetPropertyKind::Boolean:
                if (!value.is_boolean())
                    throw std::invalid_argument("Managed Boolean field value has the wrong JSON type.");
                return;
            case ManagedAssetPropertyKind::Integer:
                if (property.ManagedTypeName == "System.Char")
                {
                    if (!value.is_string() || value.get_ref<const std::string&>().size() != 1)
                        throw std::invalid_argument("Managed character field value must be a one-character string.");
                    return;
                }
                ValidateSignedInteger(value);
                return;
            case ManagedAssetPropertyKind::UnsignedInteger:
                (void)ReadUnsigned(value);
                return;
            case ManagedAssetPropertyKind::Scalar:
                (void)ReadFiniteNumber(value);
                return;
            case ManagedAssetPropertyKind::Text:
                if (!value.is_null() && !value.is_string())
                    throw std::invalid_argument("Managed text field value has the wrong JSON type.");
                return;
            case ManagedAssetPropertyKind::Enum:
                if (!value.is_number_integer() && !value.is_number_unsigned())
                    throw std::invalid_argument("Managed enum field value has the wrong JSON type.");
                return;
            case ManagedAssetPropertyKind::Vector2:
            case ManagedAssetPropertyKind::Vector3:
            case ManagedAssetPropertyKind::Vector4:
            case ManagedAssetPropertyKind::Quaternion:
            case ManagedAssetPropertyKind::Color:
            {
                if (!value.is_object())
                    throw std::invalid_argument("Managed vector, quaternion, or color field value must be an object.");
                const std::array vectorNames{"X", "Y", "Z", "W"};
                const std::array colorNames{"Red", "Green", "Blue", "Alpha"};
                const auto count = property.Kind == ManagedAssetPropertyKind::Vector2   ? 2U
                                   : property.Kind == ManagedAssetPropertyKind::Vector3 ? 3U
                                                                                        : 4U;
                const auto& names = property.Kind == ManagedAssetPropertyKind::Color ? colorNames : vectorNames;
                for (std::size_t index = 0; index < count; ++index)
                {
                    const auto fallback = std::string(1, static_cast<char>(std::tolower(names[index][0]))) +
                                          std::string(names[index]).substr(1);
                    const auto* member = FindMember(value, names[index], fallback);
                    if (!member)
                        throw std::invalid_argument("Managed vector, quaternion, or color field is incomplete.");
                    (void)ReadFiniteNumber(*member);
                }
                return;
            }
            case ManagedAssetPropertyKind::SerializableObject:
            {
                if (value.is_null())
                    return;
                if (!value.is_object())
                    throw std::invalid_argument("Managed serializable-object field value must be an object or null.");
                std::set<std::string, std::less<>> knownNames;
                for (const auto& child : property.Children)
                {
                    knownNames.emplace(child.Name);
                    if (const auto found = value.find(child.Name); found != value.end())
                        ValidateValueForProperty(*found, child, dependencies, includeDerivedByAsset, depth + 1);
                }
                for (const auto& [name, unused] : value.items())
                {
                    (void)unused;
                    if (!knownNames.contains(name))
                        throw std::invalid_argument("Managed serializable-object field contains an unknown member.");
                }
                return;
            }
            case ManagedAssetPropertyKind::Array:
            case ManagedAssetPropertyKind::List:
                if (value.is_null())
                    return;
                if (!value.is_array())
                    throw std::invalid_argument("Managed collection field value must be an array or null.");
                for (const auto& element : value)
                    ValidateValueForProperty(element, property.Children.front(), dependencies, includeDerivedByAsset,
                                             depth + 1);
                return;
            case ManagedAssetPropertyKind::AssetReference:
                AddExpectedDependency(dependencies, includeDerivedByAsset, ReadAssetReference(value), property);
                return;
            }
            throw std::invalid_argument("Managed data field uses an unsupported property kind.");
        }

        [[nodiscard]] bool
        IsManagedTypeCompatible(const ManagedTypeId actual, const ManagedTypeId expected, const bool includeDerived,
                                const std::span<const ManagedAssetTypeDescriptor> descriptors) noexcept
        {
            if (actual == expected)
                return true;
            if (!includeDerived)
                return false;
            auto current = actual;
            for (std::size_t depth = 0; current && depth < descriptors.size(); ++depth)
            {
                const auto found = std::ranges::find(descriptors, current, &ManagedAssetTypeDescriptor::StableTypeId);
                if (found == descriptors.end() || !found->BaseTypeId)
                    return false;
                current = *found->BaseTypeId;
                if (current == expected)
                    return true;
            }
            return false;
        }
    } // namespace

    ManagedDataAsset::ManagedDataAsset(ManagedDataDefinition definition)
        : m_Definition(Canonicalize(std::move(definition)))
    {
        Validate(m_Definition);
        m_ResidentBytes = Encode(m_Definition).size();
        m_Revision = 1;
    }

    std::vector<AssetId> ManagedDataAsset::AssetDependencies() const
    {
        std::vector<AssetId> result;
        result.reserve(m_Definition.Dependencies.size());
        for (const auto& dependency : m_Definition.Dependencies)
            result.push_back(dependency.Asset);
        return result;
    }

    ManagedDataDefinition ManagedDataAsset::Canonicalize(ManagedDataDefinition definition)
    {
        for (auto& field : definition.Fields)
        {
            std::ranges::sort(field.FormerNames);
            field.Value = ParseFieldValue(field.Value).dump();
        }
        std::ranges::sort(definition.Fields, FieldLess);
        std::ranges::sort(definition.Dependencies, DependencyLess);
        return definition;
    }

    void ManagedDataAsset::Validate(const ManagedDataDefinition& definition)
    {
        if (definition.SchemaVersion != ManagedDataSchemaVersion)
            throw std::invalid_argument("Managed data asset uses an unsupported schema version.");
        if (!definition.ManagedType || !HasVisibleText(definition.ManagedTypeName))
            throw std::invalid_argument("Managed data asset requires a stable managed type ID and diagnostic name.");
        if (definition.Fields.size() > MaximumFieldCount || definition.Dependencies.size() > MaximumDependencyCount)
        {
            throw std::invalid_argument("Managed data asset exceeds the field or dependency-count limit.");
        }
        if (!std::ranges::is_sorted(definition.Fields, FieldLess) ||
            !std::ranges::is_sorted(definition.Dependencies, DependencyLess))
        {
            throw std::invalid_argument("Managed data fields and dependencies must use deterministic stable-ID order.");
        }

        std::set<AssetId> stableIds;
        std::set<std::string, std::less<>> names;
        for (const auto& field : definition.Fields)
        {
            if (!field.StableFieldId || !stableIds.emplace(field.StableFieldId).second)
                throw std::invalid_argument("Managed data fields require unique non-empty stable field IDs.");
            if (!HasVisibleText(field.Name) || !HasVisibleText(field.ManagedTypeName) ||
                !names.emplace(field.Name).second)
            {
                throw std::invalid_argument("Managed data field names and managed types must be non-empty and unique.");
            }
            if (field.FormerNames.size() > MaximumAliasesPerField || !std::ranges::is_sorted(field.FormerNames))
            {
                throw std::invalid_argument("Managed data former field names must use deterministic lexical order.");
            }
            for (const auto& formerName : field.FormerNames)
            {
                if (!HasVisibleText(formerName) || !names.emplace(formerName).second)
                {
                    throw std::invalid_argument(
                        "Managed data current and former field names must be non-empty and unambiguous.");
                }
            }
            if (std::adjacent_find(field.FormerNames.begin(), field.FormerNames.end()) != field.FormerNames.end())
                throw std::invalid_argument("Managed data former field names must be unique.");
            (void)ParseFieldValue(field.Value);
        }

        std::set<AssetId> assets;
        for (const auto& dependency : definition.Dependencies)
        {
            if (!dependency.Asset || !dependency.AssetType || !assets.emplace(dependency.Asset).second)
                throw std::invalid_argument("Managed data dependencies require unique asset IDs and native types.");
            if (dependency.ManagedType && !static_cast<bool>(*dependency.ManagedType))
                throw std::invalid_argument("Managed data dependencies cannot use an empty managed type ID.");
            if (dependency.ManagedType && dependency.AssetType != StaticType())
            {
                throw std::invalid_argument(
                    "Managed data dependency type constraints may only target ManagedDataAsset values.");
            }
        }
    }

    Ref<ManagedDataAsset> ManagedDataAsset::Decode(const std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() > MaximumDocumentBytes)
            throw std::invalid_argument("Managed data document is empty or exceeds the 16 MiB safety limit.");

        try
        {
            const auto* first = reinterpret_cast<const char*>(bytes.data());
            const auto document = Json::parse(first, first + bytes.size());
            if (!document.is_object())
                throw std::invalid_argument("Managed data document root must be an object.");

            ManagedDataDefinition definition;
            definition.SchemaVersion = document.at("schemaVersion").get<std::uint32_t>();
            definition.ManagedType = ManagedTypeId::Parse(document.at("managedTypeId").get<std::string>());
            definition.ManagedTypeName = document.at("managedTypeName").get<std::string>();
            const auto& fields = document.at("fields");
            const auto& dependencies = document.at("dependencies");
            if (!fields.is_array() || !dependencies.is_array())
                throw std::invalid_argument("Managed data fields and dependencies must be arrays.");
            for (const auto& encoded : fields)
            {
                if (!encoded.is_object())
                    throw std::invalid_argument("Managed data field entries must be objects.");
                ManagedDataFieldState field;
                field.StableFieldId = AssetId::Parse(encoded.at("stableId").get<std::string>());
                field.Name = encoded.at("name").get<std::string>();
                field.ManagedTypeName = encoded.at("managedTypeName").get<std::string>();
                field.FormerNames = encoded.value("formerNames", std::vector<std::string>{});
                field.Value = encoded.at("value").dump();
                definition.Fields.push_back(std::move(field));
            }
            for (const auto& encoded : dependencies)
            {
                if (!encoded.is_object())
                    throw std::invalid_argument("Managed data dependency entries must be objects.");
                ManagedDataAssetDependency dependency;
                dependency.Asset = AssetId::Parse(encoded.at("assetId").get<std::string>());
                dependency.AssetType = AssetTypeId::Parse(encoded.at("assetTypeId").get<std::string>());
                if (const auto found = encoded.find("managedTypeId"); found != encoded.end())
                    dependency.ManagedType = ManagedTypeId::Parse(found->get<std::string>());
                definition.Dependencies.push_back(std::move(dependency));
            }

            Validate(definition);
            return CreateRef<ManagedDataAsset>(std::move(definition));
        }
        catch (const Json::exception& exception)
        {
            throw std::invalid_argument(std::string("Managed data document is malformed: ") + exception.what());
        }
    }

    std::vector<std::byte> ManagedDataAsset::Encode(const ManagedDataDefinition& source)
    {
        const auto definition = Canonicalize(source);
        ManagedDataAsset::Validate(definition);

        Json fields = Json::array();
        for (const auto& field : definition.Fields)
        {
            fields.push_back({{"stableId", field.StableFieldId.ToString()},
                              {"name", field.Name},
                              {"managedTypeName", field.ManagedTypeName},
                              {"formerNames", field.FormerNames},
                              {"value", ParseFieldValue(field.Value)}});
        }

        Json dependencies = Json::array();
        for (const auto& dependency : definition.Dependencies)
        {
            Json encoded{{"assetId", dependency.Asset.ToString()}, {"assetTypeId", dependency.AssetType.ToString()}};
            if (dependency.ManagedType)
                encoded["managedTypeId"] = dependency.ManagedType->ToString();
            dependencies.push_back(std::move(encoded));
        }

        const Json document{{"schemaVersion", definition.SchemaVersion},
                            {"managedTypeId", definition.ManagedType.ToString()},
                            {"managedTypeName", definition.ManagedTypeName},
                            {"fields", std::move(fields)},
                            {"dependencies", std::move(dependencies)}};
        const auto text = document.dump(2) + '\n';
        std::vector<std::byte> bytes(text.size());
        std::memcpy(bytes.data(), text.data(), text.size());
        return bytes;
    }

    ManagedDataReloadResult ManagedDataAsset::TryReload(const std::span<const std::byte> bytes)
    {
        try
        {
            auto candidate = Decode(bytes);
            if (m_Definition.ManagedType && candidate->m_Definition.ManagedType != m_Definition.ManagedType)
                throw std::invalid_argument("Managed data reload cannot change the authoritative stable type ID.");
            static_assert(std::is_nothrow_swappable_v<ManagedDataDefinition>);
            std::swap(m_Definition, candidate->m_Definition);
            m_ResidentBytes = candidate->m_ResidentBytes;
            ++m_Revision;
            return {.Applied = true, .Revision = m_Revision};
        }
        catch (const std::exception& exception)
        {
            return {.Applied = false, .Revision = m_Revision, .Diagnostic = exception.what()};
        }
        catch (...)
        {
            return {.Applied = false,
                    .Revision = m_Revision,
                    .Diagnostic = "Managed data reload failed with an unknown error."};
        }
    }

    void ValidateManagedAssetTypeDescriptor(const ManagedAssetTypeDescriptor& descriptor)
    {
        if (!descriptor.StableTypeId || !HasVisibleText(descriptor.FullName) || !HasVisibleText(descriptor.DisplayName))
        {
            throw std::invalid_argument(
                "Managed asset type descriptors require a stable type ID and diagnostic names.");
        }
        if (descriptor.BaseTypeId && !static_cast<bool>(*descriptor.BaseTypeId))
            throw std::invalid_argument("Managed asset type descriptors cannot use an empty base type ID.");
        if (descriptor.BaseTypeId && *descriptor.BaseTypeId == descriptor.StableTypeId)
            throw std::invalid_argument("Managed asset type descriptors cannot inherit from themselves.");
        if (!IsValidMenuPath(descriptor.MenuPath))
            throw std::invalid_argument("Managed asset Create menu path is invalid.");
        if ((!descriptor.DefaultFileName.empty() && !HasVisibleText(descriptor.DefaultFileName)) ||
            descriptor.DefaultFileName.find('/') != std::string::npos ||
            descriptor.DefaultFileName.find('\\') != std::string::npos)
        {
            throw std::invalid_argument("Managed asset default file names must be plain file names.");
        }

        std::set<std::string, std::less<>> propertyNames;
        std::set<AssetId> propertyStableIds;
        std::size_t count = 0;
        for (const auto& property : descriptor.Properties)
        {
            if (!propertyNames.emplace(property.Name).second ||
                !propertyStableIds.emplace(property.StableFieldId).second)
            {
                throw std::invalid_argument("Managed asset type properties require unique sibling names and IDs.");
            }
            ValidateProperty(property, 0, count);
        }
    }

    std::string EncodeManagedAssetTypeCatalog(const std::span<const ManagedAssetTypeDescriptor> descriptors)
    {
        ValidateTypeCatalog(descriptors);
        std::vector<const ManagedAssetTypeDescriptor*> ordered;
        ordered.reserve(descriptors.size());
        for (const auto& descriptor : descriptors)
            ordered.push_back(std::addressof(descriptor));
        std::ranges::sort(ordered, {}, [](const ManagedAssetTypeDescriptor* value) -> const std::string&
                          { return value->FullName; });

        Json types = Json::array();
        for (const auto* descriptor : ordered)
        {
            Json properties = Json::array();
            for (const auto& property : descriptor->Properties)
                properties.push_back(EncodePropertyDescriptor(property));
            Json encoded{{"stableTypeId", descriptor->StableTypeId.ToString()},
                         {"fullName", descriptor->FullName},
                         {"displayName", descriptor->DisplayName},
                         {"menuPath", descriptor->MenuPath},
                         {"defaultFileName", descriptor->DefaultFileName},
                         {"properties", std::move(properties)}};
            if (descriptor->BaseTypeId)
                encoded["baseTypeId"] = descriptor->BaseTypeId->ToString();
            types.push_back(std::move(encoded));
        }
        return Json{{"schemaVersion", 1}, {"types", std::move(types)}}.dump();
    }

    std::vector<ManagedAssetTypeDescriptor> DecodeManagedAssetTypeCatalog(const std::string_view catalog)
    {
        if (catalog.empty() || catalog.size() > MaximumTypeCatalogBytes)
            throw std::invalid_argument("Managed type catalog is empty or exceeds the 16 MiB safety limit.");
        try
        {
            const auto document = Json::parse(catalog.begin(), catalog.end());
            if (!document.is_object() || document.at("schemaVersion").get<std::uint32_t>() != 1)
                throw std::invalid_argument("Managed type catalog uses an unsupported schema version.");
            const auto& types = document.at("types");
            if (!types.is_array())
                throw std::invalid_argument("Managed type catalog types must be an array.");
            std::vector<ManagedAssetTypeDescriptor> result;
            result.reserve(types.size());
            for (const auto& source : types)
            {
                if (!source.is_object())
                    throw std::invalid_argument("Managed type catalog entries must be objects.");
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
                    throw std::invalid_argument("Managed type catalog properties must be an array.");
                descriptor.Properties.reserve(properties.size());
                for (const auto& property : properties)
                    descriptor.Properties.push_back(DecodePropertyDescriptor(property));
                result.push_back(std::move(descriptor));
            }
            std::ranges::sort(result, {}, &ManagedAssetTypeDescriptor::FullName);
            ValidateTypeCatalog(result);
            return result;
        }
        catch (const Json::exception& exception)
        {
            throw std::invalid_argument(std::string("Managed type catalog is malformed: ") + exception.what());
        }
    }

    void ValidateManagedDataForCook(const AssetId asset, const ManagedDataDefinition& definition,
                                    const std::span<const ManagedAssetTypeDescriptor> descriptors,
                                    const std::span<const ManagedDataCookAsset> assets)
    {
        if (!asset)
            throw std::invalid_argument("Managed data cook validation requires the source asset identity.");
        ManagedDataAsset::Validate(definition);
        ValidateTypeCatalog(descriptors);
        const auto descriptor =
            std::ranges::find(descriptors, definition.ManagedType, &ManagedAssetTypeDescriptor::StableTypeId);
        if (descriptor == descriptors.end())
        {
            throw std::runtime_error("Strict cooking cannot resolve managed data type '" + definition.ManagedTypeName +
                                     "' (" + definition.ManagedType.ToString() + ") for asset " + asset.ToString() +
                                     ".");
        }
        if (definition.ManagedTypeName != descriptor->FullName)
        {
            throw std::runtime_error("Strict cooking rejected managed data asset " + asset.ToString() +
                                     " because its diagnostic type name does not match the discovered type.");
        }

        std::map<AssetId, ManagedDataAssetDependency> expectedDependencies;
        std::map<AssetId, bool> includeDerivedByAsset;
        for (const auto& field : definition.Fields)
        {
            const auto property = std::ranges::find(descriptor->Properties, field.StableFieldId,
                                                    &ManagedAssetPropertyDescriptor::StableFieldId);
            if (property == descriptor->Properties.end())
            {
                throw std::runtime_error("Strict cooking rejected unknown stable field '" +
                                         field.StableFieldId.ToString() + "' in managed data asset " +
                                         asset.ToString() + ".");
            }
            if (field.ManagedTypeName != property->ManagedTypeName)
            {
                throw std::runtime_error("Strict cooking rejected managed field '" + field.Name +
                                         "' because its type is incompatible with the discovered runtime type.");
            }
            try
            {
                const auto value = Json::parse(field.Value);
                ValidateValueForProperty(value, *property, expectedDependencies, includeDerivedByAsset, 0);
            }
            catch (const Json::exception& exception)
            {
                throw std::runtime_error("Strict cooking rejected malformed value for managed field '" + field.Name +
                                         "': " + exception.what());
            }
            catch (const std::invalid_argument& exception)
            {
                throw std::runtime_error("Strict cooking rejected value for managed field '" + field.Name +
                                         "': " + exception.what());
            }
        }

        if (expectedDependencies.size() != definition.Dependencies.size())
        {
            throw std::runtime_error("Strict cooking rejected managed data asset " + asset.ToString() +
                                     " because its typed dependency table is stale or incomplete.");
        }
        for (const auto& dependency : definition.Dependencies)
        {
            const auto expected = expectedDependencies.find(dependency.Asset);
            if (expected == expectedDependencies.end() || !(expected->second == dependency))
            {
                throw std::runtime_error("Strict cooking rejected managed data asset " + asset.ToString() +
                                         " because a typed dependency constraint does not match its field.");
            }
            const auto target = std::ranges::find(assets, dependency.Asset, &ManagedDataCookAsset::Asset);
            if (target == assets.end() || target->AssetType != dependency.AssetType)
            {
                throw std::runtime_error("Strict cooking rejected managed data dependency " +
                                         dependency.Asset.ToString() + " because it is missing or has the wrong type.");
            }
            if (!dependency.ManagedType)
                continue;
            if (!target->ManagedType)
            {
                throw std::runtime_error("Strict cooking rejected managed data dependency " +
                                         dependency.Asset.ToString() + " because its managed type is unavailable.");
            }
            const auto derivedConstraint = includeDerivedByAsset.find(dependency.Asset);
            const bool includeDerived = derivedConstraint == includeDerivedByAsset.end() || derivedConstraint->second;
            if (!IsManagedTypeCompatible(*target->ManagedType, *dependency.ManagedType, includeDerived, descriptors))
            {
                throw std::runtime_error("Strict cooking rejected managed data dependency " +
                                         dependency.Asset.ToString() +
                                         " because its managed type violates the typed reference constraint.");
            }
        }
    }

    AssetImporterRegistration CreateManagedDataAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.ManagedData";
        result.Version = ManagedDataSchemaVersion;
        result.Type = ManagedDataAsset::StaticType();
        result.Extensions = {".keiredata"};
        result.Import = [](const std::span<const std::byte> bytes)
        {
            const auto asset = ManagedDataAsset::Decode(bytes);
            return ManagedDataAsset::Encode(asset->Definition());
        };
        result.ContextualImport = [](const AssetImportContext&, const std::span<const std::byte> bytes)
        {
            const auto asset = ManagedDataAsset::Decode(bytes);
            AssetImportOutput output;
            output.Bytes = ManagedDataAsset::Encode(asset->Definition());
            output.AssetDependencies = asset->AssetDependencies();
            return output;
        };
        return result;
    }

    AssetDecoderRegistration CreateManagedDataAssetDecoder()
    {
        return {ManagedDataAsset::StaticType(), CreateRef<ManagedDataAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return ManagedDataAsset::Decode(bytes); },
                [](Ref<Asset> current, Ref<Asset> replacement) -> Ref<Asset>
                {
                    auto active = DynamicRefCast<ManagedDataAsset>(current);
                    const auto candidate = DynamicRefCast<const ManagedDataAsset>(replacement);
                    if (!active || !candidate)
                        throw std::invalid_argument("Managed data reload received a mismatched asset instance.");
                    const auto result = active->TryReload(ManagedDataAsset::Encode(candidate->Definition()));
                    if (!result.Applied)
                        throw std::runtime_error(result.Diagnostic);
                    return active;
                }};
    }
} // namespace Keire
