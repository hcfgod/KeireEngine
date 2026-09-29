#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"

#include <compare>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Keire
{
    inline constexpr std::uint32_t ManagedAssemblySchemaVersion = 4;

    enum class ManagedAssemblyClassification : std::uint8_t
    {
        Runtime,
        Editor,
        Tests
    };

    struct ManagedPackageReference
    {
        std::string Name;
        std::string Version;
        auto operator<=>(const ManagedPackageReference&) const = default;
    };

    struct ManagedVersionDefine
    {
        std::string Resource;
        std::string Expression;
        std::string Define;
        auto operator<=>(const ManagedVersionDefine&) const = default;
    };

    struct ManagedAssemblyBuildContext
    {
        // Empty selects the host platform. Supported targets: Windows, Linux, macOS.
        std::string Platform;
        bool IsEditor = true;
        std::vector<std::string> DefineSymbols;
        std::vector<ManagedPackageReference> ResourceVersions;
    };

    struct ManagedAssemblyDefinition
    {
        std::uint32_t SchemaVersion = ManagedAssemblySchemaVersion;
        std::string Name;
        std::string RootNamespace;
        ManagedAssemblyClassification Classification = ManagedAssemblyClassification::Runtime;
        std::vector<std::filesystem::path> SourceRoots;
        std::vector<AssetId> References;
        std::vector<ManagedPackageReference> Packages;
        std::vector<std::string> DefineSymbols;
        bool AllowUnsafe = false;
        bool AutoReferenced = true;
        std::vector<std::string> IncludePlatforms;
        std::vector<std::string> ExcludePlatforms;
        std::vector<std::string> DefineConstraints;
        std::vector<ManagedVersionDefine> VersionDefines;
        bool OverrideReferences = false;
        std::vector<std::filesystem::path> PrecompiledReferences;
    };

    struct ManagedAssemblyGraphEntry
    {
        AssetId Asset;
        ManagedAssemblyDefinition Definition;
        // Project-relative asset path. Empty source roots use the definition's containing folder.
        std::filesystem::path DefinitionPath;
        // Resolved project-relative sources; an engaged empty list deliberately compiles no scripts.
        std::optional<std::vector<std::filesystem::path>> SourceFiles;
        // Resolved managed DLLs referenced by this assembly, relative to the project.
        std::vector<std::filesystem::path> PrecompiledFiles;
    };

    class KEIRE_API ManagedAssemblyAsset final : public Asset
    {
      public:
        explicit ManagedAssemblyAsset(ManagedAssemblyDefinition definition = {});

        [[nodiscard]] static constexpr AssetTypeId StaticType() noexcept
        {
            return AssetTypeId(AssetId(0x4b454952454d414eULL, 0x4147454441534d01ULL));
        }

        [[nodiscard]] AssetTypeId Type() const noexcept override { return StaticType(); }
        [[nodiscard]] std::size_t ResidentBytes() const noexcept override;
        [[nodiscard]] const ManagedAssemblyDefinition& Definition() const noexcept { return m_Definition; }

        [[nodiscard]] static Ref<ManagedAssemblyAsset> Decode(std::span<const std::byte> bytes);
        [[nodiscard]] static std::vector<std::byte> Encode(const ManagedAssemblyDefinition& definition);
        static void Validate(const ManagedAssemblyDefinition& definition);

      private:
        ManagedAssemblyDefinition m_Definition;
        std::size_t m_ResidentBytes = 0;
    };

    KEIRE_API void ValidateManagedAssemblyGraph(std::span<const ManagedAssemblyGraphEntry> assemblies);
    // Adds predefined assemblies and assigns each Assets/**/*.cs file to its nearest custom source root.
    // Legacy explicit roots remain supported. Custom assemblies cannot reference predefined assemblies.
    [[nodiscard]] KEIRE_API std::vector<ManagedAssemblyGraphEntry>
    ResolveProjectManagedAssemblies(const std::filesystem::path& projectRoot,
                                    std::span<const ManagedAssemblyGraphEntry> customAssemblies,
                                    const ManagedAssemblyBuildContext& context = {});
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateManagedAssemblyAssetDecoder();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateManagedAssemblyAssetImporter();
} // namespace Keire
