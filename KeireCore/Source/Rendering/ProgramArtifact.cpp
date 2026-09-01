#include "Keire/Rendering/ProgramArtifact.h"

#include <algorithm>
#include <ranges>
#include <set>
#include <stdexcept>
#include <utility>

namespace Keire
{
    namespace
    {
        [[nodiscard]] ProgramStage ConvertStages(const ShaderGraphShaderStage stages) noexcept
        {
            ProgramStage result = ProgramStage::None;
            if ((static_cast<std::uint8_t>(stages) & static_cast<std::uint8_t>(ShaderGraphShaderStage::Vertex)) != 0U)
                result = result | ProgramStage::Vertex;
            if ((static_cast<std::uint8_t>(stages) & static_cast<std::uint8_t>(ShaderGraphShaderStage::Fragment)) != 0U)
                result = result | ProgramStage::Fragment;
            if ((static_cast<std::uint8_t>(stages) & static_cast<std::uint8_t>(ShaderGraphShaderStage::Compute)) != 0U)
                result = result | ProgramStage::Compute;
            return result;
        }

        [[nodiscard]] ProgramResourceKind ConvertResourceKind(const ShaderGraphResourceKind kind) noexcept
        {
            switch (kind)
            {
            case ShaderGraphResourceKind::Sampler:
                return ProgramResourceKind::Sampler;
            case ShaderGraphResourceKind::Texture2DArray:
                return ProgramResourceKind::SampledTexture2DArray;
            case ShaderGraphResourceKind::TextureCube:
                return ProgramResourceKind::SampledTextureCube;
            case ShaderGraphResourceKind::Texture3D:
                return ProgramResourceKind::SampledTexture3D;
            case ShaderGraphResourceKind::StructuredBuffer:
                return ProgramResourceKind::StructuredBuffer;
            case ShaderGraphResourceKind::ByteAddressBuffer:
                return ProgramResourceKind::ByteAddressBuffer;
            }
            return ProgramResourceKind::Uniform;
        }

        [[nodiscard]] ShaderGraphOutput MaterialOutput(const MaterialGraphDefinition& definition) noexcept
        {
            if (definition.Domain == MaterialDomain::Decal)
                return ShaderGraphOutput::Decal;
            if (definition.Domain == MaterialDomain::Volume)
                return ShaderGraphOutput::Transparent;
            switch (definition.ShadingModel)
            {
            case MaterialShadingModel::Unlit:
                return ShaderGraphOutput::Unlit;
            case MaterialShadingModel::Hair:
                return ShaderGraphOutput::Hair;
            case MaterialShadingModel::Eye:
                return ShaderGraphOutput::Eye;
            case MaterialShadingModel::ThinTranslucent:
            case MaterialShadingModel::Water:
                return ShaderGraphOutput::Transparent;
            case MaterialShadingModel::ParticipatingMedia:
                return ShaderGraphOutput::Transparent;
            case MaterialShadingModel::OpenPbrLit:
                return ShaderGraphOutput::Surface;
            }
            return ShaderGraphOutput::Surface;
        }

        void AddEntryPoints(ProgramReflection& reflection, const ProgramStage stages)
        {
            if (HasProgramStage(stages, ProgramStage::Vertex))
                reflection.EntryPoints.push_back({ProgramStage::Vertex, "VSMain"});
            if (HasProgramStage(stages, ProgramStage::Fragment))
                reflection.EntryPoints.push_back({ProgramStage::Fragment, "PSMain"});
            if (HasProgramStage(stages, ProgramStage::Compute))
                reflection.EntryPoints.push_back({ProgramStage::Compute, "CSMain"});
        }

