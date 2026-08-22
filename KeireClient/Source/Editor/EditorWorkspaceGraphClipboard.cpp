#include "KeireClient/EditorWorkspaceLayer.h"

void EditorWorkspaceLayer::SetGraphClipboard(const std::string_view text) { Owner().Windows()->SetClipboardText(text); }

std::string EditorWorkspaceLayer::GraphClipboard() const { return Owner().Windows()->ClipboardText(); }
