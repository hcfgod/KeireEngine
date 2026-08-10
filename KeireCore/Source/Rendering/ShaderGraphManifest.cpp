#include "KeireInternal/Rendering/ShaderGraphManifest.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace Keire::Detail
{
    namespace
    {
        using Json = nlohmann::json;

        [[nodiscard]] std::string KeywordDefine(const std::string_view name) { return "KEIRE_MG_" + std::string(name); }

        [[nodiscard]] std::string PropertyTypeName(const ShaderGraphValueType type)
        {
            switch (type)
            {
            case ShaderGraphValueType::Scalar:
                return "Float";
            case ShaderGraphValueType::Vector2:
                return "Vector2";
            case ShaderGraphValueType::Vector3:
                return "Vector3";
            case ShaderGraphValueType::Vector4:
                return "Vector4";
            case ShaderGraphValueType::Color:
                return "Color";
            case ShaderGraphValueType::Texture2D:
                return "Texture2D";
            case ShaderGraphValueType::MaterialAttributes:
                return "MaterialAttributes";
            case ShaderGraphValueType::Bsdf:
                return "BSDF";
            }
            return "Float";
        }

        [[nodiscard]] std::string SemanticName(const ShaderTextureSemantic semantic)
        {
            constexpr std::array names{"Generic",   "BaseColor", "Normal",   "MetallicRoughness",
                                       "Occlusion", "Emissive",  "Metallic", "Roughness"};
            const auto index = static_cast<std::size_t>(semantic);
            return index < names.size() ? names[index] : names.front();
        }

        [[nodiscard]] Json ManifestProperty(const ShaderPropertyDefinition& property)
        {
            Json result{
                {"name", property.Name}, {"displayName", property.DisplayName}, {"category", property.Category}};
            const auto graphType = static_cast<ShaderGraphValueType>(property.Type);
            result["type"] = PropertyTypeName(graphType);
            if (property.Type == ShaderPropertyType::Texture2D)
            {
                result["semantic"] = SemanticName(property.TextureSemantic);
                result["default"] = property.DefaultTexture ? Json(property.DefaultTexture.ToString()) : Json(nullptr);
            }
            else
                result["default"] = Json::array({property.DefaultValue.X, property.DefaultValue.Y,
                                                 property.DefaultValue.Z, property.DefaultValue.W});
            if (property.Minimum)
                result["minimum"] = *property.Minimum;
            if (property.Maximum)
                result["maximum"] = *property.Maximum;
            if (property.Step)
                result["step"] = *property.Step;
            return result;
        }
    } // namespace

    std::string BuildShaderGraphManifest(const ShaderGraphDefinition& definition,
                                         const std::filesystem::path& generatedSource,
                                         const std::span<const ShaderPropertyDefinition> properties,
                                         const std::span<const std::string> keywords,
                                         const bool usesVertexMaterialParameters)
    {
        Json encodedProperties = Json::array();
        for (const auto& property : properties)
            encodedProperties.push_back(ManifestProperty(property));
        Json defines = Json::object();
        for (const auto& keyword : keywords)
            defines[KeywordDefine(keyword)] = "1";
        Json roots = Json::array();
        for (const auto& root : definition.IncludeRoots)
            roots.push_back(root.generic_string());
        const bool transparent =
            definition.Output == ShaderGraphOutput::Transparent || definition.Output == ShaderGraphOutput::Decal;
        const bool fullscreen = definition.Output == ShaderGraphOutput::Fullscreen;
        const bool lit = definition.Output != ShaderGraphOutput::Unlit && !fullscreen;
        const Json manifest{{"schemaVersion", 1},
                            {"materialGraphSourceSchemaVersion", definition.SchemaVersion},
                            {"materialGraphGeneratedShaderVersion", ShaderGraphGeneratedShaderVersion},
                            {"source", generatedSource.generic_string()},
                            {"vertexLayoutVersion", ShaderGraphVertexLayoutVersion},
                            {"receivesShadows", lit},
                            {"usesForwardPlus", lit},
                            {"usesInstancing", true},
                            {"usesImageBasedLighting", lit},
                            {"usesVertexMaterialParameters", usesVertexMaterialParameters},
                            {"stages", {{"vertex", "VSMain"}, {"fragment", "PSMain"}}},
                            {"defines", std::move(defines)},
                            {"includeRoots", std::move(roots)},
                            {"renderState",
                             {{"topology", "TriangleList"},
                              {"culling", fullscreen                                      ? "None"
                                          : definition.Output == ShaderGraphOutput::Decal ? "Front"
                                          : definition.Output == ShaderGraphOutput::Hair  ? "None"
                                                                                          : "Back"},
                              {"depthTest", !fullscreen},
                              {"depthWrite", !transparent && !fullscreen},
                              {"blend", transparent}}},
                            {"properties", std::move(encodedProperties)}};
        return manifest.dump(2) + '\n';
    }
} // namespace Keire::Detail
