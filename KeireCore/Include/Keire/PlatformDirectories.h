#pragma once

#include "Keire/Api.h"

#include <filesystem>

namespace Keire
{
    // Returns the per-user, per-product directory selected by the platform runtime.
    [[nodiscard]] KEIRE_API std::filesystem::path GetPreferenceDirectory();

    // Returns the platform's user-managed Documents folder. This is suitable for projects and other files the user
    // is expected to browse, move, or back up directly.
    [[nodiscard]] KEIRE_API std::filesystem::path GetUserDocumentsDirectory();
} // namespace Keire
