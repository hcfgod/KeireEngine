#include "Keire/Scenes/SceneSystem.h"

#include <algorithm>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Keire
{
    class SceneLoadOperation::Impl final
    {
      public:
        Impl(const AssetId value, const SceneLoadMode mode, AssetHandle<SceneAsset> asset)
            : OwnerThread(std::this_thread::get_id()), AssetIdValue(value), LoadMode(mode),
              AssetHandleValue(std::move(asset))
        {
        }

        std::thread::id OwnerThread;
        AssetId AssetIdValue;
        SceneLoadMode LoadMode = SceneLoadMode::Single;
        SceneLoadState LoadState = SceneLoadState::Queued;
        AssetHandle<SceneAsset> AssetHandleValue;
        AssetDiagnostic Failure;
        Ref<Scene> LoadedScene;
    };

    SceneLoadOperation::SceneLoadOperation(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}

    SceneLoadOperation::~SceneLoadOperation() { Cancel(); }

    AssetId SceneLoadOperation::Asset() const noexcept { return m_Impl->AssetIdValue; }

    SceneLoadMode SceneLoadOperation::Mode() const noexcept { return m_Impl->LoadMode; }

    SceneLoadState SceneLoadOperation::State() const noexcept { return m_Impl->LoadState; }

    AssetDiagnostic SceneLoadOperation::Diagnostic() const { return m_Impl->Failure; }

    Ref<Scene> SceneLoadOperation::Result() const noexcept { return m_Impl->LoadedScene; }

    void SceneLoadOperation::Cancel() noexcept
    {
        if (m_Impl && (m_Impl->LoadState == SceneLoadState::Queued || m_Impl->LoadState == SceneLoadState::Loading))
            m_Impl->LoadState = SceneLoadState::Cancelled;
    }

    class SceneSystem::Impl final
    {
      public:
        Impl(SceneSystemSpecification value, Ref<AssetSystem> assetSystem, Ref<EventBus> eventSystem)
            : Specification(std::move(value)), Assets(std::move(assetSystem)), Events(std::move(eventSystem)),
              OwnerThread(std::this_thread::get_id())
        {
            if (Specification.Mode != SceneMode::Enabled)
                throw std::invalid_argument("SceneSystem requires enabled mode.");
            if (!Assets || !Assets->IsOpen())
                throw std::invalid_argument("SceneSystem requires an open AssetSystem.");
            if (Specification.MaximumLoadedScenes == 0 || Specification.MaximumLoadedScenes > 1024)
                throw std::invalid_argument("Maximum loaded scenes must be in the range 1..1024.");
        }

        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != OwnerThread)
                throw std::logic_error(std::string("SceneSystem::") + operation + " must run on the owner thread.");
        }

        void Dispatch(const SceneLoadedEvent& event) const
        {
            if (Events && Events->IsOpen())
                (void)Events->Dispatch(event);
        }

        void Dispatch(const SceneUnloadedEvent& event) const
        {
            if (Events && Events->IsOpen())
                (void)Events->Dispatch(event);
        }

        void Dispatch(const SceneLoadFailedEvent& event) const
        {
            if (Events && Events->IsOpen())
                (void)Events->Dispatch(event);
        }

        void Dispatch(const ActiveSceneChangedEvent& event) const
        {
            if (Events && Events->IsOpen())
                (void)Events->Dispatch(event);
        }

        [[nodiscard]] auto FindLoaded(const AssetId id)
        {
            return std::ranges::find_if(Loaded, [id](const auto& scene) { return scene->Asset() == id; });
        }

        [[nodiscard]] auto FindLoaded(const AssetId id) const
        {
            return std::ranges::find_if(Loaded, [id](const auto& scene) { return scene->Asset() == id; });
        }

        void ChangeActive(const AssetId next)
        {
            if (next == ActiveId)
                return;
            const auto previous = ActiveId;
            ActiveId = next;
            Dispatch(ActiveSceneChangedEvent{previous, ActiveId});
        }

        SceneSystemSpecification Specification;
        Ref<AssetSystem> Assets;
        Ref<EventBus> Events;
        std::thread::id OwnerThread;
        std::vector<Ref<SceneLoadOperation>> PendingLoads;
        std::vector<AssetId> PendingUnloads;
        std::optional<AssetId> PendingActive;
        std::vector<Ref<Scene>> Loaded;
        AssetId ActiveId;
        bool Open = true;
    };

    SceneSystem::SceneSystem(SceneSystemSpecification specification, Ref<AssetSystem> assets, Ref<EventBus> events)
        : m_Impl(std::make_unique<Impl>(std::move(specification), std::move(assets), std::move(events)))
    {
    }

    SceneSystem::~SceneSystem() { Close(); }

    Ref<SceneLoadOperation> SceneSystem::Load(const AssetId scene, const SceneLoadMode mode,
                                              const AssetPriority priority)
    {
        m_Impl->RequireOwner("Load");
        if (!m_Impl->Open)
            throw std::logic_error("SceneSystem::Load cannot run after Close.");
        if (!scene)
            throw std::invalid_argument("Scene asset ID must not be empty.");
        auto operation = CreateRef<SceneLoadOperation>(
            std::make_unique<SceneLoadOperation::Impl>(scene, mode, m_Impl->Assets->Load<SceneAsset>(scene, priority)));
        m_Impl->PendingLoads.push_back(operation);
        return operation;
    }

    bool SceneSystem::Unload(const AssetId scene)
    {
        m_Impl->RequireOwner("Unload");
        if (!m_Impl->Open || m_Impl->FindLoaded(scene) == m_Impl->Loaded.end() ||
            std::ranges::find(m_Impl->PendingUnloads, scene) != m_Impl->PendingUnloads.end())
            return false;
        m_Impl->PendingUnloads.push_back(scene);
        return true;
    }

    bool SceneSystem::SetActive(const AssetId scene)
    {
        m_Impl->RequireOwner("SetActive");
        if (!m_Impl->Open || m_Impl->FindLoaded(scene) == m_Impl->Loaded.end())
            return false;
        m_Impl->PendingActive = scene;
        return true;
    }

    Ref<Scene> SceneSystem::Active() const noexcept
    {
        if (!m_Impl->Open)
            return {};
        const auto found = m_Impl->FindLoaded(m_Impl->ActiveId);
        return found == m_Impl->Loaded.end() ? Ref<Scene>{} : *found;
    }

    Ref<Scene> SceneSystem::Find(const AssetId scene) const noexcept
    {
        if (!m_Impl->Open)
            return {};
        const auto found = m_Impl->FindLoaded(scene);
        return found == m_Impl->Loaded.end() ? Ref<Scene>{} : *found;
    }

    std::vector<Ref<Scene>> SceneSystem::LoadedScenes() const
    {
        m_Impl->RequireOwner("LoadedScenes");
        return m_Impl->Loaded;
    }

    bool SceneSystem::IsOpen() const noexcept { return m_Impl->Open; }

    void SceneSystem::AdvanceFrame()
    {
        m_Impl->RequireOwner("AdvanceFrame");
        if (!m_Impl->Open)
            return;

        for (const auto id : std::exchange(m_Impl->PendingUnloads, {}))
        {
            const auto found = m_Impl->FindLoaded(id);
            if (found == m_Impl->Loaded.end())
                continue;
            const bool wasActive = id == m_Impl->ActiveId;
            (*found)->Close();
            m_Impl->Loaded.erase(found);
            m_Impl->Dispatch(SceneUnloadedEvent{id});
            if (wasActive)
                m_Impl->ChangeActive(m_Impl->Loaded.empty() ? AssetId{} : m_Impl->Loaded.front()->Asset());
        }

        for (const auto& operation : m_Impl->PendingLoads)
        {
            auto& state = *operation->m_Impl;
            if (state.LoadState == SceneLoadState::Cancelled)
                continue;
            if (state.LoadState == SceneLoadState::Queued)
                state.LoadState = SceneLoadState::Loading;
            const auto assetState = state.AssetHandleValue.State();
            if (assetState == AssetState::Failed || assetState == AssetState::Cancelled)
            {
                state.LoadState =
                    assetState == AssetState::Cancelled ? SceneLoadState::Cancelled : SceneLoadState::Failed;
                state.Failure = state.AssetHandleValue.Diagnostic();
                if (state.LoadState == SceneLoadState::Failed)
                    m_Impl->Dispatch(SceneLoadFailedEvent{state.AssetIdValue, state.Failure});
                continue;
            }
            const auto asset = state.AssetHandleValue.TryGetLoaded();
            if (!asset)
                continue;

            const auto existing = m_Impl->FindLoaded(state.AssetIdValue);
            if (state.LoadMode == SceneLoadMode::Additive && existing != m_Impl->Loaded.end())
            {
                state.LoadedScene = *existing;
                state.LoadState = SceneLoadState::Ready;
                continue;
            }
            if (state.LoadMode == SceneLoadMode::Additive &&
                m_Impl->Loaded.size() >= m_Impl->Specification.MaximumLoadedScenes)
            {
                state.LoadState = SceneLoadState::Failed;
                state.Failure = {"activate", "Maximum loaded scene capacity was exhausted."};
                m_Impl->Dispatch(SceneLoadFailedEvent{state.AssetIdValue, state.Failure});
                continue;
            }

            auto loadedScene = CreateRef<Scene>(state.AssetIdValue, asset->Definition());
            loadedScene->MarkSaved();
            if (state.LoadMode == SceneLoadMode::Single)
            {
                const auto previousActive = m_Impl->ActiveId;
                for (const auto& previous : m_Impl->Loaded)
                {
                    const auto previousId = previous->Asset();
                    previous->Close();
                    m_Impl->Dispatch(SceneUnloadedEvent{previousId});
                }
                m_Impl->Loaded.clear();
                m_Impl->ActiveId = state.AssetIdValue;
                if (previousActive != state.AssetIdValue)
                    m_Impl->Dispatch(ActiveSceneChangedEvent{previousActive, state.AssetIdValue});
            }
            m_Impl->Loaded.push_back(loadedScene);
            state.LoadedScene = loadedScene;
            state.LoadState = SceneLoadState::Ready;
            m_Impl->Dispatch(SceneLoadedEvent{state.AssetIdValue, state.LoadMode});
            if (!m_Impl->ActiveId)
                m_Impl->ChangeActive(state.AssetIdValue);
        }

        std::erase_if(
            m_Impl->PendingLoads, [](const auto& operation)
            { return operation->State() != SceneLoadState::Queued && operation->State() != SceneLoadState::Loading; });
        if (m_Impl->PendingActive)
        {
            if (m_Impl->FindLoaded(*m_Impl->PendingActive) != m_Impl->Loaded.end())
                m_Impl->ChangeActive(*m_Impl->PendingActive);
            m_Impl->PendingActive.reset();
        }
    }

    void SceneSystem::Close() noexcept
    {
        if (!m_Impl || !m_Impl->Open)
            return;
        m_Impl->Open = false;
        for (const auto& operation : m_Impl->PendingLoads)
            operation->Cancel();
        m_Impl->PendingLoads.clear();
        m_Impl->PendingUnloads.clear();
        for (const auto& scene : m_Impl->Loaded)
            scene->Close();
        m_Impl->Loaded.clear();
        m_Impl->ActiveId = {};
        m_Impl->Assets.Reset();
        m_Impl->Events.Reset();
    }
} // namespace Keire
