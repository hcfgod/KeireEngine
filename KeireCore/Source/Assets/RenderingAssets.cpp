#include "Keire/Assets/RenderingAssets.h"

#include "Keire/Rendering/ShaderGraph.h"

#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/Assets/ShaderCompilerJobs.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <SDL3/SDL_filesystem.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;

        constexpr std::size_t MaximumShaderNumericProperties = 64;
        constexpr std::size_t MaximumShaderTextureProperties = 16;
        constexpr std::size_t MaximumShaderProperties = MaximumShaderNumericProperties + MaximumShaderTextureProperties;
        constexpr std::size_t MaximumShaderDependencies = 256;
        constexpr std::size_t MaximumDefines = 128;
        constexpr std::size_t MaximumIncludeRoots = 16;

        struct ResolvedShaderGraphVariant final
        {
            AssetId Owner;
            std::vector<std::string> Keywords;
        };

        [[nodiscard]] ResolvedShaderGraphVariant ResolveShaderGraphVariant(const AssetImportContext& context,
                                                                           const MaterialShaderReference& reference)
        {
            if (context.ProjectRoot.empty() || context.SourceRoot.empty() || !context.ReadProjectFile ||
                !context.ResolveAssetSource)
            {
                throw std::invalid_argument(
                    "Shader Graph material references require source and cross-asset resolvers.");
            }
            const auto source = context.ResolveAssetSource(reference.Asset);
            if (!source || source->Type != ShaderGraphAsset::StaticType())
                throw std::runtime_error("Material references a Shader Graph that is not present in the source index.");
            const auto sourcePrefix = std::filesystem::relative(context.SourceRoot, context.ProjectRoot);
            const auto graph =
                ShaderGraphAsset::DecodeSource(context.ReadProjectFile(sourcePrefix / source->RelativePath));
            ShaderGraphInstanceDefinition selection;
            selection.Parent = reference.Asset;
            selection.KeywordOverrides = reference.Keywords;
            const std::array ancestry{selection};
            auto resolved = ResolveShaderGraphInstance(graph, ancestry);
            return {graph.GeneratedAssetOwner ? graph.GeneratedAssetOwner : reference.Asset,
                    std::move(resolved.Keywords)};
        }

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

        [[nodiscard]] Json Vector(const Vector4 value) { return Json::array({value.X, value.Y, value.Z, value.W}); }

        [[nodiscard]] Vector4 ParseVector(const Json& value)
        {
            if (!value.is_array() || value.size() != 4)
                throw std::invalid_argument("Shader vector values require four finite numbers.");
            Vector4 result{value[0].get<float>(), value[1].get<float>(), value[2].get<float>(), value[3].get<float>()};
            if (!Math::IsFinite(result))
                throw std::invalid_argument("Shader vector values require four finite numbers.");
            return result;
        }

        [[nodiscard]] std::vector<std::byte> ReadFile(const std::filesystem::path& path, const std::size_t maximum)
        {
            std::error_code error;
            const auto size = std::filesystem::file_size(path, error);
            if (error || size > maximum)
                throw std::runtime_error("Shader compiler output is missing or exceeds its configured limit: " +
                                         path.string());
            std::vector<std::byte> result(static_cast<std::size_t>(size));
            std::ifstream stream(path, std::ios::binary);
            if (!stream || (!result.empty() && !stream.read(reinterpret_cast<char*>(result.data()),
                                                            static_cast<std::streamsize>(result.size()))))
                throw std::runtime_error("Could not read shader compiler output: " + path.string());
            return result;
        }

        [[nodiscard]] std::string Text(const std::span<const std::byte> bytes)
        {
            return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }

        [[nodiscard]] std::string Utf8Path(const std::filesystem::path& path)
        {
            const auto value = path.generic_u8string();
            return std::string(reinterpret_cast<const char*>(value.data()), value.size());
        }

        [[nodiscard]] bool ValidIdentifier(const std::string_view value)
        {
            if (value.empty() || value.size() > 128 ||
                !(std::isalpha(static_cast<unsigned char>(value.front())) || value.front() == '_'))
                return false;
            return std::ranges::all_of(value.substr(1), [](const unsigned char character)
                                       { return std::isalnum(character) || character == '_'; });
        }

        void ValidateDefinition(const ShaderAssetDefinition& definition, const bool requireVariants,
                                const bool allowMissingVariants = false)
        {
            if (definition.SchemaVersion != 1 ||
                (definition.VertexLayoutVersion < 1 || definition.VertexLayoutVersion > 3) ||
                definition.Topology > ShaderPrimitiveTopology::PointList || definition.Culling > ShaderCullMode::Back ||
                (definition.SpatialLightingAbiVersion != 0U && definition.SpatialLightingAbiVersion != 2U) ||
                (definition.SpatialLightingAbiVersion == 2U &&
                 (!definition.UsesImageBasedLighting || definition.VertexLayoutVersion != 3U)) ||
                definition.Source.empty() || definition.Source.is_absolute() ||
                definition.Source.lexically_normal().generic_string().starts_with("..") ||
                !ValidIdentifier(definition.VertexEntry) || !ValidIdentifier(definition.FragmentEntry))
                throw std::invalid_argument("Shader definition contains an unsupported schema, path, or entry point.");
            if (definition.Properties.size() > MaximumShaderProperties ||
                definition.Dependencies.size() > MaximumShaderDependencies ||
                (!allowMissingVariants && definition.Variants.empty()) ||
                (requireVariants && definition.Variants.size() != 3))
                throw std::invalid_argument("Shader definition exceeds a bounded collection or lacks variants.");

            std::set<std::string, std::less<>> propertyNames;
            std::set<AssetId> propertyIds;
            std::size_t numericProperties = 0;
            std::size_t textureProperties = 0;
            for (const auto& property : definition.Properties)
            {
                if (!ValidIdentifier(property.Name) || !propertyNames.insert(property.Name).second ||
                    property.Type > ShaderPropertyType::Texture2D ||
                    (property.Id && !propertyIds.insert(property.Id).second))
                    throw std::invalid_argument(
                        "Shader property names and types must be unique supported identifiers.");
                if (property.Type == ShaderPropertyType::Texture2D)
                {
                    ++textureProperties;
                    if (property.TextureSemantic > ShaderTextureSemantic::Roughness)
                        throw std::invalid_argument("Shader texture property semantic is invalid.");
                }
                else
                {
                    ++numericProperties;
                    if (!Math::IsFinite(property.DefaultValue))
                        throw std::invalid_argument("Shader numeric property defaults must be finite.");
                    if ((property.Minimum && !std::isfinite(*property.Minimum)) ||
                        (property.Maximum && !std::isfinite(*property.Maximum)) ||
                        (property.Step && (!std::isfinite(*property.Step) || *property.Step <= 0.0F)) ||
                        (property.Minimum && property.Maximum && *property.Minimum > *property.Maximum))
                        throw std::invalid_argument("Shader numeric property range is invalid.");
                }
                if (property.DisplayName.size() > 128 || property.Category.size() > 128)
                    throw std::invalid_argument("Shader property editor metadata exceeds its limit.");
            }
            if (numericProperties > MaximumShaderNumericProperties ||
                textureProperties > MaximumShaderTextureProperties)
                throw std::invalid_argument("Shader exceeds the 64 numeric slot or 16 texture slot ABI limit.");
            constexpr std::size_t portableFragmentSamplerLimit = 16;
            const auto reservedSamplers = (definition.ReceivesShadows ? 2U : 0U) +
                                          (definition.UsesImageBasedLighting ? 2U : 0U) +
                                          (definition.SpatialLightingAbiVersion == 2U ? 5U : 0U);
            if (textureProperties + reservedSamplers > portableFragmentSamplerLimit)
            {
                throw std::invalid_argument(
                    "Shader material textures and fixed lighting resources exceed the portable 16-sampler limit.");
            }
            std::set<ShaderBinaryFormat> formats;
            for (const auto& variant : definition.Variants)
            {
                if (variant.Vertex.empty() || variant.Fragment.empty() || !formats.insert(variant.Format).second)
                    throw std::invalid_argument("Shader variants must be non-empty and have unique formats.");
            }
        }

        void ValidateMaterialDefinition(const MaterialAssetDefinition& definition)
        {
            if (definition.SchemaVersion < 1 || definition.SchemaVersion > 3 ||
                definition.Properties.size() > MaximumShaderProperties ||
                definition.Surface.AlphaMode > MaterialAlphaMode::AlphaHoldout ||
                !std::isfinite(definition.Surface.AlphaCutoff) || definition.Surface.AlphaCutoff < 0.0F ||
                definition.Surface.AlphaCutoff > 1.0F || !std::isfinite(definition.EmissiveGIIntensity) ||
                definition.EmissiveGIIntensity < 0.0F || definition.EmissiveGIIntensity > 100'000.0F)
                throw std::invalid_argument("Material definition is invalid or exceeds its property limit.");
            for (const auto& [name, value] : definition.Properties)
            {
                if (!ValidIdentifier(name))
                    throw std::invalid_argument("Material property name is invalid.");
                std::visit(
                    [](const auto& typed)
                    {
                        using T = std::decay_t<decltype(typed)>;
                        if constexpr (!std::same_as<T, AssetId>)
                        {
                            Vector4 packed;
                            if constexpr (std::same_as<T, float>)
                                packed.X = typed;
                            else if constexpr (std::same_as<T, Vector2>)
                                packed = {typed.X, typed.Y, 0.0F, 0.0F};
                            else if constexpr (std::same_as<T, Vector3>)
                                packed = {typed.X, typed.Y, typed.Z, 0.0F};
                            else if constexpr (std::same_as<T, Vector4>)
                                packed = typed;
                            else
                                packed = {typed.Red, typed.Green, typed.Blue, typed.Alpha};
                            if (!Math::IsFinite(packed))
                                throw std::invalid_argument("Material property value is not finite.");
                        }
                    },
                    value);
            }
        }

        [[nodiscard]] bool ValidShaderTarget(const std::string_view value)
        {
            return !value.empty() && value.size() <= 64 &&
                   std::ranges::all_of(value, [](const unsigned char character)
                                       { return std::isalnum(character) || character == '_' || character == '-'; });
        }

        void ValidateMaterialAuthoringDefinition(const MaterialAuthoringDefinition& definition)
        {
            MaterialAssetDefinition runtime;
            runtime.Surface = definition.Surface;
            runtime.ContributeEmissionToGI = definition.ContributeEmissionToGI;
            runtime.EmissiveGIIntensity = definition.EmissiveGIIntensity;
            runtime.Properties = definition.Properties;
            ValidateMaterialDefinition(runtime);
            if (definition.SchemaVersion != 4 || definition.Shader.Kind > MaterialShaderSourceKind::ShaderGraph ||
                (definition.Shader.Kind != MaterialShaderSourceKind::ShaderAsset && !definition.Shader.Asset) ||
                definition.Shader.Keywords.size() > 16 ||
                (definition.Shader.Kind == MaterialShaderSourceKind::ShaderGraph &&
                 !ValidShaderTarget(definition.Shader.Target)))
            {
                throw std::invalid_argument("Material authoring shader reference is invalid.");
            }
            if (definition.Shader.Kind != MaterialShaderSourceKind::ShaderGraph &&
                (!definition.Shader.Keywords.empty() || definition.Shader.Target != "default"))
            {
                throw std::invalid_argument("Only Shader Graph references may select targets or keywords.");
            }
            for (const auto& [name, option] : definition.Shader.Keywords)
            {
                if (!ValidIdentifier(name) || (option != "true" && option != "false" && !ValidIdentifier(option)))
                    throw std::invalid_argument("Material Shader Graph keyword selection is invalid.");
            }
        }

        [[nodiscard]] Json EncodeMaterialProperty(const MaterialPropertyValue& value)
        {
            return std::visit(
                [](const auto& typed) -> Json
                {
                    using T = std::decay_t<decltype(typed)>;
                    if constexpr (std::same_as<T, AssetId>)
                        return typed ? Json(typed.ToString()) : Json(nullptr);
                    else if constexpr (std::same_as<T, float>)
                        return typed;
                    else if constexpr (std::same_as<T, Vector2>)
                        return Json::array({typed.X, typed.Y});
                    else if constexpr (std::same_as<T, Vector3>)
                        return Json::array({typed.X, typed.Y, typed.Z});
                    else if constexpr (std::same_as<T, Vector4>)
                        return Json::array({typed.X, typed.Y, typed.Z, typed.W});
                    else
                        return Json::array({typed.Red, typed.Green, typed.Blue, typed.Alpha});
                },
                value);
        }

        [[nodiscard]] MaterialPropertyValue DecodeMaterialProperty(const Json& encoded)
        {
            const auto type = encoded.at("type").get<std::size_t>();
            const auto& value = encoded.at("value");
            if (type > std::variant_size_v<MaterialPropertyValue> - 1U)
                throw std::invalid_argument("Material property type is invalid.");
            const auto component = [&](const std::size_t index)
            {
                if (!value.is_array() || value.size() <= index)
                    throw std::invalid_argument("Material vector property has an invalid component count.");
                return value[index].get<float>();
            };
            switch (type)
            {
            case 0:
                return value.get<float>();
            case 1:
                return Vector2{component(0), component(1)};
            case 2:
                return Vector3{component(0), component(1), component(2)};
            case 3:
                return Vector4{component(0), component(1), component(2), component(3)};
            case 4:
                return Color{component(0), component(1), component(2), component(3)};
            case 5:
                return value.is_null() ? AssetId{} : AssetId::Parse(value.get<std::string>());
            default:
                throw std::invalid_argument("Material property type is invalid.");
            }
        }

        [[nodiscard]] MaterialAssetDefinition ParseMaterialSource(const Json& source);

        [[nodiscard]] MaterialAuthoringDefinition ParseMaterialAuthoringSource(const Json& source)
        {
            if (!source.is_object())
                throw std::invalid_argument("Material authoring source must be an object.");
            const auto schemaVersion = source.value("schemaVersion", 0U);
            if (schemaVersion <= 3)
            {
                const auto legacy = ParseMaterialSource(source);
                MaterialAuthoringDefinition result;
                result.Shader.Asset = legacy.Shader;
                result.Surface = legacy.Surface;
                result.ContributeEmissionToGI = legacy.ContributeEmissionToGI;
                result.EmissiveGIIntensity = legacy.EmissiveGIIntensity;
                result.Properties = legacy.Properties;
                return result;
            }
            if (schemaVersion != 4)
                throw std::invalid_argument("Material authoring source has an unsupported schema.");

            MaterialAuthoringDefinition result;
            const auto& shader = source.at("shader");
            if (!shader.is_object())
                throw std::invalid_argument("Material shader reference must be an object.");
            const auto kind = shader.at("kind").get<std::string>();
            if (kind == "builtin")
                result.Shader.Kind = MaterialShaderSourceKind::Builtin;
            else if (kind == "asset")
                result.Shader.Kind = MaterialShaderSourceKind::ShaderAsset;
            else if (kind == "graph")
                result.Shader.Kind = MaterialShaderSourceKind::ShaderGraph;
            else
                throw std::invalid_argument("Material shader reference kind is unsupported.");
            result.Shader.Asset = AssetId::Parse(shader.at("asset").get<std::string>());
            result.Shader.Target = shader.value("target", std::string("default"));
            const auto keywords = shader.value("keywords", Json::object());
            if (!keywords.is_object())
                throw std::invalid_argument("Material shader keywords must be an object.");
            for (const auto& [name, value] : keywords.items())
                result.Shader.Keywords.emplace(name, value.get<std::string>());

            const auto& surface = source.value("surface", Json::object());
            result.Surface.AlphaMode =
                static_cast<MaterialAlphaMode>(surface.value("alphaMode", static_cast<std::uint8_t>(0)));
            result.Surface.AlphaCutoff = surface.value("alphaCutoff", 0.5F);
            result.Surface.DoubleSided = surface.value("doubleSided", false);
            const auto& lighting = source.value("bakedLighting", Json::object());
            result.ContributeEmissionToGI = lighting.value("contributeEmission", true);
            result.EmissiveGIIntensity = lighting.value("emissiveIntensity", 1.0F);
            const auto& properties = source.value("properties", Json::object());
            if (!properties.is_object())
                throw std::invalid_argument("Material authoring properties must be an object.");
            for (const auto& [name, value] : properties.items())
                result.Properties.emplace(name, DecodeMaterialProperty(value));
            ValidateMaterialAuthoringDefinition(result);
            return result;
        }

        [[nodiscard]] MaterialAssetDefinition ParseMaterialSource(const Json& source)
        {
            MaterialAssetDefinition definition;
            if (!source.is_object())
                throw std::invalid_argument("Material manifest has an unsupported schema.");
            definition.SchemaVersion = source.value("schemaVersion", 0U);
            if (definition.SchemaVersion < 1 || definition.SchemaVersion > 3)
                throw std::invalid_argument("Material manifest has an unsupported schema.");
            definition.Shader =
                source.at("shader").is_null() ? AssetId{} : AssetId::Parse(source.at("shader").get<std::string>());
            if (definition.SchemaVersion >= 2)
            {
                const auto& surface = source.value("surface", Json::object());
                if (!surface.is_object())
                    throw std::invalid_argument("Material surface state must be an object.");
                definition.Surface.AlphaMode =
                    static_cast<MaterialAlphaMode>(surface.value("alphaMode", static_cast<std::uint8_t>(0)));
                definition.Surface.AlphaCutoff = surface.value("alphaCutoff", 0.5F);
                definition.Surface.DoubleSided = surface.value("doubleSided", false);
            }
            if (definition.SchemaVersion >= 3)
            {
                const auto& lighting = source.value("bakedLighting", Json::object());
                if (!lighting.is_object())
                    throw std::invalid_argument("Material baked-lighting state must be an object.");
                definition.ContributeEmissionToGI = lighting.value("contributeEmission", true);
                definition.EmissiveGIIntensity = lighting.value("emissiveIntensity", 1.0F);
            }
            const auto properties = source.value("properties", Json::object());
            if (!properties.is_object())
                throw std::invalid_argument("Material properties must be an object.");
            for (const auto& [name, value] : properties.items())
            {
                if (value.is_number())
                    definition.Properties.emplace(name, value.get<float>());
                else if (value.is_string() || value.is_null())
                    definition.Properties.emplace(name, value.is_null() ? AssetId{}
                                                                        : AssetId::Parse(value.get<std::string>()));
                else
                    definition.Properties.emplace(name, ParseVector(value));
            }
            ValidateMaterialDefinition(definition);
            return definition;
        }

        [[nodiscard]] Json EncodeShaderJson(const ShaderAssetDefinition& definition)
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
                    encoded["default"] = Vector(property.DefaultValue);
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
                    {"properties", std::move(properties)},
                    {"dependencies", std::move(dependencies)},
                    {"variants", std::move(variants)}};
        }

        [[nodiscard]] ShaderAssetDefinition DecodeShaderJson(const Json& source)
        {
            if (!source.is_object())
                throw std::invalid_argument("Canonical shader data must be an object.");
            ShaderAssetDefinition result;
            result.SchemaVersion = source.at("schemaVersion").get<std::uint32_t>();
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
                    decoded.DefaultValue = ParseVector(property.at("default"));
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
            ValidateDefinition(result, false);
            return result;
        }

        [[nodiscard]] std::filesystem::path ResolveCompiler(const ShaderImporterSpecification& specification)
        {
            if (!specification.Compiler.empty())
                return std::filesystem::absolute(specification.Compiler).lexically_normal();
#if defined(_WIN32)
            char* configured = nullptr;
            std::size_t configuredLength = 0;
            if (_dupenv_s(&configured, &configuredLength, "KEIRE_SHADER_COMPILER") == 0 && configured &&
                configuredLength > 1)
            {
                const auto result = std::filesystem::absolute(configured).lexically_normal();
                std::free(configured);
                return result;
            }
            std::free(configured);
#else
            if (const char* configured = std::getenv("KEIRE_SHADER_COMPILER"); configured && *configured)
                return std::filesystem::absolute(configured).lexically_normal();
#endif
#if defined(_WIN32)
            constexpr std::string_view compilerName = "KeireShaderCompiler.exe";
#else
            constexpr std::string_view compilerName = "KeireShaderCompiler";
#endif
            std::vector<std::filesystem::path> candidates;
            if (const char* basePath = SDL_GetBasePath(); basePath && *basePath)
            {
                auto ancestor = Detail::PathFromUtf8(basePath).lexically_normal();
                candidates.push_back(ancestor / compilerName);
                constexpr std::size_t maximumAncestorDepth = 6;
                for (std::size_t depth = 0; depth < maximumAncestorDepth; ++depth)
                {
                    candidates.push_back(ancestor / "Tools" / "ShaderCompiler" / compilerName);
                    candidates.push_back(ancestor / "bin" / compilerName);
                    const auto parent = ancestor.parent_path();
                    if (parent.empty() || parent == ancestor)
                        break;
                    ancestor = parent;
                }
            }
            const auto workingDirectory = std::filesystem::current_path();
            candidates.push_back(workingDirectory / "Build" / "Tools" / "ShaderCompiler" / compilerName);
            candidates.push_back(workingDirectory / "bin" / compilerName);
            candidates.push_back(workingDirectory / compilerName);
            const auto found = std::ranges::find_if(candidates, [](const auto& path)
                                                    { return std::filesystem::is_regular_file(path); });
            return found == candidates.end() ? std::filesystem::path{} : *found;
        }

        class TemporaryShaderDirectory final
        {
          public:
            TemporaryShaderDirectory()
            {
                m_Root = std::filesystem::absolute(std::filesystem::temp_directory_path() / "KeireShaderCompilerJobs")
                             .lexically_normal();
                std::filesystem::create_directories(m_Root);
                static std::once_flag cleanupOnce;
                std::call_once(cleanupOnce,
                               [this]
                               {
                                   constexpr auto staleAge = std::chrono::hours(1);
                                   (void)Detail::CleanupStaleShaderCompilerJobs(
                                       m_Root, std::filesystem::file_time_type::clock::now(), staleAge);
                               });
                m_Path = m_Root / AssetId::Generate().ToString();
                std::filesystem::create_directories(m_Path);
                try
                {
                    Detail::WriteShaderCompilerJobLease(m_Path, Detail::CurrentProcessId());
                }
                catch (...)
                {
                    std::error_code ignored;
                    std::filesystem::remove_all(m_Path, ignored);
                    throw;
                }
            }

            ~TemporaryShaderDirectory()
            {
                if (m_Path.parent_path() == m_Root)
                {
                    std::error_code ignored;
                    std::filesystem::remove_all(m_Path, ignored);
                }
            }

            [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_Path; }

          private:
            std::filesystem::path m_Root;
            std::filesystem::path m_Path;
        };

        void RunCompiler(const std::filesystem::path& compiler, std::vector<std::string> arguments,
                         const std::filesystem::path& workingDirectory, const std::chrono::milliseconds timeout)
        {
            const auto result = Detail::RunProcess(compiler, arguments, workingDirectory, timeout);
            if (result.TimedOut)
                throw std::runtime_error("Shader compiler timed out after " + std::to_string(timeout.count()) +
                                         " ms.\n" + result.Output);
            if (result.ExitCode != 0)
                throw std::runtime_error("Shader compiler failed with exit code " + std::to_string(result.ExitCode) +
                                         ".\n" + result.Output);
        }

        [[nodiscard]] std::vector<std::byte>
        Compile(const std::filesystem::path& compiler, const std::filesystem::path& source,
                const std::string_view destination, const std::string_view stage, const std::string_view entry,
                const std::filesystem::path& output, const std::span<const std::filesystem::path> includeRoots,
                const std::span<const std::pair<std::string, std::string>> defines,
                const ShaderImporterSpecification& specification, const std::filesystem::path& workingDirectory)
        {
            std::vector<std::string> arguments{Utf8Path(source),
                                               "-s",
                                               "HLSL",
                                               "-d",
                                               std::string(destination),
                                               "-t",
                                               std::string(stage),
                                               "-e",
                                               std::string(entry),
                                               "-o",
                                               Utf8Path(output)};
            for (const auto& root : includeRoots)
            {
                arguments.emplace_back("-I");
                arguments.push_back(Utf8Path(root));
            }
            for (const auto& [name, value] : defines)
                arguments.push_back("-D" + name + "=" + value);
            RunCompiler(compiler, std::move(arguments), workingDirectory, specification.Timeout);
            return ReadFile(output, specification.MaximumOutputBytes);
        }

        void ValidateReflection(const Json& vertex, const Json& fragment, const ShaderAssetDefinition& definition)
        {
            const auto noStorageTextures = [](const Json& value) { return value.value("storage_textures", 0U) == 0; };
            const auto textureCount = std::ranges::count(definition.Properties, ShaderPropertyType::Texture2D,
                                                         &ShaderPropertyDefinition::Type);
            const auto fragmentUniformBuffers = fragment.value("uniform_buffers", 0U);
            const bool spatialLighting = definition.SpatialLightingAbiVersion == 2U;
            const auto expectedFragmentUniformBuffers = definition.UsesImageBasedLighting ? 4U : 3U;
            const auto minimumFragmentUniformBuffers = definition.UsesImageBasedLighting ? 4U
                                                       : definition.ReceivesShadows      ? 3U
                                                                                         : 2U;
            const auto expectedSamplers = textureCount + (definition.ReceivesShadows ? 2U : 0U) +
                                          (definition.UsesImageBasedLighting ? 2U : 0U) + (spatialLighting ? 5U : 0U);
            const auto expectedFragmentStorageBuffers = definition.UsesForwardPlus ? 3U : 0U;
            if (!noStorageTextures(vertex) || !noStorageTextures(fragment) ||
                vertex.value("storage_buffers", 0U) != (definition.UsesInstancing ? 1U : 0U) ||
                fragment.value("storage_buffers", 0U) != expectedFragmentStorageBuffers ||
                vertex.value("samplers", 0U) != 0 ||
                vertex.value("uniform_buffers", 0U) != (definition.UsesVertexMaterialParameters ? 2U : 1U) ||
                (fragmentUniformBuffers != minimumFragmentUniformBuffers &&
                 fragmentUniformBuffers != expectedFragmentUniformBuffers) ||
                fragment.value("samplers", 0U) != expectedSamplers)
                throw std::invalid_argument("Shader violates Kéire's fixed graphics resource-binding ABI.");

            constexpr std::array<std::string_view, 6> vertexTypes{"float3", "float3", "float2",
                                                                  "float4", "float4", "float2"};
            const auto expectedInputs = definition.VertexLayoutVersion == 3   ? 6U
                                        : definition.VertexLayoutVersion == 2 ? 5U
                                                                              : 4U;
            if (!vertex.at("inputs").is_array() || vertex.at("inputs").size() != expectedInputs)
                throw std::invalid_argument("Shader vertex inputs do not match the fixed mesh ABI.");
            for (const auto& input : vertex.at("inputs"))
            {
                const auto location = input.at("location").get<std::uint32_t>();
                if (location >= expectedInputs || input.at("type").get<std::string>() != vertexTypes[location])
                    throw std::invalid_argument("Shader vertex inputs do not match the fixed mesh ABI.");
            }

            std::map<std::uint32_t, std::string> outputs;
            for (const auto& output : vertex.at("outputs"))
                outputs.emplace(output.at("location").get<std::uint32_t>(), output.at("type").get<std::string>());
            for (const auto& input : fragment.at("inputs"))
            {
                const auto location = input.at("location").get<std::uint32_t>();
                const auto found = outputs.find(location);
                if (found == outputs.end() || found->second != input.at("type").get<std::string>())
                    throw std::invalid_argument("Vertex and fragment shader stage interfaces are incompatible.");
            }
        }

        [[nodiscard]] std::vector<std::filesystem::path> ParseIncludeRoots(const Json& manifest,
                                                                           const AssetImportContext& context)
        {
            const auto& source = manifest.value("includeRoots", Json::array());
            if (!source.is_array() || source.size() > MaximumIncludeRoots)
                throw std::invalid_argument("Shader includeRoots must be a bounded array.");
            std::vector<std::filesystem::path> result;
            for (const auto& value : source)
            {
                const std::filesystem::path relative = value.get<std::string>();
                const auto normalized = relative.lexically_normal();
                if (relative.empty() || relative.is_absolute() || normalized.generic_string().starts_with(".."))
                    throw std::invalid_argument("Shader include roots must be confined project-relative paths.");
                result.push_back(context.ProjectRoot / normalized);
            }
            return result;
        }

        [[nodiscard]] std::vector<std::pair<std::string, std::string>> ParseDefines(const Json& manifest)
        {
            const auto& source = manifest.value("defines", Json::object());
            if (!source.is_object() || source.size() > MaximumDefines)
                throw std::invalid_argument("Shader defines must be a bounded object.");
            std::vector<std::pair<std::string, std::string>> result;
            for (const auto& [name, value] : source.items())
            {
                const auto text = value.get<std::string>();
                if (!ValidIdentifier(name) || text.size() > 128 || text.find_first_of("\r\n") != std::string::npos)
                    throw std::invalid_argument("Shader define names or values are invalid.");
                result.emplace_back(name, text);
            }
            return result;
        }

        void DiscoverDependencies(const AssetImportContext& context, const std::filesystem::path& relative,
                                  const std::span<const std::filesystem::path> includeRoots,
                                  std::vector<AssetSourceDependency>& output, std::set<std::string>& visiting,
                                  std::set<std::string>& visited)
        {
            const auto normalized = relative.lexically_normal();
            const auto comparable = normalized.generic_string();
            if (normalized.is_absolute() || comparable.starts_with(".."))
                throw std::invalid_argument("Shader dependencies must remain inside the project.");
            if (visiting.contains(comparable))
                throw std::invalid_argument("Shader include graph contains a cycle at " + comparable + ".");
            if (!visited.insert(comparable).second)
                return;
            if (visited.size() > MaximumShaderDependencies)
                throw std::invalid_argument("Shader include graph exceeds its dependency limit.");

            visiting.insert(comparable);
            const auto bytes = context.ReadProjectFile(normalized);
            output.push_back({normalized, Detail::DigestToString(Detail::Sha256(bytes))});
            const std::string text = Text(bytes);
            static const std::regex includePattern(R"((?:^|\n)\s*#\s*include\s*[\"<]([^\">]+)[\">])");
            for (auto match = std::sregex_iterator(text.begin(), text.end(), includePattern);
                 match != std::sregex_iterator(); ++match)
            {
                const std::filesystem::path include = (*match)[1].str();
                if (include.is_absolute())
                    throw std::invalid_argument("Shader includes may not use absolute paths.");
                std::vector<std::filesystem::path> candidates{normalized.parent_path() / include};
                for (const auto& root : includeRoots)
                {
                    const auto relativeRoot = std::filesystem::relative(root, context.ProjectRoot).lexically_normal();
                    const auto includeText = include.lexically_normal().generic_string();
                    const auto rootText = relativeRoot.generic_string();
                    if (rootText == "." || includeText == rootText || includeText.starts_with(rootText + '/'))
                        candidates.push_back(include);
                    candidates.push_back(relativeRoot / include);
                }
                std::optional<std::filesystem::path> resolved;
                for (const auto& candidate : candidates)
                {
                    try
                    {
                        (void)context.ReadProjectFile(candidate.lexically_normal());
                        resolved = candidate.lexically_normal();
                        break;
                    }
                    catch (const std::exception&)
                    {
                    }
                }
                if (!resolved)
                    throw std::invalid_argument("Shader include could not be resolved: " + include.generic_string());
                DiscoverDependencies(context, *resolved, includeRoots, output, visiting, visited);
            }
            visiting.erase(comparable);
        }

        [[nodiscard]] ShaderAssetDefinition ParseShaderManifest(const Json& manifest)
        {
            if (!manifest.is_object() || manifest.value("schemaVersion", 0U) != 1)
                throw std::invalid_argument("Shader manifest has an unsupported schema.");
            ShaderAssetDefinition result;
            result.Source = manifest.at("source").get<std::string>();
            const auto& stages = manifest.at("stages");
            result.VertexEntry = stages.at("vertex").get<std::string>();
            result.FragmentEntry = stages.at("fragment").get<std::string>();
            result.VertexLayoutVersion = manifest.value("vertexLayoutVersion", static_cast<std::uint8_t>(1));
            const auto& state = manifest.value("renderState", Json::object());
            const auto topology = state.value("topology", std::string("TriangleList"));
            const auto culling = state.value("culling", std::string("Back"));
            if (topology != "TriangleList" && topology != "LineList" && topology != "PointList")
                throw std::invalid_argument("Shader render-state topology is invalid.");
            if (culling != "None" && culling != "Front" && culling != "Back")
                throw std::invalid_argument("Shader render-state culling is invalid.");
            result.Topology = topology == "PointList"  ? ShaderPrimitiveTopology::PointList
                              : topology == "LineList" ? ShaderPrimitiveTopology::LineList
                                                       : ShaderPrimitiveTopology::TriangleList;
            result.Culling = culling == "None"    ? ShaderCullMode::None
                             : culling == "Front" ? ShaderCullMode::Front
                                                  : ShaderCullMode::Back;
            result.DepthTest = state.value("depthTest", true);
            result.DepthWrite = state.value("depthWrite", true);
            result.Blend = state.value("blend", false);
            result.ReceivesShadows = manifest.value("receivesShadows", false);
            result.UsesForwardPlus = manifest.value("usesForwardPlus", false);
            result.UsesInstancing = manifest.value("usesInstancing", false);
            result.UsesImageBasedLighting = manifest.value("usesImageBasedLighting", false);
            result.SpatialLightingAbiVersion =
                manifest.value("spatialLightingAbiVersion", static_cast<std::uint8_t>(0));
            result.UsesVertexMaterialParameters = manifest.value("usesVertexMaterialParameters", false);
            if (result.SpatialLightingAbiVersion != 0U && result.SpatialLightingAbiVersion != 2U)
                throw std::invalid_argument("Shader spatial-lighting ABI version is unsupported.");
            if (result.SpatialLightingAbiVersion == 2U &&
                (!result.UsesImageBasedLighting || result.VertexLayoutVersion != 3U))
            {
                throw std::invalid_argument("Spatial-lighting ABI v2 requires image-based lighting and mesh UV1.");
            }

            const auto& properties = manifest.value("properties", Json::array());
            if (!properties.is_array() || properties.size() > MaximumShaderProperties)
                throw std::invalid_argument("Shader properties must be a bounded array.");
            const std::unordered_map<std::string, ShaderPropertyType> types{
                {"Float", ShaderPropertyType::Scalar},    {"Vector2", ShaderPropertyType::Vector2},
                {"Vector3", ShaderPropertyType::Vector3}, {"Vector4", ShaderPropertyType::Vector4},
                {"Color", ShaderPropertyType::Color},     {"Texture2D", ShaderPropertyType::Texture2D}};
            const std::unordered_map<std::string, ShaderTextureSemantic> semantics{
                {"Generic", ShaderTextureSemantic::Generic},
                {"BaseColor", ShaderTextureSemantic::BaseColor},
                {"Normal", ShaderTextureSemantic::Normal},
                {"MetallicRoughness", ShaderTextureSemantic::MetallicRoughness},
                {"Occlusion", ShaderTextureSemantic::Occlusion},
                {"Emissive", ShaderTextureSemantic::Emissive},
                {"Metallic", ShaderTextureSemantic::Metallic},
                {"Roughness", ShaderTextureSemantic::Roughness}};
            for (const auto& property : properties)
            {
                const auto typeName = property.at("type").get<std::string>();
                const auto found = types.find(typeName);
                if (found == types.end())
                    throw std::invalid_argument("Shader property type is invalid: " + typeName);
                ShaderPropertyDefinition definition;
                definition.Name = property.at("name").get<std::string>();
                definition.Type = found->second;
                definition.DisplayName = property.value("displayName", std::string{});
                definition.Category = property.value("category", std::string{});
                if (property.contains("minimum"))
                    definition.Minimum = property.at("minimum").get<float>();
                if (property.contains("maximum"))
                    definition.Maximum = property.at("maximum").get<float>();
                if (property.contains("step"))
                    definition.Step = property.at("step").get<float>();
                if (definition.Type == ShaderPropertyType::Texture2D)
                {
                    definition.DefaultTexture = !property.contains("default") || property.at("default").is_null()
                                                    ? AssetId{}
                                                    : AssetId::Parse(property.at("default").get<std::string>());
                    const auto semanticName = property.value("semantic", std::string("Generic"));
                    const auto semantic = semantics.find(semanticName);
                    if (semantic == semantics.end())
                        throw std::invalid_argument("Shader texture property semantic is invalid: " + semanticName);
                    definition.TextureSemantic = semantic->second;
                }
                else
                    definition.DefaultValue = ParseVector(property.at("default"));
                result.Properties.push_back(std::move(definition));
            }
            ValidateDefinition(result, false, true);
            return result;
        }

        [[nodiscard]] AssetTargetPlatform ResolveHostTarget(const AssetTargetPlatform target) noexcept
        {
            if (target != AssetTargetPlatform::Host)
                return target;
#if defined(_WIN32)
            return AssetTargetPlatform::Windows;
#elif defined(__APPLE__)
            return AssetTargetPlatform::MacOS;
#else
            return AssetTargetPlatform::Linux;
#endif
        }
    } // namespace

    ShaderAsset::ShaderAsset(ShaderAssetDefinition definition) : m_Definition(std::move(definition)) {}

    std::size_t ShaderAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this);
        for (const auto& variant : m_Definition.Variants)
            result += variant.Vertex.size() + variant.Fragment.size();
        return result;
    }

    const ShaderVariant* ShaderAsset::Variant(const ShaderBinaryFormat format) const noexcept
    {
        const auto found = std::ranges::find(m_Definition.Variants, format, &ShaderVariant::Format);
        return found == m_Definition.Variants.end() ? nullptr : &*found;
    }

    Ref<ShaderAsset> ShaderAsset::Decode(const std::span<const std::byte> bytes)
    {
        try
        {
            return CreateRef<ShaderAsset>(DecodeShaderJson(Json::from_cbor(ToUnsigned(bytes))));
        }
        catch (const std::exception& error)
        {
            throw std::invalid_argument(std::string("Shader asset decode failed: ") + error.what());
        }
    }

    std::vector<std::byte> ShaderAsset::Encode(const ShaderAssetDefinition& definition)
    {
        ValidateDefinition(definition, true);
        return ToBytes(Json::to_cbor(EncodeShaderJson(definition)));
    }

    ShaderAssetDefinition ShaderAsset::DecodeManifest(const std::span<const std::byte> bytes)
    {
        return ParseShaderManifest(Json::parse(Text(bytes)));
    }

    Ref<ShaderAsset> ShaderAsset::Error() { return CreateRef<ShaderAsset>(); }

    void MaterialAssetDefinition::SetTexture(std::string name, const AssetId texture)
    {
        if (!ValidIdentifier(name))
            throw std::invalid_argument("Material texture property name is invalid.");
        Properties.insert_or_assign(std::move(name), texture);
    }

    bool MaterialAssetDefinition::RemoveTexture(const std::string_view name)
    {
        const auto found = Properties.find(name);
        if (found == Properties.end() || !std::holds_alternative<AssetId>(found->second))
            return false;
        Properties.erase(found);
        return true;
    }

    std::optional<AssetId> MaterialAssetDefinition::Texture(const std::string_view name) const
    {
        const auto found = Properties.find(name);
        if (found == Properties.end())
            return std::nullopt;
        const auto* texture = std::get_if<AssetId>(&found->second);
        return texture ? std::optional<AssetId>(*texture) : std::nullopt;
    }

    MaterialAsset::MaterialAsset(MaterialAssetDefinition definition) : m_Definition(std::move(definition)) {}

    std::size_t MaterialAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this);
        for (const auto& [name, value] : m_Definition.Properties)
        {
            (void)value;
            result += name.size() + sizeof(MaterialPropertyValue);
        }
        return result;
    }

    Ref<MaterialAsset> MaterialAsset::Decode(const std::span<const std::byte> bytes)
    {
        const auto source = Json::from_cbor(ToUnsigned(bytes));
        if (!source.is_object())
            throw std::invalid_argument("Material asset has an unsupported schema.");
        MaterialAssetDefinition result;
        result.SchemaVersion = source.value("schemaVersion", 0U);
        if (result.SchemaVersion < 1 || result.SchemaVersion > 3)
            throw std::invalid_argument("Material asset has an unsupported schema.");
        result.Shader =
            source.at("shader").is_null() ? AssetId{} : AssetId::Parse(source.at("shader").get<std::string>());
        if (result.SchemaVersion >= 2)
        {
            const auto& surface = source.at("surface");
            result.Surface.AlphaMode = static_cast<MaterialAlphaMode>(surface.at("alphaMode").get<std::uint8_t>());
            result.Surface.AlphaCutoff = surface.at("alphaCutoff").get<float>();
            result.Surface.DoubleSided = surface.at("doubleSided").get<bool>();
        }
        if (result.SchemaVersion >= 3)
        {
            const auto& lighting = source.at("bakedLighting");
            result.ContributeEmissionToGI = lighting.at("contributeEmission").get<bool>();
            result.EmissiveGIIntensity = lighting.at("emissiveIntensity").get<float>();
        }
        for (const auto& [name, property] : source.at("properties").items())
        {
            const auto type = property.at("type").get<std::uint8_t>();
            if (type == 5)
            {
                result.Properties.emplace(name, property.at("value").is_null()
                                                    ? AssetId{}
                                                    : AssetId::Parse(property.at("value").get<std::string>()));
                continue;
            }
            const auto value = ParseVector(property.at("value"));
            switch (type)
            {
            case 0:
                result.Properties.emplace(name, value.X);
                break;
            case 1:
                result.Properties.emplace(name, Vector2{value.X, value.Y});
                break;
            case 2:
                result.Properties.emplace(name, Vector3{value.X, value.Y, value.Z});
                break;
            case 3:
                result.Properties.emplace(name, value);
                break;
            case 4:
                result.Properties.emplace(name, Color{value.X, value.Y, value.Z, value.W});
                break;
            default:
                throw std::invalid_argument("Material property type is invalid.");
            }
        }
        ValidateMaterialDefinition(result);
        return CreateRef<MaterialAsset>(std::move(result));
    }

    std::vector<std::byte> MaterialAsset::Encode(const MaterialAssetDefinition& definition)
    {
        ValidateMaterialDefinition(definition);
        Json properties = Json::object();
        for (const auto& [name, value] : definition.Properties)
        {
            std::visit(
                [&](const auto& typed)
                {
                    using T = std::decay_t<decltype(typed)>;
                    if constexpr (std::same_as<T, AssetId>)
                        properties[name] = {{"type", 5}, {"value", typed ? Json(typed.ToString()) : Json(nullptr)}};
                    else
                    {
                        Vector4 packed;
                        std::uint8_t type = 0;
                        if constexpr (std::same_as<T, float>)
                            packed.X = typed;
                        else if constexpr (std::same_as<T, Vector2>)
                        {
                            packed = {typed.X, typed.Y, 0.0F, 0.0F};
                            type = 1;
                        }
                        else if constexpr (std::same_as<T, Vector3>)
                        {
                            packed = {typed.X, typed.Y, typed.Z, 0.0F};
                            type = 2;
                        }
                        else if constexpr (std::same_as<T, Vector4>)
                        {
                            packed = typed;
                            type = 3;
                        }
                        else
                        {
                            packed = {typed.Red, typed.Green, typed.Blue, typed.Alpha};
                            type = 4;
                        }
                        if (!Math::IsFinite(packed))
                            throw std::invalid_argument("Material property value is not finite.");
                        properties[name] = {{"type", type}, {"value", Vector(packed)}};
                    }
                },
                value);
        }
        Json source{{"schemaVersion", definition.SchemaVersion},
                    {"shader", definition.Shader ? Json(definition.Shader.ToString()) : Json(nullptr)},
                    {"properties", std::move(properties)}};
        if (definition.SchemaVersion >= 2)
            source["surface"] = {{"alphaMode", static_cast<std::uint8_t>(definition.Surface.AlphaMode)},
                                 {"alphaCutoff", definition.Surface.AlphaCutoff},
                                 {"doubleSided", definition.Surface.DoubleSided}};
        if (definition.SchemaVersion >= 3)
            source["bakedLighting"] = {{"contributeEmission", definition.ContributeEmissionToGI},
                                       {"emissiveIntensity", definition.EmissiveGIIntensity}};
        return ToBytes(Json::to_cbor(source));
    }

    MaterialAssetDefinition MaterialAsset::DecodeSource(const std::span<const std::byte> bytes)
    {
        return ParseMaterialSource(Json::parse(Text(bytes)));
    }

    std::vector<std::byte> MaterialAsset::EncodeSource(const MaterialAssetDefinition& definition)
    {
        ValidateMaterialDefinition(definition);
        Json properties = Json::object();
        for (const auto& [name, value] : definition.Properties)
        {
            std::visit(
                [&](const auto& typed)
                {
                    using T = std::decay_t<decltype(typed)>;
                    if constexpr (std::same_as<T, AssetId>)
                        properties[name] = typed ? Json(typed.ToString()) : Json(nullptr);
                    else if constexpr (std::same_as<T, float>)
                        properties[name] = typed;
                    else if constexpr (std::same_as<T, Vector2>)
                        properties[name] = Json::array({typed.X, typed.Y, 0.0F, 0.0F});
                    else if constexpr (std::same_as<T, Vector3>)
                        properties[name] = Json::array({typed.X, typed.Y, typed.Z, 0.0F});
                    else if constexpr (std::same_as<T, Vector4>)
                        properties[name] = Vector(typed);
                    else
                        properties[name] = Json::array({typed.Red, typed.Green, typed.Blue, typed.Alpha});
                },
                value);
        }
        Json source{{"schemaVersion", definition.SchemaVersion},
                    {"shader", definition.Shader ? Json(definition.Shader.ToString()) : Json(nullptr)},
                    {"properties", std::move(properties)}};
        if (definition.SchemaVersion >= 2)
            source["surface"] = {{"alphaMode", static_cast<std::uint8_t>(definition.Surface.AlphaMode)},
                                 {"alphaCutoff", definition.Surface.AlphaCutoff},
                                 {"doubleSided", definition.Surface.DoubleSided}};
        if (definition.SchemaVersion >= 3)
            source["bakedLighting"] = {{"contributeEmission", definition.ContributeEmissionToGI},
                                       {"emissiveIntensity", definition.EmissiveGIIntensity}};
        const auto text = source.dump(2) + '\n';
        return ToBytes(text);
    }

    MaterialAuthoringDefinition MaterialAsset::DecodeAuthoringSource(const std::span<const std::byte> bytes)
    {
        return ParseMaterialAuthoringSource(Json::parse(Text(bytes)));
    }

    std::vector<std::byte> MaterialAsset::EncodeAuthoringSource(const MaterialAuthoringDefinition& definition)
    {
        ValidateMaterialAuthoringDefinition(definition);
        const auto kind = definition.Shader.Kind == MaterialShaderSourceKind::Builtin       ? "builtin"
                          : definition.Shader.Kind == MaterialShaderSourceKind::ShaderGraph ? "graph"
                                                                                            : "asset";
        Json keywords = Json::object();
        for (const auto& [name, option] : definition.Shader.Keywords)
            keywords[name] = option;
        Json shader{{"kind", kind}, {"asset", definition.Shader.Asset.ToString()}};
        if (definition.Shader.Kind == MaterialShaderSourceKind::ShaderGraph)
        {
            shader["target"] = definition.Shader.Target;
            shader["keywords"] = std::move(keywords);
        }
        Json properties = Json::object();
        for (const auto& [name, value] : definition.Properties)
            properties[name] = {{"type", value.index()}, {"value", EncodeMaterialProperty(value)}};
        const Json source{{"schemaVersion", definition.SchemaVersion},
                          {"shader", std::move(shader)},
                          {"surface",
                           {{"alphaMode", static_cast<std::uint8_t>(definition.Surface.AlphaMode)},
                            {"alphaCutoff", definition.Surface.AlphaCutoff},
                            {"doubleSided", definition.Surface.DoubleSided}}},
                          {"bakedLighting",
                           {{"contributeEmission", definition.ContributeEmissionToGI},
                            {"emissiveIntensity", definition.EmissiveGIIntensity}}},
                          {"properties", std::move(properties)}};
        const auto text = source.dump(2) + '\n';
        return ToBytes(text);
    }

    Ref<MaterialAsset> MaterialAsset::Error()
    {
        MaterialAssetDefinition definition;
        definition.Properties.emplace("ErrorColor", Color{1.0F, 0.0F, 1.0F, 1.0F});
        return CreateRef<MaterialAsset>(std::move(definition));
    }

    void ValidateMaterialAgainstShader(const MaterialAssetDefinition& material, const ShaderAssetDefinition& shader)
    {
        ValidateMaterialDefinition(material);
        ValidateDefinition(shader, false, true);
        for (const auto& [name, value] : material.Properties)
        {
            const auto found = std::ranges::find(shader.Properties, name, &ShaderPropertyDefinition::Name);
            if (found == shader.Properties.end())
                throw std::invalid_argument("Material property is not declared by its shader: " + name);
            const bool correctType =
                (found->Type == ShaderPropertyType::Scalar && std::holds_alternative<float>(value)) ||
                (found->Type == ShaderPropertyType::Vector2 &&
                 (std::holds_alternative<Vector2>(value) || std::holds_alternative<Vector4>(value))) ||
                (found->Type == ShaderPropertyType::Vector3 &&
                 (std::holds_alternative<Vector3>(value) || std::holds_alternative<Vector4>(value))) ||
                (found->Type == ShaderPropertyType::Vector4 &&
                 (std::holds_alternative<Vector4>(value) || std::holds_alternative<Color>(value))) ||
                (found->Type == ShaderPropertyType::Color &&
                 (std::holds_alternative<Color>(value) || std::holds_alternative<Vector4>(value))) ||
                (found->Type == ShaderPropertyType::Texture2D && std::holds_alternative<AssetId>(value));
            if (!correctType)
                throw std::invalid_argument("Material property type does not match its shader declaration: " + name);
            if (found->Type != ShaderPropertyType::Texture2D && (found->Minimum || found->Maximum))
            {
                std::array<float, 4> components{};
                std::size_t count = 0;
                std::visit(
                    [&](const auto& typed)
                    {
                        using T = std::decay_t<decltype(typed)>;
                        if constexpr (std::same_as<T, float>)
                        {
                            components[0] = typed;
                            count = 1;
                        }
                        else if constexpr (std::same_as<T, Vector2>)
                        {
                            components = {typed.X, typed.Y, 0.0F, 0.0F};
                            count = 2;
                        }
                        else if constexpr (std::same_as<T, Vector3>)
                        {
                            components = {typed.X, typed.Y, typed.Z, 0.0F};
                            count = 3;
                        }
                        else if constexpr (std::same_as<T, Vector4>)
                        {
                            components = {typed.X, typed.Y, typed.Z, typed.W};
                            count = 4;
                        }
                        else if constexpr (std::same_as<T, Color>)
                        {
                            components = {typed.Red, typed.Green, typed.Blue, typed.Alpha};
                            count = 4;
                        }
                    },
                    value);
                for (std::size_t component = 0; component < count; ++component)
                {
                    if ((found->Minimum && components[component] < *found->Minimum) ||
                        (found->Maximum && components[component] > *found->Maximum))
                        throw std::invalid_argument("Material property is outside its shader-declared range: " + name);
                }
            }
        }
    }

    AssetImporterRegistration CreateShaderAssetImporter(ShaderImporterSpecification specification)
    {
        if (specification.Timeout.count() <= 0 || specification.MaximumOutputBytes == 0 ||
            specification.MaximumOutputBytes > 256U * 1024U * 1024U)
            throw std::invalid_argument("Shader importer limits are invalid.");
        if (specification.Formats.empty() || specification.Formats.size() > 3 ||
            !std::ranges::all_of(specification.Formats,
                                 [](const ShaderBinaryFormat format) { return format <= ShaderBinaryFormat::Msl; }) ||
            std::ranges::find(specification.Formats, ShaderBinaryFormat::SpirV) == specification.Formats.end())
            throw std::invalid_argument("Shader importer formats must be unique and include SPIR-V reflection data.");
        auto uniqueFormats = specification.Formats;
        std::ranges::sort(uniqueFormats);
        if (std::ranges::adjacent_find(uniqueFormats) != uniqueFormats.end())
            throw std::invalid_argument("Shader importer formats must be unique and include SPIR-V reflection data.");
        AssetImporterRegistration result;
        result.Name = "Keire.Shader";
        result.Version = 2;
        result.Type = ShaderAsset::StaticType();
        result.Extensions = {".keireshader"};
        result.ContextualImport =
            [specification = std::move(specification)](const AssetImportContext& context,
                                                       const std::span<const std::byte> bytes) -> AssetImportOutput
        {
            const auto manifest = Json::parse(Text(bytes));
            auto definition = ParseShaderManifest(manifest);
            const auto compiler = ResolveCompiler(specification);
            if (compiler.empty() || !std::filesystem::is_regular_file(compiler))
                throw std::runtime_error(
                    "KeireShaderCompiler is unavailable. Run project bootstrap or set KEIRE_SHADER_COMPILER.");

            const auto includeRoots = ParseIncludeRoots(manifest, context);
            const auto defines = ParseDefines(manifest);
            std::set<std::string> visiting;
            std::set<std::string> visited;
            DiscoverDependencies(context, definition.Source, includeRoots, definition.Dependencies, visiting, visited);

            TemporaryShaderDirectory temporary;
            const auto stagedRoot = temporary.Path() / "Source";
            for (const auto& dependency : definition.Dependencies)
            {
                const auto destination = stagedRoot / dependency.RelativePath;
                std::filesystem::create_directories(destination.parent_path());
                const auto dependencyBytes = context.ReadProjectFile(dependency.RelativePath);
                std::ofstream output(destination, std::ios::binary | std::ios::trunc);
                if (!output ||
                    (!dependencyBytes.empty() && !output.write(reinterpret_cast<const char*>(dependencyBytes.data()),
                                                               static_cast<std::streamsize>(dependencyBytes.size()))))
                    throw std::runtime_error("Could not stage a shader dependency for compilation.");
            }
            const auto source = stagedRoot / definition.Source;
            std::vector<std::filesystem::path> stagedIncludeRoots;
            stagedIncludeRoots.reserve(includeRoots.size());
            for (const auto& includeRoot : includeRoots)
                stagedIncludeRoots.push_back(stagedRoot / std::filesystem::relative(includeRoot, context.ProjectRoot));
            for (const auto format : specification.Formats)
            {
                const auto name = format == ShaderBinaryFormat::Dxil    ? std::string_view("DXIL")
                                  : format == ShaderBinaryFormat::SpirV ? std::string_view("SPIRV")
                                                                        : std::string_view("MSL");
                const auto extension = format == ShaderBinaryFormat::Msl ? ".metal" : ".bin";
                const auto vertexPath = temporary.Path() / (std::string("vertex-") + std::string(name) + extension);
                const auto fragmentPath = temporary.Path() / (std::string("fragment-") + std::string(name) + extension);
                definition.Variants.push_back(
                    {format,
                     Compile(compiler, source, name, "vertex", definition.VertexEntry, vertexPath, stagedIncludeRoots,
                             defines, specification, temporary.Path()),
                     Compile(compiler, source, name, "fragment", definition.FragmentEntry, fragmentPath,
                             stagedIncludeRoots, defines, specification, temporary.Path())});
            }

            const auto& spirv =
                *std::ranges::find(definition.Variants, ShaderBinaryFormat::SpirV, &ShaderVariant::Format);
            const auto vertexSpirv = temporary.Path() / "reflection.vert.spv";
            const auto fragmentSpirv = temporary.Path() / "reflection.frag.spv";
            std::ofstream(vertexSpirv, std::ios::binary)
                .write(reinterpret_cast<const char*>(spirv.Vertex.data()),
                       static_cast<std::streamsize>(spirv.Vertex.size()));
            std::ofstream(fragmentSpirv, std::ios::binary)
                .write(reinterpret_cast<const char*>(spirv.Fragment.data()),
                       static_cast<std::streamsize>(spirv.Fragment.size()));
            const auto vertexReflection = temporary.Path() / "vertex.json";
            const auto fragmentReflection = temporary.Path() / "fragment.json";
            RunCompiler(
                compiler,
                {Utf8Path(vertexSpirv), "-s", "SPIRV", "-d", "JSON", "-t", "vertex", "-o", Utf8Path(vertexReflection)},
                temporary.Path(), specification.Timeout);
            RunCompiler(compiler,
                        {Utf8Path(fragmentSpirv), "-s", "SPIRV", "-d", "JSON", "-t", "fragment", "-o",
                         Utf8Path(fragmentReflection)},
                        temporary.Path(), specification.Timeout);
            ValidateReflection(Json::parse(Text(ReadFile(vertexReflection, specification.MaximumOutputBytes))),
                               Json::parse(Text(ReadFile(fragmentReflection, specification.MaximumOutputBytes))),
                               definition);
            ValidateDefinition(definition, specification.Formats.size() == 3);
            return {ToBytes(Json::to_cbor(EncodeShaderJson(definition))), definition.Dependencies};
        };
        result.Cook = [](const std::span<const std::byte> bytes, const AssetTargetPlatform requested)
        {
            auto definition = ShaderAsset::Decode(bytes)->Definition();
            const auto target = ResolveHostTarget(requested);
            const auto required = [target](const ShaderBinaryFormat format)
            {
                if (target == AssetTargetPlatform::Windows)
                    return format == ShaderBinaryFormat::Dxil || format == ShaderBinaryFormat::SpirV;
                if (target == AssetTargetPlatform::MacOS)
                    return format == ShaderBinaryFormat::Msl;
                return format == ShaderBinaryFormat::SpirV;
            };
            std::erase_if(definition.Variants,
                          [&required](const ShaderVariant& variant) { return !required(variant.Format); });
            const std::size_t expected = target == AssetTargetPlatform::Windows ? 2U : 1U;
            if (definition.Variants.size() != expected)
                throw std::runtime_error("Shader asset does not contain the requested target variant.");
            return ToBytes(Json::to_cbor(EncodeShaderJson(definition)));
        };
        return result;
    }

    AssetImporterRegistration CreateMaterialAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.Material";
        result.Version = 5;
        result.Type = MaterialAsset::StaticType();
        result.Extensions = {".keirematerial"};
        result.ContextualImport = [](const AssetImportContext& context, const std::span<const std::byte> bytes)
        {
            const auto source = MaterialAsset::DecodeAuthoringSource(bytes);
            MaterialAssetDefinition definition;
            definition.Surface = source.Surface;
            definition.ContributeEmissionToGI = source.ContributeEmissionToGI;
            definition.EmissiveGIIntensity = source.EmissiveGIIntensity;
            definition.Properties = source.Properties;
            if (source.Shader.Kind == MaterialShaderSourceKind::ShaderGraph)
            {
                if (!context.ResolveSubAssetIdFor)
                    throw std::invalid_argument(
                        "Shader Graph material references require a stable cross-asset subasset resolver.");
                const auto variant = ResolveShaderGraphVariant(context, source.Shader);
                definition.Shader = context.ResolveSubAssetIdFor(
                    variant.Owner, MakeShaderGraphVariantSubAssetKey(source.Shader.Target, variant.Keywords));
            }
            else
                definition.Shader = source.Shader.Asset;
            AssetImportOutput output;
            output.Bytes = MaterialAsset::Encode(definition);
            if (source.Shader.Asset)
                output.AssetDependencies.push_back(source.Shader.Asset);
            if (definition.Shader != source.Shader.Asset)
                output.AssetDependencies.push_back(definition.Shader);
            for (const auto& [name, value] : definition.Properties)
            {
                (void)name;
                if (const auto* texture = std::get_if<AssetId>(&value); texture && *texture)
                    output.AssetDependencies.push_back(*texture);
            }
            std::ranges::sort(output.AssetDependencies);
            output.AssetDependencies.erase(
                std::unique(output.AssetDependencies.begin(), output.AssetDependencies.end()),
                output.AssetDependencies.end());
            return output;
        };
        return result;
    }

    AssetDecoderRegistration CreateShaderAssetDecoder()
    {
        return {ShaderAsset::StaticType(), ShaderAsset::Error(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return ShaderAsset::Decode(bytes); }};
    }

    AssetDecoderRegistration CreateMaterialAssetDecoder()
    {
        return {MaterialAsset::StaticType(), MaterialAsset::Error(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return MaterialAsset::Decode(bytes); }};
    }
} // namespace Keire
