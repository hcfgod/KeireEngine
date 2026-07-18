#pragma once

#include "Keire/Assets/Asset.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire::Detail
{
    using Sha256Digest = std::array<std::byte, 32>;

    struct CatalogEntry
    {
        AssetId Id;
        AssetTypeId Type;
        std::filesystem::path PackPath;
        std::uint64_t Offset = 0;
        std::uint64_t CompressedBytes = 0;
        std::uint64_t UncompressedBytes = 0;
        Sha256Digest Digest{};
        std::vector<AssetId> Dependencies;
    };

    struct CatalogData
    {
        std::filesystem::path Path;
        std::vector<CatalogEntry> Entries;
    };

    inline constexpr std::array<char, 8> PackMagic{'K', 'E', 'I', 'R', 'E', 'P', 'A', 'K'};
    inline constexpr std::uint32_t PackVersion = 1;
    inline constexpr std::uint64_t PackHeaderBytes = 16;

    [[nodiscard]] Sha256Digest Sha256(std::span<const std::byte> bytes) noexcept;
    [[nodiscard]] std::string DigestToString(const Sha256Digest& digest);
    [[nodiscard]] Sha256Digest ParseDigest(std::string_view value);
    [[nodiscard]] CatalogData LoadCatalog(const std::filesystem::path& path);
    void WriteCatalog(const std::filesystem::path& path, std::span<const CatalogEntry> entries);
    void WritePackHeader(std::ostream& stream);
    void ValidatePackHeader(std::istream& stream, const std::filesystem::path& path);
    void AtomicReplace(const std::filesystem::path& temporary, const std::filesystem::path& destination);
} // namespace Keire::Detail
