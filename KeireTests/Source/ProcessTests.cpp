#include "doctest/doctest.h"

#include "KeireTests/TestSupport.h"

#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"
#include "KeireInternal/VisualStudioDiscovery.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <Windows.h>

#include <OleAuto.h>

namespace
{
    class VisualStudioDispatchFixture final : public IDispatch
    {
      public:
        std::map<std::wstring, IDispatch*> Objects;
        std::vector<IDispatch*> Items;
        std::wstring ReferenceName;
        std::wstring ReferencePath;
        LONG ReportedCount = -1;
        unsigned ItemReads = 0;
        std::wstring ProjectPath;
        bool Resolved = false;
        unsigned Opens = 0;
        unsigned Activations = 0;

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** object) override
        {
            if (id != IID_IUnknown && id != IID_IDispatch)
            {
                *object = nullptr;
                return E_NOINTERFACE;
            }
            *object = this;
            AddRef();
            return S_OK;
        }

        ULONG STDMETHODCALLTYPE AddRef() override { return 1; }

        ULONG STDMETHODCALLTYPE Release() override { return 1; }

        HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) override
        {
            *count = 0;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }

        HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR* names, UINT count, LCID, DISPID* ids) override
        {
            if (count != 1)
            {
                return E_INVALIDARG;
            }
            for (std::size_t index = 0; index < Names.size(); ++index)
            {
                if (Names[index] == names[0])
                {
                    *ids = static_cast<DISPID>(index);
                    return S_OK;
                }
            }
            return DISP_E_UNKNOWNNAME;
        }

        HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID, LCID, WORD, DISPPARAMS* parameters, VARIANT* result,
                                         EXCEPINFO*, UINT*) override
        {
            if (id < 0 || static_cast<std::size_t>(id) >= Names.size())
            {
                return DISP_E_MEMBERNOTFOUND;
            }
            const auto name = Names[static_cast<std::size_t>(id)];
            if (name == L"Activate")
            {
                ++Activations;
                return S_OK;
            }
            if (!result)
            {
                return E_POINTER;
            }
            if (name == L"FullName")
            {
                result->vt = VT_BSTR;
                result->bstrVal = SysAllocString(ProjectPath.c_str());
                return S_OK;
            }
            if (name == L"Resolved")
            {
                result->vt = VT_BOOL;
                result->boolVal = Resolved ? VARIANT_TRUE : VARIANT_FALSE;
                return S_OK;
            }
            if (name == L"Name" || name == L"Path")
            {
                result->vt = VT_BSTR;
                result->bstrVal = SysAllocString((name == L"Name" ? ReferenceName : ReferencePath).c_str());
                return S_OK;
            }
            if (name == L"Count")
            {
                result->vt = VT_I4;
                result->lVal = ReportedCount >= 0 ? ReportedCount : static_cast<LONG>(Items.size());
                return S_OK;
            }
            if (name == L"Item")
            {
                ++ItemReads;
                if (!parameters || parameters->cArgs != 1U || parameters->rgvarg[0].vt != VT_I4)
                {
                    return DISP_E_TYPEMISMATCH;
                }
                const auto index = parameters->rgvarg[0].lVal;
                if (index < 1 || static_cast<std::size_t>(index) > Items.size() || !Items[index - 1])
                {
                    return DISP_E_BADINDEX;
                }
                result->vt = VT_DISPATCH;
                result->pdispVal = Items[index - 1];
                result->pdispVal->AddRef();
                return S_OK;
            }
            const auto object = Objects.find(std::wstring(name));
            if (object == Objects.end() || !object->second)
            {
                return E_FAIL;
            }
            if (name == L"Open")
            {
                ++Opens;
            }
            result->vt = VT_DISPATCH;
            result->pdispVal = object->second;
            result->pdispVal->AddRef();
            return S_OK;
        }

      private:
        static constexpr std::array<std::wstring_view, 13> Names{L"FindProjectItem",
                                                                 L"ContainingProject",
                                                                 L"FullName",
                                                                 L"Object",
                                                                 L"References",
                                                                 L"Find",
                                                                 L"Resolved",
                                                                 L"Open",
                                                                 L"Activate",
                                                                 L"Count",
                                                                 L"Item",
                                                                 L"Name",
                                                                 L"Path"};
    };
} // namespace

