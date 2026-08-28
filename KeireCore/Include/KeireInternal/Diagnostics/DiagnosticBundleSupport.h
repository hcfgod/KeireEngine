#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Keire::Internal::DiagnosticBundleDetail
{
    struct SanitizedText final
    {
        std::string Contents;
        std::uint64_t Redactions = 0;
    };

    struct ArchiveEntry final
    {
        std::string Path;
        std::vector<std::byte> Contents;
    };

    [[nodiscard]] bool IsPortableArchivePath(std::string_view path) noexcept;
    [[nodiscard]] SanitizedText SanitizeText(std::string_view input);
    [[nodiscard]] std::vector<std::byte> WriteZip(std::span<const ArchiveEntry> entries,
                                                  std::size_t maximumArchiveBytes);
} // namespace Keire::Internal::DiagnosticBundleDetail
