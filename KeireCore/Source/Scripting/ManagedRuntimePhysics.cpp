#include "KeireInternal/Scripting/ManagedRuntimePhysics.h"

#include "Keire/Scenes/Scene.h"

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4146)
#endif
#include <Coral/Assembly.hpp>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <stdexcept>

namespace Keire::Detail
{
    namespace
    {
        inline constexpr int MaximumManagedOverlapResults = 256;

        struct NativePhysicsHit
        {
            std::uint64_t EntityHigh = 0;
            std::uint64_t EntityLow = 0;
            Vector3 Point;
            Vector3 Normal;
            float Distance = 0.0F;
        };
        static_assert(sizeof(NativePhysicsHit) == 48);

        struct NativeEntityId
        {
            std::uint64_t High = 0;
            std::uint64_t Low = 0;
        };
        static_assert(sizeof(NativeEntityId) == 16);

        struct NativePrecisePhysicsQuery
        {
            std::uint64_t World;
            std::uint64_t ContextHigh;
            std::uint64_t ContextLow;
            std::uint64_t IgnoredHigh;
            std::uint64_t IgnoredLow;
            Vector3d Origin;
            Vector3 Direction;
            Quaternion Rotation;
            Vector3 Displacement;
            float MaximumDistance;
            float Radius;
            float Height;
            std::uint32_t Mask;
            std::uint32_t Kind;
            std::uint32_t IncludeTriggers;
        };
        static_assert(sizeof(NativePrecisePhysicsQuery) == 128);
        static_assert(offsetof(NativePrecisePhysicsQuery, Origin) == 40);
        static_assert(offsetof(NativePrecisePhysicsQuery, Kind) == 120);

        struct NativePrecisePhysicsHit
        {
            std::uint64_t EntityHigh;
            std::uint64_t EntityLow;
            Vector3d Point;
            Vector3 Normal;
            float Distance;
        };
        static_assert(sizeof(NativePrecisePhysicsHit) == 56);
        static_assert(offsetof(NativePrecisePhysicsHit, Point) == 16);
        thread_local IScriptRuntimeServices* ActiveServices = nullptr;

        [[nodiscard]] int PrecisePhysicsQueryV1(const NativePrecisePhysicsQuery* query, const int querySize,
                                                NativePrecisePhysicsHit* hit, const int hitSize,
                                                NativeEntityId* entities, const int capacity) noexcept
        {
            if (!ActiveServices || !query || querySize != sizeof(NativePrecisePhysicsQuery) ||
                hitSize != sizeof(NativePrecisePhysicsHit) || query->Kind > 2 || query->IncludeTriggers > 1 ||
                capacity < 0 || (query->Kind < 2 && !hit))
            {
                return -1;
            }
            try
            {
                const AssetId context(query->ContextHigh, query->ContextLow);
                const AssetId ignored(query->IgnoredHigh, query->IgnoredLow);
                if (query->Kind == 2)
                {
                    auto results = ActiveServices->OverlapSphereManagedPrecise({query->World, context, query->Origin,
                                                                                query->Radius, query->Mask, ignored,
                                                                                query->IncludeTriggers != 0});
                    if (results.size() > static_cast<std::size_t>(MaximumManagedOverlapResults))
                    {
                        results.resize(MaximumManagedOverlapResults);
                    }
                    const auto count = static_cast<int>(results.size());
                    if (!entities || capacity == 0)
                    {
                        return count;
                    }
                    if (capacity < count)
                    {
                        return -1;
                    }
                    std::ranges::transform(results, entities, [](const AssetId entity)
                                           { return NativeEntityId{entity.High(), entity.Low()}; });
                    return count;
                }
                const auto result =
                    query->Kind == 0
                        ? ActiveServices->RaycastManagedPrecise({query->World, context, query->Origin, query->Direction,
                                                                 query->MaximumDistance, query->Mask, ignored,
                                                                 query->IncludeTriggers != 0})
                        : ActiveServices->CapsuleCastManagedPrecise(
                              {query->World, context, query->Origin, query->Rotation, query->Radius, query->Height,
                               query->Displacement, query->Mask, ignored, query->IncludeTriggers != 0});
                if (!result)
                {
                    return 0;
                }
                *hit = {result->Entity.High(), result->Entity.Low(), result->Point, result->Normal, result->Distance};
                return 1;
            }
            catch (...)
            {
                return -1;
            }
        }
        void CopyHit(const ManagedRaycastHit& source, NativePhysicsHit& destination) noexcept
        {
            destination.EntityHigh = source.Entity.High();
            destination.EntityLow = source.Entity.Low();
            destination.Point = source.Point;
            destination.Normal = source.Normal;
            destination.Distance = source.Distance;
        }

