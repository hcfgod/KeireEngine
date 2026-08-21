#include "KeireInternal/InputInternal.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace Keire::Detail
{
    std::filesystem::path InputBindingProfilePath(const std::filesystem::path& root, const std::string_view profile)
    {
        if (profile.empty() || profile.size() > 128 ||
            std::ranges::any_of(profile, [](const unsigned char value)
                                { return !std::isalnum(value) && value != '-' && value != '_'; }))
        {
            throw std::invalid_argument("Input binding profile names may contain only letters, digits, '-' and '_'.");
        }
        return root / (std::string(profile) + ".json");
    }
} // namespace Keire::Detail
