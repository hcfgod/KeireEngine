#include "doctest/doctest.h"

#include "KeireInternal/FileSystem.h"
#include "KeireInternal/Process.h"
#include "KeireInternal/Scripting/ManagedBuildWorkspace.h"
#include "KeireInternal/VisualStudioDiscovery.h"
#include "KeireTests/TestSupport.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
    struct HandoffDirectory final
    {
        HandoffDirectory() : Root(KeireTests::MakeTestDirectory("ManagedIdeHandoff"))
        {
            std::filesystem::create_directories(Root);
        }

        ~HandoffDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(Root, ignored);
        }

        [[nodiscard]] std::filesystem::path WriteSolution(const std::string_view text) const
        {
            const auto solution = Root / (Root.filename().string() + ".sln");
            Keire::Detail::WriteTextFileAtomically(solution, text);
            return solution;
        }

        std::filesystem::path Root;
    };

    constexpr std::string_view SolutionHeader = "Microsoft Visual Studio Solution File, Format Version 12.00\n";
} // namespace

TEST_CASE("generated solution requirements match compatibility and packaged design-time targets")
{
    HandoffDirectory directory;
    const Keire::AssetId assemblyId(0x4b45495245544553ULL, 101);
    Keire::ManagedAssemblyDefinition assembly;
    assembly.Name = "Gameplay";
    Keire::ManagedBuildRequest request;
    request.Assemblies.push_back({assemblyId, assembly});
    const std::map<Keire::AssetId, std::string> names{{assemblyId, assembly.Name}};
    const std::map<Keire::AssetId, std::filesystem::path> projects{
        {assemblyId, directory.Root / "Gameplay.VisualStudio.csproj"}};

    const auto compatibility = Keire::Detail::GenerateSolution(
        request, names, directory.Root, directory.Root / "References/Keire.Managed.VisualStudio.csproj", projects);
    CHECK(compatibility.find("MinimumVisualStudioVersion = 17.8.0.0\n") != std::string::npos);
    CHECK(Keire::Detail::ReadVisualStudioSolutionMinimumMajor(directory.WriteSolution(compatibility)) == 17U);
    CHECK(Keire::Detail::IsCompatibleVisualStudioDteMoniker(L"!VisualStudio.DTE.17.0:1234", 17U));

    const auto packaged = Keire::Detail::GenerateSolution(request, names, directory.Root, {}, projects);
    CHECK(packaged.find("MinimumVisualStudioVersion = 18.0.0.0\n") != std::string::npos);
    CHECK(Keire::Detail::ReadVisualStudioSolutionMinimumMajor(directory.WriteSolution(packaged)) == 18U);
    CHECK_FALSE(Keire::Detail::IsCompatibleVisualStudioDteMoniker(L"!VisualStudio.DTE.17.0:1234", 18U));
    CHECK(Keire::Detail::IsCompatibleVisualStudioDteMoniker(L"!VisualStudio.DTE.18.0:1234", 18U));
}

TEST_CASE("solution requirement parsing is bounded and defaults safely for absent or invalid headers")
{
    HandoffDirectory directory;
    const auto read = [&directory](const std::string_view suffix)
    {
        return Keire::Detail::ReadVisualStudioSolutionMinimumMajor(
            directory.WriteSolution(std::string(SolutionHeader) + std::string(suffix)));
    };
    CHECK(Keire::Detail::ReadVisualStudioSolutionMinimumMajor(directory.Root / "missing.sln") == 18U);
    CHECK(Keire::Detail::ReadVisualStudioSolutionMinimumMajor(directory.WriteSolution("not a solution\n")) == 18U);
    CHECK(read("") == 18U);
    CHECK(read("MinimumVisualStudioVersion = 17.8.0.0\n") == 17U);
    CHECK(read("MinimumVisualStudioVersion = 19.0.0.0\n") == 19U);
    CHECK(read("MinimumVisualStudioVersion = 16.11.0.0\n") == 18U);
    CHECK(read("MinimumVisualStudioVersion = 17.\n") == 18U);
    CHECK(read("MinimumVisualStudioVersion = 17.8.invalid\n") == 18U);
    CHECK(read("MinimumVisualStudioVersion = 17.8.0.0") == 18U);
    CHECK(read("MinimumVisualStudioVersion = 17.8.0.0\nMinimumVisualStudioVersion = 17.8.0.0\n") == 18U);
    CHECK(read("MinimumVisualStudioVersionExtra = 17.8.0.0\n") == 18U);
    CHECK(read("Project(\"test\")\nMinimumVisualStudioVersion = 17.8.0.0\n") == 18U);
    CHECK(read("MinimumVisualStudioVersion = 17.8.0.0\n#" + std::string(5000, 'x')) == 18U);
    const auto bom = std::string("\xEF\xBB\xBF") + std::string(SolutionHeader) +
                     "  MinimumVisualStudioVersion\t=\t17.8.0.0 \r\nProject(\"test\")\r\n";
    CHECK(Keire::Detail::ReadVisualStudioSolutionMinimumMajor(directory.WriteSolution(bom)) == 17U);
}

