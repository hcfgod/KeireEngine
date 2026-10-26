#include "KeireInternal/Scripting/ManagedBuildWorkspace.h"

#include "KeireInternal/FileSystem.h"
#include "KeireInternal/VisualStudioDiscovery.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <map>
#include <span>
#include <string>
#include <string_view>

namespace Keire::Detail
{
    namespace
    {
        constexpr std::size_t MaximumReferenceAssemblyBytes = std::size_t{64} << 20U;
        constexpr std::string_view CompatibilityProjectSuffix = ".VisualStudio2022";

        void CopyReference(const std::filesystem::path& source, const std::filesystem::path& destination)
        {
            const auto bytes = ReadTextFile(source, MaximumReferenceAssemblyBytes);
            (void)WriteFileAtomicallyIfChanged(destination, std::as_bytes(std::span(bytes)));
        }
    } // namespace

    bool GenerateManagedCompatibilityWorkspace(const ManagedBuildRequest& request,
                                               const std::map<AssetId, std::string>& names,
                                               const std::filesystem::path& projectRoot,
                                               const std::filesystem::path& primarySolution,
                                               const std::filesystem::path& runtimeManagedApi,
                                               const std::filesystem::path& runtimeManagedEditorApi,
                                               const std::filesystem::path& managedGenerator)
    {
        const auto directory = projectRoot / "Library/Ide/VisualStudio2022";
        const auto ready = directory / ".keire-workspace-ready";
        const auto api = runtimeManagedApi.parent_path() / "DesignTime/Keire.Managed.dll";
        const auto editorApi = runtimeManagedEditorApi.parent_path() / "DesignTime/Keire.Editor.Managed.dll";
        const bool includesEditor = std::ranges::any_of(
            request.Assemblies, [](const auto& assembly)
            { return assembly.Definition.Classification == ManagedAssemblyClassification::Editor; });
        if (runtimeManagedApi.empty() || !std::filesystem::is_regular_file(api) ||
            (includesEditor && (runtimeManagedEditorApi.empty() || !std::filesystem::is_regular_file(editorApi))))
        {
            if (std::filesystem::is_directory(directory))
            {
                (void)WriteTextFileAtomicallyIfChanged(ready, "0\n");
            }
            return false;
        }

        // Failed or interrupted regeneration must not advertise a partially updated companion workspace.
        (void)WriteTextFileAtomicallyIfChanged(ready, "0\n");
        const auto references = directory / "References";
        const auto localApi = references / "Keire.Managed.dll";
        CopyReference(api, localApi);
        std::filesystem::path localEditorApi;
        if (includesEditor)
        {
            localEditorApi = references / "Keire.Editor.Managed.dll";
            CopyReference(editorApi, localEditorApi);
        }
        std::filesystem::path localGenerator;
        if (!managedGenerator.empty())
        {
            localGenerator = references / "Keire.Managed.Generators.dll";
            CopyReference(managedGenerator, localGenerator);
        }

        // VS 2022 must not inherit the runtime project's SDK 10 selection from an ancestor global.json.
        constexpr std::string_view sdkPolicy =
            "{\n  \"sdk\": {\n    \"version\": \"8.0.100\",\n    \"rollForward\": \"latestFeature\",\n"
            "    \"allowPrerelease\": false\n  }\n}\n";
        (void)WriteTextFileAtomicallyIfChanged(directory / "global.json", sdkPolicy);
        std::map<AssetId, std::filesystem::path> projects;
        for (const auto& assembly : request.Assemblies)
        {
            const auto project =
                directory / (assembly.Definition.Name + std::string(CompatibilityProjectSuffix) + ".csproj");
            const auto text = GenerateUserAssemblyDesignTimeProject(assembly, names, localApi, {}, localEditorApi,
                                                                    localGenerator, projectRoot, directory, "net8.0",
                                                                    "12.0", CompatibilityProjectSuffix);
            (void)WriteTextFileAtomicallyIfChanged(project, text);
            projects.emplace(assembly.Asset, project);
        }
        const auto solution =
            GenerateSolution(request, names, directory, {}, projects, ManagedWorkspaceCompatibilityVisualStudioMajor);
        (void)WriteTextFileAtomicallyIfChanged(directory / primarySolution.filename(), solution);
        (void)WriteTextFileAtomicallyIfChanged(ready, "1\n");
        return true;
    }
} // namespace Keire::Detail
