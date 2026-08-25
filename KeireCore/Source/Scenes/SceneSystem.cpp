#include "Keire/Scenes/SceneSystem.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <mutex>
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
            : AssetIdValue(value), LoadMode(mode), AssetHandleValue(std::move(asset))
        {
        }

        [[nodiscard]] SceneLoadState State() const
        {
            std::scoped_lock lock(Mutex);
            return LoadState;
        }

        [[nodiscard]] AssetDiagnostic Diagnostic() const
        {
            std::scoped_lock lock(Mutex);
            return Failure;
        }

        [[nodiscard]] Ref<Scene> Result() const
        {
            std::scoped_lock lock(Mutex);
            return LoadedScene;
        }

        [[nodiscard]] bool BeginAdvance()
        {
            std::scoped_lock lock(Mutex);
            if (LoadState == SceneLoadState::Cancelled || LoadState == SceneLoadState::Failed ||
                LoadState == SceneLoadState::Ready || CommitStarted)
                return false;
            if (LoadState == SceneLoadState::Queued)
                LoadState = SceneLoadState::Loading;
            return true;
        }

        [[nodiscard]] bool BeginCommit()
        {
            std::scoped_lock lock(Mutex);
            if (LoadState != SceneLoadState::Loading)
                return false;
            CommitStarted = true;
            return true;
        }

        [[nodiscard]] bool Fail(AssetDiagnostic diagnostic)
        {
            std::scoped_lock lock(Mutex);
            if (LoadState == SceneLoadState::Cancelled)
                return false;
            Failure = std::move(diagnostic);
            LoadState = SceneLoadState::Failed;
            CommitStarted = false;
            return true;
        }

        void MarkAssetCancelled(AssetDiagnostic diagnostic)
        {
            std::scoped_lock lock(Mutex);
            if (LoadState == SceneLoadState::Cancelled)
                return;
            Failure = std::move(diagnostic);
            LoadState = SceneLoadState::Cancelled;
            CommitStarted = false;
        }

        [[nodiscard]] bool Complete(Ref<Scene> scene)
        {
            std::scoped_lock lock(Mutex);
            if (LoadState != SceneLoadState::Loading)
                return false;
            LoadedScene = std::move(scene);
            LoadState = SceneLoadState::Ready;
            CommitStarted = false;
            return true;
        }

        void Cancel()
        {
            std::scoped_lock lock(Mutex);
            if (!CommitStarted && (LoadState == SceneLoadState::Queued || LoadState == SceneLoadState::Loading))
                LoadState = SceneLoadState::Cancelled;
        }

        mutable std::mutex Mutex;
        AssetId AssetIdValue;
        SceneLoadMode LoadMode = SceneLoadMode::Single;
        SceneLoadState LoadState = SceneLoadState::Queued;
        AssetHandle<SceneAsset> AssetHandleValue;
        AssetDiagnostic Failure;
        Ref<Scene> LoadedScene;
        bool CommitStarted = false;
    };

    SceneLoadOperation::SceneLoadOperation(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}

    SceneLoadOperation::~SceneLoadOperation()
    {
        try
        {
            Cancel();
        }
        catch (...)
        {
            // A load-operation destructor cannot report a synchronization failure.
        }
    }

    AssetId SceneLoadOperation::Asset() const noexcept { return m_Impl->AssetIdValue; }

    SceneLoadMode SceneLoadOperation::Mode() const noexcept { return m_Impl->LoadMode; }

    SceneLoadState SceneLoadOperation::State() const { return m_Impl->State(); }

    AssetDiagnostic SceneLoadOperation::Diagnostic() const { return m_Impl->Diagnostic(); }

    Ref<Scene> SceneLoadOperation::Result() const { return m_Impl->Result(); }

    void SceneLoadOperation::Cancel() { m_Impl->Cancel(); }

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
            if (!Specification.Components)
                Specification.Components = ComponentRegistry::CreateDefault();
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
        std::atomic_bool Open = true;
    };

    SceneSystem::SceneSystem(SceneSystemSpecification specification, Ref<AssetSystem> assets, Ref<EventBus> events)
        : m_Impl(std::make_unique<Impl>(std::move(specification), std::move(assets), std::move(events)))
    {
    }

    SceneSystem::~SceneSystem() { CloseInternal(); }

    Ref<SceneLoadOperation> SceneSystem::Load(const AssetId scene, const SceneLoadMode mode,
                                              const AssetPriority priority)
    {
        m_Impl->RequireOwner("Load");
        if (!m_Impl->Open.load(std::memory_order_acquire))
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
        if (!m_Impl->Open.load(std::memory_order_acquire) || m_Impl->FindLoaded(scene) == m_Impl->Loaded.end() ||
            std::ranges::find(m_Impl->PendingUnloads, scene) != m_Impl->PendingUnloads.end())
            return false;
        m_Impl->PendingUnloads.push_back(scene);
        return true;
    }

    bool SceneSystem::SetActive(const AssetId scene)
    {
        m_Impl->RequireOwner("SetActive");
        if (!m_Impl->Open.load(std::memory_order_acquire) || m_Impl->FindLoaded(scene) == m_Impl->Loaded.end())
            return false;
        m_Impl->PendingActive = scene;
        return true;
    }

    Ref<Scene> SceneSystem::Active() const
    {
        m_Impl->RequireOwner("Active");
        if (!m_Impl->Open.load(std::memory_order_acquire))
            return {};
        const auto found = m_Impl->FindLoaded(m_Impl->ActiveId);
        return found == m_Impl->Loaded.end() ? Ref<Scene>{} : *found;
    }

    Ref<Scene> SceneSystem::Find(const AssetId scene) const
    {
        m_Impl->RequireOwner("Find");
        if (!m_Impl->Open.load(std::memory_order_acquire))
            return {};
        const auto found = m_Impl->FindLoaded(scene);
        return found == m_Impl->Loaded.end() ? Ref<Scene>{} : *found;
    }

    std::vector<Ref<Scene>> SceneSystem::LoadedScenes() const
    {
        m_Impl->RequireOwner("LoadedScenes");
        return m_Impl->Loaded;
    }

    Ref<ComponentRegistry> SceneSystem::Components() const
    {
        m_Impl->RequireOwner("Components");
        return m_Impl->Specification.Components;
    }

    bool SceneSystem::IsOpen() const noexcept { return m_Impl->Open.load(std::memory_order_acquire); }

    void SceneSystem::AdvanceFrame()
    {
        m_Impl->RequireOwner("AdvanceFrame");
        if (!m_Impl->Open.load(std::memory_order_acquire))
            return;

        const auto pendingUnloadCount = m_Impl->PendingUnloads.size();
        for (std::size_t index = 0; index < pendingUnloadCount; ++index)
        {
            if (!m_Impl->Open.load(std::memory_order_acquire) || m_Impl->PendingUnloads.empty())
                break;
            const auto id = m_Impl->PendingUnloads.front();
            m_Impl->PendingUnloads.erase(m_Impl->PendingUnloads.begin());
            const auto found = m_Impl->FindLoaded(id);
            if (found == m_Impl->Loaded.end())
                continue;
            const bool wasActive = id == m_Impl->ActiveId;
            const auto previousActive = m_Impl->ActiveId;
            const auto unloadedScene = *found;
            unloadedScene->Close();
            m_Impl->Loaded.erase(found);
            const auto nextActive = wasActive && !m_Impl->Loaded.empty() ? m_Impl->Loaded.front()->Asset() : AssetId{};
            if (wasActive)
                m_Impl->ActiveId = nextActive;
            m_Impl->Dispatch(SceneUnloadedEvent{id});
            if (wasActive)
                m_Impl->Dispatch(ActiveSceneChangedEvent{previousActive, nextActive});
        }

        const auto pendingLoadCount = m_Impl->PendingLoads.size();
        for (std::size_t index = 0; index < pendingLoadCount; ++index)
        {
            if (!m_Impl->Open.load(std::memory_order_acquire) || index >= m_Impl->PendingLoads.size())
                break;
            const auto operation = m_Impl->PendingLoads[index];
            auto& state = *operation->m_Impl;
            if (!state.BeginAdvance())
                continue;
            const auto assetState = state.AssetHandleValue.State();
            if (assetState == AssetState::Failed || assetState == AssetState::Cancelled)
            {
                auto diagnostic = state.AssetHandleValue.Diagnostic();
                if (assetState == AssetState::Cancelled)
                    state.MarkAssetCancelled(std::move(diagnostic));
                else if (state.Fail(diagnostic))
                    m_Impl->Dispatch(SceneLoadFailedEvent{state.AssetIdValue, std::move(diagnostic)});
                continue;
            }
            const auto asset = state.AssetHandleValue.TryGetLoaded();
            if (!asset)
                continue;

            const auto existing = m_Impl->FindLoaded(state.AssetIdValue);
            if (state.LoadMode == SceneLoadMode::Additive && existing != m_Impl->Loaded.end())
            {
                (void)state.Complete(*existing);
                continue;
            }
            if (state.LoadMode == SceneLoadMode::Additive &&
                m_Impl->Loaded.size() >= m_Impl->Specification.MaximumLoadedScenes)
            {
                AssetDiagnostic diagnostic{"activate", "Maximum loaded scene capacity was exhausted."};
                if (state.Fail(diagnostic))
                    m_Impl->Dispatch(SceneLoadFailedEvent{state.AssetIdValue, std::move(diagnostic)});
                continue;
            }

            auto loadedScene =
                CreateRef<Scene>(state.AssetIdValue, asset->Definition(), m_Impl->Specification.Components);
            loadedScene->MarkSaved();
            auto committedScenes = state.LoadMode == SceneLoadMode::Single ? std::vector<Ref<Scene>>{} : m_Impl->Loaded;
            committedScenes.push_back(loadedScene);
            std::vector<AssetId> unloadedScenes;
            if (state.LoadMode == SceneLoadMode::Single)
            {
                unloadedScenes.reserve(m_Impl->Loaded.size());
                for (const auto& previous : m_Impl->Loaded)
                    unloadedScenes.push_back(previous->Asset());
            }
            if (!state.BeginCommit())
                continue;

            const auto previousActive = m_Impl->ActiveId;
            if (state.LoadMode == SceneLoadMode::Single)
            {
                for (const auto& previous : m_Impl->Loaded)
                    previous->Close();
                m_Impl->ActiveId = state.AssetIdValue;
            }
            else if (!m_Impl->ActiveId)
                m_Impl->ActiveId = state.AssetIdValue;
            m_Impl->Loaded = std::move(committedScenes);
            if (!state.Complete(loadedScene))
                throw std::logic_error("Scene load commit lost its claimed operation state.");

            for (const auto unloaded : unloadedScenes)
                m_Impl->Dispatch(SceneUnloadedEvent{unloaded});
            if (state.LoadMode == SceneLoadMode::Single && previousActive != state.AssetIdValue)
                m_Impl->Dispatch(ActiveSceneChangedEvent{previousActive, state.AssetIdValue});
            m_Impl->Dispatch(SceneLoadedEvent{state.AssetIdValue, state.LoadMode});
            if (state.LoadMode == SceneLoadMode::Additive && !previousActive)
                m_Impl->Dispatch(ActiveSceneChangedEvent{previousActive, state.AssetIdValue});
        }

        std::erase_if(m_Impl->PendingLoads,
                      [](const auto& operation)
                      {
                          const auto state = operation->State();
                          return state != SceneLoadState::Queued && state != SceneLoadState::Loading;
                      });
        if (m_Impl->PendingActive)
        {
            const auto requested = std::exchange(m_Impl->PendingActive, std::nullopt);
            if (m_Impl->FindLoaded(*requested) != m_Impl->Loaded.end())
                m_Impl->ChangeActive(*requested);
        }
    }

    void SceneSystem::Close()
    {
        if (!m_Impl)
            return;
        m_Impl->RequireOwner("Close");
        if (!m_Impl->Open.load(std::memory_order_acquire))
            return;
        CloseInternal();
    }

    void SceneSystem::CloseInternal() noexcept
    {
        if (!m_Impl || !m_Impl->Open.exchange(false, std::memory_order_acq_rel))
            return;
        for (const auto& operation : m_Impl->PendingLoads)
        {
            try
            {
                operation->Cancel();
            }
            catch (...)
            {
                // Service destruction is noexcept; each remaining operation still gets a cancellation attempt.
            }
        }
        m_Impl->PendingLoads.clear();
        m_Impl->PendingUnloads.clear();
        m_Impl->PendingActive.reset();
        for (const auto& scene : m_Impl->Loaded)
            scene->Close();
        m_Impl->Loaded.clear();
        m_Impl->ActiveId = {};
        m_Impl->Assets.Reset();
        m_Impl->Events.Reset();
    }
} // namespace Keire
