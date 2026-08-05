#include "doctest/doctest.h"

#include "EditorTestSupport.h"
#include "KeireHub/HubInstance.h"

#include "KeireInternal/Process.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>

namespace
{
    [[nodiscard]] std::optional<KeireHub::HubActivationRequest>
    WaitForActivation(KeireHub::HubInstanceCoordinator& instance)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        do
        {
            if (auto activation = instance.PollActivation())
                return activation;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < deadline);
        return std::nullopt;
    }

    template <std::size_t Size> void SendActivation(const std::array<std::string, Size>& arguments)
    {
        auto process = Keire::Detail::ChildProcess::Start(KeireEditorTests::ExecutablePath, arguments,
                                                          KeireEditorTests::ExecutablePath.parent_path());
        REQUIRE(process.WaitFor(std::chrono::seconds(5)));
        REQUIRE(process.ExitCode());
        INFO(process.TakeOutput());
        CHECK(*process.ExitCode() == 0);
    }
} // namespace

TEST_CASE("Hub instance coordination activates one primary process and releases ownership on shutdown")
{
    const auto identity =
        std::filesystem::temp_directory_path() /
        ("keire-hub-instance-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        KeireHub::HubInstanceCoordinator primary(identity, {}, true);
        REQUIRE(primary.IsPrimary());

        const std::array showArguments{std::string("--hub-instance-secondary"), identity.string(), std::string("show")};
        SendActivation(showArguments);
        const auto show = WaitForActivation(primary);
        REQUIRE(show);
        CHECK_FALSE(show->RequestsBuildSupport());

        const std::array supportArguments{std::string("--hub-instance-secondary"), identity.string(),
                                          std::string("build-support"), std::string("windows"), std::string("arm64")};
        SendActivation(supportArguments);
        const auto support = WaitForActivation(primary);
        REQUIRE(support);
        REQUIRE(support->Platform);
        REQUIRE(support->Architecture);
        CHECK(*support->Platform == "windows");
        CHECK(*support->Architecture == "arm64");
    }

    const KeireHub::HubInstanceCoordinator replacement(identity, {}, true);
    CHECK(replacement.IsPrimary());
}
