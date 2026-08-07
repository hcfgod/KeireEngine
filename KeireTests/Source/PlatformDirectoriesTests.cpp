#include "Keire/PlatformDirectories.h"

#include <doctest/doctest.h>

TEST_CASE("Platform directories resolve absolute user-owned locations")
{
    const auto preferences = Keire::GetPreferenceDirectory();
    const auto documents = Keire::GetUserDocumentsDirectory();

    CHECK(preferences.is_absolute());
    CHECK(documents.is_absolute());
    CHECK_FALSE(preferences.empty());
    CHECK_FALSE(documents.empty());
}