        [[nodiscard]] std::uint8_t CapsuleCast(const std::uint64_t world, const std::uint64_t contextHigh,
                                               const std::uint64_t contextLow, const Vector3 origin,
                                               const Quaternion rotation, const float radius, const float height,
                                               const Vector3 displacement, const std::uint32_t mask,
                                               const std::uint8_t includeTriggers, const std::uint64_t ignoredHigh,
                                               const std::uint64_t ignoredLow, NativePhysicsHit* destination) noexcept
        {
            if (!ActiveServices || !destination)
                return 0;
            try
            {
                const auto hit = ActiveServices->CapsuleCastManaged({.World = world,
                                                                     .ContextEntity = AssetId(contextHigh, contextLow),
                                                                     .Origin = origin,
                                                                     .Rotation = rotation,
                                                                     .Radius = radius,
                                                                     .Height = height,
                                                                     .Displacement = displacement,
                                                                     .Mask = mask,
                                                                     .IgnoredEntity = AssetId(ignoredHigh, ignoredLow),
                                                                     .IncludeTriggers = includeTriggers != 0});
                if (!hit)
                    return 0;
                CopyHit(*hit, *destination);
                return 1;
            }
            catch (...)
            {
                return 0;
            }
        }

        [[nodiscard]] int OverlapSphere(const std::uint64_t world, const std::uint64_t contextHigh,
                                        const std::uint64_t contextLow, const Vector3 center, const float radius,
                                        const std::uint32_t mask, const std::uint8_t includeTriggers,
                                        const std::uint64_t ignoredHigh, const std::uint64_t ignoredLow,
                                        NativeEntityId* destination, const int capacity) noexcept
        {
            if (!ActiveServices || capacity < 0)
                return -1;
            try
            {
                auto entities = ActiveServices->OverlapSphereManaged({.World = world,
                                                                      .ContextEntity = AssetId(contextHigh, contextLow),
                                                                      .Center = center,
                                                                      .Radius = radius,
                                                                      .Mask = mask,
                                                                      .IgnoredEntity = AssetId(ignoredHigh, ignoredLow),
                                                                      .IncludeTriggers = includeTriggers != 0});
                if (entities.size() > static_cast<std::size_t>(MaximumManagedOverlapResults))
                    entities.resize(MaximumManagedOverlapResults);
                const auto count = static_cast<int>(entities.size());
                if (!destination || capacity == 0)
                    return count;
                if (capacity < count)
                    return -1;
                std::ranges::transform(entities, destination, [](const AssetId entity)
                                       { return NativeEntityId{entity.High(), entity.Low()}; });
                return count;
            }
            catch (...)
            {
                return -1;
            }
        }
    } // namespace

