#include "KeireClientInternal/Editor/RetargetMappingStore.h"

#include "KeireInternal/FileSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>

namespace KeireEditor::Detail
{
    namespace
    {
        void ValidateMappings(const std::span<const Keire::AnimationRetargetOverride> mappings,
                              const Keire::SkeletonAsset& sourceSkeleton, const Keire::SkeletonAsset& targetSkeleton)
        {
            if (mappings.size() > 4096)
                throw std::invalid_argument("A saved mapping supports at most 4096 bone pairs.");
            std::set<std::string> sources, targets;
            for (const auto& mapping : mappings)
            {
                if (mapping.SourceBone.empty() || mapping.TargetBone.empty() || mapping.SourceBone.size() > 1024 ||
                    mapping.TargetBone.size() > 1024 || mapping.SourceBone.find('\0') != std::string::npos ||
                    mapping.TargetBone.find('\0') != std::string::npos)
                    throw std::invalid_argument("Saved mappings require nonempty bone names of at most 1024 bytes.");
                if (!sources.insert(mapping.SourceBone).second || !targets.insert(mapping.TargetBone).second)
                    throw std::invalid_argument("Each source and target bone can occur only once in a saved mapping.");
                if (std::ranges::find(sourceSkeleton.Bones(), mapping.SourceBone, &Keire::SkeletonBone::Name) ==
                    sourceSkeleton.Bones().end())
                    throw std::invalid_argument("Source bone no longer exists: " + mapping.SourceBone +
                                                ". Repair the bone mappings before saving or loading.");
                if (std::ranges::find(targetSkeleton.Bones(), mapping.TargetBone, &Keire::SkeletonBone::Name) ==
                    targetSkeleton.Bones().end())
                    throw std::invalid_argument("Target bone no longer exists: " + mapping.TargetBone +
                                                ". Repair the bone mappings before saving or loading.");
            }
        }
    } // namespace

    std::filesystem::path RetargetMappingPath(const std::filesystem::path& project, const Keire::AssetId source,
                                              const Keire::AssetId target)
    {
        if (project.empty() || !source || !target)
            throw std::invalid_argument("Choose a project and both skeletons before saving or loading mappings.");
        return project / "Config" / "RetargetMappings" / (source.ToString() + "_" + target.ToString() + ".json");
    }

    void SaveRetargetMapping(const std::filesystem::path& project, const Keire::AssetId source,
                             const Keire::AssetId target, const Keire::SkeletonAsset& sourceSkeleton,
                             const Keire::SkeletonAsset& targetSkeleton,
                             const std::span<const Keire::AnimationRetargetOverride> mappings)
    {
        const auto path = RetargetMappingPath(project, source, target);
        ValidateMappings(mappings, sourceSkeleton, targetSkeleton);
        nlohmann::json document{{"schemaVersion", 1},
                                {"sourceSkeleton", source.ToString()},
                                {"targetSkeleton", target.ToString()},
                                {"mappings", nlohmann::json::array()}};
        for (const auto& mapping : mappings)
            document["mappings"].push_back({{"source", mapping.SourceBone}, {"target", mapping.TargetBone}});
        const auto text = document.dump(2) + '\n';
        if (text.size() > 1024 * 1024)
            throw std::invalid_argument("The saved mapping exceeds the 1 MiB limit.");
        std::filesystem::create_directories(path.parent_path());
        Keire::Detail::WriteTextFileAtomically(path, text);
    }

    std::vector<Keire::AnimationRetargetOverride>
    LoadRetargetMapping(const std::filesystem::path& project, const Keire::AssetId source, const Keire::AssetId target,
                        const Keire::SkeletonAsset& sourceSkeleton, const Keire::SkeletonAsset& targetSkeleton)
    try
    {
        const auto path = RetargetMappingPath(project, source, target);
        if (!std::filesystem::exists(path))
            throw std::invalid_argument(
                "No mapping is saved for these skeletons. Edit the bone mappings, then Save Mapping.");
        const auto document = nlohmann::json::parse(Keire::Detail::ReadTextFile(path, 1024 * 1024));
        if (!document.is_object() || document.at("schemaVersion") != 1 ||
            document.at("sourceSkeleton") != source.ToString() || document.at("targetSkeleton") != target.ToString())
            throw std::invalid_argument("The mapping version or skeleton identities do not match this selection.");
        const auto& bindings = document.at("mappings");
        if (!bindings.is_array() || bindings.size() > 4096)
            throw std::invalid_argument("The mapping must contain an array of at most 4096 bone pairs.");
        std::vector<Keire::AnimationRetargetOverride> result;
        for (const auto& binding : bindings)
            result.push_back({binding.at("source").get<std::string>(), binding.at("target").get<std::string>()});
        ValidateMappings(result, sourceSkeleton, targetSkeleton);
        return result;
    }
    catch (const nlohmann::json::exception&)
    {
        throw std::invalid_argument("The saved mapping is malformed. Restore a valid copy or save a new mapping.");
    }
} // namespace KeireEditor::Detail
