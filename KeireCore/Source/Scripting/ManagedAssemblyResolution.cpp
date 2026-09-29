#include "Keire/Scripting/ManagedAssemblyAsset.h"
#include "KeireInternal/Scripting/ManagedAssemblyPolicies.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <ranges>
#include <set>
#include <stdexcept>

namespace Keire
{
    std::vector<ManagedAssemblyGraphEntry>
    ResolveProjectManagedAssemblies(const std::filesystem::path& projectRoot,
                                    const std::span<const ManagedAssemblyGraphEntry> customAssemblies,
                                    const ManagedAssemblyBuildContext& context)
    {
        // These identities belong to the engine, never to assets or their metadata.
        constexpr AssetId runtimeId(0x4b45495245425549ULL, 0x4c54494e41534d01ULL);
        constexpr AssetId editorId(0x4b45495245425549ULL, 0x4c54494e41534d02ULL);
        std::vector<ManagedAssemblyGraphEntry> result(customAssemblies.begin(), customAssemblies.end());
        for (auto& assembly : result)
            if (assembly.Definition.IncludePlatforms == std::vector<std::string>{"Editor"} &&
                assembly.Definition.Classification == ManagedAssemblyClassification::Runtime)
                assembly.Definition.Classification = ManagedAssemblyClassification::Editor;
        ValidateManagedAssemblyGraph(result);
        std::map<std::filesystem::path, std::size_t> owners;
        const auto contained = [](const auto& root, const auto& path)
        {
            const auto relative = path.lexically_relative(root);
            return !relative.empty() && !relative.is_absolute() &&
                   std::ranges::none_of(relative, [](const auto& part) { return part == ".."; });
        };
        for (std::size_t index = 0; index < result.size(); ++index)
        {
            auto& assembly = result[index];
            auto foldedName = assembly.Definition.Name;
            std::ranges::transform(foldedName, foldedName.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            if (assembly.Asset == runtimeId || assembly.Asset == editorId || foldedName == "assembly-csharp" ||
                foldedName == "assembly-csharp-editor")
                throw std::invalid_argument("Custom assemblies cannot use predefined assembly identities or names.");
            assembly.SourceFiles.emplace();
            if (assembly.Definition.SourceRoots.empty())
            {
                const auto path = assembly.DefinitionPath.lexically_normal();
                if (!contained(std::filesystem::path("Assets"), path) || path.extension() != ".keireasm")
                    throw std::invalid_argument("Folder assemblies require an asset path under Assets.");
                assembly.Definition.SourceRoots.push_back(path.parent_path());
            }
            for (const auto& root : assembly.Definition.SourceRoots)
            {
                const auto normalized = root.lexically_normal();
                if (!owners.emplace(normalized, index).second)
                    throw std::invalid_argument("Multiple managed assemblies own the same source folder.");
            }
        }
        Detail::AddManagedAssemblyReferenceRoots(projectRoot, result, owners);
        ManagedAssemblyDefinition runtime;
        runtime.Name = "Assembly-CSharp";
        runtime.RootNamespace = "Game";
        runtime.SourceRoots = {"Assets"};
        ManagedAssemblyDefinition editor = runtime;
        editor.Name = "Assembly-CSharp-Editor";
        editor.Classification = ManagedAssemblyClassification::Editor;
        editor.References.push_back(runtimeId);
        for (const auto& assembly : result)
        {
            if (!assembly.Definition.AutoReferenced ||
                assembly.Definition.Classification == ManagedAssemblyClassification::Tests)
                continue;
            editor.References.push_back(assembly.Asset);
            if (assembly.Definition.Classification == ManagedAssemblyClassification::Runtime)
                runtime.References.push_back(assembly.Asset);
        }
        const auto runtimeIndex = result.size();
        result.push_back({runtimeId, std::move(runtime), {}, std::vector<std::filesystem::path>{}});
        const auto editorIndex = result.size();
        result.push_back({editorId, std::move(editor), {}, std::vector<std::filesystem::path>{}});

        std::set<std::filesystem::path> sources;
        std::set<std::filesystem::path> roots{"Assets"};
        for (const auto& [root, owner] : owners)
        {
            (void)owner;
            roots.insert(root);
        }
        for (const auto& root : roots)
        {
            const auto directory = projectRoot / root;
            if (!std::filesystem::exists(directory))
                continue;
            for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
            {
                if (entry.is_regular_file() && entry.path().extension() == ".cs")
                {
                    if (!contained(std::filesystem::weakly_canonical(projectRoot),
                                   std::filesystem::weakly_canonical(entry.path())))
                        throw std::invalid_argument("Managed source files must remain inside the project.");
                    sources.insert(entry.path().lexically_relative(projectRoot).lexically_normal());
                }
            }
        }
        for (const auto& source : sources)
        {
            auto owner = runtimeIndex;
            std::size_t depth = 0;
            for (const auto& [root, index] : owners)
            {
                if (contained(root, source) && root.generic_string().size() > depth)
                {
                    owner = index;
                    depth = root.generic_string().size();
                }
            }
            if (owner == runtimeIndex &&
                std::ranges::any_of(source.parent_path(), [](const auto& part) { return part == "Editor"; }))
                owner = editorIndex;
            result[owner].SourceFiles->push_back(source);
        }
        // Avoid requiring the editor API in projects without any editor scripts.
        if (result.back().SourceFiles->empty())
            result.pop_back();
        Detail::ApplyManagedAssemblyPolicies(projectRoot, result, context);
        ValidateManagedAssemblyGraph(result);
        return result;
    }

} // namespace Keire
