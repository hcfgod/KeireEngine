#include "KeireInternal/Environment/EnvironmentTimeSettingsStore.h"
#include "KeireTests/TestSupport.h"

#include <doctest/doctest.h>

#include <limits>

namespace
{
    struct EnvironmentSettingsFixture final
    {
        std::filesystem::path Root = KeireTests::MakeTestDirectory("EnvironmentTimeSettings");

        ~EnvironmentSettingsFixture()
        {
            std::error_code error;
            std::filesystem::remove_all(Root, error);
        }

        [[nodiscard]] std::filesystem::path File() const
        {
            return Root / "ProjectSettings" / "EnvironmentTime.keiresettings";
        }
    };
} // namespace

TEST_CASE("Environment time settings round trip authored time and preserve failed saves")
{
    using Store = Keire::Detail::EnvironmentTimeSettingsStore;
    EnvironmentSettingsFixture fixture;
    CHECK(Store::Load(fixture.Root) == Keire::Detail::EnvironmentClockState{});
    CHECK_FALSE(std::filesystem::exists(fixture.Root));
    const Keire::Detail::EnvironmentClockState source{std::numeric_limits<std::int64_t>::max(), 0.12345678901234567,
                                                      2345.678, -2.0, true};
    Store::Save(fixture.Root, source);
    CHECK(Store::Load(fixture.Root) == source);
    const auto bytes = KeireTests::ReadFile(fixture.File());
    CHECK_THROWS_AS(Store::Save(fixture.Root, {0, 1.0}), std::invalid_argument);
    CHECK(KeireTests::ReadFile(fixture.File()) == bytes);
    auto replacement = source;
    replacement.Day = std::numeric_limits<std::int64_t>::min();
    Store::Save(fixture.Root, replacement);
    CHECK(Store::Load(fixture.Root) == replacement);
    CHECK_THROWS_AS((void)Store::Load({}), std::invalid_argument);
    CHECK_THROWS_AS(Store::Save({}, source), std::invalid_argument);
}

TEST_CASE("Environment time settings reject corruption instead of silently resetting")
{
    using Store = Keire::Detail::EnvironmentTimeSettingsStore;
    EnvironmentSettingsFixture fixture;
    Store::Save(fixture.Root, {});
    const auto valid = nlohmann::json::parse(KeireTests::ReadFile(fixture.File()));
    for (const auto version : {nlohmann::json(0), nlohmann::json(2), nlohmann::json(1.0), nlohmann::json("1")})
    {
        auto invalid = valid;
        invalid["schemaVersion"] = version;
        Keire::Detail::WriteTextFileAtomically(fixture.File(), invalid.dump());
        CHECK_THROWS_AS((void)Store::Load(fixture.Root), std::invalid_argument);
    }
    Keire::Detail::WriteTextFileAtomically(fixture.File(), "{");
    CHECK_THROWS((void)Store::Load(fixture.Root));
    Keire::Detail::WriteTextFileAtomically(fixture.File(), std::string(65537, ' '));
    CHECK_THROWS((void)Store::Load(fixture.Root));
    Store::Save(fixture.Root, {});
    CHECK(Store::Load(fixture.Root) == Keire::Detail::EnvironmentClockState{});
}

TEST_CASE("Environment time settings validate before creating files and reject directory targets")
{
    using Store = Keire::Detail::EnvironmentTimeSettingsStore;
    EnvironmentSettingsFixture fixture;
    CHECK_THROWS_AS(Store::Save(fixture.Root, {0, 0.0, 0.0}), std::invalid_argument);
    CHECK_FALSE(std::filesystem::exists(fixture.Root));
    std::filesystem::create_directories(fixture.File());
    CHECK_THROWS_AS((void)Store::Load(fixture.Root), std::runtime_error);
    CHECK_THROWS(Store::Save(fixture.Root, {}));
    CHECK(std::filesystem::is_directory(fixture.File()));
}
