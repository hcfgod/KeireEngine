#include "KeireClient/Editor/EditorWorkspaceLifecycleCoordinator.h"

#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        constexpr std::array UpdateSequence{
            EditorWorkspaceUpdatePhase::CaptureConsole,
            EditorWorkspaceUpdatePhase::SceneTransitions,
            EditorWorkspaceUpdatePhase::SmokeAutomation,
            EditorWorkspaceUpdatePhase::PlayRuntime,
            EditorWorkspaceUpdatePhase::EditModePreview,
            EditorWorkspaceUpdatePhase::PendingFileDialogs,
            EditorWorkspaceUpdatePhase::ManagedRuntime,
            EditorWorkspaceUpdatePhase::BuildAndCook,
            EditorWorkspaceUpdatePhase::AssetOperations,
            EditorWorkspaceUpdatePhase::QueuedAssetMutations,
            EditorWorkspaceUpdatePhase::QueuedPrefabCreations,
            EditorWorkspaceUpdatePhase::DocumentMaintenance,
            EditorWorkspaceUpdatePhase::SceneLoad,
            EditorWorkspaceUpdatePhase::SceneRecovery,
            EditorWorkspaceUpdatePhase::AssetAdmission,
            EditorWorkspaceUpdatePhase::AssetHotReload,
        };

        constexpr std::array ShutdownSequence{
            EditorWorkspaceShutdownPhase::DiagnosticBundle,
            EditorWorkspaceShutdownPhase::SessionPreferences,
            EditorWorkspaceShutdownPhase::BuildAndCook,
            EditorWorkspaceShutdownPhase::Packages,
            EditorWorkspaceShutdownPhase::AssetOperations,
            EditorWorkspaceShutdownPhase::ManagedRuntime,
            EditorWorkspaceShutdownPhase::MaterialDraft,
            EditorWorkspaceShutdownPhase::MaterialCatalog,
            EditorWorkspaceShutdownPhase::SceneViewport,
            EditorWorkspaceShutdownPhase::ProjectSettings,
            EditorWorkspaceShutdownPhase::Input,
            EditorWorkspaceShutdownPhase::PlayMode,
            EditorWorkspaceShutdownPhase::TransientPanels,
            EditorWorkspaceShutdownPhase::Undo,
            EditorWorkspaceShutdownPhase::SceneRecovery,
            EditorWorkspaceShutdownPhase::Documents,
            EditorWorkspaceShutdownPhase::AssetPackage,
            EditorWorkspaceShutdownPhase::AssetBrowser,
        };
    } // namespace

    EditorWorkspaceCallbackToken::EditorWorkspaceCallbackToken(
        std::weak_ptr<const Detail::EditorWorkspaceCallbackState> state, const std::uint64_t generation) noexcept
        : m_State(std::move(state)), m_Generation(generation)
    {
    }

    bool EditorWorkspaceCallbackToken::Current() const noexcept
    {
        const auto state = m_State.lock();
        return state && state->Accepting.load(std::memory_order_acquire) &&
               state->Generation.load(std::memory_order_acquire) == m_Generation;
    }

    EditorCoordinatorLifetime::EditorCoordinatorLifetime(const std::string_view name)
        : m_OwnerThread(std::this_thread::get_id()),
          m_CallbackState(std::make_shared<Detail::EditorWorkspaceCallbackState>()), m_Name(name)
    {
        if (m_Name.empty())
            throw std::invalid_argument("Editor coordinator name must not be empty.");
    }

    EditorCoordinatorLifetime::~EditorCoordinatorLifetime() noexcept { InvalidateCallbacks(); }

    void EditorCoordinatorLifetime::RequireOwnerThreadAccess(const std::string_view operation) const
    {
        if (std::this_thread::get_id() != m_OwnerThread)
            throw std::logic_error(std::string(m_Name) + " " + std::string(operation) +
                                   " must run on the owner thread.");
    }

    void EditorCoordinatorLifetime::RequireOwnerThread(const std::string_view operation) const
    {
        RequireOwnerThreadAccess(operation);
        if (m_ShutdownComplete)
            throw std::logic_error(std::string(m_Name) + " is already shut down.");
    }

    bool EditorCoordinatorLifetime::BeginShutdown() noexcept
    {
        if (std::this_thread::get_id() != m_OwnerThread)
        {
            m_ShutdownOwnerViolation = true;
            return false;
        }
        if (m_ShutdownComplete)
            return false;
        InvalidateCallbacks();
        m_ShutdownComplete = true;
        return true;
    }

    EditorWorkspaceCallbackToken EditorCoordinatorLifetime::CaptureCallbackToken() const
    {
        RequireOwnerThreadAccess("capture callback token");
        return {m_CallbackState, m_CallbackState->Generation.load(std::memory_order_acquire)};
    }

    bool EditorCoordinatorLifetime::ShutdownComplete() const
    {
        RequireOwnerThreadAccess("read shutdown state");
        return m_ShutdownComplete;
    }

    bool EditorCoordinatorLifetime::ShutdownOwnerViolation() const
    {
        RequireOwnerThreadAccess("read shutdown owner violation");
        return m_ShutdownOwnerViolation;
    }

    void EditorCoordinatorLifetime::InvalidateCallbacks() noexcept
    {
        m_CallbackState->Accepting.store(false, std::memory_order_release);
        m_CallbackState->Generation.fetch_add(1, std::memory_order_acq_rel);
    }

    EditorWorkspaceLifecycleCoordinator::EditorWorkspaceLifecycleCoordinator()
        : m_OwnerThread(std::this_thread::get_id()),
          m_CallbackState(std::make_shared<Detail::EditorWorkspaceCallbackState>())
    {
        m_LastUpdateTrace.reserve(UpdateSequence.size());
        m_ShutdownTrace.reserve(ShutdownSequence.size());
    }

    EditorWorkspaceLifecycleCoordinator::~EditorWorkspaceLifecycleCoordinator() noexcept { InvalidateCallbacks(); }

    void EditorWorkspaceLifecycleCoordinator::Update(const UpdateAction& action)
    {
        RequireOwnerThreadAccess("Update");
        if (m_ShutdownComplete)
            throw std::logic_error("Editor workspace update cannot run after shutdown.");
        if (!action)
            throw std::invalid_argument("Editor workspace update action must be callable.");

        m_LastUpdateTrace.clear();
        for (const auto phase : UpdateSequence)
        {
            m_LastUpdateTrace.push_back(phase);
            if (action(phase) == EditorWorkspaceUpdateDisposition::Stop)
                break;
        }
    }

    void EditorWorkspaceLifecycleCoordinator::Shutdown(const ShutdownAction& action,
                                                       const ShutdownFailureAction& reportFailure) noexcept
    {
        if (std::this_thread::get_id() != m_OwnerThread)
        {
            m_ShutdownOwnerViolation = true;
            return;
        }
        if (m_ShutdownComplete)
            return;

        InvalidateCallbacks();
        m_ShutdownTrace.clear();
        for (const auto phase : ShutdownSequence)
        {
            m_ShutdownTrace.push_back(phase);
            try
            {
                if (action)
                    action(phase);
            }
            catch (...)
            {
                const auto failure = std::current_exception();
                if (!m_FirstShutdownFailure)
                    m_FirstShutdownFailure = failure;
                if (reportFailure)
                {
                    try
                    {
                        reportFailure(phase, failure);
                    }
                    catch (...)
                    {
                    }
                }
            }
        }
        m_ShutdownComplete = true;
    }

    EditorWorkspaceCallbackToken EditorWorkspaceLifecycleCoordinator::CaptureCallbackToken() const
    {
        RequireOwnerThreadAccess("capture callback token");
        return {m_CallbackState, m_CallbackState->Generation.load(std::memory_order_acquire)};
    }

    std::span<const EditorWorkspaceUpdatePhase> EditorWorkspaceLifecycleCoordinator::LastUpdateTrace() const
    {
        RequireOwnerThreadAccess("read update trace");
        return m_LastUpdateTrace;
    }

    std::span<const EditorWorkspaceShutdownPhase> EditorWorkspaceLifecycleCoordinator::ShutdownTrace() const
    {
        RequireOwnerThreadAccess("read shutdown trace");
        return m_ShutdownTrace;
    }

    std::exception_ptr EditorWorkspaceLifecycleCoordinator::FirstShutdownFailure() const
    {
        RequireOwnerThreadAccess("read first shutdown failure");
        return m_FirstShutdownFailure;
    }

    bool EditorWorkspaceLifecycleCoordinator::ShutdownComplete() const
    {
        RequireOwnerThreadAccess("read shutdown state");
        return m_ShutdownComplete;
    }

    bool EditorWorkspaceLifecycleCoordinator::ShutdownOwnerViolation() const
    {
        RequireOwnerThreadAccess("read shutdown owner violation");
        return m_ShutdownOwnerViolation;
    }

    std::span<const EditorWorkspaceUpdatePhase> EditorWorkspaceLifecycleCoordinator::UpdatePhases() noexcept
    {
        return UpdateSequence;
    }

    std::span<const EditorWorkspaceShutdownPhase> EditorWorkspaceLifecycleCoordinator::ShutdownPhases() noexcept
    {
        return ShutdownSequence;
    }

    void EditorWorkspaceLifecycleCoordinator::RequireOwnerThreadAccess(const std::string_view operation) const
    {
        if (std::this_thread::get_id() != m_OwnerThread)
            throw std::logic_error("Editor workspace " + std::string(operation) + " must run on the owner thread.");
    }

    void EditorWorkspaceLifecycleCoordinator::InvalidateCallbacks() noexcept
    {
        m_CallbackState->Accepting.store(false, std::memory_order_release);
        m_CallbackState->Generation.fetch_add(1, std::memory_order_acq_rel);
    }

    std::string_view ToString(const EditorWorkspaceUpdatePhase phase) noexcept
    {
        switch (phase)
        {
        case EditorWorkspaceUpdatePhase::CaptureConsole:
            return "capture-console";
        case EditorWorkspaceUpdatePhase::SceneTransitions:
            return "scene-transitions";
        case EditorWorkspaceUpdatePhase::SmokeAutomation:
            return "smoke-automation";
        case EditorWorkspaceUpdatePhase::PlayRuntime:
            return "play-runtime";
        case EditorWorkspaceUpdatePhase::EditModePreview:
            return "edit-mode-preview";
        case EditorWorkspaceUpdatePhase::PendingFileDialogs:
            return "pending-file-dialogs";
        case EditorWorkspaceUpdatePhase::ManagedRuntime:
            return "managed-runtime";
        case EditorWorkspaceUpdatePhase::BuildAndCook:
            return "build-and-cook";
        case EditorWorkspaceUpdatePhase::AssetOperations:
            return "asset-operations";
        case EditorWorkspaceUpdatePhase::QueuedAssetMutations:
            return "queued-asset-mutations";
        case EditorWorkspaceUpdatePhase::QueuedPrefabCreations:
            return "queued-prefab-creations";
        case EditorWorkspaceUpdatePhase::DocumentMaintenance:
            return "document-maintenance";
        case EditorWorkspaceUpdatePhase::SceneLoad:
            return "scene-load";
        case EditorWorkspaceUpdatePhase::SceneRecovery:
            return "scene-recovery";
        case EditorWorkspaceUpdatePhase::AssetAdmission:
            return "asset-admission";
        case EditorWorkspaceUpdatePhase::AssetHotReload:
            return "asset-hot-reload";
        }
        return "unknown-update-phase";
    }

    std::string_view ToString(const EditorWorkspaceShutdownPhase phase) noexcept
    {
        switch (phase)
        {
        case EditorWorkspaceShutdownPhase::DiagnosticBundle:
            return "diagnostic-bundle";
        case EditorWorkspaceShutdownPhase::SessionPreferences:
            return "session-preferences";
        case EditorWorkspaceShutdownPhase::BuildAndCook:
            return "build-and-cook";
        case EditorWorkspaceShutdownPhase::Packages:
            return "packages";
        case EditorWorkspaceShutdownPhase::AssetOperations:
            return "asset-operations";
        case EditorWorkspaceShutdownPhase::ManagedRuntime:
            return "managed-runtime";
        case EditorWorkspaceShutdownPhase::MaterialDraft:
            return "material-draft";
        case EditorWorkspaceShutdownPhase::MaterialCatalog:
            return "material-catalog";
        case EditorWorkspaceShutdownPhase::SceneViewport:
            return "scene-viewport";
        case EditorWorkspaceShutdownPhase::ProjectSettings:
            return "project-settings";
        case EditorWorkspaceShutdownPhase::Input:
            return "input";
        case EditorWorkspaceShutdownPhase::PlayMode:
            return "play-mode";
        case EditorWorkspaceShutdownPhase::TransientPanels:
            return "transient-panels";
        case EditorWorkspaceShutdownPhase::Undo:
            return "undo";
        case EditorWorkspaceShutdownPhase::SceneRecovery:
            return "scene-recovery";
        case EditorWorkspaceShutdownPhase::Documents:
            return "documents";
        case EditorWorkspaceShutdownPhase::AssetPackage:
            return "asset-package";
        case EditorWorkspaceShutdownPhase::AssetBrowser:
            return "asset-browser";
        }
        return "unknown-shutdown-phase";
    }
} // namespace KeireEditor
