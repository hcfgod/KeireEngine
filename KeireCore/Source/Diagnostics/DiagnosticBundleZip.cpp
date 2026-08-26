#include "DiagnosticBundleSupport.h"

#include <array>
#include <limits>
#include <set>
#include <stdexcept>

namespace Keire::Internal::DiagnosticBundleDetail
{
    namespace
    {
        constexpr std::uint16_t ZipVersion = 20U;
        constexpr std::uint16_t Utf8Flag = 0x0800U;
        constexpr std::uint16_t StoredMethod = 0U;
        constexpr std::uint16_t DosTime = 0U;
        constexpr std::uint16_t DosDate = 0x0021U;

        struct CentralRecord final
        {
            const ArchiveEntry* Entry = nullptr;
            std::uint32_t Crc = 0;
            std::uint32_t Size = 0;
            std::uint32_t Offset = 0;
        };

        void RequireCapacity(const std::size_t current, const std::size_t additional, const std::size_t maximum)
        {
            if (additional > maximum || current > maximum - additional)
                throw std::runtime_error("Diagnostic bundle exceeds its maximum archive size.");
        }

        void Append16(std::vector<std::byte>& output, const std::uint16_t value, const std::size_t maximum)
        {
            RequireCapacity(output.size(), 2U, maximum);
            output.push_back(static_cast<std::byte>(value));
            output.push_back(static_cast<std::byte>(value >> 8U));
        }

        void Append32(std::vector<std::byte>& output, const std::uint32_t value, const std::size_t maximum)
        {
            RequireCapacity(output.size(), 4U, maximum);
            output.push_back(static_cast<std::byte>(value));
            output.push_back(static_cast<std::byte>(value >> 8U));
            output.push_back(static_cast<std::byte>(value >> 16U));
            output.push_back(static_cast<std::byte>(value >> 24U));
        }

        void Append(std::vector<std::byte>& output, const std::span<const std::byte> bytes, const std::size_t maximum)
        {
            RequireCapacity(output.size(), bytes.size(), maximum);
            output.insert(output.end(), bytes.begin(), bytes.end());
        }

        void Append(std::vector<std::byte>& output, const std::string_view text, const std::size_t maximum)
        {
            Append(output, std::as_bytes(std::span(text.data(), text.size())), maximum);
        }

        [[nodiscard]] std::uint32_t Crc32(const std::span<const std::byte> bytes) noexcept
        {
            std::uint32_t crc = 0xffffffffU;
            for (const auto value : bytes)
            {
                crc ^= std::to_integer<std::uint8_t>(value);
                for (unsigned int bit = 0; bit < 8U; ++bit)
                    crc = (crc >> 1U) ^ (0xedb88320U & (0U - (crc & 1U)));
            }
            return ~crc;
        }
    } // namespace

    std::vector<std::byte> WriteZip(const std::span<const ArchiveEntry> entries, const std::size_t maximumArchiveBytes)
    {
        if (entries.empty())
            throw std::invalid_argument("A diagnostic bundle must contain at least one entry.");
        if (entries.size() > (std::numeric_limits<std::uint16_t>::max)())
            throw std::invalid_argument("A diagnostic bundle contains too many entries.");

        std::set<std::string, std::less<>> paths;
        std::vector<CentralRecord> records;
        records.reserve(entries.size());
        std::vector<std::byte> output;
        for (const auto& entry : entries)
        {
            if (!IsPortableArchivePath(entry.Path) || !paths.emplace(entry.Path).second)
                throw std::invalid_argument("A diagnostic bundle entry path is unsafe or duplicated.");
            if (entry.Path.size() > (std::numeric_limits<std::uint16_t>::max)() ||
                entry.Contents.size() > (std::numeric_limits<std::uint32_t>::max)() ||
                output.size() > (std::numeric_limits<std::uint32_t>::max)())
                throw std::runtime_error("A diagnostic bundle entry exceeds ZIP32 limits.");

            const auto size = static_cast<std::uint32_t>(entry.Contents.size());
            const auto crc = Crc32(entry.Contents);
            records.push_back(
                {.Entry = &entry, .Crc = crc, .Size = size, .Offset = static_cast<std::uint32_t>(output.size())});
            Append32(output, 0x04034b50U, maximumArchiveBytes);
            Append16(output, ZipVersion, maximumArchiveBytes);
            Append16(output, Utf8Flag, maximumArchiveBytes);
            Append16(output, StoredMethod, maximumArchiveBytes);
            Append16(output, DosTime, maximumArchiveBytes);
            Append16(output, DosDate, maximumArchiveBytes);
            Append32(output, crc, maximumArchiveBytes);
            Append32(output, size, maximumArchiveBytes);
            Append32(output, size, maximumArchiveBytes);
            Append16(output, static_cast<std::uint16_t>(entry.Path.size()), maximumArchiveBytes);
            Append16(output, 0U, maximumArchiveBytes);
            Append(output, entry.Path, maximumArchiveBytes);
            Append(output, entry.Contents, maximumArchiveBytes);
        }

        if (output.size() > (std::numeric_limits<std::uint32_t>::max)())
            throw std::runtime_error("A diagnostic bundle exceeds ZIP32 limits.");
        const auto centralOffset = static_cast<std::uint32_t>(output.size());
        for (const auto& record : records)
        {
            Append32(output, 0x02014b50U, maximumArchiveBytes);
            Append16(output, ZipVersion, maximumArchiveBytes);
            Append16(output, ZipVersion, maximumArchiveBytes);
            Append16(output, Utf8Flag, maximumArchiveBytes);
            Append16(output, StoredMethod, maximumArchiveBytes);
            Append16(output, DosTime, maximumArchiveBytes);
            Append16(output, DosDate, maximumArchiveBytes);
            Append32(output, record.Crc, maximumArchiveBytes);
            Append32(output, record.Size, maximumArchiveBytes);
            Append32(output, record.Size, maximumArchiveBytes);
            Append16(output, static_cast<std::uint16_t>(record.Entry->Path.size()), maximumArchiveBytes);
            Append16(output, 0U, maximumArchiveBytes);
            Append16(output, 0U, maximumArchiveBytes);
            Append16(output, 0U, maximumArchiveBytes);
            Append16(output, 0U, maximumArchiveBytes);
            Append32(output, 0U, maximumArchiveBytes);
            Append32(output, record.Offset, maximumArchiveBytes);
            Append(output, record.Entry->Path, maximumArchiveBytes);
        }
        const auto centralSize = output.size() - centralOffset;
        if (centralSize > (std::numeric_limits<std::uint32_t>::max)())
            throw std::runtime_error("A diagnostic bundle central directory exceeds ZIP32 limits.");

        Append32(output, 0x06054b50U, maximumArchiveBytes);
        Append16(output, 0U, maximumArchiveBytes);
        Append16(output, 0U, maximumArchiveBytes);
        Append16(output, static_cast<std::uint16_t>(records.size()), maximumArchiveBytes);
        Append16(output, static_cast<std::uint16_t>(records.size()), maximumArchiveBytes);
        Append32(output, static_cast<std::uint32_t>(centralSize), maximumArchiveBytes);
        Append32(output, centralOffset, maximumArchiveBytes);
        Append16(output, 0U, maximumArchiveBytes);
        return output;
    }
} // namespace Keire::Internal::DiagnosticBundleDetail
