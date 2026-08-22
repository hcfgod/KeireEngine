#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/RenderingAssets.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace Keire
{
    inline constexpr std::uint32_t ShaderGraphResourceContractSchemaVersion = 1;
    inline constexpr std::size_t MaximumShaderGraphResourceDefinitions = 64;
    inline constexpr std::size_t MaximumShaderGraphSampledTextures = 16;
    inline constexpr std::size_t MaximumShaderGraphSamplers = 16;
    inline constexpr std::size_t MaximumShaderGraphReadOnlyBuffers = 8;
    inline constexpr std::uint32_t MaximumShaderGraphBufferViewBytes = 16U * 1024U * 1024U;

    enum class ShaderGraphResourceKind : std::uint8_t
    {
        Sampler,
        Texture2DArray,
        TextureCube,
        Texture3D,
        StructuredBuffer,
        ByteAddressBuffer
    };

    struct ShaderGraphBufferView
    {
        AssetId Asset;
        std::uint32_t OffsetBytes = 0;
        std::uint32_t SizeBytes = 0;
        /// Structured-buffer element stride. Byte-address buffers require zero.
        std::uint32_t StrideBytes = 0;

        bool operator==(const ShaderGraphBufferView&) const = default;
    };

    using ShaderGraphResourceValue = std::variant<SamplerDescription, AssetId, ShaderGraphBufferView>;
    using ShaderGraphResourceBindings = std::map<std::string, ShaderGraphResourceValue, std::less<>>;

    struct ShaderGraphResourceDefinition
    {
        AssetId Id;
        std::string Name;
        std::string Symbol;
        ShaderGraphResourceKind Kind = ShaderGraphResourceKind::Sampler;
        ShaderGraphResourceValue Value = SamplerDescription{};

        bool operator==(const ShaderGraphResourceDefinition&) const = default;
    };

    struct ShaderGraphResourceStatistics
    {
        std::size_t ResourceCount = 0;
        std::size_t SamplerCount = 0;
        std::size_t TextureCount = 0;
        std::size_t ReadOnlyBufferCount = 0;
        std::size_t StructuredBufferCount = 0;
        std::size_t ByteAddressBufferCount = 0;
        std::uint64_t BufferViewBytes = 0;

        bool operator==(const ShaderGraphResourceStatistics&) const = default;
    };

    struct ShaderGraphResourceDiagnostic
    {
        std::string Code;
        std::string Message;
        AssetId Resource;

        bool operator==(const ShaderGraphResourceDiagnostic&) const = default;
    };

    struct ShaderGraphResourceAnalysis
    {
        ShaderGraphResourceStatistics Statistics;
        std::vector<ShaderGraphResourceDiagnostic> Diagnostics;

        [[nodiscard]] bool Succeeded() const noexcept { return Diagnostics.empty(); }
    };

    struct ShaderGraphResourceDeclarations
    {
        std::string Hlsl;
        ShaderGraphResourceStatistics Statistics;
        std::uint32_t NextTextureRegister = 0;
        std::uint32_t NextSamplerRegister = 0;
        std::uint32_t NextBufferRegister = 0;

        bool operator==(const ShaderGraphResourceDeclarations&) const = default;
    };

    /// Validates stable identities, shader symbols, portable sampler values, and bounded read-only buffer views.
    KEIRE_API void ValidateShaderGraphResources(std::span<const ShaderGraphResourceDefinition> resources);
    [[nodiscard]] KEIRE_API ShaderGraphResourceAnalysis
    AnalyzeShaderGraphResources(std::span<const ShaderGraphResourceDefinition> resources) noexcept;
    /// Encodes a standalone, versioned contract document suitable for embedding in Shader/Material Graph schema 4.
    [[nodiscard]] KEIRE_API std::vector<std::byte>
    EncodeShaderGraphResources(std::span<const ShaderGraphResourceDefinition> resources);
    [[nodiscard]] KEIRE_API std::vector<ShaderGraphResourceDefinition>
    DecodeShaderGraphResources(std::span<const std::byte> bytes);
    /// Emits portable read-only HLSL declarations. Sampled textures, samplers, and read-only buffers advance in
    /// independent register spaces from their caller-provided bases.
    [[nodiscard]] KEIRE_API ShaderGraphResourceDeclarations GenerateShaderGraphResourceDeclarations(
        std::span<const ShaderGraphResourceDefinition> resources, std::uint32_t firstTextureRegister = 0,
        std::uint32_t firstSamplerRegister = 0, std::uint32_t firstBufferRegister = 0);
    [[nodiscard]] KEIRE_API std::vector<AssetId>
    ShaderGraphResourceDependencies(std::span<const ShaderGraphResourceDefinition> resources);
    /// Resolves material overrides by stable shader symbol and rejects unknown or type-incompatible bindings.
    [[nodiscard]] KEIRE_API ShaderGraphResourceBindings ResolveShaderGraphResourceBindings(
        std::span<const ShaderGraphResourceDefinition> resources, const ShaderGraphResourceBindings& overrides = {});
    [[nodiscard]] KEIRE_API std::vector<AssetId>
    ShaderGraphResourceBindingDependencies(std::span<const ShaderGraphResourceDefinition> resources,
                                           const ShaderGraphResourceBindings& bindings);
} // namespace Keire
