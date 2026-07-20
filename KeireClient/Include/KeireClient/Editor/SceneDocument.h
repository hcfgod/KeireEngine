#pragma once

#include "Keire/Core.h"

#include <filesystem>
#include <string>

namespace KeireEditor
{
    class SceneDocument final
    {
      public:
        [[nodiscard]] Keire::Ref<Keire::Scene> Scene() const noexcept { return m_Scene; }
        [[nodiscard]] Keire::Ref<Keire::SceneRuntimeSession> PlaySession() const noexcept { return m_PlaySession; }
        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Asset; }
        [[nodiscard]] Keire::AssetId Selection() const noexcept { return m_Selection; }
        [[nodiscard]] const std::filesystem::path& Source() const noexcept { return m_Source; }
        [[nodiscard]] const std::filesystem::path& RecoveryPath() const noexcept { return m_RecoveryPath; }
        [[nodiscard]] const std::string& Status() const noexcept { return m_Status; }
        [[nodiscard]] bool Dirty() const noexcept { return m_Scene && m_Scene->Dirty(); }
        [[nodiscard]] bool RecoveryAvailable() const noexcept { return m_RecoveryAvailable; }

        void Select(Keire::AssetId selection) noexcept;
        void ClearSelection() noexcept { m_Selection = {}; }
        void Close() noexcept;

        [[nodiscard]] Keire::Ref<Keire::Scene>& SceneStorage() noexcept { return m_Scene; }
        [[nodiscard]] Keire::Ref<Keire::SceneRuntimeSession>& PlaySessionStorage() noexcept { return m_PlaySession; }
        [[nodiscard]] Keire::Ref<Keire::SceneLoadOperation>& LoadOperationStorage() noexcept { return m_LoadOperation; }
        [[nodiscard]] Keire::Ref<Keire::SaveFileDialogOperation>& SaveDialogStorage() noexcept { return m_SaveDialog; }
        [[nodiscard]] Keire::Ref<Keire::UndoContext>& UndoStorage() noexcept { return m_Undo; }
        [[nodiscard]] Keire::AssetId& AssetStorage() noexcept { return m_Asset; }
        [[nodiscard]] Keire::AssetId& SelectionStorage() noexcept { return m_Selection; }
        [[nodiscard]] std::filesystem::path& SourceStorage() noexcept { return m_Source; }
        [[nodiscard]] std::filesystem::path& RecoveryPathStorage() noexcept { return m_RecoveryPath; }
        [[nodiscard]] std::string& StatusStorage() noexcept { return m_Status; }
        [[nodiscard]] double& RecoverySecondsStorage() noexcept { return m_RecoverySeconds; }
        [[nodiscard]] bool& RecoveryAvailableStorage() noexcept { return m_RecoveryAvailable; }

      private:
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::SceneRuntimeSession> m_PlaySession;
        Keire::Ref<Keire::SceneLoadOperation> m_LoadOperation;
        Keire::Ref<Keire::SaveFileDialogOperation> m_SaveDialog;
        Keire::Ref<Keire::UndoContext> m_Undo;
        Keire::AssetId m_Asset;
        Keire::AssetId m_Selection;
        std::filesystem::path m_Source;
        std::filesystem::path m_RecoveryPath;
        std::string m_Status;
        double m_RecoverySeconds = 0.0;
        bool m_RecoveryAvailable = false;
    };
} // namespace KeireEditor