TEST_CASE("Visual Studio handoff waits for project membership and a resolved engine reference")
{
    VisualStudioDispatchFixture solution, item, project, language, references, engine, window;
    const auto root = std::filesystem::current_path();
    const auto source = root / "Assets/Player.cs";
    const auto solutionPath = root / "Game.sln";
    std::string diagnostic;
    const auto open = [&]
    { return Keire::Detail::OpenLoadedVisualStudioProjectItem(solution, source, solutionPath, diagnostic); };

    CHECK_FALSE(open());
    solution.Objects[L"FindProjectItem"] = &item;
    CHECK_FALSE(open());
    item.Objects[L"ContainingProject"] = &project;
    project.ProjectPath = (root / "Assembly-CSharp.VisualStudio.csproj").wstring();
    project.Objects[L"Object"] = &language;
    language.Objects[L"References"] = &references;
    references.Items = {&engine};
    engine.ReferenceName = L"Keire.Managed";
    item.Objects[L"Open"] = &window;
    CHECK_FALSE(open());
    CHECK(item.Opens == 0);

    engine.Resolved = true;
    project.ProjectPath = (root / "Other/Foreign.csproj").wstring();
    CHECK_FALSE(open());
    CHECK(item.Opens == 0);
    project.ProjectPath = (root / "Assembly-CSharp.VisualStudio.csproj").wstring();
    CHECK(open());
    CHECK(item.Opens == 1);
    CHECK(window.Activations == 1);
    CHECK(diagnostic.empty());

    SUBCASE("assembly identity lookup is not required for a resolved named reference")
    {
        CHECK(references.Objects.find(L"Find") == references.Objects.end());
        CHECK(references.ItemReads > 0U);
    }
    SUBCASE("an unrelated resolved reference never permits opening the script")
    {
        engine.ReferenceName = L"Unrelated";
        CHECK_FALSE(open());
        CHECK(item.Opens == 1U);
    }
    SUBCASE("a facade project reference can identify the engine API by resolved assembly path")
    {
        engine.ReferenceName = L"Keire.Managed.VisualStudio";
        engine.ReferencePath = L"C:/References/Keire.Managed.dll";
        CHECK(open());
        CHECK(item.Opens == 2U);
    }
    SUBCASE("missing collection entries can be skipped without opening a loose file")
    {
        references.Items = {nullptr, &engine};
        CHECK(open());
    }
    SUBCASE("oversized reference collections are rejected before enumeration")
    {
        references.ReportedCount = 1025;
        const auto before = references.ItemReads;
        CHECK_FALSE(open());
        CHECK(references.ItemReads == before);
        CHECK(item.Opens == 1U);
    }
}
#endif

TEST_CASE("external editor diagnostic arguments keep spaced paths in a single argument")
{
    const auto source = std::filesystem::current_path() / "Project Space" / "Game.cs";
    const auto path = Keire::Detail::PathToUtf8(source);
    CHECK((Keire::Detail::ResolveExternalEditorSourceArguments(source, "code", 39, 27) ==
           std::vector<std::string>{"--goto", path + ":39:27"}));
    CHECK((Keire::Detail::ResolveExternalEditorSourceArguments(source, "vscodium", 2, 0) ==
           std::vector<std::string>{"--goto", path + ":2:1"}));
    CHECK(
        (Keire::Detail::ResolveExternalEditorSourceArguments(source, "code", 0, 0) == std::vector<std::string>{path}));
    CHECK((Keire::Detail::ResolveExternalEditorSourceArguments(source, "custom", 2, 3) ==
           std::vector<std::string>{path}));
}

TEST_CASE("detached process launch reports a trackable process identity")
{
    const auto currentProcessId = Keire::Detail::CurrentProcessId();
    CHECK(currentProcessId != 0);
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

TEST_CASE("current process elevation matches the Windows access token")
{
#if defined(_WIN32)
    HANDLE rawToken = nullptr;
    REQUIRE(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken));
    const auto closeToken = [](void* token)
    {
        if (token)
            CloseHandle(token);
    };
    const std::unique_ptr<void, decltype(closeToken)> token(rawToken, closeToken);
    TOKEN_ELEVATION elevation{};
    DWORD bytesWritten = 0;
    REQUIRE(GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &bytesWritten));
    REQUIRE(bytesWritten == static_cast<DWORD>(sizeof(elevation)));
    CHECK(Keire::Detail::IsCurrentProcessElevated() == (elevation.TokenIsElevated != 0));