        [[nodiscard]] ProgramReflection BuildReflection(const ShaderGraphDefinition& definition,
                                                        const ShaderGraphCompilation& compilation,
                                                        const ProgramStage stages)
        {
            ProgramReflection result;
            result.Properties = compilation.Properties;
            result.ThreadGroupSizeX = definition.Target.ThreadGroupSizeX;
            result.ThreadGroupSizeY = definition.Target.ThreadGroupSizeY;
            result.ThreadGroupSizeZ = definition.Target.ThreadGroupSizeZ;
            AddEntryPoints(result, stages);

            std::uint32_t binding = 0;
            for (const auto& property : result.Properties)
            {
                if (property.Type != ShaderPropertyType::Texture2D)
                    continue;
                result.Resources.push_back({property.Id,
                                            property.DisplayName.empty() ? property.Name : property.DisplayName,
                                            property.Name, ProgramResourceKind::SampledTexture2D,
                                            ProgramResourceAccess::ReadOnly, ProgramStage::Fragment, 1, binding++});
            }
            for (const auto& resource : definition.Resources)
            {
                std::uint32_t stride = 0;
                if (const auto* view = std::get_if<ShaderGraphBufferView>(&resource.Value))
                    stride = view->StrideBytes;
                result.Resources.push_back({resource.Id, resource.Name, resource.Symbol,
                                            ConvertResourceKind(resource.Kind), ProgramResourceAccess::ReadOnly, stages,
                                            2, binding++, 1, stride});
            }
            return result;
        }

        [[nodiscard]] bool IsError(const ShaderGraphDiagnostic& diagnostic) noexcept
        {
            return diagnostic.Severity == ShaderGraphDiagnosticSeverity::Error;
        }
    } // namespace

    bool ProgramArtifact::Succeeded() const noexcept
    {
        return !Variants.empty() && std::ranges::none_of(Diagnostics, IsError);
    }

    ProgramTarget ProgramTargetFromShaderGraph(const ShaderGraphTarget target) noexcept
    {
        switch (target)
        {
        case ShaderGraphTarget::Material:
            return ProgramTarget::Material;
        case ShaderGraphTarget::Ui:
            return ProgramTarget::Ui;
        case ShaderGraphTarget::Fullscreen:
            return ProgramTarget::Fullscreen;
        case ShaderGraphTarget::Vfx:
            return ProgramTarget::Vfx;
        case ShaderGraphTarget::CustomGraphics:
            return ProgramTarget::CustomGraphics;
        case ShaderGraphTarget::Compute:
            return ProgramTarget::Compute;
        }
        return ProgramTarget::Material;
    }

    std::string_view ProgramTargetName(const ProgramTarget target) noexcept
    {
        switch (target)
        {
        case ProgramTarget::Material:
            return "Material";
        case ProgramTarget::Ui:
            return "UI";
        case ProgramTarget::Fullscreen:
            return "Fullscreen";
        case ProgramTarget::Vfx:
            return "VFX";
        case ProgramTarget::CustomGraphics:
            return "Custom Graphics";
        case ProgramTarget::Compute:
            return "Compute";
        }
        return "Unknown";
    }

    ProgramArtifact CompileShaderGraphProgram(const ShaderGraphDefinition& definition,
                                              const ProgramCompileOptions& options)
    {
        ProgramArtifact result;
        result.Target = ProgramTargetFromShaderGraph(definition.Target.Target);
        result.Stages = ConvertStages(definition.Target.Stages);
        if (options.MaximumVariants == 0 || options.MaximumVariants > ProgramVariantHardLimit ||
            options.VariantWarningThreshold == 0 || options.VariantWarningThreshold > options.MaximumVariants)
        {
            result.Diagnostics.push_back({ShaderGraphDiagnosticSeverity::Error,
                                          "PRG0001",
                                          "Program variant policy is invalid or exceeds the portable hard limit.",
                                          {},
                                          {},
                                          0});
            return result;
        }

        auto shaderOptions = options.ShaderGraph;
        shaderOptions.MaximumVariants = options.MaximumVariants;
        auto compilation = CompileShaderGraph(definition, shaderOptions);
        result.Diagnostics = compilation.Diagnostics;
        result.Dependencies = compilation.Dependencies;
        result.Reflection = BuildReflection(definition, compilation, result.Stages);
        result.Variants.reserve(compilation.Variants.size());
        for (auto& variant : compilation.Variants)
            result.Variants.push_back({std::move(variant.Keywords), std::move(variant.StableSuffix),
                                       std::move(variant.GeneratedSource), std::move(variant.Hlsl),
                                       std::move(variant.Manifest)});
        if (result.Variants.size() > options.VariantWarningThreshold)
            result.Diagnostics.push_back({ShaderGraphDiagnosticSeverity::Warning,
                                          "PRG1001",
                                          "Program produces " + std::to_string(result.Variants.size()) +
                                              " variants; the project warning threshold is " +
                                              std::to_string(options.VariantWarningThreshold) + '.',
                                          {},
                                          {},
                                          0});
        if (result.Succeeded())
            ValidateProgramArtifact(result);
        return result;
    }