    std::optional<ManagedRaycastHit> QueryManagedCapsule(const Ref<SceneRuntimeSession>& runtime,
                                                         const ManagedCapsuleCastQuery& query) noexcept
    {
        try
        {
            if (!runtime)
                return std::nullopt;
            PhysicsCapsuleCastQuery native;
            native.Origin = query.Origin;
            native.Rotation = query.Rotation;
            native.Radius = query.Radius;
            native.Height = query.Height;
            native.Displacement = query.Displacement;
            native.Mask = query.Mask;
            native.IncludeTriggers = query.IncludeTriggers;
            const auto scene = runtime->RuntimeScene();
            const auto context = scene ? scene->FindEntity(EntityId(query.ContextEntity)) : Entity{};
            native.Layer = context ? 1U << context.Layer() : 1U;
            const auto hit = runtime->CastCapsule(native, EntityId(query.IgnoredEntity));
            return hit ? std::optional{ManagedRaycastHit{hit->Entity.Value(), hit->Hit.Position, hit->Hit.Normal,
                                                         hit->Hit.Distance}}
                       : std::nullopt;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::vector<AssetId> QueryManagedSphereOverlap(const Ref<SceneRuntimeSession>& runtime,
                                                   const ManagedSphereOverlapQuery& query)
    {
        if (!runtime)
            return {};
        PhysicsSphereOverlapQuery native;
        native.Center = query.Center;
        native.Radius = query.Radius;
        native.Mask = query.Mask;
        native.IncludeTriggers = query.IncludeTriggers;
        const auto scene = runtime->RuntimeScene();
        const auto context = scene ? scene->FindEntity(EntityId(query.ContextEntity)) : Entity{};
        native.Layer = context ? 1U << context.Layer() : 1U;
        auto entities = runtime->OverlapSphere(native, EntityId(query.IgnoredEntity));
        if (entities.size() > static_cast<std::size_t>(MaximumManagedOverlapResults))
            entities.resize(MaximumManagedOverlapResults);
        std::vector<AssetId> result;
        result.reserve(entities.size());
        std::ranges::transform(entities, std::back_inserter(result),
                               [](const EntityId entity) { return entity.Value(); });
        return result;
    }

    namespace
    {
        [[nodiscard]] std::uint32_t PreciseQueryLayer(const Ref<SceneRuntimeSession>& runtime,
                                                      const std::uint64_t world, const AssetId contextId,
                                                      const AssetId ignoredId)
        {
            const auto scene = runtime ? runtime->RuntimeScene() : Ref<Scene>{};
            const auto context = scene ? scene->FindEntity(EntityId(contextId)) : Entity{};
            if (!context || world == 0 || context.World() != world)
            {
                throw std::invalid_argument("Precise physics query context does not belong to the runtime world.");
            }
            if (ignoredId && !scene->FindEntity(EntityId(ignoredId)))
            {
                throw std::invalid_argument("Ignored physics entity does not belong to the query world.");
            }
            return 1U << context.Layer();
        }
    } // namespace

    std::optional<ManagedRaycastHitPrecise> QueryManagedRayPrecise(const Ref<SceneRuntimeSession>& runtime,
                                                                   const ManagedRaycastQueryPrecise& query)
    {
        const auto layer = PreciseQueryLayer(runtime, query.World, query.ContextEntity, query.IgnoredEntity);
        const auto hits = runtime->RayCastPrecise({.Origin = query.Origin,
                                                   .Direction = query.Direction,
                                                   .MaximumDistance = query.MaximumDistance,
                                                   .Mask = query.Mask,
                                                   .IncludeTriggers = query.IncludeTriggers,
                                                   .Layer = layer},
                                                  EntityId(query.IgnoredEntity));
        if (hits.empty())
        {
            return std::nullopt;
        }
        const auto& hit = hits.front();
        return ManagedRaycastHitPrecise{hit.Entity.Value(), hit.Position, hit.Normal, hit.Distance};
    }

    std::optional<ManagedRaycastHitPrecise> QueryManagedCapsulePrecise(const Ref<SceneRuntimeSession>& runtime,
                                                                       const ManagedCapsuleCastQueryPrecise& query)
    {
        const auto layer = PreciseQueryLayer(runtime, query.World, query.ContextEntity, query.IgnoredEntity);
        const auto hit = runtime->CastCapsulePrecise({.Origin = query.Origin,
                                                      .Rotation = query.Rotation,
                                                      .Radius = query.Radius,
                                                      .Height = query.Height,
                                                      .Displacement = query.Displacement,
                                                      .Mask = query.Mask,
                                                      .IncludeTriggers = query.IncludeTriggers,
                                                      .Layer = layer},
                                                     EntityId(query.IgnoredEntity));
        if (!hit)
        {
            return std::nullopt;
        }
        return ManagedRaycastHitPrecise{hit->Entity.Value(), hit->Position, hit->Normal, hit->Distance};
    }

    std::vector<AssetId> QueryManagedSphereOverlapPrecise(const Ref<SceneRuntimeSession>& runtime,
                                                          const ManagedSphereOverlapQueryPrecise& query)
    {
        const auto layer = PreciseQueryLayer(runtime, query.World, query.ContextEntity, query.IgnoredEntity);
        auto entities = runtime->OverlapSpherePrecise({.Center = query.Center,
                                                       .Radius = query.Radius,
                                                       .Mask = query.Mask,
                                                       .IncludeTriggers = query.IncludeTriggers,
                                                       .Layer = layer},
                                                      EntityId(query.IgnoredEntity));
        if (entities.size() > static_cast<std::size_t>(MaximumManagedOverlapResults))
        {
            entities.resize(MaximumManagedOverlapResults);
        }
        std::vector<AssetId> result;
        result.reserve(entities.size());
        std::ranges::transform(entities, std::back_inserter(result),
                               [](const EntityId entity) { return entity.Value(); });
        return result;
    }
    ManagedRuntimePhysicsScope::ManagedRuntimePhysicsScope(IScriptRuntimeServices* services) noexcept
        : m_Previous(ActiveServices)
    {
        ActiveServices = services;
    }

    ManagedRuntimePhysicsScope::~ManagedRuntimePhysicsScope() { ActiveServices = m_Previous; }

    void RegisterManagedRuntimePhysics(Coral::ManagedAssembly& assembly)
    {
        assembly.AddInternalCall("Keire.NativeRuntime", "PrecisePhysicsQueryV1Icall",
                                 reinterpret_cast<void*>(&PrecisePhysicsQueryV1));
        assembly.AddInternalCall("Keire.NativeRuntime", "CapsuleCastIcall", reinterpret_cast<void*>(&CapsuleCast));
        assembly.AddInternalCall("Keire.NativeRuntime", "OverlapSphereIcall", reinterpret_cast<void*>(&OverlapSphere));
    }
} // namespace Keire::Detail
