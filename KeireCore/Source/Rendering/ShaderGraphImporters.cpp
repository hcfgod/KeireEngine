#include "Keire/Rendering/MaterialEcosystem.h"
#include "Keire/Rendering/ShaderGraph.h"

#include "KeireInternal/Rendering/ShaderGraphCompilerInternal.h"

#include <algorithm>
#include <array>
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
        [[nodiscard]] std::vector<std::byte> TextBytes(const std::string_view value)
        {
            const auto bytes = std::as_bytes(std::span(value.data(), value.size()));
            return {bytes.begin(), bytes.end()};
        }
    } // namespace

    AssetImporterRegistration CreateShaderGraphAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.ShaderGraph";
        result.Version = 19;
        result.Type = ShaderGraphAsset::StaticType();
        result.Extensions = {".keireshadergraph"};
        result.ContextualImport = [](const AssetImportContext& context, const std::span<const std::byte> bytes)
        {
            if (!context.Asset || context.ProjectRoot.empty() || context.SourceRoot.empty() ||
                !context.ReadProjectFile || !context.ResolveSubAssetId)
            {
                throw std::invalid_argument(
                    "Shader Graph import requires a complete project context and stable subasset resolver.");
            }
            const auto definition = ShaderGraphAsset::DecodeSource(bytes);
            if (definition.GeneratedAssetOwner && !context.ResolveSubAssetIdFor)
                throw std::invalid_argument("Migrated Shader Graph import requires a cross-asset subasset resolver.");
            AssetImportOutput output;
            output.Bytes = ShaderGraphAsset::Encode(definition);
            for (const auto& node : definition.Nodes)
                if (node.Kind == ShaderGraphNodeKind::Parameter && node.ValueType == ShaderGraphValueType::Texture2D)
                {
                    const auto texture = std::get<AssetId>(node.Value);
                    if (texture)
                        output.AssetDependencies.push_back(texture);
                }
            const auto functionDependencies = ShaderGraphReferencedAssets(definition);
            output.AssetDependencies.insert(output.AssetDependencies.end(), functionDependencies.begin(),
                                            functionDependencies.end());
            const auto resourceDependencies = ShaderGraphResourceDependencies(definition.Resources);
            output.AssetDependencies.insert(output.AssetDependencies.end(), resourceDependencies.begin(),
                                            resourceDependencies.end());
            std::ranges::sort(output.AssetDependencies);
            output.AssetDependencies.erase(
                std::unique(output.AssetDependencies.begin(), output.AssetDependencies.end()),
                output.AssetDependencies.end());

            ShaderGraphCompileOptions compileOptions;
            compileOptions.GeneratedSource = std::filesystem::relative(context.SourceRoot, context.ProjectRoot) /
                                             "Generated" / "ShaderGraphs" / context.Asset.ToString() /
                                             "ShaderGraph.hlsl";
            compileOptions.ReadInclude =
                [&context](const std::filesystem::path& requested) -> std::optional<std::string>
            {
                try
                {
                    const auto include = context.ReadProjectFile(requested);
                    return std::string(reinterpret_cast<const char*>(include.data()), include.size());
                }
                catch (...)
                {
                    return std::nullopt;
                }
            };
            compileOptions.ResolveFunction = [&context](const AssetId asset) -> std::optional<ShaderGraphDefinition>
            {
                if (!context.ResolveAssetSource || !context.ReadProjectFile)
                    return std::nullopt;
                const auto source = context.ResolveAssetSource(asset);
                if (!source)
                    return std::nullopt;
                const auto sourcePrefix = std::filesystem::relative(context.SourceRoot, context.ProjectRoot);
                const auto functionBytes = context.ReadProjectFile(sourcePrefix / source->RelativePath);
                if (source->Type == MaterialFunctionAsset::StaticType())
                    return MaterialFunctionAsset::DecodeSource(functionBytes).Body;
                if (source->Type == ShaderFunctionAsset::StaticType())
                    return ShaderFunctionAsset::DecodeSource(functionBytes).Body;
                if (source->Type == MaterialLayerAsset::StaticType())
                    return MaterialLayerAsset::DecodeSource(functionBytes).Body;
                if (source->Type == MaterialLayerBlendAsset::StaticType())
                    return MaterialLayerBlendAsset::DecodeSource(functionBytes).Body;
                return std::nullopt;
            };
            const auto compilation = CompileShaderGraph(definition, compileOptions);
            if (!compilation.Succeeded() || compilation.Variants.empty())
            {
                const auto diagnostic = compilation.Diagnostics.empty()
                                            ? std::string("Shader Graph generated no shader variants.")
                                            : compilation.Diagnostics.front().Message;
                throw std::runtime_error("Shader Graph program compilation failed: " + diagnostic);
            }

            const auto shaderImporter = CreateShaderAssetImporter();
            if (!shaderImporter.ContextualImport)
                throw std::logic_error("Shader Graph import requires the contextual shader importer.");
            std::vector<std::pair<std::vector<std::string>, AssetId>> shaderVariants;
            shaderVariants.reserve(compilation.Variants.size());
            std::set<std::filesystem::path> sourceDependencies;
            for (const auto& variant : compilation.Variants)
            {
                const auto shaderKey = "shader/" + variant.StableSuffix;
                const auto shaderId = definition.GeneratedAssetOwner
                                          ? context.ResolveSubAssetIdFor(definition.GeneratedAssetOwner, shaderKey)
                                          : context.ResolveSubAssetId(shaderKey);
                auto shaderContext = context;
                shaderContext.Asset = shaderId;
                shaderContext.RelativePath = variant.GeneratedSource;
                shaderContext.RelativePath.replace_extension(".keireshader");
                shaderContext.SourcePath = context.ProjectRoot / shaderContext.RelativePath;
                shaderContext.MetadataPath = shaderContext.SourcePath;
                shaderContext.MetadataPath += ".keiremeta";
                shaderContext.ReadProjectFile =
                    [readProjectFile = context.ReadProjectFile, generatedSource = variant.GeneratedSource,
                     generatedBytes = TextBytes(variant.Hlsl)](const std::filesystem::path& requested)
                {
                    if (requested.lexically_normal() == generatedSource.lexically_normal())
                        return generatedBytes;
                    return readProjectFile(requested);
                };
                const auto importedShader = shaderImporter.ContextualImport(shaderContext, TextBytes(variant.Manifest));
                for (const auto& dependency : importedShader.SourceDependencies)
                {
                    if (dependency.RelativePath.lexically_normal() == variant.GeneratedSource.lexically_normal() ||
                        !sourceDependencies.insert(dependency.RelativePath.lexically_normal()).second)
                    {
                        continue;
                    }
                    output.SourceDependencies.push_back(dependency);
                }
                output.SubAssets.push_back({shaderId, ShaderAsset::StaticType(), shaderKey,
                                            variant.Keywords.empty() ? "Default Shader" : "Shader " + shaderKey,
                                            importedShader.Bytes, importedShader.AssetDependencies});
                shaderVariants.emplace_back(variant.Keywords, shaderId);
            }

            if (definition.Target.Target != ShaderGraphTarget::LegacySurface)
                return output;

            ShaderGraphInstanceDefinition defaults;
            defaults.Parent = context.Asset;
            const std::array ancestry{defaults};
            const auto resolved = ResolveShaderGraphInstance(definition, ancestry);
            const auto material = BakeShaderGraphInstance(
                definition, resolved,
                [&shaderVariants](const std::span<const std::string> keywords)
                {
                    const auto found = std::ranges::find_if(shaderVariants, [keywords](const auto& variant)
                                                            { return std::ranges::equal(variant.first, keywords); });
                    return found == shaderVariants.end() ? AssetId{} : found->second;
                });
            auto materialDependencies = output.AssetDependencies;
            materialDependencies.push_back(material.Shader);
            std::ranges::sort(materialDependencies);
            materialDependencies.erase(std::unique(materialDependencies.begin(), materialDependencies.end()),
                                       materialDependencies.end());
            output.SubAssets.push_back({context.ResolveSubAssetId("material/default"), MaterialAsset::StaticType(),
                                        "material/default", "Default Material", MaterialAsset::Encode(material),
                                        std::move(materialDependencies)});
            return output;
        };
        return result;
    }

    AssetImporterRegistration CreateShaderGraphInstanceAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.ShaderGraphInstance";
        result.Version = 3;
        result.Type = ShaderGraphInstanceAsset::StaticType();
        result.Extensions = {".keireshadergraphinstance"};
        result.ContextualImport = [](const AssetImportContext& context, const std::span<const std::byte> bytes)
        {
            if (!context.Asset || context.ProjectRoot.empty() || context.SourceRoot.empty() ||
                !context.ReadProjectFile || !context.ResolveSubAssetId || !context.ResolveSubAssetIdFor ||
                !context.ResolveAssetSource)
            {
                throw std::invalid_argument(
                    "Shader Graph instance import requires source and stable subasset resolvers.");
            }
            const auto definition = ShaderGraphInstanceAsset::DecodeSource(bytes);
            AssetImportOutput output;
            output.Bytes = ShaderGraphInstanceAsset::Encode(definition);
            std::vector<ShaderGraphInstanceDefinition> ancestry{definition};
            ShaderGraphDefinition graph;
            AssetId graphAsset;
            AssetId parent = definition.Parent;
            std::set<AssetId> visited{context.Asset};
            const auto sourcePrefix = std::filesystem::relative(context.SourceRoot, context.ProjectRoot);
            for (std::size_t depth = 0; depth < 16 && parent; ++depth)
            {
                if (!visited.insert(parent).second)
                    throw std::invalid_argument("Shader Graph instance parent chain contains a cycle.");
                output.AssetDependencies.push_back(parent);
                const auto source = context.ResolveAssetSource(parent);
                if (!source)
                    throw std::runtime_error("Shader Graph instance parent is not present in the source index: " +
                                             parent.ToString());
                const auto parentBytes = context.ReadProjectFile(sourcePrefix / source->RelativePath);
                if (source->Type == ShaderGraphAsset::StaticType())
                {
                    graph = ShaderGraphAsset::DecodeSource(parentBytes);
                    if (graph.Target.Target != ShaderGraphTarget::LegacySurface)
                        throw std::invalid_argument(
                            "Shader Graph instances only support legacy surface graphs; bind program properties "
                            "through the target-specific runtime API.");
                    graphAsset = parent;
                    break;
                }
                if (source->Type != ShaderGraphInstanceAsset::StaticType())
                    throw std::invalid_argument("Shader Graph instance parent must be a graph or another instance.");
                auto parentInstance = ShaderGraphInstanceAsset::DecodeSource(parentBytes);
                parent = parentInstance.Parent;
                ancestry.push_back(std::move(parentInstance));
            }
            if (!graphAsset)
                throw std::invalid_argument("Shader Graph instance parent chain exceeds 16 entries or has no graph.");

            std::ranges::reverse(ancestry);
            const auto resolved = ResolveShaderGraphInstance(graph, ancestry);
            const auto variantOwner = graph.GeneratedAssetOwner ? graph.GeneratedAssetOwner : graphAsset;
            const auto material =
                BakeShaderGraphInstance(graph, resolved,
                                        [&context, variantOwner](const std::span<const std::string> keywords)
                                        {
                                            return context.ResolveSubAssetIdFor(
                                                variantOwner, "shader/" + Detail::ShaderGraphKeywordSuffix(keywords));
                                        });
            for (const auto& [name, value] : resolved.Properties)
            {
                (void)name;
                if (const auto* asset = std::get_if<AssetId>(&value); asset && *asset)
                    output.AssetDependencies.push_back(*asset);
            }
            auto materialDependencies = output.AssetDependencies;
            materialDependencies.push_back(material.Shader);
            std::ranges::sort(output.AssetDependencies);
            output.AssetDependencies.erase(
                std::unique(output.AssetDependencies.begin(), output.AssetDependencies.end()),
                output.AssetDependencies.end());
            std::ranges::sort(materialDependencies);
            materialDependencies.erase(std::unique(materialDependencies.begin(), materialDependencies.end()),
                                       materialDependencies.end());
            output.SubAssets.push_back({context.ResolveSubAssetId("material/default"), MaterialAsset::StaticType(),
                                        "material/default", "Runtime Material", MaterialAsset::Encode(material),
                                        std::move(materialDependencies)});
            return output;
        };
        return result;
    }
} // namespace Keire
