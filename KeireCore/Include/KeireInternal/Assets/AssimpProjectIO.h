#pragma once

#include "Keire/Assets/AssetPipeline.h"

#include <assimp/IOSystem.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Keire::Detail
{
    struct AssimpProjectFile final
    {
        std::filesystem::path RelativePath;
        std::span<const std::byte> Bytes;
    };

    // Adapts Assimp's private file interface to the asset pipeline's confined, bounded project-file callback.
    class AssimpProjectIO final : public Assimp::IOSystem
    {
      public:
        explicit AssimpProjectIO(const AssetImportContext& context);

        [[nodiscard]] bool Exists(const char* file) const override;
        [[nodiscard]] char getOsSeparator() const override;
        [[nodiscard]] Assimp::IOStream* Open(const char* file, const char* mode = "rb") override;
        void Close(Assimp::IOStream* file) override;

        [[nodiscard]] std::optional<AssimpProjectFile> ReadReferencedFile(std::string_view file);
        [[nodiscard]] bool ValidateReference(std::string_view file) const;
        [[nodiscard]] const std::vector<AssetSourceDependency>& SourceDependencies() const noexcept;
        [[nodiscard]] std::string_view LastReadFailure() const noexcept;
        [[nodiscard]] std::string_view Violation() const noexcept;

      private:
        struct CachedFile final
        {
            std::filesystem::path RelativePath;
            std::shared_ptr<const std::vector<std::byte>> Bytes;
            std::string Failure;
        };

        [[nodiscard]] const CachedFile* Read(std::string_view file) const;
        [[nodiscard]] std::optional<std::filesystem::path> Resolve(std::string_view file) const;
        void Reject(std::string message) const;

        std::filesystem::path m_SourcePrefix;
        std::filesystem::path m_SourceDirectory;
        std::size_t m_MaximumDependencyBytes = 0;
        std::function<std::vector<std::byte>(const std::filesystem::path&)> m_ReadProjectFile;
        mutable std::unordered_map<std::string, CachedFile> m_Cache;
        mutable std::vector<AssetSourceDependency> m_SourceDependencies;
        mutable std::size_t m_TotalBytes = 0;
        mutable std::string m_LastReadFailure;
        mutable std::string m_Violation;
    };
} // namespace Keire::Detail
