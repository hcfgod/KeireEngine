#include "Keire/PlatformDirectories.h"

#include "Keire/BuildInfo.h"

#include <doctest/doctest.h>

TEST_CASE("Platform directories resolve absolute user-owned locations")
{
    const auto preferences = Keire::GetPreferenceDirectory();
    const auto documents = Keire::GetUserDocumentsDirectory();

    CHECK(preferences.is_absolute());
    CHECK(documents.is_absolute());
    CHECK_FALSE(preferences.empty());
    CHECK_FALSE(documents.empty());

    const auto encoded = preferences.generic_u8string();
    const auto project = Keire::GetBuildInfo().ProjectName;
    const auto* first = reinterpret_cast<const char8_t*>(project.data());
    const std::u8string expected(first, first + project.size());
    CHECK(encoded.find(expected) != std::u8string::npos);
    CHECK(encoded.find(u8"KÃ©ire") == std::u8string::npos);
}