TEST_CASE("Visual Studio discovery rejects incomplete version numbers")
{
    const auto installations = Keire::Detail::ParseVisualStudioInstallations(R"json([
        {"installationVersion":"18.","productPath":"invalid.exe","isComplete":true,"isLaunchable":true},
        {"installationVersion":"18.1.2.3.","productPath":"invalid.exe","isComplete":true,"isLaunchable":true},
        {"installationVersion":"18.1.2.3","productPath":"valid.exe","isComplete":true,"isLaunchable":true}
    ])json");
    REQUIRE(installations.size() == 1U);
    CHECK(installations.front().Executable == std::filesystem::path("valid.exe"));
}

TEST_CASE("Visual Studio executable requirements reject missing and unversioned files")
{
    HandoffDirectory directory;
    CHECK_FALSE(Keire::Detail::IsVisualStudioExecutableCompatible(directory.Root / "missing.exe", 17U));
    const auto executable = directory.Root / "devenv.exe";
    Keire::Detail::WriteTextFileAtomically(executable, "not an executable");
    CHECK_FALSE(Keire::Detail::IsVisualStudioExecutableCompatible(executable, 17U));
    CHECK_FALSE(Keire::Detail::IsVisualStudioExecutableCompatible(executable, 18U));
#if defined(_WIN32)
    const auto source = directory.Root / "Gameplay.cs";
    Keire::Detail::WriteTextFileAtomically(source, "using Keire;\n");
    (void)directory.WriteSolution(std::string(SolutionHeader) + "MinimumVisualStudioVersion = 18.0.0.0\n");
    std::string diagnostic;
    CHECK_FALSE(Keire::Detail::OpenInExternalEditor(source, executable, directory.Root, diagnostic));
    CHECK(diagnostic.find("Visual Studio 2022") != std::string::npos);
    CHECK(diagnostic.find("not opened as a loose file") != std::string::npos);
#endif
}

