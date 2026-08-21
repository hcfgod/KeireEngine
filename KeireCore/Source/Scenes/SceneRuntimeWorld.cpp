#include "Keire/Scenes/SceneRuntimeWorld.h"

#include <algorithm>
#include <limits>
#include <ranges>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

namespace Keire
{
    class SceneRuntimeLoadOperation::Impl final
    {
      public:
        explicit Impl(Ref<SceneLoadOperation> operation) : Load(std::move(operation)) {}

        Ref<SceneLoadOperation> Load;
        SceneLoadState ActivationState = SceneLoadState::Queued;
        AssetDiagnostic Failure;
        SceneHandle Handle;
    };

    SceneRuntimeLoadOperation::SceneRuntimeLoadOperation(std::unique_ptr<Impl> implementation)
        : m_Impl(std::move(implementation))
    {
    }

    SceneRuntimeLoadOperation::~SceneRuntimeLoadOperation() { Cancel(); }

    AssetId SceneRuntimeLoadOperation::Asset() const noexcept { return m_Impl->Load->Asset(); }

    SceneLoadMode SceneRuntimeLoadOperation::Mode() const noexcept { return m_Impl->Load->Mode(); }

    SceneLoadState SceneRuntimeLoadOperation::State() const noexcept
    {
        const auto loadState = m_Impl->Load->State();
        if (loadState == SceneLoadState::Failed || loadState == SceneLoadState::Cancelled)
            return loadState;
        return m_Impl->ActivationState;
    }

    float SceneRuntimeLoadOperation::Progress() const noexcept
    {
        switch (State())
        {
        case SceneLoadState::Queued:
            return 0.0F;
        case SceneLoadState::Loading:
            return m_Impl->Load->State() == SceneLoadState::Ready ? 0.9F : 0.5F;
        case SceneLoadState::Ready:
        case SceneLoadState::Failed:
        case SceneLoadState::Cancelled:
            return 1.0F;
        }
        return 0.0F;
    }

    AssetDiagnostic SceneRuntimeLoadOperation::Diagnostic() const
    {
        return m_Impl->Failure.Message.empty() ? m_Impl->Load->Diagnostic() : m_Impl->Failure;
    }

    SceneHandle SceneRuntimeLoadOperation::Result() const noexcept { return m_Impl->Handle; }

    void SceneRuntimeLoadOperation::Cancel() noexcept
    {
        if (!m_Impl)
            return;
        if (m_Impl->ActivationState == SceneLoadState::Queued || m_Impl->ActivationState == SceneLoadState::Loading)
        {
            m_Impl->Load->Cancel();
            m_Impl->ActivationState = SceneLoadState::Cancelled;
        }
    }

    class SceneRuntimeWorld::Impl final
    {
      public:
        struct Entry final
        {
            SceneHandle Handle;
            AssetId Asset;
            Ref<SceneRuntimeSession> Runtime;
            std::set<EntityId> PersistentRoots;
            bool Loaded = true;
            bool Persistent = false;
        };

