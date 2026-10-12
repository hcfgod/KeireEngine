#pragma once

#include "Keire/Assets/AssetPackage.h"
#include "KeireInternal/FileSystem.h"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace KeireEditor::Detail
{
    class LocalPackageInspection final
    {
      public:
        [[nodiscard]] bool SetPath(std::string path)
        {
            if (path == m_Path)
                return false;
            m_Path = std::move(path);
            m_Metadata.reset();
            return true;
        }

        [[nodiscard]] const std::string& Path() const noexcept { return m_Path; }
        [[nodiscard]] const std::optional<Keire::AssetPackageArchiveMetadata>& Metadata() const noexcept
        {
            return m_Metadata;
        }

        // Clear the previous result before inspection: a failed refresh must not
        // leave an earlier archive available for review or installation.
        void Inspect()
        {
            m_Metadata.reset();
            const auto archive = std::filesystem::absolute(Keire::Detail::PathFromUtf8(m_Path)).lexically_normal();
            if (!std::filesystem::is_regular_file(archive) || archive.extension() != ".keireassetpackage")
                throw std::invalid_argument("Choose an existing .keireassetpackage file.");
            m_Metadata = Keire::InspectAssetPackageArchive(archive);
        }

        void Clear() noexcept
        {
            m_Metadata.reset();
            m_Path.clear();
        }

      private:
        std::string m_Path;
        std::optional<Keire::AssetPackageArchiveMetadata> m_Metadata;
    };
} // namespace KeireEditor::Detail
