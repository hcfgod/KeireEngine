#pragma once

#include "Keire/Assets/RenderingAssets.h"
#include "KeireInternal/Assets/ShaderCompileIdentity.h"
#include "KeireInternal/Assets/ShaderSourceSnapshot.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    struct ShaderPassCompileLane final
    {
        std::string Role;
        std::optional<std::string> Define;
    };

    // Fixed collision-checked slots bound committed entries without a shared eviction lock.
    inline constexpr std::size_t ShaderImportCacheSlotCount = 64;
    inline constexpr std::size_t ShaderImportCacheMaximumEntryBytes = 8U * 1024U * 1024U;

    class ShaderImportCache final
    {
      public:
        [[nodiscard]] static std::unique_ptr<ShaderImportCache>
        TryCreate(const std::filesystem::path& compiler, const AssetImportContext& context,
                  const ShaderAssetDefinition& definition, const ShaderImporterSpecification& specification,
                  std::span<const ShaderPassCompileLane> passes, std::span<const ShaderSourceFile> sources,
                  std::span<const std::filesystem::path> includeRoots,
                  std::span<const std::pair<std::string, std::string>> defines,
                  std::span<const std::byte> authoredManifest);

        [[nodiscard]] std::vector<std::byte> TryRead(const ShaderAssetDefinition& expected,
                                                     const ShaderImporterSpecification& specification,
                                                     std::span<const ShaderPassCompileLane> passes) const;
        void TryStore(std::span<const std::byte> validatedArtifact) const;
        [[nodiscard]] std::filesystem::path StageCompiler(const std::filesystem::path& directory) const;

      private:
        std::filesystem::path m_Project;
        std::filesystem::path m_Slot;
        std::string m_Key;
        ShaderToolchainSnapshot m_Toolchain;
    };
} // namespace Keire::Detail
