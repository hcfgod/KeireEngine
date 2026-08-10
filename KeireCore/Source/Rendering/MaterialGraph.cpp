#include "Keire/Rendering/MaterialGraph.h"

#include "Keire/Rendering/ShaderGraph.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t MaximumMaterialGraphBytes = std::size_t{4} * 1024U * 1024U;
        constexpr std::size_t MaximumMaterialProperties = 80;
        constexpr std::size_t MaximumMaterialKeywords = 16;

        [[nodiscard]] AssetId ResolveShaderGraphVariantOwner(const AssetImportContext& context,
                                                             const AssetId graphAsset)
        {
            if (context.ProjectRoot.empty() || context.SourceRoot.empty() || !context.ReadProjectFile ||
                !context.ResolveAssetSource)
            {
                throw std::invalid_argument("Shader Graph material bindings require source and cross-asset resolvers.");
            }
            const auto source = context.ResolveAssetSource(graphAsset);
            if (!source || source->Type != ShaderGraphAsset::StaticType())
                throw std::runtime_error("Material references a Shader Graph that is not present in the source index.");
            const auto sourcePrefix = std::filesystem::relative(context.SourceRoot, context.ProjectRoot);
            const auto graph =
                ShaderGraphAsset::DecodeSource(context.ReadProjectFile(sourcePrefix / source->RelativePath));
            return graph.GeneratedAssetOwner ? graph.GeneratedAssetOwner : graphAsset;
        }

        [[nodiscard]] std::string Text(const std::span<const std::byte> bytes)
        {
            return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        }

        [[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text)
        {
            return {reinterpret_cast<const std::byte*>(text.data()),
                    reinterpret_cast<const std::byte*>(text.data() + text.size())};
        }

        [[nodiscard]] std::vector<std::uint8_t> Unsigned(const std::span<const std::byte> values)
        {
            std::vector<std::uint8_t> result(values.size());
            std::ranges::transform(values, result.begin(),
                                   [](const std::byte value) { return std::to_integer<std::uint8_t>(value); });
            return result;
        }

        [[nodiscard]] std::vector<std::byte> Bytes(const std::vector<std::uint8_t>& values)
        {
            std::vector<std::byte> result(values.size());
            std::ranges::transform(values, result.begin(), [](const std::uint8_t value) { return std::byte(value); });
            return result;
        }

        [[nodiscard]] bool ValidIdentifier(const std::string_view value)
        {
            if (value.empty() || value.size() > 128 ||
                !(std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_'))
                return false;
            return std::ranges::all_of(value.substr(1), [](const unsigned char character)
                                       { return std::isalnum(character) || character == '_'; });
        }

        [[nodiscard]] bool ValidTarget(const std::string_view value)
        {
            return !value.empty() && value.size() <= 64 &&
                   std::ranges::all_of(value, [](const unsigned char character)
                                       { return std::isalnum(character) || character == '_' || character == '-'; });
        }

        [[nodiscard]] Json EncodeValue(const MaterialPropertyValue& value)
        {
            return std::visit(
                [](const auto& typed) -> Json
                {
                    using T = std::decay_t<decltype(typed)>;
                    if constexpr (std::same_as<T, float>)
                        return typed;
                    else if constexpr (std::same_as<T, Vector2>)
                        return Json::array({typed.X, typed.Y});
                    else if constexpr (std::same_as<T, Vector3>)
                        return Json::array({typed.X, typed.Y, typed.Z});
                    else if constexpr (std::same_as<T, Vector4>)
                        return Json::array({typed.X, typed.Y, typed.Z, typed.W});
                    else if constexpr (std::same_as<T, Color>)
                        return Json::array({typed.Red, typed.Green, typed.Blue, typed.Alpha});
                    else
                        return typed ? Json(typed.ToString()) : Json(nullptr);
                },
                value);
        }

        [[nodiscard]] MaterialPropertyValue DecodeValue(const Json& source, const std::size_t type)
        {
            const auto component = [&](const std::size_t index)
            {
                if (!source.is_array() || source.size() <= index)
                    throw std::invalid_argument("Material Graph vector value has an invalid component count.");
                return source[index].get<float>();
            };
            switch (type)
            {
            case 0:
                return source.get<float>();
            case 1:
                return Vector2{component(0), component(1)};
            case 2:
                return Vector3{component(0), component(1), component(2)};
            case 3:
                return Vector4{component(0), component(1), component(2), component(3)};
            case 4:
                return Color{component(0), component(1), component(2), component(3)};
            case 5:
                return source.is_null() ? AssetId{} : AssetId::Parse(source.get<std::string>());
            default:
                throw std::invalid_argument("Material Graph property type is unsupported.");
            }
        }

        [[nodiscard]] Json EncodeShaderReference(const MaterialShaderReference& reference)
        {
            const auto kind = reference.Kind == MaterialShaderSourceKind::Builtin       ? "builtin"
                              : reference.Kind == MaterialShaderSourceKind::ShaderGraph ? "graph"
                                                                                        : "asset";
            Json keywords = Json::object();
            for (const auto& [name, option] : reference.Keywords)
                keywords[name] = option;
            return {{"kind", kind},
                    {"asset", reference.Asset ? Json(reference.Asset.ToString()) : Json(nullptr)},
                    {"target", reference.Target},
                    {"keywords", std::move(keywords)}};
        }

        [[nodiscard]] MaterialShaderReference DecodeShaderReference(const Json& source)
        {
            if (!source.is_object())
                throw std::invalid_argument("Material Graph shader reference must be an object.");
            MaterialShaderReference result;
            const auto kind = source.at("kind").get<std::string>();
            if (kind == "builtin")
                result.Kind = MaterialShaderSourceKind::Builtin;
            else if (kind == "asset")
                result.Kind = MaterialShaderSourceKind::ShaderAsset;
            else if (kind == "graph")
                result.Kind = MaterialShaderSourceKind::ShaderGraph;
            else
                throw std::invalid_argument("Material Graph shader reference kind is unsupported.");
            result.Asset =
                source.at("asset").is_null() ? AssetId{} : AssetId::Parse(source.at("asset").get<std::string>());
            result.Target = source.value("target", std::string("default"));
            const auto& keywords = source.value("keywords", Json::object());
            if (!keywords.is_object() || keywords.size() > MaximumMaterialKeywords)
                throw std::invalid_argument("Material Graph shader keywords must be a bounded object.");
            for (const auto& [name, option] : keywords.items())
                result.Keywords.emplace(name, option.get<std::string>());
            return result;
        }

        [[nodiscard]] Json EncodeDefinition(const MaterialGraphDefinition& definition)
        {
            Json properties = Json::array();
            for (const auto& property : definition.Properties)
            {
                properties.push_back({{"id", property.Property ? Json(property.Property.ToString()) : Json(nullptr)},
                                      {"name", property.Name},
                                      {"type", property.Value.index()},
                                      {"value", EncodeValue(property.Value)}});
            }
            return {{"schemaVersion", definition.SchemaVersion},
                    {"shader", EncodeShaderReference(definition.Shader)},
                    {"surface",
                     {{"alphaMode", static_cast<std::uint8_t>(definition.Surface.AlphaMode)},
                      {"alphaCutoff", definition.Surface.AlphaCutoff},
                      {"doubleSided", definition.Surface.DoubleSided}}},
                    {"bakedLighting",
                     {{"contributeEmission", definition.ContributeEmissionToGI},
                      {"emissiveIntensity", definition.EmissiveGIIntensity}}},
                    {"properties", std::move(properties)}};
        }

        [[nodiscard]] MaterialGraphDefinition DecodeDefinition(const Json& source)
        {
            if (!source.is_object())
                throw std::invalid_argument("Material Graph source must be an object.");
            MaterialGraphDefinition result;
            result.SchemaVersion = source.value("schemaVersion", 0U);
            result.Shader = DecodeShaderReference(source.at("shader"));
            const auto& surface = source.value("surface", Json::object());
            result.Surface.AlphaMode =
                static_cast<MaterialAlphaMode>(surface.value("alphaMode", static_cast<std::uint8_t>(0)));
            result.Surface.AlphaCutoff = surface.value("alphaCutoff", 0.5F);
            result.Surface.DoubleSided = surface.value("doubleSided", false);
            const auto& lighting = source.value("bakedLighting", Json::object());
            result.ContributeEmissionToGI = lighting.value("contributeEmission", true);
            result.EmissiveGIIntensity = lighting.value("emissiveIntensity", 1.0F);
            const auto& properties = source.value("properties", Json::array());
            if (!properties.is_array() || properties.size() > MaximumMaterialProperties)
                throw std::invalid_argument("Material Graph properties must be a bounded array.");
            for (const auto& property : properties)
            {
                MaterialGraphPropertyBinding binding;
                binding.Property =
                    property.at("id").is_null() ? AssetId{} : AssetId::Parse(property.at("id").get<std::string>());
                binding.Name = property.at("name").get<std::string>();
                binding.Value = DecodeValue(property.at("value"), property.at("type").get<std::size_t>());
                result.Properties.push_back(std::move(binding));
            }
            ValidateMaterialGraph(result);
            return result;
        }

        [[nodiscard]] bool ValueMatches(const MaterialPropertyValue& value, const ShaderPropertyType type)
        {
            return (type == ShaderPropertyType::Scalar && std::holds_alternative<float>(value)) ||
                   (type == ShaderPropertyType::Vector2 && std::holds_alternative<Vector2>(value)) ||
                   (type == ShaderPropertyType::Vector3 && std::holds_alternative<Vector3>(value)) ||
                   (type == ShaderPropertyType::Vector4 && std::holds_alternative<Vector4>(value)) ||
                   (type == ShaderPropertyType::Color && std::holds_alternative<Color>(value)) ||
                   (type == ShaderPropertyType::Texture2D && std::holds_alternative<AssetId>(value));
        }
    } // namespace

    MaterialGraphAsset::MaterialGraphAsset(MaterialGraphDefinition definition) : m_Definition(std::move(definition)) {}

    std::size_t MaterialGraphAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this) + m_Definition.Shader.Target.size();
        for (const auto& [name, option] : m_Definition.Shader.Keywords)
            result += name.size() + option.size();
        for (const auto& property : m_Definition.Properties)
            result += sizeof(property) + property.Name.size();
        return result;
    }

    Ref<MaterialGraphAsset> MaterialGraphAsset::Decode(const std::span<const std::byte> bytes)
    {
        try
        {
            if (bytes.size() > MaximumMaterialGraphBytes)
                throw std::invalid_argument("Material Graph cooked data exceeds its byte limit.");
            return CreateRef<MaterialGraphAsset>(DecodeDefinition(Json::from_cbor(Unsigned(bytes))));
        }
        catch (const std::exception& error)
        {
            throw std::invalid_argument(std::string("Material Graph asset decode failed: ") + error.what());
        }
    }

    std::vector<std::byte> MaterialGraphAsset::Encode(const MaterialGraphDefinition& definition)
    {
        ValidateMaterialGraph(definition);
        return Bytes(Json::to_cbor(EncodeDefinition(definition)));
    }

    MaterialGraphDefinition MaterialGraphAsset::DecodeSource(const std::span<const std::byte> bytes)
    {
        if (bytes.size() > MaximumMaterialGraphBytes)
            throw std::invalid_argument("Material Graph source exceeds its byte limit.");
        return DecodeDefinition(Json::parse(Text(bytes)));
    }

    std::vector<std::byte> MaterialGraphAsset::EncodeSource(const MaterialGraphDefinition& definition)
    {
        ValidateMaterialGraph(definition);
        const auto text = EncodeDefinition(definition).dump(2) + '\n';
        return Bytes(text);
    }

    Ref<MaterialGraphAsset> MaterialGraphAsset::Error() { return CreateRef<MaterialGraphAsset>(); }

    void ValidateMaterialGraph(const MaterialGraphDefinition& definition)
    {
        if (definition.SchemaVersion != MaterialGraphSourceSchemaVersion ||
            definition.Shader.Kind > MaterialShaderSourceKind::ShaderGraph ||
            definition.Properties.size() > MaximumMaterialProperties ||
            definition.Shader.Keywords.size() > MaximumMaterialKeywords ||
            definition.Surface.AlphaMode > MaterialAlphaMode::Blend || !std::isfinite(definition.Surface.AlphaCutoff) ||
            definition.Surface.AlphaCutoff < 0.0F || definition.Surface.AlphaCutoff > 1.0F ||
            !std::isfinite(definition.EmissiveGIIntensity) || definition.EmissiveGIIntensity < 0.0F ||
            definition.EmissiveGIIntensity > 100'000.0F)
            throw std::invalid_argument("Material Graph definition is invalid or exceeds a portable bound.");
        if (!definition.Shader.Asset ||
            (definition.Shader.Kind == MaterialShaderSourceKind::ShaderGraph && !ValidTarget(definition.Shader.Target)))
            throw std::invalid_argument("Material Graph requires a valid shader or Shader Graph target.");
        if (definition.Shader.Kind != MaterialShaderSourceKind::ShaderGraph &&
            (!definition.Shader.Keywords.empty() || definition.Shader.Target != "default"))
            throw std::invalid_argument("Only Shader Graph references may select targets or keywords.");
        for (const auto& [name, option] : definition.Shader.Keywords)
            if (!ValidIdentifier(name) || (option != "true" && option != "false" && !ValidIdentifier(option)))
                throw std::invalid_argument("Material Graph keyword selection is invalid.");

        std::set<AssetId> propertyIds;
        std::set<std::string, std::less<>> propertyNames;
        for (const auto& property : definition.Properties)
        {
            if (!ValidIdentifier(property.Name) || !propertyNames.insert(property.Name).second ||
                (property.Property && !propertyIds.insert(property.Property).second))
                throw std::invalid_argument("Material Graph property bindings must have unique valid identities.");
            std::visit(
                [](const auto& value)
                {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (!std::same_as<T, AssetId>)
                    {
                        const bool finite = [&]
                        {
                            if constexpr (std::same_as<T, float>)
                                return std::isfinite(value);
                            else if constexpr (std::same_as<T, Vector2>)
                                return Math::IsFinite(Vector4{value.X, value.Y, 0.0F, 0.0F});
                            else if constexpr (std::same_as<T, Vector3>)
                                return Math::IsFinite(Vector4{value.X, value.Y, value.Z, 0.0F});
                            else if constexpr (std::same_as<T, Vector4>)
                                return Math::IsFinite(value);
                            else
                                return Math::IsFinite(Vector4{value.Red, value.Green, value.Blue, value.Alpha});
                        }();
                        if (!finite)
                            throw std::invalid_argument("Material Graph property values must be finite.");
                    }
                },
                property.Value);
        }
    }

    std::vector<MaterialGraphDiagnostic>
    ValidateMaterialGraphAgainstInterface(const MaterialGraphDefinition& definition,
                                          const ShaderInterfaceDefinition& interfaceDefinition)
    {
        ValidateMaterialGraph(definition);
        if (interfaceDefinition.SchemaVersion != 1 || interfaceDefinition.AbiVersion == 0)
            throw std::invalid_argument("Shader interface schema or ABI version is unsupported.");
        std::vector<MaterialGraphDiagnostic> diagnostics;
        for (const auto& binding : definition.Properties)
        {
            auto found = interfaceDefinition.Properties.end();
            if (binding.Property)
                found =
                    std::ranges::find(interfaceDefinition.Properties, binding.Property, &ShaderPropertyDefinition::Id);
            if (found == interfaceDefinition.Properties.end())
                found =
                    std::ranges::find(interfaceDefinition.Properties, binding.Name, &ShaderPropertyDefinition::Name);
            if (found == interfaceDefinition.Properties.end())
                diagnostics.push_back({MaterialGraphDiagnosticSeverity::Error, "MAT1001",
                                       "Property is not exposed by the selected shader: " + binding.Name,
                                       binding.Property});
            else if (!ValueMatches(binding.Value, found->Type))
                diagnostics.push_back({MaterialGraphDiagnosticSeverity::Error, "MAT1002",
                                       "Property type does not match the selected shader: " + binding.Name,
                                       binding.Property});
            else if (binding.Property && found->Name != binding.Name)
                diagnostics.push_back({MaterialGraphDiagnosticSeverity::Info, "MAT1003",
                                       "Property was renamed to " + found->Name + "; its stable identity was retained.",
                                       binding.Property});
        }
        return diagnostics;
    }

    MaterialAssetDefinition
    BakeMaterialGraph(const MaterialGraphDefinition& definition,
                      const std::function<AssetId(const MaterialShaderReference&)>& resolveShader)
    {
        ValidateMaterialGraph(definition);
        if (!resolveShader)
            throw std::invalid_argument("Material Graph baking requires a shader resolver.");
        MaterialAssetDefinition result;
        result.Shader = resolveShader(definition.Shader);
        if (!result.Shader)
            throw std::runtime_error("Material Graph selected a shader target that is not published.");
        result.Surface = definition.Surface;
        result.ContributeEmissionToGI = definition.ContributeEmissionToGI;
        result.EmissiveGIIntensity = definition.EmissiveGIIntensity;
        for (const auto& property : definition.Properties)
            result.Properties.emplace(property.Name, property.Value);
        return result;
    }

    AssetImporterRegistration CreateMaterialGraphAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.MaterialGraph";
        result.Version = 2;
        result.Type = MaterialGraphAsset::StaticType();
        result.Extensions = {".keirematerialgraph"};
        result.ContextualImport = [](const AssetImportContext& context, const std::span<const std::byte> bytes)
        {
            if (!context.Asset || !context.ResolveSubAssetId)
                throw std::invalid_argument("Material Graph import requires a stable asset and subasset resolver.");
            const auto definition = MaterialGraphAsset::DecodeSource(bytes);
            const auto material = BakeMaterialGraph(
                definition,
                [&context](const MaterialShaderReference& shader)
                {
                    if (shader.Kind != MaterialShaderSourceKind::ShaderGraph)
                        return shader.Asset;
                    if (!context.ResolveSubAssetIdFor)
                        throw std::invalid_argument(
                            "Shader Graph material bindings require a cross-asset subasset resolver.");
                    return context.ResolveSubAssetIdFor(
                        ResolveShaderGraphVariantOwner(context, shader.Asset),
                        MakeShaderGraphVariantSubAssetKey(shader.Target, shader.Keywords));
                });
            AssetImportOutput output;
            output.Bytes = MaterialGraphAsset::Encode(definition);
            output.AssetDependencies.push_back(definition.Shader.Asset);
            if (material.Shader != definition.Shader.Asset)
                output.AssetDependencies.push_back(material.Shader);
            for (const auto& property : definition.Properties)
                if (const auto* texture = std::get_if<AssetId>(&property.Value); texture && *texture)
                    output.AssetDependencies.push_back(*texture);
            std::ranges::sort(output.AssetDependencies);
            output.AssetDependencies.erase(
                std::unique(output.AssetDependencies.begin(), output.AssetDependencies.end()),
                output.AssetDependencies.end());
            output.SubAssets.push_back({context.ResolveSubAssetId("material/default"), MaterialAsset::StaticType(),
                                        "material/default", "Runtime Material", MaterialAsset::Encode(material),
                                        output.AssetDependencies});
            return output;
        };
        return result;
    }

    AssetDecoderRegistration CreateMaterialGraphAssetDecoder()
    {
        return {MaterialGraphAsset::StaticType(), MaterialGraphAsset::Error(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return MaterialGraphAsset::Decode(bytes); }};
    }
} // namespace Keire
