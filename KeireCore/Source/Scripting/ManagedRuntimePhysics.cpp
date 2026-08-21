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

        thread_local IScriptRuntimeServices* ActiveServices = nullptr;

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

    ManagedRuntimePhysicsScope::ManagedRuntimePhysicsScope(IScriptRuntimeServices* services) noexcept
        : m_Previous(ActiveServices)
    {
        ActiveServices = services;
    }

    ManagedRuntimePhysicsScope::~ManagedRuntimePhysicsScope() { ActiveServices = m_Previous; }

    void RegisterManagedRuntimePhysics(Coral::ManagedAssembly& assembly)
    {
        assembly.AddInternalCall("Keire.NativeRuntime", "CapsuleCastIcall", reinterpret_cast<void*>(&CapsuleCast));
        assembly.AddInternalCall("Keire.NativeRuntime", "OverlapSphereIcall", reinterpret_cast<void*>(&OverlapSphere));
    }
} // namespace Keire::Detail
