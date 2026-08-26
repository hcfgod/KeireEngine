#pragma once

#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace KeireEditor
{
    enum class EditorWorkspaceUpdatePhase : std::uint8_t
    {
        CaptureConsole,
        SceneTransitions,
        SmokeAutomation,
        PlayRuntime,
        EditModePreview,
        PendingFileDialogs,
        ManagedRuntime,
        BuildAndCook,
        AssetOperations,
        QueuedAssetMutations,
        QueuedPrefabCreations,
        DocumentMaintenance,
        SceneLoad,
        SceneRecovery,
        AssetAdmission,
        AssetHotReload
    };

    enum class EditorWorkspaceShutdownPhase : std::uint8_t
    {
        DiagnosticBundle,
        SessionPreferences,
        BuildAndCook,
        Packages,
        AssetOperations,
        ManagedRuntime,
        MaterialDraft,
        MaterialCatalog,
        SceneViewport,
        ProjectSettings,
        Input,
        PlayMode,
        TransientPanels,
        Undo,
        SceneRecovery,
        Documents,
        AssetPackage,
        AssetBrowser
    };

    enum class EditorWorkspaceUpdateDisposition : std::uint8_t
    {
        Continue,
        Stop
    };

    namespace Detail
    {
        struct EditorWorkspaceCallbackState final
        {
            std::atomic<std::uint64_t> Generation{1};
            std::atomic_bool Accepting{true};
        };
    } // namespace Detail

    class EditorWorkspaceCallbackToken final
    {
      public:
        EditorWorkspaceCallbackToken() = default;

        [[nodiscard]] bool Current() const noexcept;

      private:
        friend class EditorCoordinatorLifetime;
        friend class EditorWorkspaceLifecycleCoordinator;

        EditorWorkspaceCallbackToken(std::weak_ptr<const Detail::EditorWorkspaceCallbackState> state,
                                     std::uint64_t generation) noexcept;

        std::weak_ptr<const Detail::EditorWorkspaceCallbackState> m_State;
        std::uint64_t m_Generation = 0;
    };

    class EditorCoordinatorLifetime final
    {
      public:
        explicit EditorCoordinatorLifetime(std::string_view name);
        ~EditorCoordinatorLifetime() noexcept;

        EditorCoordinatorLifetime(const EditorCoordinatorLifetime&) = delete;
        EditorCoordinatorLifetime& operator=(const EditorCoordinatorLifetime&) = delete;
        EditorCoordinatorLifetime(EditorCoordinatorLifetime&&) = delete;
        EditorCoordinatorLifetime& operator=(EditorCoordinatorLifetime&&) = delete;

        void RequireOwnerThreadAccess(std::string_view operation) const;
        void RequireOwnerThread(std::string_view operation) const;
        [[nodiscard]] bool BeginShutdown() noexcept;
        [[nodiscard]] EditorWorkspaceCallbackToken CaptureCallbackToken() const;
        [[nodiscard]] bool ShutdownComplete() const;
        [[nodiscard]] bool ShutdownOwnerViolation() const;

      private:
        void InvalidateCallbacks() noexcept;

        std::thread::id m_OwnerThread;
        std::shared_ptr<Detail::EditorWorkspaceCallbackState> m_CallbackState;
        std::string m_Name;
        bool m_ShutdownComplete = false;
        bool m_ShutdownOwnerViolation = false;
    };

    class EditorWorkspaceLifecycleCoordinator final
    {
      public:
        using UpdateAction = std::function<EditorWorkspaceUpdateDisposition(EditorWorkspaceUpdatePhase)>;
        using ShutdownAction = std::function<void(EditorWorkspaceShutdownPhase)>;
        using ShutdownFailureAction = std::function<void(EditorWorkspaceShutdownPhase, const std::exception_ptr&)>;

        EditorWorkspaceLifecycleCoordinator();
        ~EditorWorkspaceLifecycleCoordinator() noexcept;

        EditorWorkspaceLifecycleCoordinator(const EditorWorkspaceLifecycleCoordinator&) = delete;
        EditorWorkspaceLifecycleCoordinator& operator=(const EditorWorkspaceLifecycleCoordinator&) = delete;
        EditorWorkspaceLifecycleCoordinator(EditorWorkspaceLifecycleCoordinator&&) = delete;
        EditorWorkspaceLifecycleCoordinator& operator=(EditorWorkspaceLifecycleCoordinator&&) = delete;

        void Update(const UpdateAction& action);
        void Shutdown(const ShutdownAction& action, const ShutdownFailureAction& reportFailure = {}) noexcept;

        [[nodiscard]] EditorWorkspaceCallbackToken CaptureCallbackToken() const;
        [[nodiscard]] std::span<const EditorWorkspaceUpdatePhase> LastUpdateTrace() const;
        [[nodiscard]] std::span<const EditorWorkspaceShutdownPhase> ShutdownTrace() const;
        [[nodiscard]] std::exception_ptr FirstShutdownFailure() const;
        [[nodiscard]] bool ShutdownComplete() const;
        [[nodiscard]] bool ShutdownOwnerViolation() const;

        [[nodiscard]] static std::span<const EditorWorkspaceUpdatePhase> UpdatePhases() noexcept;
        [[nodiscard]] static std::span<const EditorWorkspaceShutdownPhase> ShutdownPhases() noexcept;

      private:
        void RequireOwnerThreadAccess(std::string_view operation) const;
        void InvalidateCallbacks() noexcept;

        std::thread::id m_OwnerThread;
        std::shared_ptr<Detail::EditorWorkspaceCallbackState> m_CallbackState;
        std::vector<EditorWorkspaceUpdatePhase> m_LastUpdateTrace;
        std::vector<EditorWorkspaceShutdownPhase> m_ShutdownTrace;
        std::exception_ptr m_FirstShutdownFailure;
        bool m_ShutdownComplete = false;
        bool m_ShutdownOwnerViolation = false;
    };

    [[nodiscard]] std::string_view ToString(EditorWorkspaceUpdatePhase phase) noexcept;
    [[nodiscard]] std::string_view ToString(EditorWorkspaceShutdownPhase phase) noexcept;
} // namespace KeireEditor
