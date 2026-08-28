#include "KeireInternal/Assets/ShaderAssetCodecInternal.h"

#include "KeireInternal/Assets/RenderingAssetValidation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace Keire::Detail
{
    namespace
    {
        using Json = nlohmann::json;

        template <typename Range> [[nodiscard]] std::vector<std::byte> ToBytes(const Range& values)
        {
            std::vector<std::byte> result(values.size());
            std::ranges::transform(values, result.begin(), [](const std::uint8_t value) { return std::byte(value); });
            return result;
        }

        [[nodiscard]] std::vector<std::uint8_t> ToUnsigned(const std::span<const std::byte> values)
        {
            std::vector<std::uint8_t> result(values.size());
            std::ranges::transform(values, result.begin(),
                                   [](const std::byte value) { return std::to_integer<std::uint8_t>(value); });
            return result;
        }

        [[nodiscard]] Json EncodeVector(const Vector4 value)
        {
            return Json::array({value.X, value.Y, value.Z, value.W});
        }

        [[nodiscard]] Vector4 DecodeVector(const Json& value)
        {
            if (!value.is_array() || value.size() != 4)
                throw std::invalid_argument("Shader vector values require four finite numbers.");
            Vector4 result{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
            if (!Math::IsFinite(result))
                throw std::invalid_argument("Shader vector values require four finite numbers.");
            return result;
        }

        [[nodiscard]] Json EncodeDefinition(const ShaderAssetDefinition& definition)
        {
            Json properties = Json::array();
            for (const auto& property : definition.Properties)
            {
                Json encoded{{"name", property.Name}, {"type", static_cast<std::uint8_t>(property.Type)}};
                if (property.Id)
                    encoded["id"] = property.Id.ToString();
                if (!property.DisplayName.empty())
                    encoded["displayName"] = property.DisplayName;
                if (!property.Category.empty())
                    encoded["category"] = property.Category;
                if (property.Minimum)
                    encoded["minimum"] = *property.Minimum;
                if (property.Maximum)
                    encoded["maximum"] = *property.Maximum;
                if (property.Step)
                    encoded["step"] = *property.Step;
                if (property.Type == ShaderPropertyType::Texture2D)
                {
                    encoded["defaultTexture"] =
                        property.DefaultTexture ? Json(property.DefaultTexture.ToString()) : Json(nullptr);
                    encoded["textureSemantic"] = static_cast<std::uint8_t>(property.TextureSemantic);
                }
                else
                    encoded["default"] = EncodeVector(property.DefaultValue);
                properties.push_back(std::move(encoded));
            }
            Json dependencies = Json::array();
            for (const auto& dependency : definition.Dependencies)
                dependencies.push_back(
                    {{"path", dependency.RelativePath.generic_string()}, {"digest", dependency.Digest}});
            Json variants = Json::array();
            for (const auto& variant : definition.Variants)
            {
                variants.push_back({{"format", static_cast<std::uint8_t>(variant.Format)},
                                    {"vertex", Json::binary(ToUnsigned(variant.Vertex))},
                                    {"fragment", Json::binary(ToUnsigned(variant.Fragment))}});
            }
            return {{"schemaVersion", definition.SchemaVersion},
                    {"source", definition.Source.generic_string()},
                    {"vertexEntry", definition.VertexEntry},
                    {"fragmentEntry", definition.FragmentEntry},
                    {"vertexLayoutVersion", definition.VertexLayoutVersion},
                    {"topology", static_cast<std::uint8_t>(definition.Topology)},
                    {"culling", static_cast<std::uint8_t>(definition.Culling)},
                    {"depthTest", definition.DepthTest},
                    {"depthWrite", definition.DepthWrite},
                    {"blend", definition.Blend},
                    {"receivesShadows", definition.ReceivesShadows},
                    {"usesForwardPlus", definition.UsesForwardPlus},
                    {"usesInstancing", definition.UsesInstancing},
                    {"usesImageBasedLighting", definition.UsesImageBasedLighting},
                    {"spatialLightingAbiVersion", definition.SpatialLightingAbiVersion},
                    {"usesVertexMaterialParameters", definition.UsesVertexMaterialParameters},
                    {"instanceAddressingAbiVersion", definition.InstanceAddressingAbiVersion},
                    {"occlusionSupport", static_cast<std::uint8_t>(definition.OcclusionSupport)},
                    {"maximumWorldPositionDisplacementRadius",
                     definition.MaximumWorldPositionDisplacementRadius
                         ? Json(*definition.MaximumWorldPositionDisplacementRadius)
                         : Json(nullptr)},
                    {"userResourceSlots", definition.UserResourceSlots},
                    {"userReadOnlyBuffers", definition.UserReadOnlyBuffers},
                    {"properties", std::move(properties)},
                    {"dependencies", std::move(dependencies)},
                    {"variants", std::move(variants)}};
        }

        [[nodiscard]] ShaderAssetDefinition DecodeDefinition(const Json& source)
        {
            if (!source.is_object())
                throw std::invalid_argument("Canonical shader data must be an object.");
            ShaderAssetDefinition result;
            const auto sourceSchemaVersion = source.at("schemaVersion").get<std::uint32_t>();
            if (sourceSchemaVersion == 0U || sourceSchemaVersion > 2U)
                throw std::invalid_argument("Canonical shader data has an unsupported schema.");
            result.SchemaVersion = 2;
            result.Source = source.at("source").get<std::string>();
            result.VertexEntry = source.at("vertexEntry").get<std::string>();
            result.FragmentEntry = source.at("fragmentEntry").get<std::string>();
            result.VertexLayoutVersion = source.value("vertexLayoutVersion", static_cast<std::uint8_t>(1));
            result.Topology = static_cast<ShaderPrimitiveTopology>(source.at("topology").get<std::uint8_t>());
            result.Culling = static_cast<ShaderCullMode>(source.at("culling").get<std::uint8_t>());
            result.DepthTest = source.at("depthTest").get<bool>();
            result.DepthWrite = source.at("depthWrite").get<bool>();
            result.Blend = source.at("blend").get<bool>();
            result.ReceivesShadows = source.value("receivesShadows", false);
            result.UsesForwardPlus = source.value("usesForwardPlus", false);
            result.UsesInstancing = source.value("usesInstancing", false);
            result.UsesImageBasedLighting = source.value("usesImageBasedLighting", false);
            result.SpatialLightingAbiVersion = source.value("spatialLightingAbiVersion", static_cast<std::uint8_t>(0));
            result.UsesVertexMaterialParameters = source.value("usesVertexMaterialParameters", false);
            result.InstanceAddressingAbiVersion =
                source.value("instanceAddressingAbiVersion", static_cast<std::uint8_t>(0));
            result.OcclusionSupport = static_cast<ShaderOcclusionSupport>(
                source.value("occlusionSupport", static_cast<std::uint8_t>(ShaderOcclusionSupport::None)));
            if (sourceSchemaVersion >= 2U && source.contains("maximumWorldPositionDisplacementRadius") &&
                !source.at("maximumWorldPositionDisplacementRadius").is_null())
            {
                result.MaximumWorldPositionDisplacementRadius =
                    source.at("maximumWorldPositionDisplacementRadius").get<float>();
            }
            else
            {
                result.MaximumWorldPositionDisplacementRadius.reset();
                result.OcclusionSupport = ShaderOcclusionSupport::None;
            }
            result.UserResourceSlots = source.value("userResourceSlots", static_cast<std::uint8_t>(0));
            result.UserReadOnlyBuffers = source.value("userReadOnlyBuffers", static_cast<std::uint8_t>(0));
            for (const auto& property : source.at("properties"))
            {
                ShaderPropertyDefinition decoded;
                if (property.contains("id"))
                    decoded.Id = AssetId::Parse(property.at("id").get<std::string>());
                decoded.Name = property.at("name").get<std::string>();
                decoded.Type = static_cast<ShaderPropertyType>(property.at("type").get<std::uint8_t>());
                decoded.DisplayName = property.value("displayName", std::string{});
                decoded.Category = property.value("category", std::string{});
                if (property.contains("minimum"))
                    decoded.Minimum = property.at("minimum").get<float>();
                if (property.contains("maximum"))
                    decoded.Maximum = property.at("maximum").get<float>();
                if (property.contains("step"))
                    decoded.Step = property.at("step").get<float>();
                if (decoded.Type == ShaderPropertyType::Texture2D)
                {
                    decoded.DefaultTexture = property.at("defaultTexture").is_null()
                                                 ? AssetId{}
                                                 : AssetId::Parse(property.at("defaultTexture").get<std::string>());
                    decoded.TextureSemantic = static_cast<ShaderTextureSemantic>(
                        property.value("textureSemantic", static_cast<std::uint8_t>(ShaderTextureSemantic::Generic)));
                }
                else
                    decoded.DefaultValue = DecodeVector(property.at("default"));
                result.Properties.push_back(std::move(decoded));
            }
            for (const auto& dependency : source.at("dependencies"))
                result.Dependencies.push_back(
                    {dependency.at("path").get<std::string>(), dependency.at("digest").get<std::string>()});
            for (const auto& variant : source.at("variants"))
            {
                const auto& vertex = variant.at("vertex").get_binary();
                const auto& fragment = variant.at("fragment").get_binary();
                result.Variants.push_back({static_cast<ShaderBinaryFormat>(variant.at("format").get<std::uint8_t>()),
                                           ToBytes(vertex), ToBytes(fragment)});
            }
            ValidateShaderDefinition(result, false);
            return result;
        }
    } // namespace

    ShaderAssetDefinition DecodeCanonicalShaderAsset(const std::span<const std::byte> bytes)
    {
        return DecodeDefinition(Json::from_cbor(ToUnsigned(bytes)));
    }

    std::vector<std::byte> EncodeCanonicalShaderAsset(const ShaderAssetDefinition& definition)
    {
        return ToBytes(Json::to_cbor(EncodeDefinition(definition)));
    }
} // namespace Keire::Detail
