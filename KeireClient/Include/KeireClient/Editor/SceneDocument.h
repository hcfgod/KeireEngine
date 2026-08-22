#pragma once

#include "Keire/Core.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace KeireEditor
{
    class SceneDocument final
    {
      public:
        static constexpr std::size_t MaximumEntityNameBytes = 256;

        struct TransformValues final
        {
            std::optional<Keire::Vector3> Position;
            std::optional<Keire::Vector3> EulerDegrees;
            std::optional<Keire::Vector3> Scale;
        };

        [[nodiscard]] Keire::Ref<Keire::Scene> EditingScene() const noexcept
        {
            return m_Scene && m_Scene->IsOpen() ? m_Scene : Keire::Ref<Keire::Scene>{};
        }
        [[nodiscard]] Keire::Ref<Keire::Scene> ActiveScene() const noexcept;
        [[nodiscard]] Keire::Ref<Keire::SceneRuntimeSession> PlaySession() const noexcept { return m_PlaySession; }
        [[nodiscard]] Keire::Ref<Keire::SceneLoadOperation> LoadOperation() const noexcept { return m_LoadOperation; }
        [[nodiscard]] Keire::Ref<Keire::SaveFileDialogOperation> SaveDialog() const noexcept { return m_SaveDialog; }
        [[nodiscard]] Keire::Ref<Keire::UndoContext> UndoContext() const noexcept { return m_Undo; }
        [[nodiscard]] Keire::Ref<Keire::UndoContext> History() const noexcept
        {
            return m_PlaySession ? m_PlayUndo : m_Undo;
        }
        [[nodiscard]] Keire::AssetId Asset() const noexcept { return m_Asset; }
        [[nodiscard]] Keire::AssetId Selection() const noexcept { return m_Selection; }
        [[nodiscard]] std::span<const Keire::AssetId> Selections() const noexcept { return m_Selections; }
        [[nodiscard]] bool IsSelected(Keire::AssetId entity) const noexcept;
        [[nodiscard]] const std::filesystem::path& Source() const noexcept { return m_Source; }
        [[nodiscard]] const std::filesystem::path& RecoveryPath() const noexcept { return m_RecoveryPath; }
        [[nodiscard]] const std::string& Status() const noexcept { return m_Status; }
        [[nodiscard]] bool Dirty() const noexcept { return m_Scene && m_Scene->IsOpen() && m_Scene->Dirty(); }
        [[nodiscard]] bool RecoveryAvailable() const noexcept { return m_RecoveryAvailable; }
        [[nodiscard]] double RecoverySeconds() const noexcept { return m_RecoverySeconds; }

        void Select(Keire::AssetId selection) noexcept;
        void Select(Keire::AssetId selection, bool additive) noexcept;
        void SetSelections(std::span<const Keire::AssetId> selections) noexcept;
        void SynchronizeSelection() noexcept;
        void ClearSelection() noexcept;
        [[nodiscard]] static bool IsValidEntityName(std::string_view name) noexcept;
        void RenameEntity(Keire::EntityId entity, std::string name);
        [[nodiscard]] Keire::EntityId CreateEntity(std::string name = "GameObject", Keire::EntityId parent = {},
                                                   Keire::ComponentTypeId component = {});
        [[nodiscard]] Keire::EntityId DuplicateEntity(Keire::EntityId entity);
        void DeleteEntity(Keire::EntityId entity);
        void SetEntityActive(Keire::EntityId entity, bool active);
        void SetEntityLayer(Keire::EntityId entity, std::uint32_t layer);
        void SetEntitiesLayer(std::span<const Keire::AssetId> entities, std::uint32_t layer);
        void SetEntityTags(Keire::EntityId entity, std::vector<std::string> tags);
        void ReparentEntity(Keire::EntityId entity, Keire::EntityId parent, bool keepWorldTransform = true);
        void MoveEntity(Keire::EntityId entity, Keire::EntityId parent, Keire::EntityId beforeSibling = {},
                        bool keepWorldTransform = true);
        [[nodiscard]] std::vector<Keire::EntityId> MoveEntities(std::span<const Keire::EntityId> entities,
                                                                Keire::EntityId parent,
                                                                Keire::EntityId beforeSibling = {},
                                                                bool keepWorldTransform = true);
        void SetTransform(Keire::EntityId entity, const TransformValues& values);
        [[nodiscard]] Keire::Ref<Keire::Component> AddComponent(Keire::EntityId entity, Keire::ComponentTypeId type);
        void RemoveComponent(Keire::EntityId entity, Keire::ComponentTypeId type);
        void RemoveComponent(Keire::EntityId entity, const Keire::Ref<Keire::Component>& component);
        void MoveComponentBefore(Keire::EntityId entity, const Keire::Ref<Keire::Component>& component,
                                 const Keire::Ref<Keire::Component>& before = {});
        void SetComponentEnabled(Keire::EntityId entity, Keire::ComponentTypeId type, bool enabled);
        void SetComponentValues(Keire::EntityId entity, const Keire::Ref<Keire::Component>& component,
                                const Keire::ComponentPropertyBag& values);
        void ResetComponent(Keire::EntityId entity, Keire::ComponentTypeId type);
        void SetComponentProperty(Keire::EntityId entity, Keire::ComponentTypeId type, std::string_view property,
                                  Keire::ComponentPropertyValue value);
        void SetMeshRendererMaterial(Keire::EntityId entity, std::size_t slot, Keire::AssetId material);
        void Open(Keire::Ref<Keire::Scene> scene, Keire::AssetId asset = {}, std::filesystem::path source = {},
                  Keire::Ref<Keire::UndoContext> undo = {});
        void ReplaceEditingScene(Keire::Ref<Keire::Scene> scene, bool preserveSelection = true);
        void SetLoadOperation(Keire::Ref<Keire::SceneLoadOperation> operation) noexcept;
        void SetSaveDialog(Keire::Ref<Keire::SaveFileDialogOperation> operation) noexcept;
        [[nodiscard]] Keire::Ref<Keire::SaveFileDialogOperation> TakeSaveDialog() noexcept;
        void SetUndoContext(Keire::Ref<Keire::UndoContext> undo) noexcept;
        void SetIdentity(Keire::AssetId asset, std::filesystem::path source);
        void SetRecoveryPath(std::filesystem::path path);
        void SetStatus(std::string status);
        void Save();
        [[nodiscard]] bool WriteRecovery();
        void RestoreRecovery();
        void DiscardRecovery() noexcept;
        void AdvanceRecovery(double seconds) noexcept;
        void ResetRecoveryTimer() noexcept { m_RecoverySeconds = 0.0; }
        void BeginPlay(Keire::Ref<Keire::UndoContext> playUndo = {}, Keire::Ref<Keire::AssetSystem> assets = {},
                       Keire::Ref<Keire::AudioSystem> audio = {}, Keire::Ref<Keire::PhysicsSystem> physics = {},
                       Keire::AssetId defaultMixer = {}, const Keire::Ref<Keire::SceneRuntimeWorld>& runtimeWorld = {});
        void SetPlaySession(Keire::Ref<Keire::SceneRuntimeSession> session);
        void EndPlay() noexcept;
        void SetRecoveryAvailable(bool available) noexcept { m_RecoveryAvailable = available; }
        void Close() noexcept;

      private:
        Keire::Ref<Keire::Scene> m_Scene;
        Keire::Ref<Keire::SceneRuntimeSession> m_PlaySession;
        Keire::Ref<Keire::SceneLoadOperation> m_LoadOperation;
        Keire::Ref<Keire::SaveFileDialogOperation> m_SaveDialog;
        Keire::Ref<Keire::UndoContext> m_Undo;
        Keire::Ref<Keire::UndoContext> m_PlayUndo;
        Keire::AssetId m_Asset;
        Keire::AssetId m_Selection;
        std::vector<Keire::AssetId> m_Selections;
        std::filesystem::path m_Source;
        std::filesystem::path m_RecoveryPath;
        std::string m_Status;
        double m_RecoverySeconds = 0.0;
        bool m_RecoveryAvailable = false;
    };
} // namespace KeireEditor