        explicit Impl(SceneRuntimeWorldSpecification value)
            : Specification(std::move(value)), OwnerThread(std::this_thread::get_id())
        {
            if (!Specification.Scenes || !Specification.Scenes->IsOpen())
                throw std::invalid_argument("SceneRuntimeWorld requires an open SceneSystem.");
            if (!Specification.Assets || !Specification.Assets->IsOpen())
                throw std::invalid_argument("SceneRuntimeWorld requires an open AssetSystem.");
        }

        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != OwnerThread)
                throw std::logic_error(std::string("SceneRuntimeWorld::") + operation +
                                       " must run on the owner thread.");
            if (!Open)
                throw std::logic_error(std::string("SceneRuntimeWorld::") + operation + " cannot run after Close.");
        }

        [[nodiscard]] auto Find(const SceneHandle handle) { return std::ranges::find(Entries, handle, &Entry::Handle); }

        [[nodiscard]] auto Find(const SceneHandle handle) const
        {
            return std::ranges::find(Entries, handle, &Entry::Handle);
        }

        [[nodiscard]] SceneHandle AllocateHandle()
        {
            if (NextHandle == std::numeric_limits<std::uint32_t>::max())
                throw std::length_error("Scene runtime handle capacity was exhausted.");
            return SceneHandle::FromParts(NextHandle++, 1);
        }

        [[nodiscard]] SceneHandle Add(Ref<SceneRuntimeSession> session)
        {
            if (!session || !session->RuntimeScene())
                throw std::invalid_argument("SceneRuntimeWorld requires an active runtime scene.");
            const auto handle = AllocateHandle();
            const auto asset = session->RuntimeScene()->Asset();
            Entries.push_back({handle, asset, std::move(session)});
            if (!Active)
                Active = handle;
            return handle;
        }

        void StopAndErase(const std::size_t index) noexcept
        {
            if (Entries[index].Runtime)
                Entries[index].Runtime->Stop();
            Entries.erase(Entries.begin() + static_cast<std::ptrdiff_t>(index));
        }

        void Retire(const std::size_t index)
        {
            auto& entry = Entries[index];
            const auto scene = entry.Runtime ? entry.Runtime->RuntimeScene() : Ref<Scene>{};
            std::set<EntityId> liveRoots;
            if (scene)
            {
                for (const auto persistent : entry.PersistentRoots)
                {
                    auto root = scene->FindEntity(persistent);
                    if (!root)
                        continue;
                    while (root.Parent())
                        root = root.Parent();
                    liveRoots.insert(root.Id());
                }
            }
            entry.PersistentRoots = std::move(liveRoots);
            if (!scene || entry.PersistentRoots.empty())
            {
                StopAndErase(index);
                return;
            }
            for (const auto& entity : scene->Entities())
            {
                if (!entity.Parent() && !entry.PersistentRoots.contains(entity.Id()))
                    (void)scene->DestroyEntity(entity.Id());
            }
            entry.Loaded = false;
            entry.Persistent = true;
        }

        void RetireLoadedExcept(const SceneHandle replacement)
        {
            for (std::size_t index = Entries.size(); index > 0; --index)
            {
                const auto current = index - 1;
                if (Entries[current].Handle != replacement && Entries[current].Loaded)
                    Retire(current);
            }
        }

        [[nodiscard]] std::vector<Ref<Scene>> SelectScenes(const SceneQueryScope scope,
                                                           const SceneHandle specific) const
        {
            std::vector<Ref<Scene>> result;
            for (const auto& entry : Entries)
            {
                const bool selected = scope == SceneQueryScope::Active       ? entry.Handle == Active
                                      : scope == SceneQueryScope::Loaded     ? entry.Loaded
                                      : scope == SceneQueryScope::Persistent ? entry.Persistent
                                                                             : entry.Handle == specific;
                if (selected && entry.Runtime && entry.Runtime->RuntimeScene())
                    result.push_back(entry.Runtime->RuntimeScene());
            }
            return result;
        }

        template <typename Query>
        [[nodiscard]] std::vector<Entity> QueryEntities(const SceneQueryScope scope, const SceneHandle specific,
                                                        const std::size_t maximum, Query&& query) const
        {
            std::vector<Entity> result;
            result.reserve(std::min<std::size_t>(maximum, 256));
            for (const auto& scene : SelectScenes(scope, specific))
            {
                for (const auto& entity : std::forward<Query>(query)(*scene))
                {
                    if (result.size() == maximum)
                        return result;
                    result.push_back(entity);
                }
            }
            return result;
        }

        SceneRuntimeWorldSpecification Specification;
        std::thread::id OwnerThread;
        std::vector<Entry> Entries;
        std::vector<Ref<SceneRuntimeLoadOperation>> Loads;
        std::vector<SceneHandle> PendingUnloads;
        std::optional<SceneHandle> PendingActive;
        SceneHandle Active;
        std::uint32_t NextHandle = 0;
        bool Open = true;
    };

    SceneRuntimeWorld::SceneRuntimeWorld(SceneRuntimeWorldSpecification specification)
        : m_Impl(std::make_unique<Impl>(std::move(specification)))
    {
    }

    SceneRuntimeWorld::~SceneRuntimeWorld() { Close(); }

    SceneHandle SceneRuntimeWorld::Adopt(Ref<SceneRuntimeSession> session)
    {
        m_Impl->RequireOwner("Adopt");
        return m_Impl->Add(std::move(session));
    }

    Ref<SceneRuntimeLoadOperation> SceneRuntimeWorld::Load(const AssetId scene, const SceneLoadMode mode,
                                                           const AssetPriority priority)
    {
        m_Impl->RequireOwner("Load");
        auto result = CreateRef<SceneRuntimeLoadOperation>(std::make_unique<SceneRuntimeLoadOperation::Impl>(
            m_Impl->Specification.Scenes->Load(scene, mode, priority)));
        m_Impl->Loads.push_back(result);
        return result;
    }

    bool SceneRuntimeWorld::Unload(const SceneHandle scene)
    {
        m_Impl->RequireOwner("Unload");
        const auto found = m_Impl->Find(scene);
        if (found == m_Impl->Entries.end() || !found->Loaded ||
            std::ranges::find(m_Impl->PendingUnloads, scene) != m_Impl->PendingUnloads.end())
            return false;
        if (scene == m_Impl->Active &&
            std::ranges::count_if(m_Impl->Entries, [](const Impl::Entry& entry) { return entry.Loaded; }) == 1)
            return false;
        m_Impl->PendingUnloads.push_back(scene);
        (void)m_Impl->Specification.Scenes->Unload(found->Asset);
        return true;
    }

    bool SceneRuntimeWorld::SetActive(const SceneHandle scene)
    {
        m_Impl->RequireOwner("SetActive");
        const auto found = m_Impl->Find(scene);
        if (found == m_Impl->Entries.end() || !found->Loaded)
            return false;
        m_Impl->PendingActive = scene;
        (void)m_Impl->Specification.Scenes->SetActive(found->Asset);
        return true;
    }

    bool SceneRuntimeWorld::MakePersistent(const Entity& entity)
    {
        m_Impl->RequireOwner("MakePersistent");
        if (!entity)
            return false;
        auto found = std::ranges::find_if(
            m_Impl->Entries,
            [world = entity.World()](const Impl::Entry& entry)
            {
                const auto scene = entry.Runtime ? entry.Runtime->RuntimeScene() : Ref<Scene>{};
                const auto entities = scene ? scene->Entities() : std::vector<Entity>{};
                return std::ranges::any_of(entities, [world](const Entity& value) { return value.World() == world; });
            });
        if (found == m_Impl->Entries.end())
            return false;
        auto root = entity;
        while (root.Parent())
            root = root.Parent();
        found->PersistentRoots.insert(root.Id());
        return true;
    }

    void SceneRuntimeWorld::Process(const SceneValidator validator)
    {
        m_Impl->RequireOwner("Process");
        for (const auto handle : std::exchange(m_Impl->PendingUnloads, {}))
        {
            const auto found = m_Impl->Find(handle);
            if (found == m_Impl->Entries.end() || !found->Loaded)
                continue;
            const bool active = handle == m_Impl->Active;
            const auto index = static_cast<std::size_t>(std::distance(m_Impl->Entries.begin(), found));
            m_Impl->Retire(index);
            if (active)
            {
                const auto replacement =
                    std::ranges::find_if(m_Impl->Entries, [](const Impl::Entry& entry) { return entry.Loaded; });
                m_Impl->Active = replacement == m_Impl->Entries.end() ? SceneHandle{} : replacement->Handle;
            }
        }

        for (const auto& operation : m_Impl->Loads)
        {
            auto& state = *operation->m_Impl;
            if (operation->State() == SceneLoadState::Cancelled || operation->State() == SceneLoadState::Failed ||
                operation->State() == SceneLoadState::Ready)
                continue;
            state.ActivationState = SceneLoadState::Loading;
            if (state.Load->State() != SceneLoadState::Ready)
                continue;
            if (state.Load->Mode() == SceneLoadMode::Additive)
            {
                const auto existing =
                    std::ranges::find_if(m_Impl->Entries, [asset = state.Load->Asset()](const Impl::Entry& entry)
                                         { return entry.Loaded && entry.Asset == asset; });
                if (existing != m_Impl->Entries.end())
                {
                    state.Handle = existing->Handle;
                    state.ActivationState = SceneLoadState::Ready;
                    continue;
                }
            }
            try
            {
                auto session =
                    CreateRef<SceneRuntimeSession>(state.Load->Result(), m_Impl->Specification.Assets,
                                                   m_Impl->Specification.Audio, m_Impl->Specification.Physics);
                if (const auto presentation = session->Presentation())
                    presentation->SetDefaultMixer(m_Impl->Specification.DefaultMixer);
                session->SetDeterministicSimulation(m_Impl->Specification.DeterministicSimulation);
                session->Play();
                if (session->State() == ScenePlayState::Faulted)
                    throw std::runtime_error("Scene Play failed: " + session->Diagnostic().Message);
                if (state.Load->Mode() == SceneLoadMode::Single && validator && !validator(session->RuntimeScene()))
                    throw std::runtime_error("The loaded scene failed runtime validation.");
                state.Handle = m_Impl->Add(std::move(session));
                if (state.Load->Mode() == SceneLoadMode::Single)
                {
                    m_Impl->RetireLoadedExcept(state.Handle);
                    m_Impl->Active = state.Handle;
                }
                state.ActivationState = SceneLoadState::Ready;
            }
            catch (const std::exception& error)
            {
                state.Failure = {"activate", error.what()};
                state.ActivationState = SceneLoadState::Failed;
                (void)m_Impl->Specification.Scenes->Unload(state.Load->Asset());
            }
            catch (...)
            {
                state.Failure = {"activate", "The loaded scene failed during runtime activation."};
                state.ActivationState = SceneLoadState::Failed;
                (void)m_Impl->Specification.Scenes->Unload(state.Load->Asset());
            }
        }

        if (m_Impl->PendingActive)
        {
            const auto found = m_Impl->Find(*m_Impl->PendingActive);
            if (found != m_Impl->Entries.end() && found->Loaded)
                m_Impl->Active = found->Handle;
            m_Impl->PendingActive.reset();
        }

        std::erase_if(m_Impl->Loads,
                      [](const Ref<SceneRuntimeLoadOperation>& operation)
                      {
                          const auto state = operation->State();
                          return state == SceneLoadState::Ready || state == SceneLoadState::Failed ||
                                 state == SceneLoadState::Cancelled;
                      });
    }

    void SceneRuntimeWorld::FixedUpdate(const float deltaSeconds)
    {
        m_Impl->RequireOwner("FixedUpdate");
        for (const auto& entry : m_Impl->Entries)
            if (entry.Runtime)
                entry.Runtime->FixedUpdate(deltaSeconds);
    }

    void SceneRuntimeWorld::Update(const float deltaSeconds, const float interpolationAlpha)
    {
        m_Impl->RequireOwner("Update");
        for (const auto& entry : m_Impl->Entries)
            if (entry.Runtime)
                entry.Runtime->Update(deltaSeconds, interpolationAlpha);
    }

    void SceneRuntimeWorld::SetDeterministicSimulation(const bool enabled)
    {
        m_Impl->RequireOwner("SetDeterministicSimulation");
        m_Impl->Specification.DeterministicSimulation = enabled;
        for (const auto& entry : m_Impl->Entries)
            if (entry.Runtime)
                entry.Runtime->SetDeterministicSimulation(enabled);
    }

    void SceneRuntimeWorld::SetPresentationViewport(const float width, const float height,
                                                    const RuntimeUiInsets safeArea)
    {
        m_Impl->RequireOwner("SetPresentationViewport");
        for (const auto& entry : m_Impl->Entries)
            if (entry.Runtime)
                entry.Runtime->SetPresentationViewport(width, height, safeArea);
    }

    SceneHandle SceneRuntimeWorld::Active() const noexcept { return m_Impl->Open ? m_Impl->Active : SceneHandle{}; }

    bool SceneRuntimeWorld::IsLoaded(const SceneHandle scene) const noexcept
    {
        const auto found = m_Impl->Find(scene);
        return m_Impl->Open && found != m_Impl->Entries.end() && found->Loaded;
    }

    bool SceneRuntimeWorld::IsPersistent(const Entity& entity) const noexcept
    {
        if (!m_Impl->Open || !entity)
            return false;
        return std::ranges::any_of(m_Impl->Entries,
                                   [entity](const Impl::Entry& entry)
                                   {
                                       if (!entry.Runtime || entry.PersistentRoots.empty())
                                           return false;
                                       const auto scene = entry.Runtime->RuntimeScene();
                                       auto current = scene ? scene->FindEntity(entity.Id()) : Entity{};
                                       if (!current || current.World() != entity.World())
                                           return false;
                                       while (current.Parent())
                                           current = current.Parent();
                                       return entry.PersistentRoots.contains(current.Id());
                                   });
    }

    AssetId SceneRuntimeWorld::Asset(const SceneHandle scene) const noexcept
    {
        const auto found = m_Impl->Find(scene);
        return m_Impl->Open && found != m_Impl->Entries.end() ? found->Asset : AssetId{};
    }

    Ref<Scene> SceneRuntimeWorld::Find(const SceneHandle scene) const noexcept
    {
        const auto found = m_Impl->Find(scene);
        return m_Impl->Open && found != m_Impl->Entries.end() && found->Runtime ? found->Runtime->RuntimeScene()
                                                                                : Ref<Scene>{};
    }

    Ref<Scene> SceneRuntimeWorld::FindWorld(const std::uint64_t world) const noexcept
    {
        if (!m_Impl->Open || world == 0)
            return {};
        for (const auto& entry : m_Impl->Entries)
        {
            const auto scene = entry.Runtime ? entry.Runtime->RuntimeScene() : Ref<Scene>{};
            const auto entities = scene ? scene->Entities() : std::vector<Entity>{};
            if (std::ranges::any_of(entities, [world](const Entity& entity) { return entity.World() == world; }))
                return scene;
        }
        return {};
    }

    Ref<SceneRuntimeSession> SceneRuntimeWorld::Session(const SceneHandle scene) const noexcept
    {
        const auto found = m_Impl->Find(scene);
        return m_Impl->Open && found != m_Impl->Entries.end() ? found->Runtime : Ref<SceneRuntimeSession>{};
    }

    Ref<SceneRuntimeSession> SceneRuntimeWorld::SessionForWorld(const std::uint64_t world) const noexcept
    {
        if (!m_Impl->Open || world == 0)
            return {};
        for (const auto& entry : m_Impl->Entries)
        {
            const auto scene = entry.Runtime ? entry.Runtime->RuntimeScene() : Ref<Scene>{};
            const auto entities = scene ? scene->Entities() : std::vector<Entity>{};
            if (std::ranges::any_of(entities, [world](const Entity& entity) { return entity.World() == world; }))
                return entry.Runtime;
        }
        return {};
    }

    Ref<SceneRuntimeSession> SceneRuntimeWorld::SessionForEntity(const EntityId entity) const noexcept
    {
        if (!m_Impl->Open || !entity)
            return {};
        for (const auto& entry : m_Impl->Entries)
            if (entry.Runtime && entry.Runtime->RuntimeScene() && entry.Runtime->RuntimeScene()->FindEntity(entity))
                return entry.Runtime;
        return {};
    }

    std::vector<SceneHandle> SceneRuntimeWorld::LoadedScenes() const
    {
        m_Impl->RequireOwner("LoadedScenes");
        std::vector<SceneHandle> result;
        for (const auto& entry : m_Impl->Entries)
            if (entry.Loaded)
                result.push_back(entry.Handle);
        return result;
    }

    std::vector<Ref<SceneRuntimeSession>> SceneRuntimeWorld::Sessions(const bool includePersistent) const
    {
        m_Impl->RequireOwner("Sessions");
        std::vector<Ref<SceneRuntimeSession>> result;
        for (const auto& entry : m_Impl->Entries)
            if (entry.Loaded || (includePersistent && entry.Persistent))
                result.push_back(entry.Runtime);
        return result;
    }

    std::vector<Ref<Scene>> SceneRuntimeWorld::QueryScenes(const SceneQueryScope scope,
                                                           const SceneHandle specific) const
    {
        m_Impl->RequireOwner("QueryScenes");
        if (scope == SceneQueryScope::Specific && !specific)
            return {};
        return m_Impl->SelectScenes(scope, specific);
    }

    std::vector<Entity> SceneRuntimeWorld::QueryName(const std::string_view name, const SceneQueryScope scope,
                                                     const SceneHandle specific, const std::size_t maximum) const
    {
        m_Impl->RequireOwner("QueryName");
        return m_Impl->QueryEntities(scope, specific, maximum,
                                     [name](const Scene& scene) { return scene.QueryName(name); });
    }

    std::vector<Entity> SceneRuntimeWorld::QueryTag(const std::string_view tag, const SceneQueryScope scope,
                                                    const SceneHandle specific, const std::size_t maximum) const
    {
        m_Impl->RequireOwner("QueryTag");
        return m_Impl->QueryEntities(scope, specific, maximum,
                                     [tag](const Scene& scene) { return scene.QueryTag(tag); });
    }

    std::vector<Entity> SceneRuntimeWorld::Query(const ComponentTypeId component, const SceneQueryScope scope,
                                                 const SceneHandle specific, const std::size_t maximum) const
    {
        m_Impl->RequireOwner("Query");
        return m_Impl->QueryEntities(scope, specific, maximum,
                                     [component](const Scene& scene) { return scene.Query(component); });
    }

    bool SceneRuntimeWorld::IsOpen() const noexcept { return m_Impl && m_Impl->Open; }

    void SceneRuntimeWorld::Close() noexcept
    {
        if (!m_Impl || !m_Impl->Open)
            return;
        m_Impl->Open = false;
        for (const auto& load : m_Impl->Loads)
            load->Cancel();
        m_Impl->Loads.clear();
        for (auto entry = m_Impl->Entries.rbegin(); entry != m_Impl->Entries.rend(); ++entry)
            if (entry->Runtime)
                entry->Runtime->Stop();
        m_Impl->Entries.clear();
        m_Impl->PendingUnloads.clear();
        m_Impl->PendingActive.reset();
        m_Impl->Active = {};
        m_Impl->Specification.Scenes.Reset();
        m_Impl->Specification.Assets.Reset();
        m_Impl->Specification.Audio.Reset();
        m_Impl->Specification.Physics.Reset();
    }
} // namespace Keire
