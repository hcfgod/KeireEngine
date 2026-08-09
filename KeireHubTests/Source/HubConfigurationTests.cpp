#include "KeireHub/HubApplicationFactory.h"
#include "KeireHub/HubConfiguration.h"

#include "KeireHubTests/TestSupport.h"

#include <doctest/doctest.h>

#include <stdexcept>

TEST_CASE("Hub development configuration falls back to the repository Config directory")
{
    const auto repositoryConfiguration = std::filesystem::current_path() / "Config" / "Distribution.json";
    REQUIRE(std::filesystem::is_regular_file(repositoryConfiguration));

    KeireHubTests::TemporaryDirectory temporary;
    const auto executable = temporary.Path() / "Build" / "Bin" / "KeireHub" / "KeireHub";
    CHECK(KeireHub::ResolveHubConfigurationPath(executable, "Distribution.json") == repositoryConfiguration);
}

TEST_CASE("Hub executable resolution makes relative launch paths absolute")
{
    const auto executable = KeireHub::ResolveHubExecutablePath("Build/Bin/KeireHub/KeireHub");
    CHECK(executable.is_absolute());
    CHECK(executable.filename() == "KeireHub");
    CHECK_THROWS_AS((void)KeireHub::ResolveHubExecutablePath({}), std::invalid_argument);
}

TEST_CASE("Packaged Hub configuration takes precedence over the development fallback")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto distribution = temporary.Path() / "distribution";
    const auto executable = distribution / "bin" / "KeireHub";
    const auto configuration = distribution / "Config" / "Distribution.json";
    KeireHubTests::WriteText(configuration, "{}");

    CHECK(KeireHub::ResolveHubConfigurationPath(executable, "Distribution.json") == configuration);
    CHECK_THROWS_AS((void)KeireHub::ResolveHubConfigurationPath(executable, "../Distribution.json"),
                    std::invalid_argument);
}

TEST_CASE("Hub remains visible after editor launch when no system tray is available")
{
    CHECK(KeireHub::ShouldHideHubAfterEditorLaunch(true, true));
    CHECK_FALSE(KeireHub::ShouldHideHubAfterEditorLaunch(true, false));
    CHECK_FALSE(KeireHub::ShouldHideHubAfterEditorLaunch(false, true));
}
