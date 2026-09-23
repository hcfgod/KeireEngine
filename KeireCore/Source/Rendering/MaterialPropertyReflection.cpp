#include "KeireInternal/Rendering/MaterialPropertyReflection.h"

#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Keire::Detail
{
    ShaderAssetDefinition ReflectMaterialProperties(const ShaderGraphDefinition& graph,
                                                    const std::filesystem::path& source)
    {
        ShaderAssetDefinition result;
        result.Source = source;
        result.ProgramTarget = ShaderGraphTargetName(graph.Target.Target);
        for (const auto& keyword : graph.Keywords)
            result.Keywords.push_back(keyword.Name);
        for (const auto& node : graph.Nodes)
        {
            if (node.Kind != ShaderGraphNodeKind::Parameter)
                continue;
            ShaderPropertyDefinition property;
            property.Id = node.Id;
            property.Name = node.Symbol;
            property.DisplayName = node.Name;
            property.Category = node.ParameterMetadata.Category;
            property.Description = node.ParameterMetadata.Description;
            property.HighDynamicRange = node.ParameterMetadata.HighDynamicRange;
            property.TextureSemantic = node.TextureSemantic;
            property.TextureTransformProperty = node.ParameterMetadata.TextureTransformProperty;
            property.Type = static_cast<ShaderPropertyType>(node.ValueType);
            property.Minimum = node.ParameterMetadata.Minimum;
            property.Maximum = node.ParameterMetadata.Maximum;
            property.Step = node.ParameterMetadata.Step;
            std::visit(
                [&](const auto& value)
                {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, float>)
                        property.DefaultValue.X = value;
                    else if constexpr (std::is_same_v<T, Vector2>)
                        property.DefaultValue = {value.X, value.Y, 0.0F, 0.0F};
                    else if constexpr (std::is_same_v<T, Vector3>)
                        property.DefaultValue = {value.X, value.Y, value.Z, 0.0F};
                    else if constexpr (std::is_same_v<T, Vector4>)
                        property.DefaultValue = value;
                    else if constexpr (std::is_same_v<T, Color>)
                        property.DefaultValue = {value.Red, value.Green, value.Blue, value.Alpha};
                    else if constexpr (std::is_same_v<T, AssetId>)
                        property.DefaultTexture = value;
                },
                node.Value);
            result.Properties.push_back(std::move(property));
        }
        return result;
    }

    ShaderAssetDefinition ReadMaterialPropertyReflection(const AssetImportContext& context,
                                                         const MaterialShaderReference& reference)
    {
        if (!context.ResolveAssetSource || !context.ReadProjectFile || context.SourceRoot.empty() ||
            context.ProjectRoot.empty())
            throw std::invalid_argument("Material reflection requires project source resolvers.");
        const auto source = context.ResolveAssetSource(reference.Asset);
        if (!source)
            throw std::runtime_error("Material shader source is unavailable.");
        const auto prefix = std::filesystem::relative(context.SourceRoot, context.ProjectRoot);
        const auto bytes = context.ReadProjectFile(prefix / source->RelativePath);
        if (reference.Kind != MaterialShaderSourceKind::ShaderGraph)
        {
            if (source->Type != ShaderAsset::StaticType())
                throw std::invalid_argument("Material requires a compatible code shader.");
            return ShaderAsset::DecodeManifest(bytes);
        }
        if (source->Type != ShaderGraphAsset::StaticType())
            throw std::invalid_argument("Material requires a compatible Shader Graph.");
        const auto graph = ShaderGraphAsset::DecodeSource(bytes);
        if (graph.Target.Target == ShaderGraphTarget::Compute)
            throw std::invalid_argument("Material properties require a graphics shader target.");
        return ReflectMaterialProperties(graph, source->RelativePath);
    }

    MaterialAssetDefinition ResolveMaterialVariant(const MaterialAssetDefinition& parent,
                                                   const MaterialInstanceDefinition& instance,
                                                   const ShaderAssetDefinition& shader)
    {
        ValidateMaterialInstance(instance);
        auto settings = instance;
        settings.Properties.clear();
        settings.PropertyOverrides.clear();
        auto result = BakeMaterialInstance(parent, settings);
        MaterialAuthoringDefinition source;
        source.SchemaVersion = 5;
        source.Shader.Asset = parent.Shader;
        source.Properties = instance.Properties;
        source.PropertyOverrides = instance.PropertyOverrides;
        const auto resolved = ResolveMaterialProperties(source, shader);
        for (const auto& [name, value] : resolved.Properties)
            result.Properties.insert_or_assign(name, value);
        ValidateMaterialAgainstShader(result, shader);
        return result;
    }
} // namespace Keire::Detail
