#include "KeireClient/Editor/SceneDocument.h"

#include "KeireInternal/FileSystem.h"

#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    Keire::Ref<Keire::Scene> SceneDocument::ActiveScene() const noexcept
    {
        if (m_PlaySession && m_PlaySession->State() != Keire::ScenePlayState::Stopped)
        {
            if (const auto runtime = m_PlaySession->RuntimeScene())
                return runtime;
        }
        return m_Scene;
    }

    bool SceneDocument::IsSelected(const Keire::AssetId entity) const noexcept
    {
        return std::ranges::find(m_Selections, entity) != m_Selections.end();
    }

    void SceneDocument::Select(const Keire::AssetId selection) noexcept { Select(selection, false); }

    void SceneDocument::Select(const Keire::AssetId selection, const bool additive) noexcept
    {
        const auto scene = ActiveScene();
        if (!selection || !scene || !scene->FindEntity(Keire::EntityId(selection)))
        {
            if (!additive)
                ClearSelection();
            return;
        }
        const auto found = std::ranges::find(m_Selections, selection);
        if (additive && found != m_Selections.end())
        {
            m_Selections.erase(found);
            m_Selection = m_Selections.empty() ? Keire::AssetId{} : m_Selections.back();
            return;
        }
        if (!additive)
            m_Selections.clear();
        if (std::ranges::find(m_Selections, selection) == m_Selections.end())
            m_Selections.push_back(selection);
        m_Selection = selection;
    }

    void SceneDocument::SetSelections(const std::span<const Keire::AssetId> selections) noexcept
    {
        m_Selections.clear();
        const auto scene = ActiveScene();
        if (!scene)
        {
            m_Selection = {};
            return;
        }
        for (const auto selection : selections)
        {
            if (selection && scene->FindEntity(Keire::EntityId(selection)) &&
                std::ranges::find(m_Selections, selection) == m_Selections.end())
                m_Selections.push_back(selection);
        }
        m_Selection = m_Selections.empty() ? Keire::AssetId{} : m_Selections.back();
    }

    void SceneDocument::SynchronizeSelection() noexcept
    {
        const auto scene = ActiveScene();
        if (!scene || !m_Selection)
        {
            ClearSelection();
            return;
        }
        std::erase_if(m_Selections,
                      [&scene](const auto entity) { return !scene->FindEntity(Keire::EntityId(entity)); });
        if (!scene->FindEntity(Keire::EntityId(m_Selection)))
            m_Selection = m_Selections.empty() ? Keire::AssetId{} : m_Selections.back();
        else if (std::ranges::find(m_Selections, m_Selection) == m_Selections.end())
            m_Selections = {m_Selection};
    }

    void SceneDocument::ClearSelection() noexcept
    {
        m_Selection = {};
        m_Selections.clear();
    }

    void SceneDocument::RenameEntity(const Keire::EntityId entity, std::string name)
    {
        const auto scene = ActiveScene();
        auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        if (!target)
            throw std::invalid_argument("Cannot rename an entity outside the active scene.");
        target.SetName(std::move(name));
    }

    Keire::EntityId SceneDocument::CreateEntity(std::string name, const Keire::EntityId parent,
                                                const Keire::ComponentTypeId component)
    {
        const auto scene = ActiveScene();
        const auto parentEntity = parent && scene ? scene->FindEntity(parent) : Keire::Entity{};
        if (!scene || (parent && !parentEntity))
            throw std::invalid_argument("Cannot create an entity outside the active scene.");
        auto created = scene->CreateEntity(std::move(name), parentEntity);
        if (component)
            (void)created.AddComponent(component);
        return created.Id();
    }

    Keire::EntityId SceneDocument::DuplicateEntity(const Keire::EntityId entity)
    {
        const auto scene = ActiveScene();
        const auto source = scene ? scene->FindEntity(entity) : Keire::Entity{};
        if (!source)
            throw std::invalid_argument("Cannot duplicate an entity outside the active scene.");
        return scene->DuplicateEntity(entity).Id();
    }

    void SceneDocument::DeleteEntity(const Keire::EntityId entity)
    {
        const auto scene = ActiveScene();
        if (!scene || !scene->DestroyEntity(entity))
            throw std::invalid_argument("Cannot delete an entity outside the active scene.");
        SynchronizeSelection();
    }

    void SceneDocument::SetEntityActive(const Keire::EntityId entity, const bool active)
    {
        const auto scene = ActiveScene();
        auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        if (!target)
            throw std::invalid_argument("Cannot activate an entity outside the active scene.");
        target.SetActive(active);
    }

    void SceneDocument::ReparentEntity(const Keire::EntityId entity, const Keire::EntityId parent,
                                       const bool keepWorldTransform)
    {
        const auto scene = ActiveScene();
        auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        const auto newParent = parent && scene ? scene->FindEntity(parent) : Keire::Entity{};
        if (!target || (parent && !newParent))
            throw std::invalid_argument("Cannot reparent entities outside the active scene.");
        target.SetParent(newParent, keepWorldTransform);
    }

    void SceneDocument::MoveEntity(const Keire::EntityId entity, const Keire::EntityId parent,
                                   const Keire::EntityId beforeSibling, const bool keepWorldTransform)
    {
        const auto scene = ActiveScene();
        if (!scene || !scene->FindEntity(entity) || (parent && !scene->FindEntity(parent)) ||
            (beforeSibling && !scene->FindEntity(beforeSibling)))
            throw std::invalid_argument("Cannot move entities outside the active scene.");
        scene->MoveEntity(entity, parent, beforeSibling, keepWorldTransform);
    }

    void SceneDocument::SetTransform(const Keire::EntityId entity, const TransformValues& values)
    {
        const auto scene = ActiveScene();
        auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        const auto transform = target ? target.GetComponent<Keire::TransformComponent>() : nullptr;
        if (!transform)
            throw std::invalid_argument("Transform editing requires an entity in the active scene.");
        if (values.Position)
            transform->SetLocalPosition(*values.Position);
        if (values.EulerDegrees)
            transform->SetLocalEulerAngles(*values.EulerDegrees);
        if (values.Scale)
            transform->SetLocalScale(*values.Scale);
    }

    Keire::Ref<Keire::Component> SceneDocument::AddComponent(const Keire::EntityId entity,
                                                             const Keire::ComponentTypeId type)
    {
        const auto scene = ActiveScene();
        auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        if (!target)
            throw std::invalid_argument("Cannot add a component outside the active scene.");
        return target.AddComponent(type);
    }

    void SceneDocument::RemoveComponent(const Keire::EntityId entity, const Keire::ComponentTypeId type)
    {
        const auto scene = ActiveScene();
        auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        if (!target || !target.RemoveComponent(type))
            throw std::invalid_argument("Cannot remove that component from the active scene entity.");
    }

    void SceneDocument::SetComponentEnabled(const Keire::EntityId entity, const Keire::ComponentTypeId type,
                                            const bool enabled)
    {
        const auto scene = ActiveScene();
        const auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        const auto component = target ? target.GetComponent(type) : nullptr;
        if (!component)
            throw std::invalid_argument("Cannot edit a component outside the active scene.");
        component->SetEnabled(enabled);
    }

    void SceneDocument::ResetComponent(const Keire::EntityId entity, const Keire::ComponentTypeId type)
    {
        const auto scene = ActiveScene();
        const auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        const auto component = target ? target.GetComponent(type) : nullptr;
        const auto registration = scene ? scene->Components()->Find(type) : std::nullopt;
        if (!component || !registration)
            throw std::invalid_argument("Cannot reset a component outside the active scene.");
        const auto defaults = registration->Factory();
        if (!defaults)
            throw std::runtime_error("The component factory returned null while resetting the component.");
        registration->Deserialize(*component, registration->Serialize(*defaults), registration->SchemaVersion);
    }

    void SceneDocument::SetComponentProperty(const Keire::EntityId entity, const Keire::ComponentTypeId type,
                                             const std::string_view property, Keire::ComponentPropertyValue value)
    {
        const auto scene = ActiveScene();
        const auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        const auto component = target ? target.GetComponent(type) : nullptr;
        const auto registration = scene ? scene->Components()->Find(type) : std::nullopt;
        if (!component || !registration)
            throw std::invalid_argument("Cannot edit a component outside the active scene.");
        if (std::ranges::find(registration->Properties, property, &Keire::ComponentProperty::Key) ==
            registration->Properties.end())
            throw std::invalid_argument("The component does not declare that property.");
        auto values = registration->Serialize(*component);
        values.insert_or_assign(std::string(property), std::move(value));
        auto validation = registration->Factory();
        registration->Deserialize(*validation, values, registration->SchemaVersion);
        registration->Deserialize(*component, values, registration->SchemaVersion);
    }

    void SceneDocument::SetMeshRendererMaterial(const Keire::EntityId entity, const std::size_t slot,
                                                const Keire::AssetId material)
    {
        const auto scene = ActiveScene();
        const auto target = scene ? scene->FindEntity(entity) : Keire::Entity{};
        const auto renderer = target ? target.GetComponent<Keire::MeshRendererComponent>() : nullptr;
        if (!renderer)
            throw std::invalid_argument("Material editing requires a Mesh Renderer in the active scene.");
        renderer->SetMaterial(slot, material);
    }

    void SceneDocument::Open(Keire::Ref<Keire::Scene> scene, const Keire::AssetId asset, std::filesystem::path source,
                             Keire::Ref<Keire::UndoContext> undo)
    {
        if (!scene)
            throw std::invalid_argument("SceneDocument::Open requires a scene.");
        Close();
        m_Scene = std::move(scene);
        m_Asset = asset ? asset : m_Scene->Asset();
        m_Source = std::move(source);
        m_Undo = std::move(undo);
    }

    void SceneDocument::ReplaceEditingScene(Keire::Ref<Keire::Scene> scene, const bool preserveSelection)
    {
        if (!scene)
            throw std::invalid_argument("SceneDocument::ReplaceEditingScene requires a scene.");
        if (m_Scene && m_Scene != scene)
            m_Scene->Close();
        m_Scene = std::move(scene);
        if (preserveSelection)
            SynchronizeSelection();
        else
            ClearSelection();
    }

    void SceneDocument::SetLoadOperation(Keire::Ref<Keire::SceneLoadOperation> operation) noexcept
    {
        m_LoadOperation = std::move(operation);
    }

    void SceneDocument::SetSaveDialog(Keire::Ref<Keire::SaveFileDialogOperation> operation) noexcept
    {
        m_SaveDialog = std::move(operation);
    }

    Keire::Ref<Keire::SaveFileDialogOperation> SceneDocument::TakeSaveDialog() noexcept
    {
        auto operation = std::move(m_SaveDialog);
        m_SaveDialog.Reset();
        return operation;
    }

    void SceneDocument::SetUndoContext(Keire::Ref<Keire::UndoContext> undo) noexcept { m_Undo = std::move(undo); }

    void SceneDocument::SetIdentity(const Keire::AssetId asset, std::filesystem::path source)
    {
        m_Asset = asset;
        m_Source = std::move(source);
    }

    void SceneDocument::SetRecoveryPath(std::filesystem::path path) { m_RecoveryPath = std::move(path); }

    void SceneDocument::SetStatus(std::string status) { m_Status = std::move(status); }

    void SceneDocument::Save()
    {
        if (!m_Scene || m_Source.empty())
            throw std::logic_error("SceneDocument cannot save without an editing scene and source path.");
        const auto bytes = Keire::SceneAsset::Encode(m_Scene->Snapshot());
        const std::string contents(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        Keire::Detail::WriteTextFileAtomically(m_Source, contents);
        m_Scene->MarkSaved();
        DiscardRecovery();
    }

    bool SceneDocument::WriteRecovery()
    {
        if (!m_Scene || !m_Scene->Dirty() || m_RecoveryPath.empty())
            return false;
        const auto bytes = Keire::SceneAsset::Encode(m_Scene->Snapshot());
        const std::string contents(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        Keire::Detail::WriteTextFileAtomically(m_RecoveryPath, contents);
        m_RecoveryAvailable = true;
        m_RecoverySeconds = 0.0;
        return true;
    }

    void SceneDocument::RestoreRecovery()
    {
        if (!m_Scene || m_RecoveryPath.empty() || !std::filesystem::is_regular_file(m_RecoveryPath))
            throw std::logic_error("SceneDocument has no recovery snapshot to restore.");
        const auto source = Keire::Detail::ReadTextFile(m_RecoveryPath, 64U * 1024U * 1024U);
        const auto bytes = std::as_bytes(std::span(source.data(), source.size()));
        auto restored = Keire::CreateRef<Keire::Scene>(m_Asset, Keire::SceneAsset::Decode(bytes)->Definition(),
                                                       m_Scene->Components());
        restored->MarkDirty();
        ReplaceEditingScene(std::move(restored), true);
        m_RecoveryAvailable = false;
        m_RecoverySeconds = 0.0;
    }

    void SceneDocument::DiscardRecovery() noexcept
    {
        if (!m_RecoveryPath.empty())
        {
            std::error_code ignored;
            std::filesystem::remove(m_RecoveryPath, ignored);
        }
        m_RecoveryAvailable = false;
        m_RecoverySeconds = 0.0;
    }

    void SceneDocument::AdvanceRecovery(const double seconds) noexcept
    {
        if (seconds > 0.0)
            m_RecoverySeconds += seconds;
    }

    void SceneDocument::BeginPlay(Keire::Ref<Keire::UndoContext> playUndo)
    {
        if (!m_Scene)
            throw std::logic_error("SceneDocument cannot enter Play without an editing scene.");
        if (m_PlaySession && m_PlaySession->State() != Keire::ScenePlayState::Stopped)
            throw std::logic_error("SceneDocument is already in Play.");
        m_PlaySession = Keire::CreateRef<Keire::SceneRuntimeSession>(m_Scene);
        m_PlayUndo = std::move(playUndo);
        m_PlaySession->Play();
        SynchronizeSelection();
    }

    void SceneDocument::EndPlay() noexcept
    {
        if (m_PlaySession)
            m_PlaySession->Stop();
        m_PlaySession.Reset();
        if (m_PlayUndo)
            m_PlayUndo->Close();
        m_PlayUndo.Reset();
        SynchronizeSelection();
    }

    void SceneDocument::Close() noexcept
    {
        if (m_PlaySession)
            m_PlaySession->Stop();
        m_PlaySession.Reset();
        if (m_PlayUndo)
            m_PlayUndo->Close();
        m_PlayUndo.Reset();
        m_LoadOperation.Reset();
        m_SaveDialog.Reset();
        if (m_Scene)
            m_Scene->Close();
        m_Scene.Reset();
        m_Undo.Reset();
        m_Asset = {};
        ClearSelection();
        m_Source.clear();
        m_RecoveryPath.clear();
        m_Status.clear();
        m_RecoverySeconds = 0.0;
        m_RecoveryAvailable = false;
    }
} // namespace KeireEditor
