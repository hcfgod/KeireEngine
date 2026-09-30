#pragma once

#include <cstddef>
#include <string_view>

namespace Keire::Detail
{
    namespace PortablePathDetail
    {
        [[nodiscard]] constexpr char LowercaseAscii(const char value) noexcept
        {
            return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
        }

        [[nodiscard]] constexpr bool EqualsAsciiInsensitive(const std::string_view left,
                                                            const std::string_view right) noexcept
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index = 0; index < left.size(); ++index)
                if (LowercaseAscii(left[index]) != LowercaseAscii(right[index]))
                    return false;
            return true;
        }

        [[nodiscard]] constexpr bool HasDevicePrefix(const std::string_view value,
                                                     const std::string_view prefix) noexcept
        {
            return value.size() >= prefix.size() && EqualsAsciiInsensitive(value.substr(0, prefix.size()), prefix);
        }
    } // namespace PortablePathDetail

    [[nodiscard]] constexpr bool IsPortableDirectoryComponent(const std::string_view value) noexcept
    {
        if (value.empty() || value == "." || value == ".." || value.back() == ' ' || value.back() == '.')
            return false;

        constexpr std::string_view reservedCharacters = "<>:\"/\\|?*";
        for (const unsigned char byte : value)
        {
            if (byte <= 0x1FU || reservedCharacters.find(static_cast<char>(byte)) != std::string_view::npos)
                return false;
        }

        auto stem = value.substr(0, value.find('.'));
        while (!stem.empty() && (stem.back() == ' ' || stem.back() == '.'))
            stem.remove_suffix(1);
        if (PortablePathDetail::EqualsAsciiInsensitive(stem, "con") ||
            PortablePathDetail::EqualsAsciiInsensitive(stem, "prn") ||
            PortablePathDetail::EqualsAsciiInsensitive(stem, "aux") ||
            PortablePathDetail::EqualsAsciiInsensitive(stem, "nul"))
        {
            return false;
        }

        const bool communicationDevice = PortablePathDetail::HasDevicePrefix(stem, "com");
        const bool printerDevice = PortablePathDetail::HasDevicePrefix(stem, "lpt");
        if (!communicationDevice && !printerDevice)
            return true;
        const auto suffix = stem.substr(3);
        if (suffix.size() == 1 && suffix.front() >= '1' && suffix.front() <= '9')
            return false;
        return suffix != "\xC2\xB9" && suffix != "\xC2\xB2" && suffix != "\xC2\xB3";
    }
} // namespace Keire::Detail
