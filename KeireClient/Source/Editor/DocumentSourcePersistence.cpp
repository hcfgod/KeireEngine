#include "KeireClient/Editor/DocumentSourcePersistence.h"

#include "KeireClient/Editor/EditorAssetFileService.h"
#include "KeireInternal/FileSystem.h"

#include <system_error>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] std::optional<std::vector<std::byte>> ReadRevision(const std::filesystem::path& path)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            if (error && error != std::errc::no_such_file_or_directory)
                throw std::filesystem::filesystem_error("Cannot inspect document source", path, error);
            if (status.type() == std::filesystem::file_type::not_found)
                return std::nullopt;
            if (!std::filesystem::is_regular_file(status))
                throw std::runtime_error("Document source must be a regular file, not a directory or symbolic link.");
            return Detail::ReadBytes(path, "document");
        }
    } // namespace

    DocumentSourceConflict::DocumentSourceConflict()
        : std::runtime_error("The document source changed or was deleted externally. Reload the source or preserve "
                             "local changes in a separate asset before retrying.")
    {
    }

    void DocumentSourcePersistence::Bind(std::filesystem::path path, std::optional<std::vector<std::byte>> loadedBytes)
    {
        auto expected = loadedBytes ? std::move(loadedBytes) : path.empty() ? std::nullopt : ReadRevision(path);
        m_Path = std::move(path);
        m_Expected = std::move(expected);
        m_Conflict = false;
    }

    void DocumentSourcePersistence::Publish(std::vector<std::byte> bytes, const bool overwriteExternalChanges)
    {
        if (m_Path.empty())
            throw std::logic_error("Document publication requires a source path.");
        if (bytes.size() > 64U * 1024U * 1024U)
            throw std::runtime_error("Document source exceeds the supported size limit.");
        const auto actual = ReadRevision(m_Path);
        if (!overwriteExternalChanges && actual != m_Expected)
        {
            m_Conflict = true;
            throw DocumentSourceConflict();
        }
        // Ordinary filesystem replacement is not a compare-and-swap against non-cooperating external writers.
        // Check before publication; never accept a new baseline on a failed publication.
        Keire::Detail::WriteFileAtomically(m_Path, bytes);
        m_Expected = std::move(bytes);
        m_Conflict = false;
    }
} // namespace KeireEditor
