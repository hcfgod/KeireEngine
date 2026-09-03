#include "KeireInternal/Rendering/ShaderGraphManifest.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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
            constexpr std::array names{"Generic",  "BaseColor", "Normal",    "MetallicRoughness", "Occlusion",
                                       "Emissive", "Metallic",  "Roughness", "Specular"};
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

        [[nodiscard]] std::string_view
        FullscreenInjectionPointName(const ShaderGraphFullscreenInjectionPoint injectionPoint) noexcept
        {
            switch (injectionPoint)
            {
            case ShaderGraphFullscreenInjectionPoint::BeforeTonemapping:
                return "BeforeTonemapping";
            case ShaderGraphFullscreenInjectionPoint::AfterTonemapping:
                return "AfterTonemapping";
            case ShaderGraphFullscreenInjectionPoint::AfterUi:
                return "AfterUi";
            }
            return "AfterTonemapping";
        }
    } // namespace

    std::string BuildShaderGraphManifest(const ShaderGraphDefinition& definition,
                                         const std::filesystem::path& generatedSource,
                                         const std::span<const ShaderPropertyDefinition> properties,
                                         const std::span<const std::string> keywords,
                                         const bool usesVertexMaterialParameters,
                                         const ShaderOcclusionSupport occlusionSupport,
                                         const std::optional<float> maximumWorldPositionDisplacementRadius)
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
        const auto resourceBytes = EncodeShaderGraphResources(definition.Resources);
        const auto resourceContract =
            Json::parse(reinterpret_cast<const char*>(resourceBytes.data()),
                        reinterpret_cast<const char*>(resourceBytes.data()) + resourceBytes.size());
        const bool transparent =
            definition.Output == ShaderGraphOutput::Transparent || definition.Output == ShaderGraphOutput::Decal;
        const bool fullscreen = definition.Target.Target == ShaderGraphTarget::Fullscreen;
        const bool lit = definition.Output != ShaderGraphOutput::Unlit && !fullscreen;
        Json passes = Json::array();
        const auto addPass = [&](const std::string_view role, const std::string_view define)
        {
            Json pass{{"role", role}};
            if (!define.empty())
                pass["define"] = define;
            passes.push_back(std::move(pass));
        };
        if (definition.Target.Target != ShaderGraphTarget::Material ||
            definition.Output == ShaderGraphOutput::Fullscreen)
        {
            addPass("primary", {});
        }
        else
        {
            switch (definition.Output)
            {
            case ShaderGraphOutput::Surface:
            case ShaderGraphOutput::Unlit:
                addPass("depthVelocity", "KEIRE_PASS_DEPTH_VELOCITY");
                addPass("deferredGBufferStandard", "KEIRE_PASS_DEFERRED_GBUFFER_STANDARD");
                addPass("forwardOpaque", "KEIRE_PASS_FORWARD_OPAQUE");
                break;
            case ShaderGraphOutput::Hair:
            case ShaderGraphOutput::Eye:
                addPass("depthVelocity", "KEIRE_PASS_DEPTH_VELOCITY");
                addPass("forwardOpaque", "KEIRE_PASS_FORWARD_OPAQUE");
                break;
            case ShaderGraphOutput::Transparent:
                addPass("forwardTransparent", "KEIRE_PASS_FORWARD_TRANSPARENT");
                break;
            case ShaderGraphOutput::Decal:
                addPass("decalDBuffer", "KEIRE_PASS_DECAL_DBUFFER");
                addPass("forwardTransparent", "KEIRE_PASS_FORWARD_TRANSPARENT");
                break;
            case ShaderGraphOutput::Fullscreen:
                addPass("primary", {});
                break;
            }
        }
        const Json manifest{
            {"schemaVersion", 3},
            {"materialGraphSourceSchemaVersion", definition.SchemaVersion},
            {"materialGraphGeneratedShaderVersion", ShaderGraphGeneratedShaderVersion},
            {"programTarget", ShaderGraphTargetName(definition.Target.Target)},
            {"programStages", static_cast<std::uint8_t>(definition.Target.Stages)},
            {"fullscreenInjectionPoint", FullscreenInjectionPointName(definition.Target.FullscreenInjectionPoint)},
            {"computeThreadGroupSize",
             {definition.Target.ThreadGroupSizeX, definition.Target.ThreadGroupSizeY,
              definition.Target.ThreadGroupSizeZ}},
            {"source", generatedSource.generic_string()},
            {"vertexLayoutVersion", ShaderGraphVertexLayoutVersion},
            {"receivesShadows", lit},
            {"usesForwardPlus", lit},
            {"usesInstancing", true},
            {"instanceAddressingAbiVersion", 2},
            {"occlusionSupport", static_cast<std::uint8_t>(occlusionSupport)},
            {"maximumWorldPositionDisplacementRadius",
             maximumWorldPositionDisplacementRadius ? Json(*maximumWorldPositionDisplacementRadius) : Json(nullptr)},
            {"usesImageBasedLighting", lit},
            {"spatialLightingAbiVersion", lit ? 3 : 0},
            {"usesVertexMaterialParameters", usesVertexMaterialParameters},
            {"stages", {{"vertex", "VSMain"}, {"fragment", "PSMain"}}},
            {"defines", std::move(defines)},
            {"passes", std::move(passes)},
            {"includeRoots", std::move(roots)},
            {"resources", resourceContract.at("resources")},
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