    std::vector<MaterialPassContract> BuildMaterialPassContract(const MaterialDomain domain,
                                                                const MaterialShadingModel shadingModel,
                                                                const MaterialAlphaMode alphaMode,
                                                                const MaterialAuthoringMode authoringMode)
    {
        if (domain == MaterialDomain::Decal)
            return {{MaterialPass::DecalDBuffer}, {MaterialPass::SelectionId}};
        if (domain == MaterialDomain::Volume)
            return {{MaterialPass::VolumeInject}, {MaterialPass::BakeSurface}, {MaterialPass::SelectionId}};

        const bool transparent = IsTransparentMaterial(alphaMode) || shadingModel == MaterialShadingModel::Water ||
                                 shadingModel == MaterialShadingModel::ThinTranslucent;
        std::vector<MaterialPassContract> result;
        if (transparent)
        {
            result.push_back({MaterialPass::ForwardTransparent, MaterialPassCondition::Transparent});
            result.push_back({MaterialPass::ShadowTransmittance, MaterialPassCondition::Transparent});
        }
        else
        {
            result.push_back({MaterialPass::DepthOnly, MaterialPassCondition::OpaqueOrMasked});
            result.push_back({MaterialPass::DepthVelocity, MaterialPassCondition::OpaqueOrMasked});
            const bool forwardOnly =
                shadingModel == MaterialShadingModel::Hair || shadingModel == MaterialShadingModel::Eye;
            if (!forwardOnly && authoringMode == MaterialAuthoringMode::SimpleSurface)
                result.push_back({MaterialPass::DeferredGBufferStandard, MaterialPassCondition::OpaqueOrMasked});
            else if (!forwardOnly)
                result.push_back({MaterialPass::DeferredGBufferExtended, MaterialPassCondition::ExtendedGBuffer});
            result.push_back({MaterialPass::ForwardOpaque, MaterialPassCondition::OpaqueOrMasked});
            result.push_back({MaterialPass::ShadowAtlasDepth, MaterialPassCondition::OpaqueOrMasked});
            result.push_back({MaterialPass::ShadowVirtualDepth, MaterialPassCondition::VirtualShadowMaps});
        }
        result.push_back({MaterialPass::SurfaceCacheCapture});
        result.push_back({MaterialPass::BakeSurface});
        result.push_back({MaterialPass::SelectionId});
        return result;
    }