#else
    CHECK_FALSE(Keire::Detail::IsCurrentProcessElevated());
#endif
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

TEST_CASE("Child process output remains bounded without losing normal completion diagnostics")
{
    std::string probe;
    std::size_t expectedSize = 0;
    bool expectTail = false;
    SUBCASE("output larger than one poll retains its final standard-error diagnostic")
    {
        probe = "--child-process-large-output";
        expectedSize = std::size_t{384} * 1024U;
        expectTail = true;
    }
    SUBCASE("output beyond the retention cap still drains and exits normally")
    {
        probe = "--child-process-capped-output";
        expectedSize = std::size_t{4} * 1024U * 1024U;
    }

    const std::array arguments{probe};
    auto process = Keire::Detail::ChildProcess::Start(KeireTests::TestExecutable, arguments,
                                                      KeireTests::TestExecutable.parent_path());
    REQUIRE(process.WaitFor(std::chrono::seconds(15)));
    REQUIRE(process.ExitCode());
    CHECK(*process.ExitCode() == 37);
    CHECK_FALSE(process.Running());
    const auto output = process.TakeOutput();
    if (expectTail)
    {
        REQUIRE(output.size() > expectedSize);
        CHECK(output.substr(0, expectedSize) == std::string(expectedSize, 'x'));
        CHECK(output.substr(expectedSize).find("diagnostic-tail") != std::string::npos);
    }
    else
    {
        CHECK(output == std::string(expectedSize, 'x'));
    }
    CHECK(process.TakeOutput().empty());
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

TEST_CASE("desktop process launch does not inherit an elevated Windows token")
{
    if (Keire::Detail::IsCurrentProcessElevated() && !GetShellWindow())
    {
        MESSAGE("An elevated headless session has no signed-in desktop token to test.");
        return;
    }

    const std::array arguments{std::string("--child-process-hang")};
    std::string diagnostic;
    std::uint64_t processId = 0;
    const auto launched = Keire::Detail::LaunchDetachedProcessAtDesktopUserIntegrity(
        KeireTests::TestExecutable, arguments, KeireTests::TestExecutable.parent_path(), diagnostic, &processId);
    REQUIRE_MESSAGE(launched, diagnostic);
    REQUIRE(processId != 0);

    const auto closeHandle = [](void* handle)
    {
        if (handle)
            CloseHandle(handle);
    };
    const std::unique_ptr<void, decltype(closeHandle)> process(
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE,
                    static_cast<DWORD>(processId)),
        closeHandle);
    REQUIRE(process);
    HANDLE rawToken = nullptr;
    REQUIRE(OpenProcessToken(process.get(), TOKEN_QUERY, &rawToken));
    const std::unique_ptr<void, decltype(closeHandle)> token(rawToken, closeHandle);
    TOKEN_ELEVATION elevation{};
    DWORD bytesWritten = 0;
    REQUIRE(GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &bytesWritten));
    CHECK(bytesWritten == static_cast<DWORD>(sizeof(elevation)));
    CHECK(elevation.TokenIsElevated == 0);
    REQUIRE(TerminateProcess(process.get(), 91));
    REQUIRE(WaitForSingleObject(process.get(), 5000) == WAIT_OBJECT_0);
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

TEST_CASE("managed external editor targeting reuses one solution for scripts and assembly definitions")
{
    const auto root = std::filesystem::absolute(std::filesystem::path("Build") /
                                                ("ExternalEditor-" + Keire::AssetId::Generate().ToString()));
    std::filesystem::create_directories(root);
    const auto source = root / "NewBehaviour.cs";
    const auto assembly = root / "Gameplay.keireasm";
    const auto solution = root / (root.filename().string() + ".sln");
    std::ofstream(source) << "public sealed class NewBehaviour {}\n";
    std::ofstream(assembly) << "{\"name\":\"Gameplay\"}\n";
    std::ofstream(solution) << "Microsoft Visual Studio Solution File\n";

    CHECK(Keire::Detail::ResolveManagedSolutionForExternalEditor(source, root) == solution);
    CHECK(Keire::Detail::ResolveManagedSolutionForExternalEditor(assembly, root) == solution);
    CHECK(Keire::Detail::ResolveManagedSolutionForExternalEditor(root / "Texture.png", root).empty());
    CHECK((Keire::Detail::ResolveVisualStudioExternalEditorArguments(source, solution) ==
           std::vector<std::string>{Keire::Detail::PathToUtf8(solution)}));
    CHECK((Keire::Detail::ResolveVisualStudioExternalEditorArguments(source, {}) ==
           std::vector<std::string>{Keire::Detail::PathToUtf8(source)}));

    std::error_code error;
    std::filesystem::remove_all(root, error);
}

