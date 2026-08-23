#pragma once

#include "Keire/Core.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace KeireEditor
{
    struct ManagedDataTypeCacheRefresh
    {
        std::size_t DecodedSources = 0;
        std::size_t ReusedSources = 0;
        std::vector<std::string> Diagnostics;
    };

    class ManagedDataTypeCache final
    {
      public:
        [[nodiscard]] ManagedDataTypeCacheRefresh Refresh(std::span<const Keire::AssetSourceRecord> records,
                                                          const std::filesystem::path& sourceRoot,
                                                          std::size_t maximumSourceBytes);
        [[nodiscard]] std::optional<Keire::ManagedTypeId> Type(Keire::AssetId asset) const noexcept;

      private:
        struct Entry
        {
            std::filesystem::path RelativePath;
            std::string SourceDigest;
            std::filesystem::file_time_type Modified;
            std::uintmax_t Size = 0;
            std::optional<Keire::ManagedTypeId> Type;
            std::optional<std::size_t> FailedMaximumSourceBytes;
        };

        std::unordered_map<Keire::AssetId, Entry> m_Entries;
    };
} // namespace KeireEditor
