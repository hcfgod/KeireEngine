#include "TestSupport.h"

#include "KeireHubRuntime/ProjectStatusProbe.h"

#include <doctest/doctest.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

using namespace KeireHub;

TEST_CASE("Project status probing distinguishes stale and actively held editor locks")
{
    KeireHubTests::TemporaryDirectory temporary;
    const auto root = temporary.Path() / "Project";
    const auto lockPath = root / "Library" / "Editor.lock";
    KeireHubTests::WriteText(lockPath, "lock\n");

    const auto stale = ProbeProjectLock(root);
    REQUIRE(stale);
    CHECK_FALSE(stale.Value());

#if defined(_WIN32)
    const auto handle = CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(handle != INVALID_HANDLE_VALUE);
    const auto active = ProbeProjectLock(root);
    CloseHandle(handle);
#else
    const auto handle = open(lockPath.c_str(), O_RDWR);
    REQUIRE(handle >= 0);
    REQUIRE(flock(handle, LOCK_EX | LOCK_NB) == 0);
    const auto active = ProbeProjectLock(root);
    (void)flock(handle, LOCK_UN);
    close(handle);
#endif
    REQUIRE(active);
    CHECK(active.Value());
}
