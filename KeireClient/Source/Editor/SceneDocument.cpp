#include "KeireClient/Editor/SceneDocument.h"

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

    void SceneDocument::BeginPlay()
    {
        if (!m_Scene)
            throw std::logic_error("SceneDocument cannot enter Play without an editing scene.");
        if (m_PlaySession && m_PlaySession->State() != Keire::ScenePlayState::Stopped)
            throw std::logic_error("SceneDocument is already in Play.");
        m_PlaySession = Keire::CreateRef<Keire::SceneRuntimeSession>(m_Scene);
        m_PlaySession->Play();
        SynchronizeSelection();
    }

    void SceneDocument::Close() noexcept
    {
        if (m_PlaySession)
            m_PlaySession->Stop();
        m_PlaySession.Reset();
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
