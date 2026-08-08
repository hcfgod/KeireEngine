#pragma once

#include <KeireHubRuntimeInternal/Persistence.h>

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireHub::Detail
{
    struct UtcInstant final
    {
        std::int64_t UnixSeconds = 0;
        std::uint32_t Nanoseconds = 0;

        friend bool operator==(const UtcInstant&, const UtcInstant&) noexcept = default;
        friend std::strong_ordering operator<=>(const UtcInstant&, const UtcInstant&) noexcept = default;
    };

    [[nodiscard]] std::string EncodeBase64(std::span<const std::byte> bytes);
    [[nodiscard]] std::string Sha256Hex(std::span<const std::byte> bytes);
    [[nodiscard]] std::string MakeDistributionETag(std::span<const std::byte> bytes);
    [[nodiscard]] std::optional<std::vector<std::byte>> DecodeCanonicalBase64(std::string_view value,
                                                                              std::size_t maximumBytes);
    [[nodiscard]] HubResult<Json> ParseStrictJson(std::string_view document, std::size_t maximumDepth,
                                                  HubErrorCode code, std::string message,
                                                  std::string affectedItem = {});
    [[nodiscard]] std::optional<UtcInstant> ParseUtcInstant(std::string_view value) noexcept;
    [[nodiscard]] UtcInstant ToUtcInstant(std::chrono::system_clock::time_point value) noexcept;
    [[nodiscard]] bool HasMinimumValidity(const UtcInstant& expiry, const UtcInstant& now,
                                          std::chrono::seconds minimumRemaining) noexcept;
    [[nodiscard]] bool IsDistributionKeyId(std::string_view value) noexcept;
    [[nodiscard]] bool IsDistributionRouteToken(std::string_view value) noexcept;
    [[nodiscard]] bool IsDistributionLocale(std::string_view value) noexcept;
    [[nodiscard]] std::optional<std::string> NormalizeServiceBaseUrl(std::string_view value,
                                                                     bool allowInsecureLoopback);
    [[nodiscard]] bool EqualsCaseInsensitiveAscii(std::string_view left, std::string_view right) noexcept;
} // namespace KeireHub::Detail
