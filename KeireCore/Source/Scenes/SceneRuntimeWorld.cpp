#include "Keire/Scenes/SceneRuntimeWorld.h"

#include "KeireInternal/Environment/EnvironmentLightingEvaluation.h"
#include "KeireInternal/Environment/SceneRuntimeEnvironmentAccess.h"
#include "KeireInternal/Physics/GravityField.h"
#include "KeireInternal/Scenes/SceneRuntimeIdentityAccess.h"
#include "KeireInternal/Spatial/FrameRegistry.h"
#include "KeireInternal/Spatial/PlanetEnvironmentSelection.h"
#include "KeireInternal/Spatial/PlanetFrameCache.h"
#include "KeireInternal/Spatial/SceneRuntimePlanetAccess.h"
#include "KeireInternal/Spatial/SceneRuntimeSpatialAccess.h"

#include <algorithm>
#include <cmath>
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
            AssetId Instance;
            Ref<SceneRuntimeSession> Runtime;
            std::set<EntityId> PersistentRoots;
            Detail::FrameAttachment SpatialAttachment;
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
            if (!session)
                throw std::invalid_argument("SceneRuntimeWorld requires a runtime session.");
            if (std::ranges::any_of(Entries, [&](const Entry& entry) { return entry.Runtime == session; }))
            {
                throw std::invalid_argument("A runtime session already belongs to this world.");
            }
            const auto runtimeScene = session->RuntimeScene();
            const auto editScene = session->EditScene();
            if (!runtimeScene && !editScene)
                throw std::invalid_argument("SceneRuntimeWorld requires a runtime session with a source scene.");
            const auto handle = AllocateHandle();
            const auto asset = runtimeScene ? runtimeScene->Asset() : editScene->Asset();
            Entry entry;
            entry.Handle = handle;
            entry.Asset = asset;
            entry.Instance = AssetId::Generate();
            entry.Runtime = std::move(session);
            entry.SpatialAttachment = Spatial->Attach(Spatial->Find(Detail::SceneRuntimeSpatialAccess::RootFrame));
            Entries.push_back(std::move(entry));
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

        [[nodiscard]] SceneHandle Activate(Ref<SceneRuntimeSession> session, const SceneValidator validator)
        {
            const auto previousActive = Active;
            const auto handle = Add(session);
            try
            {
                // Publish ownership before Play invokes callbacks on the newly cloned scene.
                session->Play();
                ApplyConfiguredGravity(session);
                if (Paused)
                {
                    session->Pause();
                }
                if (session->State() == ScenePlayState::Faulted)
                {
                    throw std::runtime_error("Scene Play failed: " + session->Diagnostic().Message);
                }
                if (validator && !validator(session->RuntimeScene()))
                {
                    throw std::runtime_error("The loaded scene failed runtime validation.");
                }
            }
            catch (...)
            {
                const auto found = Find(handle);
                if (found != Entries.end())
                {
                    StopAndErase(static_cast<std::size_t>(std::distance(Entries.begin(), found)));
                }
                Active = previousActive;
                throw;
            }
            return handle;
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

        [[nodiscard]] std::span<const Detail::BoundFrameSnapshot> PublishPlanetFrames(const double time)
        {
            return PlanetFrames.Resolve(*Spatial, Planets, time);
        }

        [[nodiscard]] std::vector<PhysicsGravitySource> BuildPlanetGravity(const double time)
        {
            const auto snapshots = PublishPlanetFrames(time);
            const auto target = Spatial->Find(Detail::SceneRuntimeSpatialAccess::RootFrame);
            const Detail::SectorPosition targetZero(Spatial->Identity(target), {}, {});
            std::vector<PhysicsGravitySource> sources;
            sources.reserve(Planets.size());
            for (const auto& planet : Planets)
            {
                if (planet.GravitationalParameter == 0.0)
                {
                    continue;
                }
                const auto source = Spatial->Find(planet.Frame);
                const auto position = Detail::ConvertFrameMotion(
                    *Spatial, source, target,
                    Detail::SpatialMotion{Detail::SectorPosition(Spatial->Identity(source), {}, {}), {}, {}}, snapshots,
                    time);
                const auto center = position.Position.RelativeTo(targetZero);
                constexpr double maximumPositionErrorMetres = 0.001;
                for (const double error : targetZero.Translated(center).RelativeTo(position.Position))
                {
                    if (std::abs(error) > maximumPositionErrorMetres)
                    {
                        throw std::out_of_range(
                            "Planet gravity requires a local simulation frame to preserve placement.");
                    }
                }
                PhysicsGravitySource gravity{
                    {center[0], center[1], center[2]}, planet.GravitationalParameter, planet.RadiusMetres};
                Detail::ValidateGravitySource(gravity);
                sources.push_back(gravity);
            }
            return sources;
        }

        void ApplyConfiguredGravity(const Ref<SceneRuntimeSession>& session)
        {
            if (!session || !session->Physics() || PlanetSettings.Gravity == Detail::PlanetGravityMode::Uniform)
            {
                return;
            }
            auto sources = BuildPlanetGravity(PlanetSimulationTime);
            session->Physics()->SetGravitySources(std::move(sources));
            if (PlanetSettings.Gravity == Detail::PlanetGravityMode::Radial)
            {
                session->Physics()->SetGravity({});
            }
        }

        void PreparePlanetTick(const double deltaSeconds)
        {
            if (!std::isfinite(PlanetSimulationTime + deltaSeconds))
            {
                throw std::overflow_error("Planet simulation time exceeded its finite local epoch.");
            }
            // Every source is projected and validated before any session is changed.
            if (PlanetSettings.Gravity != Detail::PlanetGravityMode::Uniform)
            {
                const auto sources = BuildPlanetGravity(PlanetSimulationTime);
                std::vector<std::pair<Ref<PhysicsWorld>, std::vector<PhysicsGravitySource>>> prepared;
                for (const auto& entry : Entries)
                {
                    if (entry.Runtime && entry.Runtime->Physics())
                    {
                        prepared.emplace_back(entry.Runtime->Physics(), sources);
                    }
                }
                for (auto& [physics, gravity] : prepared)
                {
                    physics->SetGravitySources(std::move(gravity));
                    if (PlanetSettings.Gravity == Detail::PlanetGravityMode::Radial)
                    {
                        physics->SetGravity({});
                    }
                }
            }
        }

        SceneRuntimeWorldSpecification Specification;
        Detail::EnvironmentClock Environment;
        Detail::EnvironmentLightingProfile EnvironmentLighting;
        const AssetId EnvironmentSource = AssetId::Generate();
        std::unique_ptr<Detail::FrameRegistry> Spatial = std::make_unique<Detail::FrameRegistry>(
            Detail::SceneRuntimeSpatialAccess::DefaultLogicalWorld, Detail::SceneRuntimeSpatialAccess::RootFrame);
        std::vector<Detail::PlanetDefinition> Planets;
        std::vector<Detail::FrameAttachment> PlanetAttachments;
        Detail::PlanetProjectSettings PlanetSettings;
        Detail::PlanetFrameCache PlanetFrames;
        double PlanetSimulationTime = 0.0;
        std::thread::id OwnerThread;
        std::vector<Entry> Entries;
        std::vector<Ref<SceneRuntimeLoadOperation>> Loads;
        std::vector<SceneHandle> PendingUnloads;
        std::optional<SceneHandle> PendingActive;
        SceneHandle Active;
        std::uint32_t NextHandle = 0;
        bool Open = true;
        bool Processing = false;
        bool Paused = false;
        bool Stepping = false;
    };

    SceneRuntimeWorld::SceneRuntimeWorld(SceneRuntimeWorldSpecification specification)
        : m_Impl(std::make_unique<Impl>(std::move(specification)))
    {
    }

    SceneRuntimeWorld::~SceneRuntimeWorld() { Close(); }

    void Detail::SceneRuntimePlanetAccess::Configure(SceneRuntimeWorld& world, std::vector<PlanetDefinition> planets)
    {
        auto& state = *world.m_Impl;
        state.RequireOwner("ConfigurePlanets");
        if (state.Stepping || state.Processing || !state.Entries.empty() || !state.Loads.empty())
        {
            throw std::logic_error("Planet topology must be configured before runtime scene adoption or loading.");
        }
        constexpr std::size_t maximumPlanets = 64;
        if (planets.size() > maximumPlanets)
        {
            throw std::invalid_argument("Runtime planet configuration exceeds 64 sources.");
        }
        auto registry = std::make_unique<FrameRegistry>(SceneRuntimeSpatialAccess::DefaultLogicalWorld,
                                                        SceneRuntimeSpatialAccess::RootFrame);
        std::set<AssetId> identities;
        std::set<std::uint64_t> pending;
        for (const auto& planet : planets)
        {
            ValidatePlanetDefinition(planet);
            if (planet.ParentCenter.Identity().World != SceneRuntimeSpatialAccess::DefaultLogicalWorld ||
                planet.Frame == SceneRuntimeSpatialAccess::RootFrame || !identities.insert(planet.Planet).second ||
                !pending.insert(planet.Frame).second)
            {
                throw std::invalid_argument("Planet identity/frame is duplicate or belongs to another logical world.");
            }
        }
        std::set<std::uint64_t> created{SceneRuntimeSpatialAccess::RootFrame};
        std::vector<FrameAttachment> attachments;
        attachments.reserve(planets.size());
        while (!pending.empty())
        {
            bool progressed = false;
            for (const auto& planet : planets)
            {
                const auto parent = planet.ParentCenter.Identity().Frame;
                if (pending.contains(planet.Frame) && created.contains(parent))
                {
                    const auto frame = registry->Create(planet.Frame, registry->Find(parent));
                    attachments.push_back(registry->Attach(frame));
                    created.insert(planet.Frame);
                    pending.erase(planet.Frame);
                    progressed = true;
                }
            }
            if (!progressed)
            {
                throw std::invalid_argument("Planet frames contain a cycle or unknown parent.");
            }
        }
        // Nothing visible changes until the complete topology and all attachments have been allocated.
        auto environment = state.Environment;
        environment.Restore(environment.State());
        state.Spatial.swap(registry);
        state.Planets.swap(planets);
        state.PlanetFrames = {};
        state.PlanetAttachments.swap(attachments);
        state.Environment = environment;
        state.PlanetSettings = {};
        state.PlanetSimulationTime = 0.0;
    }

    void Detail::SceneRuntimePlanetAccess::ConfigureProject(SceneRuntimeWorld& world, PlanetProjectSettings settings)
    {
        ValidatePlanetProjectSettings(settings);
        Configure(world, settings.Planets);
        world.m_Impl->PlanetSettings = std::move(settings);
    }

    Detail::PlanetProjectSettings Detail::SceneRuntimePlanetAccess::Settings(const SceneRuntimeWorld& world)
    {
        world.m_Impl->RequireOwner("PlanetProjectSettings");
        return world.m_Impl->PlanetSettings;
    }

    std::optional<Detail::ResolvedPlanetFrame>
    Detail::SceneRuntimePlanetAccess::ResolveSelected(SceneRuntimeWorld& world,
                                                      const SectorPosition& rootObserverPosition)
    {
        world.m_Impl->RequireOwner("ResolveSelectedPlanet");
        auto& state = *world.m_Impl;
        if (state.PlanetSettings.Geometry == PlanetGeometryMode::Flat)
        {
            return std::nullopt;
        }
        return SelectPlanetEnvironment(state.PlanetSettings,
                                       [&](const AssetId identity)
                                       {
                                           return Resolve(world, identity,
                                                          state.Spatial->Find(SceneRuntimeSpatialAccess::RootFrame),
                                                          rootObserverPosition, state.PlanetSimulationTime);
                                       });
    }

    void Detail::SceneRuntimePlanetAccess::InitializeGravity(SceneRuntimeWorld& world)
    {
        world.m_Impl->RequireOwner("InitializePlanetGravity");
        if (world.m_Impl->Stepping || world.m_Impl->Processing)
        {
            throw std::logic_error("Planet gravity initialization requires a runtime safe boundary.");
        }
        world.m_Impl->PreparePlanetTick(0.0);
    }

    Detail::PlanetRuntimeCheckpoint Detail::SceneRuntimePlanetAccess::CaptureCheckpoint(const SceneRuntimeWorld& world)
    {
        world.m_Impl->RequireOwner("CapturePlanetCheckpoint");
        return {world.m_Impl->PlanetSettings, world.m_Impl->PlanetSimulationTime, world.m_Impl->Environment.State()};
    }

    void Detail::SceneRuntimePlanetAccess::RestoreCheckpoint(SceneRuntimeWorld& world,
                                                             const PlanetRuntimeCheckpoint& checkpoint)
    {
        auto& state = *world.m_Impl;
        state.RequireOwner("RestorePlanetCheckpoint");
        if (state.Stepping || state.Processing)
        {
            throw std::logic_error("Planet checkpoint restoration requires a runtime safe boundary.");
        }
        ValidatePlanetProjectSettings(checkpoint.Settings);
        if (checkpoint.Settings != state.PlanetSettings || !std::isfinite(checkpoint.SimulationTime) ||
            checkpoint.SimulationTime < 0.0)
        {
            throw std::invalid_argument("Planet checkpoint settings or simulation time are incompatible.");
        }
        auto clock = state.Environment;
        clock.Restore(checkpoint.Clock);
        // Preflight frame evaluations before committing either clock. Physics owns its separate body/source checkpoint.
        for (const auto& planet : state.Planets)
        {
            (void)EvaluatePlanetFrame(planet, checkpoint.SimulationTime, state.Spatial->Revision());
        }
        state.Environment = clock;
        state.PlanetSimulationTime = checkpoint.SimulationTime;
    }

    std::vector<Detail::PlanetDefinition> Detail::SceneRuntimePlanetAccess::Definitions(const SceneRuntimeWorld& world)
    {
        world.m_Impl->RequireOwner("PlanetDefinitions");
        return world.m_Impl->Planets;
    }

    Detail::FrameRotation Detail::SceneRuntimePlanetAccess::Rotation(SceneRuntimeWorld& world, const LiveFrame& source,
                                                                     const LiveFrame& target, const double snapshotTime)
    {
        auto& state = *world.m_Impl;
        state.RequireOwner("ResolveFrameRotation");
        (void)state.Spatial->Identity(source);
        (void)state.Spatial->Identity(target);
        const auto snapshots = state.PublishPlanetFrames(snapshotTime);
        return ConvertFrameRotation(*state.Spatial, source, target, snapshots, snapshotTime);
    }

    Detail::ResolvedPlanetFrame Detail::SceneRuntimePlanetAccess::Resolve(SceneRuntimeWorld& world,
                                                                          const AssetId planet,
                                                                          const LiveFrame& observerFrame,
                                                                          const SectorPosition& observerPosition,
                                                                          const double snapshotTime)
    {
        auto& state = *world.m_Impl;
        state.RequireOwner("ResolvePlanet");
        const auto found = std::ranges::find(state.Planets, planet, &PlanetDefinition::Planet);
        if (found == state.Planets.end())
        {
            throw std::invalid_argument("Unknown planet identity in this runtime world.");
        }
        // Reject foreign observer handles before publishing a new evaluation epoch.
        if (state.Spatial->Identity(observerFrame) != observerPosition.Identity())
        {
            throw std::invalid_argument("Planet observer position does not match its live frame.");
        }
        const auto snapshots = state.PublishPlanetFrames(snapshotTime);
        return ResolvePlanetFrame(*state.Spatial, state.Spatial->Find(found->Frame), observerFrame, observerPosition,
                                  snapshots, snapshotTime, state.Environment.State(), planet, found->RadiusMetres,
                                  state.Environment.DiscontinuityRevision());
    }

    void Detail::SceneRuntimePlanetAccess::ApplyGravity(SceneRuntimeWorld& world, const SceneHandle scene,
                                                        const double snapshotTime)
    {
        auto& state = *world.m_Impl;
        state.RequireOwner("ApplyPlanetGravity");
        if (state.Stepping || state.Processing)
        {
            throw std::logic_error("Planet gravity changes require a runtime safe boundary.");
        }
        const auto entry = state.Find(scene);
        if (entry == state.Entries.end() || !entry->Runtime || !entry->Runtime->Physics())
        {
            throw std::invalid_argument("Planet gravity requires an owned scene with active physics.");
        }
        auto sources = state.BuildPlanetGravity(snapshotTime);
        // Physics validates before its vector swap; failed preparation leaves every existing source untouched.
        entry->Runtime->Physics()->SetGravitySources(std::move(sources));
    }

    Detail::EnvironmentClockState Detail::SceneRuntimeEnvironmentAccess::State(const SceneRuntimeWorld& world)
    {
        world.m_Impl->RequireOwner("EnvironmentState");
        return world.m_Impl->Environment.State();
    }

    std::uint64_t Detail::SceneRuntimeEnvironmentAccess::Revision(const SceneRuntimeWorld& world)
    {
        world.m_Impl->RequireOwner("EnvironmentRevision");
        return world.m_Impl->Environment.DiscontinuityRevision();
    }

    void Detail::SceneRuntimeEnvironmentAccess::Restore(SceneRuntimeWorld& world, const EnvironmentClockState state)
    {
        world.m_Impl->RequireOwner("RestoreEnvironment");
        if (world.m_Impl->Stepping)
        {
            throw std::logic_error("Environment restoration is not allowed during a world tick.");
        }
        world.m_Impl->Environment.Restore(state);
    }

    Detail::EnvironmentLightingProfile Detail::SceneRuntimeEnvironmentAccess::Lighting(const SceneRuntimeWorld& world)
    {
        world.m_Impl->RequireOwner("EnvironmentLighting");
        return world.m_Impl->EnvironmentLighting;
    }

    EnvironmentLightingSnapshot Detail::SceneRuntimeEnvironmentAccess::CaptureLighting(const SceneRuntimeWorld& world)
    {
        world.m_Impl->RequireOwner("CaptureEnvironmentLighting");
        return EvaluateEnvironmentLighting(world.m_Impl->EnvironmentLighting, world.m_Impl->Environment.State(),
                                           world.m_Impl->EnvironmentSource,
                                           world.m_Impl->Environment.DiscontinuityRevision());
    }

    void Detail::SceneRuntimeEnvironmentAccess::Restore(SceneRuntimeWorld& world, const EnvironmentClockState state,
                                                        EnvironmentLightingProfile lighting)
    {
        world.m_Impl->RequireOwner("RestoreEnvironmentLighting");
        if (world.m_Impl->Stepping)
        {
            throw std::logic_error("Environment restoration is not allowed during a world tick.");
        }
        auto clock = world.m_Impl->Environment;
        clock.Restore(state);
        world.m_Impl->EnvironmentLighting = std::move(lighting);
        world.m_Impl->Environment = clock;
    }

    const Detail::FrameRegistry& Detail::SceneRuntimeSpatialAccess::Frames(const SceneRuntimeWorld& world)
    {
        world.m_Impl->RequireOwner("SpatialFrames");
        return *world.m_Impl->Spatial;
    }

    Detail::LiveFrame Detail::SceneRuntimeSpatialAccess::FrameForEntity(const SceneRuntimeWorld& world,
                                                                        const Entity& entity)
    {
        world.m_Impl->RequireOwner("SpatialFrameForEntity");
        if (!entity)
        {
            throw std::invalid_argument("Spatial frame resolution requires a live entity.");
        }
        for (const auto& entry : world.m_Impl->Entries)
        {
            const auto scene = entry.Runtime ? entry.Runtime->RuntimeScene() : Ref<Scene>{};
            if (scene && scene->FindEntity(entity.Id()) == entity)
            {
                return world.m_Impl->Spatial->Find(RootFrame);
            }
        }
        throw std::invalid_argument("The entity does not belong to this runtime spatial world.");
    }

    std::vector<Detail::SceneRuntimeIdentityBinding>
    Detail::SceneRuntimeIdentityAccess::Capture(const SceneRuntimeWorld& world)
    {
        world.m_Impl->RequireOwner("CaptureInstanceIdentities");
        std::vector<SceneRuntimeIdentityBinding> result;
        result.reserve(world.m_Impl->Entries.size());
        for (const auto& entry : world.m_Impl->Entries)
        {
            result.push_back({entry.Handle, entry.Instance});
        }
        return result;
    }

    void Detail::SceneRuntimeIdentityAccess::Rebind(SceneRuntimeWorld& world,
                                                    const std::span<const SceneRuntimeIdentityBinding> bindings)
    {
        world.m_Impl->RequireOwner("RestoreInstanceIdentities");
        if (world.m_Impl->Stepping || world.m_Impl->Processing)
        {
            throw std::logic_error("Runtime instance identities cannot change during world callbacks.");
        }
        if (bindings.size() != world.m_Impl->Entries.size())
        {
            throw std::invalid_argument("Runtime instance identity bindings must contain every scene instance.");
        }
        std::set<SceneHandle> handles;
        std::set<AssetId> identities;
        for (const auto& binding : bindings)
        {
            if (!binding.Instance || !handles.insert(binding.Handle).second ||
                !identities.insert(binding.Instance).second ||
                world.m_Impl->Find(binding.Handle) == world.m_Impl->Entries.end())
            {
                throw std::invalid_argument("Runtime instance identity binding is invalid or duplicated.");
            }
        }
        for (const auto& binding : bindings)
        {
            world.m_Impl->Find(binding.Handle)->Instance = binding.Instance;
        }
    }

    SceneHandle SceneRuntimeWorld::Adopt(Ref<SceneRuntimeSession> session)
    {
        m_Impl->RequireOwner("Adopt");
        const auto previousActive = m_Impl->Active;
        const auto handle = m_Impl->Add(session);
        try
        {
            m_Impl->ApplyConfiguredGravity(session);
        }
        catch (...)
        {
            m_Impl->Entries.pop_back();
            m_Impl->Active = previousActive;
            throw;
        }
        if (m_Impl->Paused)
        {
            session->Pause();
        }
        return handle;
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
        if (m_Impl->Processing)
        {
            throw std::logic_error("SceneRuntimeWorld::Process cannot run recursively from a lifecycle callback.");
        }
        struct ProcessingScope final
        {
            bool& Active;

            ~ProcessingScope() { Active = false; }
        };
        m_Impl->Processing = true;
        const ProcessingScope processingScope{m_Impl->Processing};
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

        // Startup callbacks may enqueue loads. Keep this pass bounded and its operation references stable.
        const auto processingLoads = m_Impl->Loads;
        for (const auto& operation : processingLoads)
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
                state.Handle = m_Impl->Activate(std::move(session),
                                                state.Load->Mode() == SceneLoadMode::Single ? validator : nullptr);
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

    void SceneRuntimeWorld::Pause(const bool paused)
    {
        m_Impl->RequireOwner("Pause");
        const auto sessions = Sessions();
        for (const auto& session : sessions)
        {
            session->Pause(paused);
        }
        m_Impl->Paused = paused;
    }

    bool SceneRuntimeWorld::Step(const float fixedDeltaSeconds)
    {
        m_Impl->RequireOwner("Step");
        if (m_Impl->Stepping)
        {
            throw std::logic_error("World stepping cannot be recursive.");
        }
        if (!std::isfinite(fixedDeltaSeconds) || fixedDeltaSeconds <= 0.0F)
        {
            throw std::invalid_argument("World step delta must be finite and positive.");
        }
        const auto sessions = Sessions();
        if (sessions.empty() || std::ranges::any_of(sessions, [](const auto& session)
                                                    { return session->State() != ScenePlayState::Paused; }))
        {
            return false;
        }
        m_Impl->PreparePlanetTick(fixedDeltaSeconds);
        struct StepScope
        {
            bool& Active;
            ~StepScope() { Active = false; }
        } scope{m_Impl->Stepping};
        m_Impl->Stepping = true;
        // Commit once before callbacks. A gameplay fault cannot roll back an already accepted world tick.
        m_Impl->Environment.Advance(fixedDeltaSeconds);
        m_Impl->PlanetSimulationTime += fixedDeltaSeconds;
        bool succeeded = true;
        for (const auto& session : sessions)
        {
            const bool stepped = session->Step(fixedDeltaSeconds);
            succeeded = stepped && succeeded;
        }
        return succeeded;
    }

    void SceneRuntimeWorld::FixedUpdate(const float deltaSeconds)
    {
        m_Impl->RequireOwner("FixedUpdate");
        if (m_Impl->Stepping)
        {
            throw std::logic_error("World stepping cannot be recursive.");
        }
        if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F)
        {
            throw std::invalid_argument("World fixed delta must be finite and nonnegative.");
        }
        if (m_Impl->Paused)
        {
            return;
        }
        const auto sessions = Sessions();
        if (std::ranges::none_of(sessions,
                                 [](const auto& session) { return session->State() == ScenePlayState::Playing; }))
        {
            return;
        }
        m_Impl->PreparePlanetTick(deltaSeconds);
        struct TickScope
        {
            bool& Active;
            ~TickScope() { Active = false; }
        } scope{m_Impl->Stepping};
        m_Impl->Stepping = true;
        m_Impl->Environment.Advance(deltaSeconds);
        m_Impl->PlanetSimulationTime += deltaSeconds;
        for (const auto& session : sessions)
        {
            session->FixedUpdate(deltaSeconds);
        }
    }

    void SceneRuntimeWorld::Update(const float deltaSeconds, const float interpolationAlpha)
    {
        m_Impl->RequireOwner("Update");
        if (m_Impl->Paused)
        {
            return;
        }
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
        m_Impl->PlanetAttachments.clear();
        m_Impl->Planets.clear();
        m_Impl->Spatial->Close();
        m_Impl->PendingUnloads.clear();
        m_Impl->PendingActive.reset();
        m_Impl->Active = {};
        m_Impl->Specification.Scenes.Reset();
        m_Impl->Specification.Assets.Reset();
        m_Impl->Specification.Audio.Reset();
        m_Impl->Specification.Physics.Reset();
    }
} // namespace Keire
