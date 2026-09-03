#pragma once

#include "Keire/Api.h"
#include "Keire/Rendering/MaterialGraph.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    inline constexpr std::uint32_t ProgramArtifactSchemaVersion = 2;
    inline constexpr std::uint32_t MaterialProgramArtifactSchemaVersion = 2;
    inline constexpr std::uint32_t ProgramReflectionAbiVersion = 2;
    inline constexpr std::size_t ProgramVariantWarningThreshold = 128;
    inline constexpr std::size_t ProgramVariantHardLimit = 1024;
    inline constexpr std::size_t ProgramBinaryHardLimit = 256;
    inline constexpr std::size_t ProgramBinaryMaximumBytes = 64ULL * 1024ULL * 1024ULL;
    inline constexpr std::size_t ProgramResourceHardLimit = 1024;

    enum class ProgramTarget : std::uint8_t
    {
        Material,
        Ui,
        Fullscreen,
        Vfx,
        CustomGraphics,
        Compute
    };

    enum class ProgramStage : std::uint8_t
    {
        None = 0,
        Vertex = 1U << 0U,
        Fragment = 1U << 1U,
        Compute = 1U << 2U
    };

    [[nodiscard]] constexpr ProgramStage operator|(const ProgramStage left, const ProgramStage right) noexcept
    {
        return static_cast<ProgramStage>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
    }

    [[nodiscard]] constexpr bool HasProgramStage(const ProgramStage stages, const ProgramStage stage) noexcept
    {
        return (static_cast<std::uint8_t>(stages) & static_cast<std::uint8_t>(stage)) != 0U;
    }

    enum class ProgramResourceKind : std::uint8_t
    {
        Uniform,
        SampledTexture2D,
        SampledTexture2DArray,
        SampledTextureCube,
        SampledTexture3D,
        Sampler,
        StructuredBuffer,
        ByteAddressBuffer,
        StorageTexture,
        StorageBuffer
    };

    enum class ProgramResourceAccess : std::uint8_t
    {
        ReadOnly,
        WriteOnly,
        ReadWrite
    };

    struct ProgramResourceBinding
    {
        AssetId Id;
        std::string Name;
        std::string Symbol;
        ProgramResourceKind Kind = ProgramResourceKind::Uniform;
        ProgramResourceAccess Access = ProgramResourceAccess::ReadOnly;
        ProgramStage Stages = ProgramStage::Fragment;
        std::uint32_t Space = 0;
        std::uint32_t Binding = 0;
        std::uint32_t ArrayCount = 1;
        std::uint32_t StrideBytes = 0;

        bool operator==(const ProgramResourceBinding&) const = default;
    };

    struct ProgramEntryPoint
    {
        ProgramStage Stage = ProgramStage::None;
        std::string Name;

        bool operator==(const ProgramEntryPoint&) const = default;
    };

    struct ProgramReflection
    {
        std::uint32_t AbiVersion = ProgramReflectionAbiVersion;
        std::vector<ProgramEntryPoint> EntryPoints;
        std::vector<ShaderPropertyDefinition> Properties;
        std::vector<ProgramResourceBinding> Resources;
        std::uint32_t ThreadGroupSizeX = 1;
        std::uint32_t ThreadGroupSizeY = 1;
        std::uint32_t ThreadGroupSizeZ = 1;

        bool operator==(const ProgramReflection&) const = default;
    };

    enum class ProgramBackend : std::uint8_t
    {
        D3D12,
        Vulkan,
        Metal
    };

    enum class ProgramBinaryFormat : std::uint8_t
    {
        Dxil,
        SpirV,
        Metallib
    };

    struct ProgramStageBinary
    {
        /// Stable pass role. Material roles use MaterialPassName; non-material programs use "primary".
        std::string PassRole = "primary";
        ProgramBackend Backend = ProgramBackend::D3D12;
        ProgramBinaryFormat Format = ProgramBinaryFormat::Dxil;
        ProgramStage Stage = ProgramStage::None;
        std::string EntryPoint;
        std::string Sha256;
        std::vector<std::byte> Bytes;
        ProgramReflection Reflection;

        bool operator==(const ProgramStageBinary&) const = default;
    };

    struct ProgramArtifactVariant
    {
        std::vector<std::string> Keywords;
        std::string StableSuffix;
        std::filesystem::path GeneratedSource;
        std::string Hlsl;
        std::string Manifest;
        std::vector<ProgramStageBinary> Binaries;

        bool operator==(const ProgramArtifactVariant&) const = default;
    };

    struct ProgramArtifact
    {
        std::uint32_t SchemaVersion = ProgramArtifactSchemaVersion;
        ProgramTarget Target = ProgramTarget::Material;
        ProgramStage Stages = ProgramStage::Vertex | ProgramStage::Fragment;
        ProgramReflection Reflection;
        std::vector<ProgramArtifactVariant> Variants;
        std::vector<std::filesystem::path> Dependencies;
        std::vector<ShaderGraphDiagnostic> Diagnostics;

        [[nodiscard]] bool Succeeded() const noexcept;
    };

    enum class MaterialPass : std::uint8_t
    {
        DepthOnly,
        DepthVelocity,
        DeferredGBufferStandard,
        DeferredGBufferExtended,
        ForwardOpaque,
        ForwardTransparent,
        DecalDBuffer,
        ShadowAtlasDepth,
        ShadowVirtualDepth,
        ShadowTransmittance,
        VolumeInject,
        SurfaceCacheCapture,
        BakeSurface,
        SelectionId
    };

    enum class MaterialPassCondition : std::uint8_t
    {
        Always,
        OpaqueOrMasked,
        Transparent,
        ExtendedGBuffer,
        VirtualShadowMaps
    };

    struct MaterialPassContract
    {
        MaterialPass Pass = MaterialPass::ForwardOpaque;
        MaterialPassCondition Condition = MaterialPassCondition::Always;
        std::string VertexEntry = "VSMain";
        std::string FragmentEntry = "PSMain";

        bool operator==(const MaterialPassContract&) const = default;
    };

    struct MaterialProgramArtifact
    {
        std::uint32_t SchemaVersion = MaterialProgramArtifactSchemaVersion;
        MaterialDomain Domain = MaterialDomain::Surface;
        MaterialShadingModel ShadingModel = MaterialShadingModel::OpenPbrLit;
        MaterialAuthoringMode AuthoringMode = MaterialAuthoringMode::SimpleSurface;
        std::uint8_t MaximumClosures = MaximumMaterialClosureCount;
        ProgramArtifact Program;
        std::vector<MaterialPassContract> Passes;

        [[nodiscard]] bool Succeeded() const noexcept { return Program.Succeeded(); }
    };

    struct ProgramCompileOptions
    {
        ShaderGraphCompileOptions ShaderGraph;
        std::size_t VariantWarningThreshold = ProgramVariantWarningThreshold;
        std::size_t MaximumVariants = ProgramVariantHardLimit;
    };

    [[nodiscard]] KEIRE_API ProgramTarget ProgramTargetFromShaderGraph(ShaderGraphTarget target) noexcept;
    [[nodiscard]] KEIRE_API std::string_view ProgramTargetName(ProgramTarget target) noexcept;
    [[nodiscard]] KEIRE_API std::string_view ProgramBackendName(ProgramBackend backend) noexcept;
    [[nodiscard]] KEIRE_API std::string_view ProgramBinaryFormatName(ProgramBinaryFormat format) noexcept;
    [[nodiscard]] KEIRE_API std::string_view MaterialPassName(MaterialPass pass) noexcept;
    [[nodiscard]] KEIRE_API ProgramArtifact CompileShaderGraphProgram(const ShaderGraphDefinition& definition,
                                                                      const ProgramCompileOptions& options = {});
    [[nodiscard]] KEIRE_API MaterialProgramArtifact CompileMaterialProgram(const MaterialGraphDefinition& definition,
                                                                           const ProgramCompileOptions& options = {});
    [[nodiscard]] KEIRE_API std::vector<MaterialPassContract>
    BuildMaterialPassContract(MaterialDomain domain, MaterialShadingModel shadingModel, MaterialAlphaMode alphaMode,
                              MaterialAuthoringMode authoringMode = MaterialAuthoringMode::SimpleSurface);
    KEIRE_API void ValidateProgramArtifact(const ProgramArtifact& artifact);
    KEIRE_API void ValidateMaterialProgramArtifact(const MaterialProgramArtifact& artifact);
    /// Requires verified backend binaries for every declared stage in every variant/pass/backend lane.
    KEIRE_API void ValidateCookedProgramArtifact(const ProgramArtifact& artifact);
    /// Additionally requires every declared material pass to be complete for every backend lane.
    KEIRE_API void ValidateCookedMaterialProgramArtifact(const MaterialProgramArtifact& artifact);
} // namespace Keire
