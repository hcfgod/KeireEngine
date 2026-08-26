#include "Keire/Scripting/ManagedDataAsset.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t MaximumCollectionElements = 16ULL * 1024U;
        constexpr std::size_t MaximumStringBytes = 1ULL * 1024U * 1024U;
        constexpr std::size_t MaximumDepth = 32;

        [[nodiscard]] bool IsContainer(const ManagedAssetPropertyKind kind) noexcept
        {
            return kind == ManagedAssetPropertyKind::SerializableObject || kind == ManagedAssetPropertyKind::Array ||
                   kind == ManagedAssetPropertyKind::List || kind == ManagedAssetPropertyKind::Dictionary;
        }

        void ValidateString(const std::string_view value, const ManagedAssetPropertyDescriptor& property,
                            const std::string& path)
        {
            if (value.size() > MaximumStringBytes)
            {
                throw std::invalid_argument("KEIRE-MANAGED-SERIALIZATION-0003: Managed string field '" + path +
                                            "' declared as '" + property.ManagedTypeName +
                                            "' exceeds the 1,048,576 UTF-8 byte limit.");
            }
        }

        [[nodiscard]] const Json* Member(const Json& value, const std::string_view primary,
                                         const std::string_view fallback)
        {
            if (const auto found = value.find(std::string(primary)); found != value.end())
                return std::addressof(*found);
            if (const auto found = value.find(std::string(fallback)); found != value.end())
                return std::addressof(*found);
            return nullptr;
        }

        [[nodiscard]] double Number(const Json& value, const std::string_view primary, const std::string_view fallback)
        {
            const auto* member = Member(value, primary, fallback);
            if (!member || !member->is_number())
                throw std::invalid_argument("Managed vector or color data is missing a numeric member.");
            const auto result = member->get<double>();
            if (!std::isfinite(result))
                throw std::invalid_argument("Managed vector or color data contains a non-finite number.");
            return result;
        }

        [[nodiscard]] std::uint64_t Unsigned(const Json& value)
        {
            if (value.is_number_unsigned())
                return value.get<std::uint64_t>();
            if (value.is_number_integer())
            {
                const auto signedValue = value.get<std::int64_t>();
                if (signedValue >= 0)
                    return static_cast<std::uint64_t>(signedValue);
            }
            throw std::invalid_argument("Managed unsigned data is not a non-negative integer.");
        }

        [[nodiscard]] AssetId ReferenceId(const Json& value)
        {
            if (value.is_null())
                return {};
            const auto* nested = Member(value, "Id", "id");
            const auto& id = nested && nested->is_object() ? *nested : value;
            if (!id.is_object())
                throw std::invalid_argument("Managed asset-reference data is not an object.");
            const auto* high = Member(id, "High", "high");
            const auto* low = Member(id, "Low", "low");
            if (!high || !low)
                throw std::invalid_argument("Managed asset-reference data is missing its ID.");
            return AssetId(Unsigned(*high), Unsigned(*low));
        }

        [[nodiscard]] ManagedAssetValueNode DefaultNode(const ManagedAssetPropertyDescriptor& property)
        {
            ManagedAssetValueNode result;
            result.StableFieldId = property.StableFieldId;
            result.Kind = property.Kind;
            if (property.Kind == ManagedAssetPropertyKind::SerializableObject)
            {
                result.Children.reserve(property.Children.size());
                for (const auto& child : property.Children)
                    result.Children.push_back(DefaultNode(child));
            }
            return result;
        }

        [[nodiscard]] ManagedAssetValueNode DecodeNode(const Json& source,
                                                       const ManagedAssetPropertyDescriptor& property,
                                                       const std::string& path, const std::size_t parentDepth)
        {
            const auto depth = parentDepth + (IsContainer(property.Kind) ? 1U : 0U);
            if (depth > MaximumDepth)
                throw std::invalid_argument("Managed data field '" + path + "' exceeds 32 nested levels.");
            auto result = DefaultNode(property);
            switch (property.Kind)
            {
            case ManagedAssetPropertyKind::Boolean:
                result.Value = source.get<bool>();
                break;
            case ManagedAssetPropertyKind::Integer:
                if (property.ManagedTypeName == "System.Char")
                {
                    const auto character = source.get<std::string>();
                    if (character.size() != 1)
                        throw std::invalid_argument("Managed character data is not a one-character string.");
                    result.Value = character;
                    break;
                }
                result.Value = source.get<std::int64_t>();
                break;
            case ManagedAssetPropertyKind::Enum:
                result.Value = source.get<std::int64_t>();
                break;
            case ManagedAssetPropertyKind::UnsignedInteger:
                result.Value = Unsigned(source);
                break;
            case ManagedAssetPropertyKind::Scalar:
            {
                const auto value = source.get<double>();
                if (!std::isfinite(value))
                    throw std::invalid_argument("Managed scalar data is non-finite.");
                result.Value = value;
                break;
            }
            case ManagedAssetPropertyKind::Text:
                if (!source.is_null())
                {
                    auto text = source.get<std::string>();
                    ValidateString(text, property, path);
                    result.Value = std::move(text);
                }
                break;
            case ManagedAssetPropertyKind::Vector2:
                result.Value =
                    Vector2{static_cast<float>(Number(source, "X", "x")), static_cast<float>(Number(source, "Y", "y"))};
                break;
            case ManagedAssetPropertyKind::Vector3:
                result.Value =
                    Vector3{static_cast<float>(Number(source, "X", "x")), static_cast<float>(Number(source, "Y", "y")),
                            static_cast<float>(Number(source, "Z", "z"))};
                break;
            case ManagedAssetPropertyKind::Vector4:
                result.Value =
                    Vector4{static_cast<float>(Number(source, "X", "x")), static_cast<float>(Number(source, "Y", "y")),
                            static_cast<float>(Number(source, "Z", "z")), static_cast<float>(Number(source, "W", "w"))};
                break;
            case ManagedAssetPropertyKind::Quaternion:
                result.Value = Quaternion{
                    static_cast<float>(Number(source, "X", "x")), static_cast<float>(Number(source, "Y", "y")),
                    static_cast<float>(Number(source, "Z", "z")), static_cast<float>(Number(source, "W", "w"))};
                break;
            case ManagedAssetPropertyKind::Color:
                result.Value = Color{static_cast<float>(Number(source, "Red", "red")),
                                     static_cast<float>(Number(source, "Green", "green")),
                                     static_cast<float>(Number(source, "Blue", "blue")),
                                     static_cast<float>(Number(source, "Alpha", "alpha"))};
                break;
            case ManagedAssetPropertyKind::AssetReference:
                result.Value = ReferenceId(source);
                break;
            case ManagedAssetPropertyKind::SerializableObject:
            {
                if (source.is_null())
                    break;
                if (!source.is_object())
                    throw std::invalid_argument("Managed serializable-object data is not an object.");
                result.Value = true;
                for (std::size_t index = 0; index < property.Children.size(); ++index)
                {
                    const auto found = source.find(property.Children[index].Name);
                    if (found != source.end())
                        result.Children[index] = DecodeNode(*found, property.Children[index],
                                                            path + "." + property.Children[index].Name, depth);
                }
                break;
            }
            case ManagedAssetPropertyKind::Array:
            case ManagedAssetPropertyKind::List:
            {
                if (source.is_null())
                    break;
                if (!source.is_array())
                    throw std::invalid_argument("Managed collection data is not an array.");
                if (source.size() > MaximumCollectionElements)
                    throw std::invalid_argument("Managed collection data exceeds the editor element limit.");
                if (property.Children.size() != 1)
                    throw std::invalid_argument("Managed collection descriptor requires one element property.");
                result.Value = true;
                result.Children.reserve(source.size());
                for (std::size_t index = 0; index < source.size(); ++index)
                    result.Children.push_back(DecodeNode(source[index], property.Children.front(),
                                                         path + "[" + std::to_string(index) + "]", depth));
                break;
            }
            case ManagedAssetPropertyKind::Dictionary:
            {
                if (source.is_null())
                    break;
                if (!source.is_array())
                    throw std::invalid_argument("Managed dictionary data is not an array.");
                if (source.size() > MaximumCollectionElements)
                    throw std::invalid_argument("Managed dictionary data exceeds the editor element limit.");
                if (property.Children.size() != 2)
                    throw std::invalid_argument("Managed dictionary descriptor requires Key and Value properties.");
                result.Value = true;
                result.Children.reserve(source.size());
                std::size_t index = 0;
                for (const auto& encoded : source)
                {
                    if (!encoded.is_object() || encoded.size() != 2 || !encoded.contains("key") ||
                        !encoded.contains("value"))
                    {
                        throw std::invalid_argument(
                            "Managed dictionary entries require exactly key and value members.");
                    }
                    ManagedAssetValueNode entry;
                    entry.StableFieldId = property.StableFieldId;
                    entry.Kind = ManagedAssetPropertyKind::Dictionary;
                    entry.Value = true;
                    const auto entryPath = path + "[" + std::to_string(index) + "]";
                    entry.Children.push_back(
                        DecodeNode(encoded.at("key"), property.Children[0], entryPath + ".Key", depth));
                    entry.Children.push_back(DecodeNode(encoded.at("value"), property.Children[1], entryPath, depth));
                    result.Children.push_back(std::move(entry));
                    ++index;
                }
                break;
            }
            }
            return result;
        }

        [[nodiscard]] bool IsPresent(const ManagedAssetValueNode& value) noexcept
        {
            return !std::holds_alternative<std::monostate>(value.Value);
        }

        [[nodiscard]] Json EncodeNode(const ManagedAssetValueNode& value,
                                      const ManagedAssetPropertyDescriptor& property, const std::string& path,
                                      const std::size_t parentDepth)
        {
            const auto depth = parentDepth + (IsContainer(property.Kind) ? 1U : 0U);
            if (depth > MaximumDepth)
                throw std::invalid_argument("Managed data field '" + path + "' exceeds 32 nested levels.");
            if (value.StableFieldId != property.StableFieldId || value.Kind != property.Kind)
                throw std::invalid_argument("Managed data value does not match its property descriptor.");
            switch (property.Kind)
            {
            case ManagedAssetPropertyKind::Boolean:
                return std::get<bool>(value.Value);
            case ManagedAssetPropertyKind::Integer:
                if (property.ManagedTypeName == "System.Char")
                {
                    const auto& character = std::get<std::string>(value.Value);
                    if (character.size() != 1)
                        throw std::invalid_argument("Managed character data is not a one-character string.");
                    return character;
                }
                return std::get<std::int64_t>(value.Value);
            case ManagedAssetPropertyKind::Enum:
                return std::get<std::int64_t>(value.Value);
            case ManagedAssetPropertyKind::UnsignedInteger:
                return std::get<std::uint64_t>(value.Value);
            case ManagedAssetPropertyKind::Scalar:
            {
                const auto result = std::get<double>(value.Value);
                if (!std::isfinite(result))
                    throw std::invalid_argument("Managed scalar data is non-finite.");
                return result;
            }
            case ManagedAssetPropertyKind::Text:
                if (!IsPresent(value))
                    return nullptr;
                ValidateString(std::get<std::string>(value.Value), property, path);
                return std::get<std::string>(value.Value);
            case ManagedAssetPropertyKind::Vector2:
            {
                const auto vector = std::get<Vector2>(value.Value);
                return {{"X", vector.X}, {"Y", vector.Y}};
            }
            case ManagedAssetPropertyKind::Vector3:
            {
                const auto vector = std::get<Vector3>(value.Value);
                return {{"X", vector.X}, {"Y", vector.Y}, {"Z", vector.Z}};
            }
            case ManagedAssetPropertyKind::Vector4:
            {
                const auto vector = std::get<Vector4>(value.Value);
                return {{"X", vector.X}, {"Y", vector.Y}, {"Z", vector.Z}, {"W", vector.W}};
            }
            case ManagedAssetPropertyKind::Quaternion:
            {
                const auto rotation = std::get<Quaternion>(value.Value);
                return {{"X", rotation.X}, {"Y", rotation.Y}, {"Z", rotation.Z}, {"W", rotation.W}};
            }
            case ManagedAssetPropertyKind::Color:
            {
                const auto color = std::get<Color>(value.Value);
                return {{"Red", color.Red}, {"Green", color.Green}, {"Blue", color.Blue}, {"Alpha", color.Alpha}};
            }
            case ManagedAssetPropertyKind::AssetReference:
            {
                const auto asset = std::get<AssetId>(value.Value);
                return {{"Id", {{"High", asset.High()}, {"Low", asset.Low()}}}};
            }
            case ManagedAssetPropertyKind::SerializableObject:
            {
                if (!IsPresent(value))
                    return nullptr;
                Json result = Json::object();
                for (const auto& childProperty : property.Children)
                {
                    const auto child = std::ranges::find(value.Children, childProperty.StableFieldId,
                                                         &ManagedAssetValueNode::StableFieldId);
                    if (child != value.Children.end() && IsPresent(*child))
                        result[childProperty.Name] =
                            EncodeNode(*child, childProperty, path + "." + childProperty.Name, depth);
                }
                return result;
            }
            case ManagedAssetPropertyKind::Array:
            case ManagedAssetPropertyKind::List:
            {
                if (!IsPresent(value))
                    return nullptr;
                if (value.Children.size() > MaximumCollectionElements)
                    throw std::invalid_argument("Managed collection data exceeds the editor element limit.");
                if (property.Children.size() != 1)
                    throw std::invalid_argument("Managed collection descriptor requires one element property.");
                Json result = Json::array();
                for (std::size_t index = 0; index < value.Children.size(); ++index)
                    result.push_back(EncodeNode(value.Children[index], property.Children.front(),
                                                path + "[" + std::to_string(index) + "]", depth));
                return result;
            }
            case ManagedAssetPropertyKind::Dictionary:
            {
                if (!IsPresent(value))
                    return nullptr;
                if (value.Children.size() > MaximumCollectionElements)
                    throw std::invalid_argument("Managed dictionary data exceeds the editor element limit.");
                if (property.Children.size() != 2)
                    throw std::invalid_argument("Managed dictionary descriptor requires Key and Value properties.");
                std::set<std::string, std::less<>> keys;
                std::vector<std::pair<std::string, Json>> entries;
                entries.reserve(value.Children.size());
                for (std::size_t index = 0; index < value.Children.size(); ++index)
                {
                    const auto& entry = value.Children[index];
                    if (entry.Kind != ManagedAssetPropertyKind::Dictionary || entry.Children.size() != 2)
                        throw std::invalid_argument("Managed dictionary field '" + path +
                                                    "' contains malformed entry " + std::to_string(index) + ".");
                    const auto entryPath = path + "[" + std::to_string(index) + "]";
                    Json key = EncodeNode(entry.Children[0], property.Children[0], entryPath + ".Key", depth);
                    auto order = key.dump();
                    if (!keys.emplace(order).second)
                        throw std::invalid_argument("KEIRE-MANAGED-SERIALIZATION-0003: Managed dictionary field '" +
                                                    path + "' declared as '" + property.ManagedTypeName +
                                                    "' contains duplicate key " + order + ".");
                    entries.emplace_back(
                        std::move(order),
                        Json{{"key", std::move(key)},
                             {"value", EncodeNode(entry.Children[1], property.Children[1], entryPath, depth)}});
                }
                std::ranges::sort(entries, {}, [](const auto& entry) -> const std::string& { return entry.first; });
                Json result = Json::array();
                for (auto& [unused, encoded] : entries)
                {
                    (void)unused;
                    result.push_back(std::move(encoded));
                }
                return result;
            }
            }
            throw std::logic_error("Unsupported managed data property kind.");
        }
    } // namespace

    ManagedAssetValueNode DecodeManagedAssetValue(const std::string_view value,
                                                  const ManagedAssetPropertyDescriptor& property)
    {
        try
        {
            const auto path = property.Name.empty() ? std::string("Root") : property.Name;
            return DecodeNode(Json::parse(value.begin(), value.end()), property, path, 0);
        }
        catch (const Json::exception& error)
        {
            throw std::invalid_argument(std::string("Managed data value is malformed: ") + error.what());
        }
    }

    std::string EncodeManagedAssetValue(const ManagedAssetValueNode& value,
                                        const ManagedAssetPropertyDescriptor& property)
    {
        try
        {
            const auto path = property.Name.empty() ? std::string("Root") : property.Name;
            return EncodeNode(value, property, path, 0).dump();
        }
        catch (const Json::exception& error)
        {
            throw std::invalid_argument(std::string("Managed data value could not be encoded: ") + error.what());
        }
    }
} // namespace Keire