TEST_CASE("packaged design-time APIs generate an isolated ready VS2022 workspace")
{
    HandoffDirectory directory;
    const auto projectRoot = directory.Root / "Game Space";
    const auto runtime = directory.Root / "Managed";
    const auto primary = projectRoot / "Game_Space.sln";
    const auto companion = projectRoot / "Library/Ide/VisualStudio2022";
    const auto runtimeApi = runtime / "Keire.Managed.dll";
    const auto editorApi = runtime / "Keire.Editor.Managed.dll";
    const auto generator = runtime / "Keire.Managed.Generators.dll";
    Keire::Detail::WriteTextFileAtomically(projectRoot / "global.json", "runtime-sdk-10\n");
    Keire::Detail::WriteTextFileAtomically(primary,
                                           std::string(SolutionHeader) + "MinimumVisualStudioVersion = 18.0.0.0\n");
    const std::string coreBytes("core-design-time\0bytes", 22U);
    Keire::Detail::WriteTextFileAtomically(runtime / "DesignTime/Keire.Managed.dll", coreBytes);
    Keire::Detail::WriteTextFileAtomically(runtime / "DesignTime/Keire.Editor.Managed.dll", "editor-design-time");
    Keire::Detail::WriteTextFileAtomically(generator, "portable-generator");
    const Keire::AssetId gameplayId(0x4b45495245544553ULL, 102);
    const Keire::AssetId toolsId(0x4b45495245544553ULL, 103);
    Keire::ManagedAssemblyDefinition gameplay;
    gameplay.Name = "Gameplay";
    Keire::ManagedAssemblyDefinition tools;
    tools.Name = "Tools";
    tools.Classification = Keire::ManagedAssemblyClassification::Editor;
    tools.References = {gameplayId};
    Keire::ManagedBuildRequest request;
    request.Assemblies.push_back({gameplayId, gameplay});
    request.Assemblies.back().SourceFiles = std::vector<std::filesystem::path>{"Assets/Game Space/Player.cs"};
    request.Assemblies.push_back({toolsId, tools});
    request.Assemblies.back().SourceFiles = std::vector<std::filesystem::path>{"Assets/Editor/Tools.cs"};
    const std::map<Keire::AssetId, std::string> names{{gameplayId, gameplay.Name}, {toolsId, tools.Name}};
    const auto generate = [&]
    {
        return Keire::Detail::GenerateManagedCompatibilityWorkspace(request, names, projectRoot, primary, runtimeApi,
                                                                    editorApi, generator);
    };

    REQUIRE(generate());
    CHECK(Keire::Detail::ReadTextFile(projectRoot / "global.json", 1024U) == "runtime-sdk-10\n");
    CHECK(Keire::Detail::ReadTextFile(companion / "global.json", 1024U).find("8.0.100") != std::string::npos);
    CHECK(Keire::Detail::ReadVisualStudioSolutionMinimumMajor(companion / primary.filename()) == 17U);
    CHECK(Keire::Detail::SelectManagedVisualStudioSolution(primary, 17U) == companion / primary.filename());
    CHECK(Keire::Detail::SelectManagedVisualStudioSolution(primary, 18U) == primary);
    CHECK(Keire::Detail::SelectManagedVisualStudioSolution(primary, 19U) == primary);
    const auto project = companion / "Tools.VisualStudio2022.csproj";
    const auto text = Keire::Detail::ReadTextFile(project, 65536U);
    CHECK(text.find("<TargetFramework>net8.0</TargetFramework>") != std::string::npos);
    CHECK(text.find("<LangVersion>12.0</LangVersion>") != std::string::npos);
    CHECK(text.find("<EnableDefaultNoneItems>false</EnableDefaultNoneItems>") != std::string::npos);
    CHECK(text.find("../../../Assets/Editor/Tools.cs") != std::string::npos);
    CHECK(text.find("Gameplay.VisualStudio2022.csproj") != std::string::npos);
    CHECK(text.find("References/Keire.Editor.Managed.dll") != std::string::npos);
    CHECK(text.find("References/Keire.Managed.Generators.dll") != std::string::npos);
    const auto timestamp = std::filesystem::last_write_time(project);
    REQUIRE(generate());
    CHECK(std::filesystem::last_write_time(project) == timestamp);
    CHECK(Keire::Detail::ReadTextFile(companion / "References/Keire.Managed.dll", 1024U) ==
          Keire::Detail::ReadTextFile(runtime / "DesignTime/Keire.Managed.dll", 1024U));

    Keire::Detail::WriteTextFileAtomically(companion / ".keire-workspace-ready", "0\n");
    CHECK(Keire::Detail::SelectManagedVisualStudioSolution(primary, 17U) == primary);
    REQUIRE(generate());
    std::filesystem::remove(runtime / "DesignTime/Keire.Editor.Managed.dll");
    CHECK_FALSE(generate());
    CHECK(Keire::Detail::SelectManagedVisualStudioSolution(primary, 17U) == primary);
    CHECK(Keire::Detail::ReadTextFile(companion / ".keire-workspace-ready", 16U) == "0\n");
    Keire::Detail::WriteTextFileAtomically(runtime / "DesignTime/Keire.Editor.Managed.dll", "editor-design-time");
    std::filesystem::remove(generator);
    CHECK_THROWS_AS(generate(), std::exception);
    CHECK(Keire::Detail::ReadTextFile(companion / ".keire-workspace-ready", 16U) == "0\n");
}

#if defined(_WIN32)
TEST_CASE("installed Visual Studio executable version checks agree with supported discovery")
{
    const auto installed = Keire::Detail::ResolveCompatibleVisualStudioExecutable(17U);
    if (installed.empty())
    {
        MESSAGE("No installed Visual Studio was available for this executable-version integration check.");
        return;
    }
    CHECK(Keire::Detail::IsVisualStudioExecutableCompatible(installed, 17U));
    CHECK_FALSE(
        Keire::Detail::IsVisualStudioExecutableCompatible(installed, std::numeric_limits<std::uint32_t>::max()));
}
#endif
