#include "KeireHub/HubProjectsUi.h"

#include <utility>

namespace KeireHub
{
    HubProjectUiCommand TakeOpenWithEditorCommand(std::optional<HubProjectUiPendingSelection>& pending,
                                                  std::string editorId)
    {
        if (!pending)
            return {};
        auto selected = std::exchange(pending, std::nullopt);
        return {.Type = HubProjectUiCommandType::OpenWithEditor,
                .ProjectId = std::move(selected->Id),
                .EditorId = std::move(editorId),
                .Path = std::move(selected->Root)};
    }
} // namespace KeireHub
