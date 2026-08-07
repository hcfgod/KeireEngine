#include "KeireHubRuntime/EditorSelection.h"

#include <doctest/doctest.h>

#include <array>

using namespace KeireHub;

namespace
{
    [[nodiscard]] EditorInstallation Editor(std::string id, std::string version, const std::uint32_t minimumSchema = 1,
                                            const std::uint32_t maximumSchema = 3)
    {
        return {.Id = std::move(id),
                .Version = std::move(version),
                .Root = "editors",
                .Entrypoints = {"bin/editor"},
                .MinimumProjectSchema = minimumSchema,
                .MaximumProjectSchema = maximumSchema,
                .Health = InstallationHealth::Healthy};
    }
} // namespace

TEST_CASE("Editor selection honors preferred and exact last-saved installations")
{
    const std::array editors{Editor("old", "1.0.0"), Editor("exact", "1.2.0"), Editor("new", "1.4.0")};
    auto exact =
        SelectCompatibleEditor(editors, {.LastSavedVersion = "1.2.0", .MinimumVersion = "1.0.0", .ProjectSchema = 3});
    REQUIRE(exact);
    CHECK(exact.Value().Id == "exact");

    auto preferred = SelectCompatibleEditor(
        editors, {.PreferredInstallationId = "new", .LastSavedVersion = "1.2.0", .ProjectSchema = 3});
    REQUIRE(preferred);
    CHECK(preferred.Value().Id == "new");

    auto stalePreferred = SelectCompatibleEditor(
        editors, {.PreferredInstallationId = "old", .LastSavedVersion = "1.2.0", .ProjectSchema = 3});
    REQUIRE(stalePreferred);
    CHECK(stalePreferred.Value().Id == "exact");
}

TEST_CASE("Editor selection recommends the least disruptive compatible newer version")
{
    const std::array editors{Editor("older", "1.0.0"), Editor("newest", "2.0.0"), Editor("next", "1.3.0")};
    auto selected = SelectCompatibleEditor(editors, {.LastSavedVersion = "1.2.0", .ProjectSchema = 3});
    REQUIRE(selected);
    CHECK(selected.Value().Id == "next");
}

TEST_CASE("Editor selection rejects old, unhealthy, and schema-incompatible installations")
{
    auto unhealthy = Editor("damaged", "2.0.0");
    unhealthy.Health = InstallationHealth::Damaged;
    const std::array editors{Editor("old", "1.0.0"), Editor("wrong-schema", "2.0.0", 4, 5), unhealthy};
    auto selected =
        SelectCompatibleEditor(editors, {.LastSavedVersion = "1.5.0", .MinimumVersion = "1.1.0", .ProjectSchema = 3});
    REQUIRE_FALSE(selected);
    CHECK(selected.Error().Retryable);
    CHECK(selected.Error().Code == HubErrorCode::NotFound);

    auto malformed = SelectCompatibleEditor(editors, {.LastSavedVersion = "not-a-version", .ProjectSchema = 3});
    REQUIRE_FALSE(malformed);
    CHECK(malformed.Error().Code == HubErrorCode::InvalidArgument);
}

TEST_CASE("Editor selection accepts a project schema newer than this Hub when the editor manifest supports it")
{
    const std::array editors{Editor("current-hub", "2.0.0", 1, 3), Editor("future-capable", "2.1.0", 3, 5)};
    const auto selected =
        SelectCompatibleEditor(editors, {.LastSavedVersion = "2.1.0", .MinimumVersion = "2.0.0", .ProjectSchema = 4});
    REQUIRE(selected);
    CHECK(selected.Value().Id == "future-capable");
}
