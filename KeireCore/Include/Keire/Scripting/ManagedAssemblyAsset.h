#pragma once

#include "Keire/Api.h"
#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Keire
{
    enum class ManagedAssemblyClassification : std::uint8_t
    {
        Runtime,
        Editor,
        Tests
    };

    struct ManagedAssemblyDefinition
    {
        std::uint32_t SchemaVersion = 1;
        std::string Name;
        std::string RootNamespace;
        ManagedAssemblyClassification Classification = ManagedAssemblyClassification::Runtime;
        std::vector<std::filesystem::path> SourceRoots;
        std::vector<AssetId> References;
    };

    struct ManagedAssemblyGraphEntry
    {
        AssetId Asset;
        ManagedAssemblyDefinition Definition;
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
    [[nodiscard]] KEIRE_API AssetDecoderRegistration CreateManagedAssemblyAssetDecoder();
    [[nodiscard]] KEIRE_API AssetImporterRegistration CreateManagedAssemblyAssetImporter();
} // namespace Keire
