#include "Keire/Rendering/ProgramArtifact.h"

#include "KeireInternal/Assets/AssetInternal.h"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <set>
#include <stdexcept>
#include <tuple>
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

        [[nodiscard]] bool IsLowercaseSha256(const std::string_view value) noexcept
        {
            return value.size() == 64U &&
                   std::ranges::all_of(
                       value, [](const unsigned char character)
                       { return std::isdigit(character) != 0 || (character >= 'a' && character <= 'f'); });
        }

        [[nodiscard]] bool IsPassRole(const std::string_view value) noexcept
        {
            if (value.empty() || value.size() > 128U ||
                !(std::isalpha(static_cast<unsigned char>(value.front())) != 0 || value.front() == '_'))
                return false;
            return std::ranges::all_of(value.substr(1), [](const unsigned char character)
                                       { return std::isalnum(character) != 0 || character == '_'; });
        }

        [[nodiscard]] bool IsSingleProgramStage(const ProgramStage stage) noexcept
        {
            return stage == ProgramStage::Vertex || stage == ProgramStage::Fragment || stage == ProgramStage::Compute;
        }

        [[nodiscard]] bool BackendFormatMatches(const ProgramBackend backend, const ProgramBinaryFormat format) noexcept
        {
            return (backend == ProgramBackend::D3D12 && format == ProgramBinaryFormat::Dxil) ||
                   (backend == ProgramBackend::Vulkan && format == ProgramBinaryFormat::SpirV) ||
                   (backend == ProgramBackend::Metal && format == ProgramBinaryFormat::Metallib);
        }

        void ValidateStageBinary(const ProgramStageBinary& binary)
        {
            if (!IsPassRole(binary.PassRole) || !BackendFormatMatches(binary.Backend, binary.Format) ||
                !IsSingleProgramStage(binary.Stage) || binary.EntryPoint.empty() || binary.Bytes.empty() ||
                binary.Bytes.size() > ProgramBinaryMaximumBytes || !IsLowercaseSha256(binary.Sha256) ||
                binary.Reflection.AbiVersion != ProgramReflectionAbiVersion ||
                binary.Reflection.Resources.size() > ProgramResourceHardLimit)
            {
                throw std::invalid_argument("Program backend binary contract is incomplete or incompatible.");
            }
            if (Detail::DigestToString(Detail::Sha256(binary.Bytes)) != binary.Sha256)
                throw std::invalid_argument("Program backend binary digest does not match its payload.");
            if (std::ranges::none_of(binary.Reflection.EntryPoints, [&binary](const ProgramEntryPoint& entry)
                                     { return entry.Stage == binary.Stage && entry.Name == binary.EntryPoint; }))
            {
                throw std::invalid_argument("Program backend binary reflection does not expose its entry point.");
            }
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

    std::string_view ProgramBackendName(const ProgramBackend backend) noexcept
    {
        switch (backend)
        {
        case ProgramBackend::D3D12:
            return "D3D12";
        case ProgramBackend::Vulkan:
            return "Vulkan";
        case ProgramBackend::Metal:
            return "Metal";
        }
        return "Unknown";
    }

    std::string_view ProgramBinaryFormatName(const ProgramBinaryFormat format) noexcept
    {
        switch (format)
        {
        case ProgramBinaryFormat::Dxil:
            return "DXIL";
        case ProgramBinaryFormat::SpirV:
            return "SPIR-V";
        case ProgramBinaryFormat::Metallib:
            return "Metallib";
        }
        return "Unknown";
    }

    std::string_view MaterialPassName(const MaterialPass pass) noexcept
    {
        switch (pass)
        {
        case MaterialPass::DepthOnly:
            return "depthOnly";
        case MaterialPass::DepthVelocity:
            return "depthVelocity";
        case MaterialPass::DeferredGBufferStandard:
            return "deferredGBufferStandard";
        case MaterialPass::DeferredGBufferExtended:
            return "deferredGBufferExtended";
        case MaterialPass::ForwardOpaque:
            return "forwardOpaque";
        case MaterialPass::ForwardTransparent:
            return "forwardTransparent";
        case MaterialPass::DecalDBuffer:
            return "decalDBuffer";
        case MaterialPass::ShadowAtlasDepth:
            return "shadowAtlasDepth";
        case MaterialPass::ShadowVirtualDepth:
            return "shadowVirtualDepth";
        case MaterialPass::ShadowTransmittance:
            return "shadowTransmittance";
        case MaterialPass::VolumeInject:
            return "volumeInject";
        case MaterialPass::SurfaceCacheCapture:
            return "surfaceCacheCapture";
        case MaterialPass::BakeSurface:
            return "bakeSurface";
        case MaterialPass::SelectionId:
            return "selectionId";
        }
        return "unknown";
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
            artifact.Variants.size() > ProgramVariantHardLimit ||
            artifact.Reflection.AbiVersion != ProgramReflectionAbiVersion)
            throw std::invalid_argument("Program artifact schema, target, stages, or variant bounds are invalid.");
        const bool compute = artifact.Target == ProgramTarget::Compute;
        if (compute != HasProgramStage(artifact.Stages, ProgramStage::Compute) ||
            (compute && (HasProgramStage(artifact.Stages, ProgramStage::Vertex) ||
                         HasProgramStage(artifact.Stages, ProgramStage::Fragment))))
            throw std::invalid_argument("Program artifact target and stage contract are incompatible.");

        std::set<std::string, std::less<>> suffixes;
        for (const auto& variant : artifact.Variants)
        {
            if (variant.StableSuffix.empty() || !suffixes.insert(variant.StableSuffix).second ||
                variant.GeneratedSource.empty() || variant.Hlsl.empty() || variant.Manifest.empty() ||
                variant.Binaries.size() > ProgramBinaryHardLimit)
                throw std::invalid_argument("Program artifact variants require unique complete payloads.");
            std::set<std::tuple<std::string, ProgramBackend, ProgramStage>> binaries;
            for (const auto& binary : variant.Binaries)
            {
                ValidateStageBinary(binary);
                if (!HasProgramStage(artifact.Stages, binary.Stage) ||
                    !binaries.emplace(binary.PassRole, binary.Backend, binary.Stage).second)
                {
                    throw std::invalid_argument("Program artifact contains a duplicate or undeclared stage binary.");
                }
            }
        }
        std::set<std::pair<std::uint32_t, std::uint32_t>> bindings;
        std::set<std::string, std::less<>> symbols;
        if (artifact.Reflection.Resources.size() > ProgramResourceHardLimit)
            throw std::invalid_argument("Program reflection exceeds the portable resource limit.");
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

    void ValidateCookedProgramArtifact(const ProgramArtifact& artifact)
    {
        ValidateProgramArtifact(artifact);
        for (const auto& variant : artifact.Variants)
        {
            if (variant.Binaries.empty())
                throw std::invalid_argument("Cooked program variants require backend binaries.");
            std::set<std::pair<std::string, ProgramBackend>> lanes;
            for (const auto& binary : variant.Binaries)
                lanes.emplace(binary.PassRole, binary.Backend);
            for (const auto& [role, backend] : lanes)
            {
                for (const auto stage : {ProgramStage::Vertex, ProgramStage::Fragment, ProgramStage::Compute})
                {
                    if (!HasProgramStage(artifact.Stages, stage))
                        continue;
                    if (std::ranges::none_of(
                            variant.Binaries, [role, backend, stage](const ProgramStageBinary& binary)
                            { return binary.PassRole == role && binary.Backend == backend && binary.Stage == stage; }))
                    {
                        throw std::invalid_argument("Cooked program backend lane is missing a declared stage.");
                    }
                }
            }
        }
    }

    void ValidateCookedMaterialProgramArtifact(const MaterialProgramArtifact& artifact)
    {
        ValidateMaterialProgramArtifact(artifact);
        ValidateCookedProgramArtifact(artifact.Program);
        std::set<std::string, std::less<>> declaredPasses;
        for (const auto& pass : artifact.Passes)
            declaredPasses.emplace(MaterialPassName(pass.Pass));
        for (const auto& variant : artifact.Program.Variants)
        {
            std::set<ProgramBackend> backends;
            for (const auto& binary : variant.Binaries)
            {
                if (!declaredPasses.contains(binary.PassRole))
                    throw std::invalid_argument("Cooked material program contains an undeclared pass binary.");
                backends.insert(binary.Backend);
            }
            for (const auto backend : backends)
            {
                for (const auto& pass : artifact.Passes)
                {
                    const auto role = MaterialPassName(pass.Pass);
                    if (std::ranges::none_of(variant.Binaries, [role, backend](const ProgramStageBinary& binary)
                                             { return binary.PassRole == role && binary.Backend == backend; }))
                    {
                        throw std::invalid_argument("Cooked material backend lane is missing a declared pass.");
                    }
                }
            }
        }
    }
} // namespace Keire