TEST_CASE("Visual Studio discovery parses complete launchable instances and ranks compatible versions")
{
    constexpr std::string_view document = R"json(
[
  {"installationVersion":"17.14.37516.0","productPath":"C:/VS17/devenv.exe","isComplete":true,"isLaunchable":true},
  {"installationVersion":"18.9.999.1","productPath":"C:/VS18.9/devenv.exe","isComplete":true,"isLaunchable":true},
  {"installationVersion":"18.10.12224.181","productPath":"C:/VS18.10-A/devenv.exe","isComplete":true,"isLaunchable":true},
  {"installationVersion":"18.10.12224.181","productPath":"C:/VS18.10-B/devenv.exe","isComplete":true,"isLaunchable":true},
  {"installationVersion":"19.bad","productPath":"C:/Invalid/devenv.exe","isComplete":true,"isLaunchable":true},
  {"installationVersion":20,"productPath":"C:/WrongType/devenv.exe","isComplete":true,"isLaunchable":true},
  {"installationVersion":"20.0.0.0","productPath":"C:/Incomplete/devenv.exe","isComplete":false,"isLaunchable":true},
  {"installationVersion":"21.0.0.0","productPath":"C:/Unlaunchable/devenv.exe","isComplete":true,"isLaunchable":false}
]
)json";
    const auto parsed = Keire::Detail::ParseVisualStudioInstallations(document);
    REQUIRE(parsed.size() == 4);

    const auto selected = Keire::Detail::SelectCompatibleVisualStudioInstallation(
        parsed, Keire::Detail::ManagedWorkspaceMinimumVisualStudioMajor);
    REQUIRE(selected);
    constexpr std::array<std::uint32_t, 4> expectedVersion{18, 10, 12224, 181};
    CHECK(selected->Version == expectedVersion);
    CHECK(selected->Executable == std::filesystem::path("C:/VS18.10-B/devenv.exe"));
    auto reversed = parsed;
    std::ranges::reverse(reversed);
    CHECK(Keire::Detail::SelectCompatibleVisualStudioInstallation(reversed, 18) == selected);
    CHECK_FALSE(Keire::Detail::SelectCompatibleVisualStudioInstallation(parsed, 19));
}

TEST_CASE("Visual Studio discovery rejects malformed and unbounded input")
{
    CHECK(Keire::Detail::ParseVisualStudioInstallations("not json").empty());
    CHECK(Keire::Detail::ParseVisualStudioInstallations("{}").empty());
    CHECK(Keire::Detail::ParseVisualStudioInstallations(std::string((std::size_t{1} << 20U) + 1, ' ')).empty());
    std::string tooMany = "[";
    for (std::size_t index = 0; index < 129; ++index)
        tooMany += index == 0 ? "{}" : ",{}";
    tooMany += ']';
    CHECK(Keire::Detail::ParseVisualStudioInstallations(tooMany).empty());
}

TEST_CASE("managed Visual Studio DTE reuse requires a compatible major version")
{
    using Keire::Detail::IsCompatibleVisualStudioDteMoniker;
    constexpr auto minimum = Keire::Detail::ManagedWorkspaceMinimumVisualStudioMajor;
    CHECK_FALSE(IsCompatibleVisualStudioDteMoniker(L"!VisualStudio.DTE.17.0:1234", minimum));
    CHECK(IsCompatibleVisualStudioDteMoniker(L"!VisualStudio.DTE.18.0:1234", minimum));
    CHECK(IsCompatibleVisualStudioDteMoniker(L"!VisualStudio.DTE.19.2:1234", minimum));
    CHECK_FALSE(IsCompatibleVisualStudioDteMoniker(L"!VisualStudio.DTE.bad:1234", minimum));
    CHECK_FALSE(IsCompatibleVisualStudioDteMoniker(L"Other.DTE.18.0:1234", minimum));
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