    MaterialProgramArtifact CompileMaterialProgram(const MaterialGraphDefinition& definition,
                                                   const ProgramCompileOptions& options)
    {
        MaterialProgramArtifact result;
        result.Domain = definition.Domain;
        result.ShadingModel = definition.ShadingModel;
        result.AuthoringMode = definition.AuthoringMode;
        result.MaximumClosures = definition.MaximumClosures;
        try
        {
            ValidateMaterialGraph(definition);
            auto graph = definition.SurfaceGraph;
            graph.Target.Target = ShaderGraphTarget::Material;
            graph.Target.Stages =
                static_cast<ShaderGraphShaderStage>(static_cast<std::uint8_t>(ShaderGraphShaderStage::Vertex) |
                                                    static_cast<std::uint8_t>(ShaderGraphShaderStage::Fragment));
            graph.Output = MaterialOutput(definition);

            const auto values = EvaluateMaterialGraphProperties(definition);
            for (auto& node : graph.Nodes)
            {
                if (node.Kind != ShaderGraphNodeKind::Parameter)
                    continue;
                const auto value = values.find(node.Symbol);
                if (value == values.end())
                    continue;
                node.Value = std::visit([](const auto& typed) -> ShaderGraphValue { return typed; }, value->second);
            }
            result.Program = CompileShaderGraphProgram(graph, options);
            result.Passes = BuildMaterialPassContract(definition.Domain, definition.ShadingModel,
                                                      definition.Surface.AlphaMode, definition.AuthoringMode);
            if (result.Succeeded())
                ValidateMaterialProgramArtifact(result);
        }
        catch (const std::exception& error)
        {
            result.Program.Variants.clear();
            result.Program.Diagnostics.push_back(
                {ShaderGraphDiagnosticSeverity::Error, "MATP0001", error.what(), {}, {}, 0});
        }
        return result;
    }

    void ValidateProgramArtifact(const ProgramArtifact& artifact)
    {
        if (artifact.SchemaVersion != ProgramArtifactSchemaVersion || artifact.Target > ProgramTarget::Compute ||
            artifact.Stages == ProgramStage::None || artifact.Variants.empty() ||
            artifact.Variants.size() > ProgramVariantHardLimit || artifact.Reflection.AbiVersion == 0)
            throw std::invalid_argument("Program artifact schema, target, stages, or variant bounds are invalid.");
        const bool compute = artifact.Target == ProgramTarget::Compute;
        if (compute != HasProgramStage(artifact.Stages, ProgramStage::Compute) ||
            (compute && (HasProgramStage(artifact.Stages, ProgramStage::Vertex) ||
                         HasProgramStage(artifact.Stages, ProgramStage::Fragment))))
            throw std::invalid_argument("Program artifact target and stage contract are incompatible.");

        std::set<std::string, std::less<>> suffixes;
        for (const auto& variant : artifact.Variants)
            if (variant.StableSuffix.empty() || !suffixes.insert(variant.StableSuffix).second ||
                variant.GeneratedSource.empty() || variant.Hlsl.empty() || variant.Manifest.empty())
                throw std::invalid_argument("Program artifact variants require unique complete payloads.");
        std::set<std::pair<std::uint32_t, std::uint32_t>> bindings;
        std::set<std::string, std::less<>> symbols;
        for (const auto& resource : artifact.Reflection.Resources)
            if (resource.Symbol.empty() || !symbols.insert(resource.Symbol).second || resource.ArrayCount == 0 ||
                !bindings.emplace(resource.Space, resource.Binding).second)
                throw std::invalid_argument("Program reflection contains a duplicate or invalid resource binding.");
    }

    void ValidateMaterialProgramArtifact(const MaterialProgramArtifact& artifact)
    {
        ValidateProgramArtifact(artifact.Program);
        if (artifact.SchemaVersion != MaterialProgramArtifactSchemaVersion ||
            artifact.Program.Target != ProgramTarget::Material || artifact.Domain > MaterialDomain::Volume ||
            artifact.ShadingModel > MaterialShadingModel::ParticipatingMedia ||
            artifact.AuthoringMode > MaterialAuthoringMode::LayerStack || artifact.MaximumClosures == 0 ||
            artifact.MaximumClosures > MaximumMaterialClosureCount || artifact.Passes.empty())
            throw std::invalid_argument("Material program artifact contract is invalid.");
        std::set<MaterialPass> passes;
        for (const auto& pass : artifact.Passes)
            if (!passes.insert(pass.Pass).second || pass.Pass > MaterialPass::SelectionId ||
                pass.Condition > MaterialPassCondition::VirtualShadowMaps || pass.VertexEntry.empty() ||
                pass.FragmentEntry.empty())
                throw std::invalid_argument("Material program artifact contains an invalid pass contract.");
    }
} // namespace Keire
