#pragma once

#include "Keire/Api.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Keire
{
    inline constexpr std::uint32_t ShaderCompileManifestSchemaVersion = 2;
    inline constexpr std::uint32_t ShaderCompileProgramAbiVersion = 2;

    enum class ShaderCompilationPolicy : std::uint8_t
    {
        LocalOnly,
        RemotePreferred,
        RemoteRequired
    };

    enum class ShaderCompilationPriority : std::uint8_t
    {
        Interactive,
        Normal,
        Background
    };

    enum class ShaderCompileStage : std::uint8_t
    {
        Vertex,
        Fragment,
        Compute
    };

    enum class ShaderCompilePlatform : std::uint8_t
    {
        Windows,
        Linux,
        MacOS
    };

    enum class ShaderCompileArchitecture : std::uint8_t
    {
        X86_64,
        Arm64
    };

    enum class ShaderCompileBinaryFormat : std::uint8_t
    {
        Dxil,
        SpirV,
        Msl,
        Metallib
    };

    enum class ShaderCompileBackend : std::uint8_t
    {
        D3D12,
        Vulkan,
        Metal,
        /// Source-compatible authoring default. Canonicalization always resolves this to an exact backend.
        Automatic
    };

    enum class ShaderCompileOptimization : std::uint8_t
    {
        Debug,
        Development,
        Shipping
    };

    struct ShaderCompileTarget
    {
        ShaderCompilePlatform Platform = ShaderCompilePlatform::Windows;
        ShaderCompileArchitecture Architecture = ShaderCompileArchitecture::X86_64;
        ShaderCompileBinaryFormat Format = ShaderCompileBinaryFormat::Dxil;
        ShaderCompileBackend Backend = ShaderCompileBackend::Automatic;

        bool operator==(const ShaderCompileTarget&) const = default;
    };

    struct ShaderCompileDefine
    {
        std::string Name;
        std::string Value;

        bool operator==(const ShaderCompileDefine&) const = default;
    };

    struct ShaderCompileDependency
    {
        std::string VirtualPath;
        std::string Sha256;

        bool operator==(const ShaderCompileDependency&) const = default;
    };

    struct ShaderCompileManifest
    {
        std::uint32_t SchemaVersion = ShaderCompileManifestSchemaVersion;
        std::uint32_t ProgramAbiVersion = ShaderCompileProgramAbiVersion;
        std::string ToolchainSha256;
        std::string SourceSha256;
        ShaderCompileStage Stage = ShaderCompileStage::Vertex;
        std::string EntryPoint = "VSMain";
        ShaderCompileTarget Target;
        std::vector<ShaderCompileDefine> Defines;
        std::vector<ShaderCompileDependency> Dependencies;
        bool WarningsAsErrors = false;
        bool DebugInformation = false;
        /// Stable logical role such as "primary", "depthVelocity", or "deferredGBufferStandard".
        std::string PassRole = "primary";
        ShaderCompileOptimization Optimization = ShaderCompileOptimization::Development;

        bool operator==(const ShaderCompileManifest&) const = default;
    };

    struct ShaderCompilationRequest
    {
        ShaderCompileManifest Manifest;
        ShaderCompilationPolicy Policy = ShaderCompilationPolicy::RemotePreferred;
        ShaderCompilationPriority Priority = ShaderCompilationPriority::Normal;

        bool operator==(const ShaderCompilationRequest&) const = default;
    };

    KEIRE_API void ValidateShaderCompileManifest(const ShaderCompileManifest& manifest);
    KEIRE_API void ValidateShaderCompilationRequest(const ShaderCompilationRequest& request);
    [[nodiscard]] KEIRE_API ShaderCompileManifest CanonicalizeShaderCompileManifest(ShaderCompileManifest manifest);
    /// Human-readable canonical JSON for diagnostics. Work keys are derived from the canonical CBOR encoding below.
    [[nodiscard]] KEIRE_API std::string EncodeShaderCompileManifest(const ShaderCompileManifest& manifest);
    [[nodiscard]] KEIRE_API std::vector<std::byte>
    EncodeShaderCompileManifestCbor(const ShaderCompileManifest& manifest);
    [[nodiscard]] KEIRE_API std::string ShaderCompileWorkKey(const ShaderCompileManifest& manifest);
    [[nodiscard]] KEIRE_API bool IsShaderCompileWorkKey(std::string_view value) noexcept;
} // namespace Keire
