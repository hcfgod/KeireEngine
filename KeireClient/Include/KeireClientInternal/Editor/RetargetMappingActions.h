#pragma once

#include "Keire/Ui.h"
#include "Keire/UiWorkspace.h"
#include "KeireClientInternal/Editor/RetargetMappingStore.h"

#include <exception>
#include <string>
#include <utility>

namespace KeireEditor::Detail
{
    inline bool DrawRetargetMappingActions(Keire::UiFrame& ui, const Keire::UiThemeDefinition& theme,
                                           const std::filesystem::path& project, const Keire::AssetId source,
                                           const Keire::AssetId target, const Keire::SkeletonAsset& sourceSkeleton,
                                           const Keire::SkeletonAsset& targetSkeleton,
                                           std::vector<Keire::AnimationRetargetOverride>& mappings,
                                           std::string& message, bool& error)
    {
        bool loaded = false;
        if (ui.Button("Save Mapping", {ui.ContentAvailable().Width, 0.0F}))
        {
            try
            {
                SaveRetargetMapping(project, source, target, sourceSkeleton, targetSkeleton, mappings);
                message = "Mapping saved in Config/RetargetMappings. Reuse it with other clips from these skeletons.";
                error = false;
            }
            catch (const std::exception& failure)
            {
                message = "Could not save mapping: " + std::string(failure.what());
                error = true;
            }
        }
        if (ui.Button("Load Saved Mapping", {ui.ContentAvailable().Width, 0.0F}))
        {
            try
            {
                auto saved = LoadRetargetMapping(project, source, target, sourceSkeleton, targetSkeleton);
                mappings = std::move(saved);
                message =
                    "Saved mapping loaded. Review compatibility before baking. Current unsaved mappings were replaced.";
                error = false;
                loaded = true;
            }
            catch (const std::exception& failure)
            {
                message = "Could not load mapping; current edits are unchanged. " + std::string(failure.what());
                error = true;
            }
        }
        ui.TextColoredWrapped(
            theme.MutedText,
            "Save replaces this skeleton pair's saved mapping. Load replaces the current manual edits.");
        if (!message.empty())
            ui.TextColoredWrapped(error ? theme.Error : theme.Success, message);
        return loaded;
    }
} // namespace KeireEditor::Detail
