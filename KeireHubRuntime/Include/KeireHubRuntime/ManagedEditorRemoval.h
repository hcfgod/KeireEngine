#pragma once

#include "KeireHubRuntime/EditorInstallationManager.h"

#include <filesystem>
#include <functional>
#include <string>

namespace KeireHub
{
    enum class ManagedEditorRemovalPhase
    {
        Prepared,
        RootRenamed,
        Purging,
        RemovingAnchors
    };

    struct ManagedEditorRemovalCallbacks final
    {
        // Cancellation is honored only before the installation root is atomically renamed. Once committed, recovery
        // must finish the removal so callers never mistake a hidden tombstone for an installed editor.
        std::function<bool()> CancelBeforeCommit;
        // A false result simulates or reports an interruption after a durable phase boundary. It is primarily a
        // deterministic recovery-test seam; production workers normally leave it empty.
        std::function<bool(ManagedEditorRemovalPhase)> ContinueAfterPhase;
        // Called only after the complete managed tree has been verified and immediately before the final verification
        // and commit rename. Implementations may release trusted background services that hold files in the tree.
        std::function<void()> PrepareForCommit;
    };

    struct ManagedEditorRemovalResult final
    {
        bool Completed = false;
        bool CancelledBeforeCommit = false;
    };

    [[nodiscard]] HubResult<ManagedEditorRemovalResult>
    RemoveManagedEditorInstallation(const EditorManagedOperationPlan& plan, std::string operationId,
                                    const ManagedEditorRemovalCallbacks& callbacks = {});
} // namespace KeireHub
