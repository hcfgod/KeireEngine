#include <KeireHubRuntimeInternal/DistributionEncoding.h>

#include <KeireHubRuntimeInternal/Sha256.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <ranges>
#include <set>

namespace KeireHub::Detail
{
    namespace
    {
        constexpr std::string_view Base64Alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        [[nodiscard]] std::optional<unsigned int> Base64Value(const char character) noexcept
        {
            const auto position = Base64Alphabet.find(character);
            if (position == std::string_view::npos)
                return std::nullopt;
            return static_cast<unsigned int>(position);
        }

        [[nodiscard]] std::optional<unsigned int> Decimal(const std::string_view value, const std::size_t offset,
                                                          const std::size_t count) noexcept
        {
            if (offset > value.size() || count > value.size() - offset)
                return std::nullopt;
            unsigned int result = 0;
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto character = value[offset + index];
                if (character < '0' || character > '9')
                    return std::nullopt;
                result = result * 10U + static_cast<unsigned int>(character - '0');
            }
            return result;
        }

        [[nodiscard]] constexpr bool IsLeapYear(const unsigned int year) noexcept
        {
            return year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U);
        }

        [[nodiscard]] constexpr unsigned int DaysInMonth(const unsigned int year, const unsigned int month) noexcept
        {
            constexpr std::array<unsigned int, 12> values{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            return month == 2U && IsLeapYear(year) ? 29U : values[month - 1U];
        }

        [[nodiscard]] constexpr std::int64_t DaysFromCivil(int year, const unsigned int month,
                                                           const unsigned int day) noexcept
        {
            year -= month <= 2U;
            const auto era = (year >= 0 ? year : year - 399) / 400;
            const auto yearOfEra = static_cast<unsigned int>(year - era * 400);
            const auto adjustedMonth = month > 2U ? month - 3U : month + 9U;
            const auto dayOfYear = (153U * adjustedMonth + 2U) / 5U + day - 1U;
            const auto dayOfEra = yearOfEra * 365U + yearOfEra / 4U - yearOfEra / 100U + dayOfYear;
            return static_cast<std::int64_t>(era) * 146097LL + static_cast<std::int64_t>(dayOfEra) - 719468LL;
        }

        [[nodiscard]] bool IsLoopbackHost(const std::string_view host) noexcept
        {
            if (host == "localhost" || host == "[::1]")
                return true;
            std::array<unsigned int, 4> octets{};
            std::size_t offset = 0;
            for (std::size_t index = 0; index < octets.size(); ++index)
            {
                const auto separator = host.find('.', offset);
                const auto end = index + 1U == octets.size() ? host.size() : separator;
                if (end == std::string_view::npos || end == offset || end - offset > 3U)
                    return false;
                auto parsed = Decimal(host, offset, end - offset);
                if (!parsed || *parsed > 255U || std::to_string(*parsed) != host.substr(offset, end - offset))
                    return false;
                octets[index] = *parsed;
                offset = end + 1U;
            }
            return offset == host.size() + 1U && octets[0] == 127U;
        }

        [[nodiscard]] bool IsHostCharacter(const unsigned char character) noexcept
        {
            return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9') || character == '.' || character == '-';
        }
    } // namespace

    std::string EncodeBase64(const std::span<const std::byte> bytes)
    {
        if (bytes.empty())
            return {};
        std::string result;
        result.reserve((bytes.size() + 2U) / 3U * 4U);
        for (std::size_t offset = 0; offset < bytes.size(); offset += 3U)
        {
            const auto first = std::to_integer<unsigned int>(bytes[offset]);
            const auto second = offset + 1U < bytes.size() ? std::to_integer<unsigned int>(bytes[offset + 1U]) : 0U;
            const auto third = offset + 2U < bytes.size() ? std::to_integer<unsigned int>(bytes[offset + 2U]) : 0U;
            const auto packed = (first << 16U) | (second << 8U) | third;
            result.push_back(Base64Alphabet[(packed >> 18U) & 0x3fU]);
            result.push_back(Base64Alphabet[(packed >> 12U) & 0x3fU]);
            result.push_back(offset + 1U < bytes.size() ? Base64Alphabet[(packed >> 6U) & 0x3fU] : '=');
            result.push_back(offset + 2U < bytes.size() ? Base64Alphabet[packed & 0x3fU] : '=');
        }
        return result;
    }

    std::string Sha256Hex(const std::span<const std::byte> bytes)
    {
        Sha256Builder digest;
        digest.Update(bytes);
        return DigestToString(digest.Finish());
    }

    std::string MakeDistributionETag(const std::span<const std::byte> bytes)
    {
        return "\"sha256-" + Sha256Hex(bytes) + "\"";
    }

    std::optional<std::vector<std::byte>> DecodeCanonicalBase64(const std::string_view value,
                                                                const std::size_t maximumBytes)
    {
        if (value.size() % 4U != 0U || value.size() / 4U > maximumBytes / 3U + 1U)
            return std::nullopt;
        std::vector<std::byte> result;
        result.reserve(value.size() / 4U * 3U);
        for (std::size_t offset = 0; offset < value.size(); offset += 4U)
        {
            const bool final = offset + 4U == value.size();
            const bool thirdPadding = value[offset + 2U] == '=';
            const bool fourthPadding = value[offset + 3U] == '=';
            if ((!final && (thirdPadding || fourthPadding)) || (thirdPadding && !fourthPadding))
                return std::nullopt;
            auto first = Base64Value(value[offset]);
            auto second = Base64Value(value[offset + 1U]);
            auto third = thirdPadding ? std::optional<unsigned int>{0U} : Base64Value(value[offset + 2U]);
            auto fourth = fourthPadding ? std::optional<unsigned int>{0U} : Base64Value(value[offset + 3U]);
            if (!first || !second || !third || !fourth)
                return std::nullopt;
            const auto packed = (*first << 18U) | (*second << 12U) | (*third << 6U) | *fourth;
            result.push_back(static_cast<std::byte>((packed >> 16U) & 0xffU));
            if (!thirdPadding)
                result.push_back(static_cast<std::byte>((packed >> 8U) & 0xffU));
            if (!fourthPadding)
                result.push_back(static_cast<std::byte>(packed & 0xffU));
            if (result.size() > maximumBytes)
                return std::nullopt;
        }
        if (EncodeBase64(result) != value)
            return std::nullopt;
        return result;
    }

    HubResult<Json> ParseStrictJson(const std::string_view document, const std::size_t maximumDepth,
                                    const HubErrorCode code, std::string message, std::string affectedItem)
    {
        bool duplicate = false;
        bool tooDeep = false;
        std::vector<std::set<std::string, std::less<>>> objectKeys;
        const auto callback = [&](const int depth, const Json::parse_event_t event, Json& parsed)
        {
            if (depth < 0 || static_cast<std::size_t>(depth) > maximumDepth)
                tooDeep = true;
            if (event == Json::parse_event_t::object_start)
                objectKeys.emplace_back();
            else if (event == Json::parse_event_t::key)
            {
                if (objectKeys.empty() || !objectKeys.back().insert(parsed.get<std::string>()).second)
                    duplicate = true;
            }
            else if (event == Json::parse_event_t::object_end)
            {
                if (!objectKeys.empty())
                    objectKeys.pop_back();
            }
            return true;
        };
        try
        {
            auto result = Json::parse(document, callback, true, false);
            if (duplicate || tooDeep || result.is_discarded())
                throw std::invalid_argument("Duplicate properties or excessive nesting are not allowed.");
            return HubResult<Json>::Success(std::move(result));
        }
        catch (const std::exception& error)
        {
            return HubResult<Json>::Failure({.Code = code,
                                             .Message = std::move(message),
                                             .AffectedItem = std::move(affectedItem),
                                             .TechnicalDetails = error.what()});
        }
    }

    std::optional<UtcInstant> ParseUtcInstant(const std::string_view value) noexcept
    {
        if (value.size() < 20U || value[4] != '-' || value[7] != '-' || value[10] != 'T' || value[13] != ':' ||
            value[16] != ':')
            return std::nullopt;
        const auto year = Decimal(value, 0, 4);
        const auto month = Decimal(value, 5, 2);
        const auto day = Decimal(value, 8, 2);
        const auto hour = Decimal(value, 11, 2);
        const auto minute = Decimal(value, 14, 2);
        const auto second = Decimal(value, 17, 2);
        if (!year || !month || !day || !hour || !minute || !second || *year == 0U || *month == 0U || *month > 12U ||
            *day == 0U || *day > DaysInMonth(*year, *month) || *hour > 23U || *minute > 59U || *second > 59U)
            return std::nullopt;

        std::size_t offset = 19U;
        std::uint32_t nanoseconds = 0;
        if (offset < value.size() && value[offset] == '.')
        {
            ++offset;
            const auto fractionStart = offset;
            while (offset < value.size() && value[offset] >= '0' && value[offset] <= '9')
                ++offset;
            const auto digits = offset - fractionStart;
            if (digits == 0U || digits > 9U)
                return std::nullopt;
            auto fraction = Decimal(value, fractionStart, digits);
            if (!fraction)
                return std::nullopt;
            nanoseconds = *fraction;
            for (std::size_t index = digits; index < 9U; ++index)
                nanoseconds *= 10U;
        }
        if (offset == value.size())
            return std::nullopt;
        if (value[offset] == 'Z')
        {
            if (offset + 1U != value.size())
                return std::nullopt;
        }
        else
        {
            if (offset + 6U != value.size() || (value[offset] != '+' && value[offset] != '-') ||
                value[offset + 1U] != '0' || value[offset + 2U] != '0' || value[offset + 3U] != ':' ||
                value[offset + 4U] != '0' || value[offset + 5U] != '0')
                return std::nullopt;
        }
        const auto seconds = DaysFromCivil(static_cast<int>(*year), *month, *day) * 86400LL +
                             static_cast<std::int64_t>(*hour) * 3600LL + static_cast<std::int64_t>(*minute) * 60LL +
                             static_cast<std::int64_t>(*second);
        return UtcInstant{.UnixSeconds = seconds, .Nanoseconds = nanoseconds};
    }

    UtcInstant ToUtcInstant(const std::chrono::system_clock::time_point value) noexcept
    {
        const auto sinceEpoch = value.time_since_epoch();
        const auto seconds = std::chrono::floor<std::chrono::seconds>(sinceEpoch);
        const auto remainder = std::chrono::duration_cast<std::chrono::nanoseconds>(sinceEpoch - seconds);
        return {.UnixSeconds = seconds.count(), .Nanoseconds = static_cast<std::uint32_t>(remainder.count())};
    }

    bool HasMinimumValidity(const UtcInstant& expiry, const UtcInstant& now,
                            const std::chrono::seconds minimumRemaining) noexcept
    {
        if (minimumRemaining.count() < 0 ||
            now.UnixSeconds > std::numeric_limits<std::int64_t>::max() - minimumRemaining.count())
            return false;
        const UtcInstant minimum{.UnixSeconds = now.UnixSeconds + minimumRemaining.count(),
                                 .Nanoseconds = now.Nanoseconds};
        return expiry > minimum;
    }

    bool IsDistributionKeyId(const std::string_view value) noexcept
    {
        const auto isAlphaNumeric = [](const unsigned char character)
        {
            return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9');
        };
        if (value.empty() || value.size() > 64U || !isAlphaNumeric(static_cast<unsigned char>(value.front())))
            return false;
        return std::ranges::all_of(
            value, [&](const unsigned char character)
            { return isAlphaNumeric(character) || character == '.' || character == '_' || character == '-'; });
    }

    bool IsDistributionRouteToken(const std::string_view value) noexcept
    {
        if (value.empty() || value.size() > 64U || value.front() < 'a' || value.front() > 'z')
        {
            if (value.empty() || value.size() > 64U || value.front() < '0' || value.front() > '9')
                return false;
        }
        return std::ranges::all_of(value,
                                   [](const unsigned char character)
                                   {
                                       return (character >= 'a' && character <= 'z') ||
                                              (character >= '0' && character <= '9') || character == '.' ||
                                              character == '_' || character == '-';
                                   });
    }

    bool IsDistributionLocale(const std::string_view value) noexcept
    {
        const auto isAlphaNumeric = [](const unsigned char character)
        {
            return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                   (character >= '0' && character <= '9');
        };
        if (value.empty() || value.size() > 32U || !isAlphaNumeric(static_cast<unsigned char>(value.front())))
            return false;
        return std::ranges::all_of(value, [&](const unsigned char character)
                                   { return isAlphaNumeric(character) || character == '-'; });
    }

    std::optional<std::string> NormalizeServiceBaseUrl(const std::string_view value, const bool allowInsecureLoopback)
    {
        if (value.empty() || value.size() > 2048U ||
            std::ranges::any_of(value,
                                [](const unsigned char character) { return character < 0x21U || character > 0x7eU; }))
            return std::nullopt;
        const auto schemeEnd = value.find("://");
        if (schemeEnd == std::string_view::npos)
            return std::nullopt;
        auto scheme = std::string(value.substr(0, schemeEnd));
        std::ranges::transform(scheme, scheme.begin(), [](const unsigned char character)
                               { return static_cast<char>(std::tolower(character)); });
        if (scheme != "https" && scheme != "http")
            return std::nullopt;
        const auto authorityStart = schemeEnd + 3U;
        const auto pathStart = value.find('/', authorityStart);
        const auto authority =
            value.substr(authorityStart, pathStart == std::string_view::npos ? value.size() - authorityStart
                                                                             : pathStart - authorityStart);
        if (authority.empty() || authority.find('@') != std::string_view::npos)
            return std::nullopt;

        std::string host;
        std::string port;
        if (authority.front() == '[')
        {
            const auto bracket = authority.find(']');
            if (bracket == std::string_view::npos || bracket == 1U)
                return std::nullopt;
            host = std::string(authority.substr(0, bracket + 1U));
            if (bracket + 1U < authority.size())
            {
                if (authority[bracket + 1U] != ':')
                    return std::nullopt;
                port = std::string(authority.substr(bracket + 2U));
            }
            const auto interior = std::string_view(host).substr(1, host.size() - 2U);
            if (std::ranges::any_of(interior, [](const unsigned char character)
                                    { return !std::isxdigit(character) && character != ':' && character != '.'; }))
                return std::nullopt;
        }
        else
        {
            const auto colon = authority.rfind(':');
            const auto hostValue = colon == std::string_view::npos ? authority : authority.substr(0, colon);
            if (hostValue.empty() || !std::ranges::all_of(hostValue, IsHostCharacter))
                return std::nullopt;
            host = std::string(hostValue);
            if (colon != std::string_view::npos)
                port = std::string(authority.substr(colon + 1U));
        }
        std::ranges::transform(host, host.begin(), [](const unsigned char character)
                               { return static_cast<char>(std::tolower(character)); });
        if (!port.empty())
        {
            if (port.size() > 5U ||
                !std::ranges::all_of(port, [](const unsigned char character) { return std::isdigit(character); }))
                return std::nullopt;
            const auto number = std::stoul(port);
            if (number == 0UL || number > 65535UL)
                return std::nullopt;
        }
        if (scheme == "http" && (!allowInsecureLoopback || !IsLoopbackHost(host)))
            return std::nullopt;

        std::string path;
        if (pathStart != std::string_view::npos)
        {
            path = std::string(value.substr(pathStart));
            if (path.find_first_of("?#\\%") != std::string::npos)
                return std::nullopt;
            while (path.size() > 1U && path.back() == '/')
                path.pop_back();
            if (path == "/")
                path.clear();
            std::size_t offset = 1U;
            while (offset <= path.size())
            {
                const auto separator = path.find('/', offset);
                const auto component = std::string_view(path).substr(
                    offset, separator == std::string::npos ? path.size() - offset : separator - offset);
                if (component.empty() || component == "." || component == "..")
                    return std::nullopt;
                if (separator == std::string::npos)
                    break;
                offset = separator + 1U;
            }
        }
        return scheme + "://" + host + (port.empty() ? std::string{} : ':' + port) + path;
    }

    bool EqualsCaseInsensitiveAscii(const std::string_view left, const std::string_view right) noexcept
    {
        return left.size() == right.size() &&
               std::ranges::equal(left, right, [](const unsigned char first, const unsigned char second)
                                  { return std::tolower(first) == std::tolower(second); });
    }
} // namespace KeireHub::Detail
