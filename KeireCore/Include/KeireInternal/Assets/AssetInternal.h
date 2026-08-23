#pragma once

#include "Keire/Assets/Asset.h"
#include "Keire/Assets/AssetMetadata.h"

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
    class AnchoredFileSystem;
    struct AnchoredFileMetadata;

    using Sha256Digest = std::array<std::byte, 32>;

    struct CatalogPage
    {
        std::uint64_t Offset = 0;
        std::uint64_t CompressedBytes = 0;
        std::uint64_t UncompressedOffset = 0;
        std::uint64_t UncompressedBytes = 0;
        Sha256Digest Digest{};
    };

    struct CatalogSegment
    {
        std::uint8_t Kind = 0;
        std::uint32_t Segment = 0;
        std::uint64_t UncompressedOffset = 0;
        std::uint64_t UncompressedBytes = 0;
        float WindowStartSeconds = 0.0F;
        float WindowEndSeconds = 0.0F;
    };

    struct CatalogEntry
    {
        AssetId Id;
        AssetTypeId Type;
        std::filesystem::path PackPath;
        std::uint64_t Offset = 0;
        std::uint64_t CompressedBytes = 0;
        std::uint64_t UncompressedBytes = 0;
        std::uint32_t StreamLayoutVersion = 0;
        Sha256Digest Digest{};
        std::vector<CatalogPage> Pages;
        std::vector<CatalogSegment> Segments;
        std::vector<AssetId> Dependencies;
        AssetDerivedMetadata Metadata;
    };

    struct CatalogData
    {
        std::filesystem::path Path;
        std::vector<CatalogEntry> Entries;
    };

    inline constexpr std::array<char, 8> PackMagic{'K', 'E', 'I', 'R', 'E', 'P', 'A', 'K'};
    inline constexpr std::uint32_t PackVersion = 2;
    inline constexpr std::uint32_t MinimumReadablePackVersion = 1;
    inline constexpr std::uint64_t PackHeaderBytes = 16;

    [[nodiscard]] Sha256Digest Sha256(std::span<const std::byte> bytes) noexcept;
    [[nodiscard]] Sha256Digest Sha256File(const std::filesystem::path& path,
                                          std::uintmax_t maximumBytes = 16ULL * 1024ULL * 1024ULL * 1024ULL);
    [[nodiscard]] Sha256Digest Sha256File(const AnchoredFileSystem& fileSystem, const std::filesystem::path& relative,
                                          std::uintmax_t maximumBytes, AnchoredFileMetadata& metadata);
    [[nodiscard]] std::string DigestToString(const Sha256Digest& digest);
    [[nodiscard]] Sha256Digest ParseDigest(std::string_view value);
    [[nodiscard]] CatalogData LoadCatalog(const std::filesystem::path& path);
    void WriteCatalog(const std::filesystem::path& path, std::span<const CatalogEntry> entries);
    void WritePackHeader(std::ostream& stream);
    void ValidatePackHeader(std::istream& stream, const std::filesystem::path& path);
    void AtomicReplace(const std::filesystem::path& temporary, const std::filesystem::path& destination);
} // namespace Keire::Detail
