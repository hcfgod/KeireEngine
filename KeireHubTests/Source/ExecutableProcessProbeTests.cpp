#include <KeireHubRuntimeInternal/ExecutableProcessProbe.h>

#include <KeireHubTests/TestSupport.h>

#include <doctest/doctest.h>

#include <filesystem>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#endif

using namespace KeireHub;

TEST_CASE("Executable process probes fail safely for invalid targets")
{
    CHECK(Detail::ProbeEditorEntrypointActivity("relative/editor") == EditorEntrypointActivity::Indeterminate);
    CHECK(Detail::ProbeEditorEntrypointActivity({}) == EditorEntrypointActivity::Indeterminate);
}

#if defined(_WIN32)
TEST_CASE("Windows executable process probe finds a live executable by exact path")
{
    std::wstring executable(32768U, L'\0');
    const auto length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    REQUIRE(length > 0U);
    REQUIRE(length < executable.size());
    executable.resize(length);

    CHECK(Detail::ProbeEditorEntrypointActivity(std::filesystem::path(executable)) ==
          EditorEntrypointActivity::Running);
    CHECK(Detail::ProbeEditorProcessActivity(static_cast<std::uint64_t>(GetCurrentProcessId()),
                                             std::filesystem::path(executable)) == EditorEntrypointActivity::Running);

    KeireHubTests::TemporaryDirectory temporary;
    const auto absent = temporary.Path() / std::filesystem::path(executable).filename();
    CHECK(Detail::ProbeEditorEntrypointActivity(absent) == EditorEntrypointActivity::NotRunning);
    CHECK(Detail::ProbeEditorProcessActivity(static_cast<std::uint64_t>(GetCurrentProcessId()), absent) ==
          EditorEntrypointActivity::NotRunning);
}
#endif
