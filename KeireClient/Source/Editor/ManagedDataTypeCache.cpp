#include "KeireClient/Editor/ManagedDataTypeCache.h"

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        [[nodiscard]] std::vector<std::byte> ReadBounded(const std::filesystem::path& path, const std::uintmax_t size,
                                                         const std::size_t maximumSourceBytes)
        {
            if (size > maximumSourceBytes)
            {
                throw std::runtime_error("source exceeds the configured " + std::to_string(maximumSourceBytes) +
                                         " byte limit");
            }
            if (size > std::numeric_limits<std::size_t>::max() ||
                size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
            {
                throw std::runtime_error("source is too large to decode on this platform");
            }

            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            std::ifstream input(path, std::ios::binary);
            if (!input)
                throw std::runtime_error("source could not be opened");
            if (!bytes.empty() &&
                !input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            {
                throw std::runtime_error("source could not be read completely");
            }
            return bytes;
        }

        [[nodiscard]] std::string Diagnostic(const std::filesystem::path& relativePath, const std::string_view message)
        {
            return "Managed data picker ignored '" + relativePath.generic_string() + "': " + std::string(message) + '.';
        }
    } // namespace

    ManagedDataTypeCacheRefresh ManagedDataTypeCache::Refresh(const std::span<const Keire::AssetSourceRecord> records,
                                                              const std::filesystem::path& sourceRoot,
                                                              const std::size_t maximumSourceBytes)
    {
        ManagedDataTypeCacheRefresh result;
        std::unordered_set<Keire::AssetId> retained;
        for (const auto& record : records)
        {
            if (record.Type != Keire::ManagedDataAsset::StaticType())
                continue;
            retained.insert(record.Id);

            std::filesystem::path source;
            try
            {
                source = Keire::Detail::ResolveConfinedPath(sourceRoot, record.RelativePath);
            }
            catch (const std::exception& exception)
            {
                m_Entries.erase(record.Id);
                result.Diagnostics.push_back(Diagnostic(record.RelativePath, exception.what()));
                continue;
            }
            std::error_code error;
            const auto size = std::filesystem::file_size(source, error);
            if (error)
            {
                m_Entries.erase(record.Id);
                result.Diagnostics.push_back(Diagnostic(record.RelativePath, "source size could not be queried"));
                continue;
            }
            const auto modified = std::filesystem::last_write_time(source, error);
            if (error)
            {
                m_Entries.erase(record.Id);
                result.Diagnostics.push_back(
                    Diagnostic(record.RelativePath, "source modification time could not be queried"));
                continue;
            }

            const auto previous = m_Entries.find(record.Id);
            if (previous != m_Entries.end() && previous->second.RelativePath == record.RelativePath &&
                previous->second.SourceDigest == record.SourceDigest && previous->second.Modified == modified &&
                previous->second.Size == size &&
                (!previous->second.FailedMaximumSourceBytes ||
                 *previous->second.FailedMaximumSourceBytes == maximumSourceBytes))
            {
                ++result.ReusedSources;
                continue;
            }

            Entry entry{.RelativePath = record.RelativePath,
                        .SourceDigest = record.SourceDigest,
                        .Modified = modified,
                        .Size = size};
            ++result.DecodedSources;
            if (size > maximumSourceBytes)
            {
                entry.FailedMaximumSourceBytes = maximumSourceBytes;
                result.Diagnostics.push_back(
                    Diagnostic(record.RelativePath,
                               "source exceeds the configured " + std::to_string(maximumSourceBytes) + " byte limit"));
                m_Entries.insert_or_assign(record.Id, std::move(entry));
                continue;
            }

            std::vector<std::byte> bytes;
            try
            {
                bytes = ReadBounded(source, size, maximumSourceBytes);
            }
            catch (const std::exception& exception)
            {
                m_Entries.erase(record.Id);
                result.Diagnostics.push_back(Diagnostic(record.RelativePath, exception.what()));
                continue;
            }

            try
            {
                entry.Type = Keire::ManagedDataAsset::Decode(bytes)->Definition().ManagedType;
            }
            catch (const std::exception& exception)
            {
                result.Diagnostics.push_back(Diagnostic(record.RelativePath, exception.what()));
            }
            m_Entries.insert_or_assign(record.Id, std::move(entry));
        }

        std::erase_if(m_Entries, [&retained](const auto& entry) { return !retained.contains(entry.first); });
        return result;
    }

    std::optional<Keire::ManagedTypeId> ManagedDataTypeCache::Type(const Keire::AssetId asset) const noexcept
    {
        const auto found = m_Entries.find(asset);
        return found == m_Entries.end() ? std::nullopt : found->second.Type;
    }

} // namespace KeireEditor
