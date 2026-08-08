#include "doctest/doctest.h"

#include "KeireTests/TestSupport.h"

#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <Windows.h>
#else
#include <unistd.h>
#endif

TEST_CASE("detached process launch reports a trackable process identity")
{
#if defined(_WIN32)
    const auto currentProcessId = static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    const auto currentProcessId = static_cast<std::uint64_t>(getpid());
#endif
    CHECK(Keire::Detail::IsProcessAlive(currentProcessId));
    CHECK_FALSE(Keire::Detail::IsProcessAlive(0));

    const std::array arguments{std::string("--child-process-probe")};
    std::string diagnostic;
    std::uint64_t processId = 0;
    REQUIRE(Keire::Detail::LaunchDetachedProcess(KeireTests::TestExecutable, arguments,
                                                 KeireTests::TestExecutable.parent_path(), diagnostic, &processId));
    CHECK(processId != 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (Keire::Detail::IsProcessAlive(processId) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK_FALSE(Keire::Detail::IsProcessAlive(processId));
}

TEST_CASE("Child process captures output and preserves a nonzero exit code")
{
    const std::array arguments{std::string("--child-process-probe")};
    auto process = Keire::Detail::ChildProcess::Start(KeireTests::TestExecutable, arguments,
                                                      KeireTests::TestExecutable.parent_path());
    CHECK(process.ProcessId() != 0);
    REQUIRE(process.WaitFor(std::chrono::seconds(5)));
    REQUIRE(process.ExitCode());
    CHECK(*process.ExitCode() == 23);
    CHECK(process.TakeOutput().find("child-process-output") != std::string::npos);
}

#if defined(_WIN32)
TEST_CASE("captured child processes inherit only their declared standard handles")
{
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    const auto closeHandle = [](void* handle)
    {
        if (handle)
            CloseHandle(handle);
    };
    const std::unique_ptr<void, decltype(closeHandle)> inheritedEvent(CreateEventW(&security, TRUE, FALSE, nullptr),
                                                                      closeHandle);
    REQUIRE(inheritedEvent);
    const auto eventValue = std::to_string(reinterpret_cast<std::uintptr_t>(inheritedEvent.get()));
    const std::array arguments{std::string("--child-inherited-event-probe"), eventValue};

    auto child = Keire::Detail::ChildProcess::Start(KeireTests::TestExecutable, arguments,
                                                    KeireTests::TestExecutable.parent_path());
    REQUIRE(child.WaitFor(std::chrono::seconds(5)));
    REQUIRE(child.ExitCode());
    CHECK(*child.ExitCode() == 0);
    CHECK(WaitForSingleObject(inheritedEvent.get(), 0) == WAIT_TIMEOUT);

    const auto result = Keire::Detail::RunProcess(KeireTests::TestExecutable, arguments,
                                                  KeireTests::TestExecutable.parent_path(), std::chrono::seconds(5));
    CHECK_FALSE(result.TimedOut);
    CHECK(result.ExitCode == 0);
    CHECK(WaitForSingleObject(inheritedEvent.get(), 0) == WAIT_TIMEOUT);
}
#endif

TEST_CASE("companion executable resolution supports build, package, and Unicode layouts")
{
    const auto root = std::filesystem::absolute(
        std::filesystem::path("Build") /
        Keire::Detail::PathFromUtf8("Companion-Kéire-" + Keire::AssetId::Generate().ToString()));
#if defined(_WIN32)
    const auto hubName = Keire::Detail::PathFromUtf8("KeireHub.exe");
    const auto editorName = Keire::Detail::PathFromUtf8("KeireClient.exe");
#else
    const auto hubName = Keire::Detail::PathFromUtf8("KeireHub");
    const auto editorName = Keire::Detail::PathFromUtf8("KeireClient");
#endif
    const auto writeProbe = [](const std::filesystem::path& path)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << "probe";
    };

    const auto packagedHub = root / "package/bin" / hubName;
    const auto packagedEditor = root / "package/bin" / editorName;
    writeProbe(packagedHub);
    writeProbe(packagedEditor);
    CHECK(Keire::Detail::ResolveCompanionExecutable(packagedHub, "KeireClient") == packagedEditor);

    const auto buildHub = root / "build/KeireHub" / hubName;
    const auto buildEditor = root / "build/KeireClient" / editorName;
    writeProbe(buildHub);
    writeProbe(buildEditor);
    CHECK(Keire::Detail::ResolveCompanionExecutable(buildHub, "KeireClient") == buildEditor);

    try
    {
        (void)Keire::Detail::ResolveCompanionExecutable(root / "missing" / hubName, "KeireClient");
        FAIL("Expected companion resolution to reject missing executables.");
    }
    catch (const std::runtime_error& error)
    {
        CHECK(std::string(error.what()).find("Checked") != std::string::npos);
        CHECK(std::string(error.what()).find("KeireClient") != std::string::npos);
    }
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("external editor launch rejects unavailable sources without starting a process")
{
    std::string diagnostic;
    CHECK_FALSE(
        Keire::Detail::OpenInExternalEditor("missing-script.cs", {}, std::filesystem::current_path(), diagnostic));
    CHECK_FALSE(diagnostic.empty());
}

TEST_CASE("Child process termination is bounded and idempotent")
{
    const std::array arguments{std::string("--child-process-hang")};
    auto process = Keire::Detail::ChildProcess::Start(KeireTests::TestExecutable, arguments,
                                                      KeireTests::TestExecutable.parent_path());
    CHECK_FALSE(process.WaitFor(std::chrono::milliseconds(20)));
    const auto started = std::chrono::steady_clock::now();
    process.Terminate();
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(500));
    process.Terminate();
    CHECK_FALSE(process.Running());
    REQUIRE(process.ExitCode());
    CHECK(*process.ExitCode() != 0);
}

TEST_CASE("asset operation file locks serialize independent database owners and release cleanly")
{
    const auto root = std::filesystem::absolute(std::filesystem::path("Build") /
                                                ("InterprocessLock-" + Keire::AssetId::Generate().ToString()));
    const auto path = root / "project.lock";
    {
        Keire::Detail::InterprocessMutex first(path);
        Keire::Detail::InterprocessMutex second(path);
        first.lock();
        auto waiting = std::async(std::launch::async,
                                  [&second]
                                  {
                                      second.lock();
                                      second.unlock();
                                  });
        CHECK(waiting.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout);
        first.unlock();
        CHECK(waiting.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        waiting.get();
    }
    std::error_code error;
    std::filesystem::remove_all(root, error);
}
