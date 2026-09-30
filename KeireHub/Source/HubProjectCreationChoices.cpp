#include "KeireHub/HubProjectCreationUi.h"

#include "KeireHubRuntime/PackageResolver.h"

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <tuple>

namespace KeireHub
{
    namespace
    {
        [[nodiscard]] HubTemplateEditorCompatibilityInput CompatibilityInput(const HubEditorUiRecord& editor)
        {
            return {.Version = editor.Version,
                    .MinimumProjectSchema = editor.MinimumProjectSchema,
                    .MaximumProjectSchema = editor.MaximumProjectSchema,
                    .Healthy = editor.Healthy,
                    .HasEntrypoint = !editor.Entrypoint.empty(),
                    .HasAssetToolEntrypoint = !editor.AssetToolEntrypoint.empty()};
        }

        [[nodiscard]] int ChannelRank(std::string channel)
        {
            std::ranges::transform(channel, channel.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            if (channel == "stable")
                return 0;
            if (channel == "preview")
                return 1;
            if (channel == "nightly")
                return 2;
            if (channel == "development")
                return 3;
            return 4;
        }
    } // namespace

    std::vector<HubProjectEditorChoice> BuildHubProjectEditorChoices(const std::span<const HubEditorUiRecord> editors,
                                                                     const HubTemplateUiRecord& projectTemplate)
    {
        std::vector<HubProjectEditorChoice> result;
        result.reserve(editors.size());
        for (std::size_t index = 0; index < editors.size(); ++index)
        {
            const auto& editor = editors[index];
            if (!EvaluateTemplateCompatibility(projectTemplate, CompatibilityInput(editor)).Compatible())
                continue;
            result.push_back({.EditorIndex = index,
                              .Id = editor.Id,
                              .PrimaryLabel = editor.Version + "  ·  " + editor.Channel + "  ·  " +
                                              (editor.Managed ? "Managed" : "External"),
                              .RootLabel = Keire::Detail::PathToUtf8(editor.Root.lexically_normal())});
        }
        std::ranges::sort(result,
                          [&editors](const HubProjectEditorChoice& left, const HubProjectEditorChoice& right)
                          {
                              const auto& leftEditor = editors[left.EditorIndex];
                              const auto& rightEditor = editors[right.EditorIndex];
                              const auto leftVersion = SemanticVersion::Parse(leftEditor.Version).Value();
                              const auto rightVersion = SemanticVersion::Parse(rightEditor.Version).Value();
                              if (leftVersion != rightVersion)
                                  return leftVersion > rightVersion;
                              const auto leftKey = std::tuple{ChannelRank(leftEditor.Channel), !leftEditor.Managed,
                                                              left.RootLabel, leftEditor.Id};
                              const auto rightKey = std::tuple{ChannelRank(rightEditor.Channel), !rightEditor.Managed,
                                                               right.RootLabel, rightEditor.Id};
                              return leftKey < rightKey;
                          });
        return result;
    }

    std::string SelectHubProjectEditorId(const std::span<const HubProjectEditorChoice> choices,
                                         const std::string_view currentId)
    {
        if (const auto selected = std::ranges::find(choices, currentId, &HubProjectEditorChoice::Id);
            selected != choices.end())
        {
            return selected->Id;
        }
        return choices.empty() ? std::string{} : choices.front().Id;
    }
} // namespace KeireHub
