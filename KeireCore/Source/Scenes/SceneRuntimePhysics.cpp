#include "KeireInternal/Physics/PhysicsFrameAccess.h"
#include "KeireInternal/Scenes/SceneRuntimeSessionImpl.h"
#include "KeireInternal/Spatial/PlanetCharacterKinematicPath.h"
#include "KeireInternal/Spatial/PlanetCharacterMovingClearance.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <utility>

namespace Keire
{
    namespace
    {
        constexpr double MaximumPhysicsPositionErrorMetres = 0.001;

        [[nodiscard]] float CharacterDot(const Vector3 left, const Vector3 right) noexcept
        {
            return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
        }

        [[nodiscard]] Vector3 CharacterUp(const Quaternion rotation)
        {
            return Math::TransformDirection(Math::ComposeTransform({}, rotation, {1.0F, 1.0F, 1.0F}),
                                            {0.0F, 1.0F, 0.0F});
        }

        [[nodiscard]] Vector3d InitialPhysicsOrigin(const Scene& scene)
        {
            std::optional<Vector3d> minimum;
            Vector3d maximum;
            for (const auto& entity : scene.Entities())
            {
                const auto collider = entity.GetComponent<ColliderComponent>();
                const auto character = entity.GetComponent<CharacterControllerComponent>();
                const auto transform = entity.GetComponent<TransformComponent>();
                if (!entity.ActiveInHierarchy() || !transform ||
                    !((collider && collider->Enabled()) || (character && character->Enabled())))
                {
                    continue;
                }
                const auto position = transform->WorldPositionPrecise();
                if (!minimum)
                {
                    minimum = position;
                    maximum = position;
                    continue;
                }
                minimum->X = std::min(minimum->X, position.X);
                minimum->Y = std::min(minimum->Y, position.Y);
                minimum->Z = std::min(minimum->Z, position.Z);
                maximum.X = std::max(maximum.X, position.X);
                maximum.Y = std::max(maximum.Y, position.Y);
                maximum.Z = std::max(maximum.Z, position.Z);
            }
            if (!minimum)
            {
                return {};
            }
            constexpr double defaultRegionRadiusMetres = 8192.0;
            if (std::max({std::abs(minimum->X), std::abs(minimum->Y), std::abs(minimum->Z), std::abs(maximum.X),
                          std::abs(maximum.Y), std::abs(maximum.Z)}) <= defaultRegionRadiusMetres)
            {
                return {};
            }
            // Stable bounds-based selection is independent of entity iteration order. Keeping the origin on
            // a power-of-two grid avoids introducing fractional offsets into ordinary local coordinates.
            constexpr double originGridMetres = 1024.0;
            const auto center = [](const double low, const double high)
            { return std::round(std::midpoint(low, high) / originGridMetres) * originGridMetres; };
            return {center(minimum->X, maximum.X), center(minimum->Y, maximum.Y), center(minimum->Z, maximum.Z)};
        }

        [[nodiscard]] Matrix4 PhysicsRelativeTransform(const TransformComponent& transform, const PhysicsWorld& world)
        {
            return transform.WorldRelativeMatrix(world.WorldOrigin(), MaximumPhysicsPositionErrorMetres);
        }

        // A capsule's rounded bottom reports a contact normal across a box edge, not the top face normal.
        // Only recover the face normal when a tiny local probe confirms the SAME body's surface at that edge.
        [[nodiscard]] Vector3 CharacterSupportNormal(PhysicsWorld& world, const PhysicsQueryHit& hit,
                                                     const float minimumNormal, const std::uint32_t mask,
                                                     const std::uint32_t layer, const Vector3 up)
        {
            const auto vertical = CharacterDot(hit.Normal, up);
            if (vertical >= minimumNormal)
                return hit.Normal;
            const Vector3 tangent{hit.Normal.X - up.X * vertical, hit.Normal.Y - up.Y * vertical,
                                  hit.Normal.Z - up.Z * vertical};
            const auto planar = std::sqrt(CharacterDot(tangent, tangent));
            if (planar <= 0.000001F)
                return hit.Normal;
            constexpr float inset = 0.001F;
            const Vector3 probe{hit.Position.X - tangent.X / planar * inset + up.X * 0.01F,
                                hit.Position.Y - tangent.Y / planar * inset + up.Y * 0.01F,
                                hit.Position.Z - tangent.Z / planar * inset + up.Z * 0.01F};
            const auto surfaces = world.RayCast({.Origin = probe,
                                                 .Direction = {-up.X, -up.Y, -up.Z},
                                                 .MaximumDistance = 0.02F,
                                                 .Mask = mask,
                                                 .IncludeTriggers = false,
                                                 .Layer = layer});
            for (const auto& surface : surfaces)
            {
                const Vector3 difference{surface.Position.X - hit.Position.X, surface.Position.Y - hit.Position.Y,
                                         surface.Position.Z - hit.Position.Z};
                if (surface.Body == hit.Body && CharacterDot(surface.Normal, up) >= minimumNormal &&
                    std::abs(CharacterDot(difference, up)) <= 0.002F)
                    return surface.Normal;
            }
            return hit.Normal;
        }
    } // namespace

    bool SceneRuntimeSession::Impl::SameCollision(const std::shared_ptr<const CookedCollisionMesh>& first,
                                                  const std::shared_ptr<const CookedCollisionMesh>& second) noexcept
    {
        if (!first || !second)
            return !first && !second;
        return first->ContentHash == second->ContentHash && first->Kind == second->Kind;
    }

    bool SceneRuntimeSession::Impl::SamePhysicsDefinition(const PhysicsBodyDefinition& first,
                                                          const PhysicsBodyDefinition& second) noexcept
    {
        // Shape dimensions derived from a rotated world matrix can vary by a few float ULPs.
        // Those round-off differences must not destroy/recreate a live body and discard its contacts.
        const auto sameDimension = [](const float left, const float right)
        {
            return std::abs(left - right) <=
                   std::max(std::abs(left), std::abs(right)) * std::numeric_limits<float>::epsilon() * 8.0F;
        };
        const bool shapeSizeMatches = sameDimension(first.HalfExtent.X, second.HalfExtent.X) &&
                                      sameDimension(first.HalfExtent.Y, second.HalfExtent.Y) &&
                                      sameDimension(first.HalfExtent.Z, second.HalfExtent.Z) &&
                                      sameDimension(first.Radius, second.Radius) &&
                                      sameDimension(first.Height, second.Height);
        return first.Motion == second.Motion && first.Shape == second.Shape && shapeSizeMatches &&
               first.Mass == second.Mass && first.Layer == second.Layer && first.Mask == second.Mask &&
               first.Trigger == second.Trigger && first.Continuous == second.Continuous &&
               first.Friction == second.Friction && first.Restitution == second.Restitution &&
               first.FrictionCombine == second.FrictionCombine &&
               first.RestitutionCombine == second.RestitutionCombine &&
               SameCollision(first.Collision, second.Collision);
    }

    std::optional<PhysicsBodyDefinition> SceneRuntimeSession::Impl::BuildPhysicsDefinition(const Entity& entity,
                                                                                           PhysicsRuntimeState& state)
    {
        const auto collider = entity.GetComponent<ColliderComponent>();
        const auto character = entity.GetComponent<CharacterControllerComponent>();
        const auto transform = entity.GetComponent<TransformComponent>();
        const bool useCharacter = character && character->Enabled();
        if ((!collider && !useCharacter) || !transform || (!useCharacter && !collider->Enabled()) ||
            !entity.ActiveInHierarchy())
            return std::nullopt;
        auto rigidBody = entity.GetComponent<RigidBodyComponent>();
        if (rigidBody && !rigidBody->Enabled())
            rigidBody.Reset();

        Vector3 worldPosition;
        Quaternion worldRotation;
        Vector3 worldScale;
        // Keep shape orientation independent of absolute translation, including distant parent cancellation.
        const auto precisePosition = transform->WorldPositionPrecise();
        const auto linear = transform->WorldRelativeMatrix(precisePosition, 0.0);
        if (!Math::DecomposeTransform(linear, worldPosition, worldRotation, worldScale))
            throw std::runtime_error("Physics body Transform cannot be decomposed.");
        const Vector3 absoluteScale{std::abs(worldScale.X), std::abs(worldScale.Y), std::abs(worldScale.Z)};

        PhysicsBodyDefinition definition;
        definition.Motion =
            useCharacter ? PhysicsMotionType::Kinematic : (rigidBody ? rigidBody->Motion() : PhysicsMotionType::Static);
        definition.Shape = useCharacter ? ColliderShape::Capsule : collider->Shape();
        const auto centerOffset = useCharacter ? Vector3{} : Math::TransformDirection(linear, collider->Center());
        definition.Position = PhysicsWorldService->ToLocalPosition({precisePosition.X + centerOffset.X,
                                                                    precisePosition.Y + centerOffset.Y,
                                                                    precisePosition.Z + centerOffset.Z},
                                                                   MaximumPhysicsPositionErrorMetres);
        definition.Rotation = worldRotation;
        definition.LinearVelocity = rigidBody ? rigidBody->LinearVelocity() : Vector3{};
        definition.HalfExtent =
            useCharacter
                ? Vector3{character->Radius() * absoluteScale.X, character->Height() * absoluteScale.Y * 0.5F,
                          character->Radius() * absoluteScale.Z}
                : Vector3{collider->HalfExtent().X * absoluteScale.X, collider->HalfExtent().Y * absoluteScale.Y,
                          collider->HalfExtent().Z * absoluteScale.Z};
        definition.Radius = useCharacter
                                ? character->Radius() * std::max(absoluteScale.X, absoluteScale.Z)
                                : collider->Radius() * std::max({absoluteScale.X, absoluteScale.Y, absoluteScale.Z});
        definition.Height = (useCharacter ? character->Height() : collider->Height()) * absoluteScale.Y;
        definition.Mass = rigidBody ? rigidBody->Mass() : 1.0F;
        definition.Layer = EntityLayerBit(entity.Layer());
        definition.Mask = useCharacter ? character->Mask() : collider->Mask();
        definition.Trigger = !useCharacter && collider->Trigger();
        definition.Continuous = !useCharacter && rigidBody && rigidBody->Continuous();
        definition.UseGravity = !useCharacter && rigidBody && rigidBody->UseGravity();
        definition.MotionSettings = !useCharacter && rigidBody ? rigidBody->MotionSettings() : PhysicsMotionSettings{};

        if (useCharacter)
        {
            state.Material = {};
            state.MaterialHandle = {};
            state.MaterialRevision = 0;
            state.Mesh = {};
            state.MeshHandle = {};
            state.MeshRevision = 0;
            state.CookedCollision.reset();
            state.ColliderCenter = {};
            state.WorldScale = absoluteScale;
            return definition;
        }

        if (state.Material != collider->PhysicsMaterial())
        {
            state.Material = collider->PhysicsMaterial();
            state.MaterialHandle = {};
            state.MaterialRevision = 0;
            if (state.Material && Assets)
                state.MaterialHandle = Assets->Load<PhysicsMaterialAsset>(state.Material, AssetPriority::High);
        }
        if (state.Material)
        {
            const auto material = state.MaterialHandle.TryGetLoaded();
            if (!material)
                return std::nullopt;
            const auto& value = material->Definition();
            definition.Friction = value.Friction;
            definition.Restitution = value.Restitution;
            definition.FrictionCombine = value.FrictionCombine;
            definition.RestitutionCombine = value.RestitutionCombine;
            state.MaterialRevision = state.MaterialHandle.Revision();
        }

        const bool meshShape =
            definition.Shape == ColliderShape::ConvexMesh || definition.Shape == ColliderShape::TriangleMesh;
        if (state.Mesh != collider->CollisionMesh())
        {
            state.Mesh = collider->CollisionMesh();
            state.MeshHandle = {};
            state.MeshRevision = 0;
            state.CookedCollision.reset();
            if (state.Mesh && Assets)
                state.MeshHandle = Assets->Load<MeshAsset>(state.Mesh, AssetPriority::High);
        }
        if (meshShape)
        {
            if (!state.Mesh)
                throw std::runtime_error("Mesh collider requires a collision Mesh asset.");
            const auto mesh = state.MeshHandle.TryGetLoaded();
            if (!mesh)
                return std::nullopt;
            const auto revision = state.MeshHandle.Revision();
            if (!state.CookedCollision || state.MeshRevision != revision || state.CookedScale != absoluteScale)
            {
                CollisionCookInput input;
                input.Kind = definition.Shape == ColliderShape::ConvexMesh ? CollisionMeshKind::Convex
                                                                           : CollisionMeshKind::Triangle;
                input.Vertices.reserve(mesh->Vertices().size());
                for (const auto& vertex : mesh->Vertices())
                    input.Vertices.push_back({vertex.Position.X * absoluteScale.X, vertex.Position.Y * absoluteScale.Y,
                                              vertex.Position.Z * absoluteScale.Z});
                input.Indices.assign(mesh->Indices().begin(), mesh->Indices().end());
                state.CookedCollision = CookCollisionMesh(std::move(input));
                state.MeshRevision = revision;
                state.CookedScale = absoluteScale;
            }
            definition.Collision = state.CookedCollision;
        }
        state.ColliderCenter = collider->Center();
        state.WorldScale = absoluteScale;
        return definition;
    }

    void SceneRuntimeSession::Impl::InitializePhysics()
    {
        ClearPhysics();
        PublishWaterSimulationTime();
        if (!PhysicsService || !Runtime)
            return;
        PhysicsWorldService = PhysicsService->CreateWorld(InitialPhysicsOrigin(*Runtime));
        SynchronizePhysicsBodies();
        CapturePhysicsPresentationSamples();
    }

    void SceneRuntimeSession::Impl::SynchronizePhysicsQueries()
    {
        if (PhysicsWorldService && Runtime && PhysicsQueryRevision != Runtime->PhysicsRevision())
            SynchronizePhysicsBodies(true);
    }

    void SceneRuntimeSession::Impl::SynchronizePhysicsBodies(const bool queryOnly)
    {
        if (!PhysicsWorldService || !Runtime)
            return;
        const auto revision = Runtime->PhysicsRevision();
        std::set<EntityId> candidates;
        for (const auto& entity : Runtime->Query<ColliderComponent>())
            candidates.emplace(entity.Id());
        for (const auto& entity : Runtime->Query<CharacterControllerComponent>())
            candidates.emplace(entity.Id());
        struct PreparedBody
        {
            EntityId Entity;
            PhysicsRuntimeState State;
            std::optional<PhysicsBodyDefinition> Definition;
            PhysicsBodyId Replacement;
        };
        std::vector<PreparedBody> prepared;
        prepared.reserve(candidates.size());
        std::set<EntityId> seen;
        for (const auto entityId : candidates)
        {
            const auto entity = Runtime->FindEntity(entityId);
            seen.emplace(entityId);
            const auto rigid = entity.GetComponent<RigidBodyComponent>();
            const auto character = entity.GetComponent<CharacterControllerComponent>();
            const auto collider = entity.GetComponent<ColliderComponent>();
            const bool characterEnabled = character && character->Enabled();
            const bool eligible = entity.ActiveInHierarchy() && entity.GetComponent<TransformComponent>() &&
                                  (characterEnabled || (collider && collider->Enabled()));
            if (queryOnly && eligible &&
                (characterEnabled || (rigid && rigid->Enabled() && rigid->Motion() == PhysicsMotionType::Dynamic)))
            {
                continue;
            }
            const auto existing = PhysicsBodies.find(entityId);
            auto candidateState = existing == PhysicsBodies.end() ? PhysicsRuntimeState{} : existing->second;
            auto definition = BuildPhysicsDefinition(entity, candidateState);
            prepared.push_back({entityId, std::move(candidateState), std::move(definition)});
        }
        // Allocate only newly needed map nodes before native installation. Existing scene-sized
        // maps must not be copied on every tick merely to make replacement allocation reversible.
        std::map<EntityId, PhysicsRuntimeState> newEntries;
        for (const auto& candidate : prepared)
        {
            if (!PhysicsBodies.contains(candidate.Entity))
            {
                newEntries.try_emplace(candidate.Entity);
            }
        }
        struct ReplacementBatch final
        {
            PhysicsWorld& World;
            std::vector<PreparedBody>& Candidates;

            ~ReplacementBatch() noexcept
            {
                for (const auto& candidate : Candidates)
                {
                    if (!candidate.Replacement)
                    {
                        continue;
                    }
                    try
                    {
                        World.DestroyBody(candidate.Replacement);
                    }
                    catch (...)
                    {
                        // Preserve the original installation failure during cleanup.
                    }
                }
            }
        } replacements{*PhysicsWorldService, prepared};
        for (auto& candidate : prepared)
        {
            const auto& state = candidate.State;
            const auto& definition = candidate.Definition;
            if (!definition ||
                (state.Body && state.HasDefinition && SamePhysicsDefinition(state.Definition, *definition)))
            {
                continue;
            }
            candidate.Replacement = Detail::PhysicsFrameAccess::CreateFrameBody(*PhysicsWorldService, *definition);
            if (FrameFollowing && definition->Motion == PhysicsMotionType::Static)
            {
                const std::array support{candidate.Replacement};
                Detail::PhysicsFrameAccess::SetFrameKinematics(*PhysicsWorldService, support, true);
            }
        }
        std::vector<Detail::AuthoredPhysicsBodyUpdate> updates;
        updates.reserve(prepared.size());
        for (const auto& [entity, state] : PhysicsBodies)
        {
            (void)entity;
            if (state.Body && !PhysicsWorldService->TryGetBody(state.Body))
            {
                throw std::runtime_error("Authored physics batch lost an installed body before commit.");
            }
        }
        for (const auto& candidate : prepared)
        {
            const auto& state = candidate.State;
            const auto& definition = candidate.Definition;
            if (!definition || candidate.Replacement)
            {
                continue;
            }
            Detail::AuthoredPhysicsBodyUpdate update;
            update.Body = state.Body;
            const bool changedPose = definition->Motion != PhysicsMotionType::Dynamic &&
                                     (state.Definition.Position != definition->Position ||
                                      state.Definition.Rotation != definition->Rotation);
            const bool changedVelocity = definition->Motion != PhysicsMotionType::Static &&
                                         state.Definition.LinearVelocity != definition->LinearVelocity;
            if (changedPose || changedVelocity)
            {
                update.State = PhysicsWorldService->TryGetBody(state.Body);
                if (!update.State)
                {
                    throw std::runtime_error("Authored physics batch could not capture its current body state.");
                }
                if (changedPose)
                {
                    update.State->Position = definition->Position;
                    update.State->Rotation = definition->Rotation;
                    if (definition->Motion == PhysicsMotionType::Kinematic)
                    {
                        update.State->Sleeping = false;
                    }
                }
                if (changedVelocity)
                {
                    update.State->LinearVelocity = definition->LinearVelocity;
                    update.State->Sleeping = false;
                }
            }
            if (state.Definition.MotionSettings != definition->MotionSettings)
            {
                update.MotionSettings = definition->MotionSettings;
            }
            if (state.Definition.UseGravity != definition->UseGravity)
            {
                update.UseGravity = definition->UseGravity;
            }
            if (update.State || update.MotionSettings || update.UseGravity)
            {
                updates.push_back(std::move(update));
            }
        }
        Detail::PhysicsFrameAccess::ApplyAuthoredBodyUpdates(*PhysicsWorldService, updates);
        // Node transfer uses the same allocator and performs no map-node allocation.
        PhysicsBodies.merge(newEntries);
        // No body is removed until every replacement has been created successfully.
        for (auto& candidate : prepared)
        {
            const auto entityId = candidate.Entity;
            auto& state = PhysicsBodies.at(entityId);
            state = std::move(candidate.State);
            const auto& definition = candidate.Definition;
            if (!definition)
            {
                if (state.Body)
                {
                    PhysicsWorldService->DestroyBody(state.Body);
                    state.Body = {};
                }
                state.HasDefinition = false;
                state.CharacterSupport = {};
                continue;
            }
            if (!state.Body || !state.HasDefinition || !SamePhysicsDefinition(state.Definition, *definition))
            {
                if (state.Body)
                {
                    PhysicsWorldService->DestroyBody(state.Body);
                }
                state.Body = candidate.Replacement;
                candidate.Replacement = {};
                state.Definition = *definition;
                state.CharacterSupport = {};
                state.CharacterRequestedVerticalDisplacement = 0.0F;
                state.CharacterMissedWalkableFrames = 0;
                ++state.Generation;
                if (state.Generation == 0)
                    state.Generation = 1;
            }
            state.Definition.MotionSettings = definition->MotionSettings;
            state.Definition.LinearVelocity = definition->LinearVelocity;
            state.Definition.UseGravity = definition->UseGravity;
            // Keep the installed shape dimensions as the comparison baseline. Tiny authored scale increments
            // must eventually rebuild once their cumulative change exceeds the round-off tolerance.
            state.Definition.Position = definition->Position;
            state.Definition.Rotation = definition->Rotation;
            state.HasDefinition = true;
        }
        for (auto iterator = PhysicsBodies.begin(); iterator != PhysicsBodies.end();)
        {
            if (!seen.contains(iterator->first))
            {
                if (iterator->second.Body)
                    PhysicsWorldService->DestroyBody(iterator->second.Body);
                iterator = PhysicsBodies.erase(iterator);
            }
            else
                ++iterator;
        }
        PhysicsQueryRevision = revision;
    }

    void SceneRuntimeSession::Impl::MoveTransformInWorld(TransformComponent& transform, const Vector3 displacement)
    {
        const auto position = transform.WorldPositionPrecise();
        transform.SetWorldPositionPrecise(
            {position.X + displacement.X, position.Y + displacement.Y, position.Z + displacement.Z});
    }

    std::vector<SceneRuntimeSession::Impl::PreparedCharacterMovement>
    SceneRuntimeSession::Impl::PrepareCharacterMovement(const float deltaSeconds)
    {
        std::vector<PreparedCharacterMovement> prepared;
        for (const auto& entity : Runtime->Query<CharacterControllerComponent>())
        {
            const auto character = entity.GetComponent<CharacterControllerComponent>();
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!character || !character->Enabled() || !entity.ActiveInHierarchy() || !transform)
                continue;
            const auto input = Detail::CharacterMovementAccess::Capture(*character);
            const auto displacement = input.Displacement;
            auto runtimeState = PhysicsBodies.find(entity.Id());
            if (runtimeState == PhysicsBodies.end() || !runtimeState->second.Body ||
                !runtimeState->second.HasDefinition)
            {
                continue;
            }
            auto state = runtimeState->second;
            Vector3 start;
            Quaternion rotation;
            Vector3 scale;
            if (!Math::DecomposeTransform(PhysicsRelativeTransform(*transform, *PhysicsWorldService), start, rotation,
                                          scale))
                throw std::runtime_error("Character Controller Transform cannot be decomposed.");
            const auto up = CharacterUp(rotation);
            const auto verticalDisplacement = CharacterDot(displacement, up);
            state.CharacterRequestedVerticalDisplacement = verticalDisplacement;

            const auto add = [](const Vector3 left, const Vector3 right) noexcept
            { return Vector3{left.X + right.X, left.Y + right.Y, left.Z + right.Z}; };
            const auto subtract = [](const Vector3 left, const Vector3 right) noexcept
            { return Vector3{left.X - right.X, left.Y - right.Y, left.Z - right.Z}; };
            const auto multiply = [](const Vector3 value, const float scalar) noexcept
            { return Vector3{value.X * scalar, value.Y * scalar, value.Z * scalar}; };
            const auto dot = [](const Vector3 left, const Vector3 right) noexcept
            { return left.X * right.X + left.Y * right.Y + left.Z * right.Z; };
            const auto length = [&](const Vector3 value) noexcept { return std::sqrt(dot(value, value)); };
            const auto hasResolvableDisplacement = [&](const Vector3 value) noexcept
            { return dot(value, value) > std::numeric_limits<float>::epsilon(); };

            const auto slopeNormal = std::cos(character->MaximumSlopeDegrees() * 3.14159265358979323846F / 180.0F);
            const bool wasGrounded =
                character->Grounded() && dot(character->RuntimeState().GroundNormal, up) >= slopeNormal;
            std::optional<Vector3> carryTarget;
            auto& carried = state.CharacterSupport;
            if (carried.Entity)
            {
                const auto support = Runtime->FindEntity(carried.Entity);
                const auto collider = support ? support.GetComponent<ColliderComponent>() : Ref<ColliderComponent>{};
                const auto supportTransform =
                    support ? support.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
                const auto supportState = PhysicsBodies.find(carried.Entity);
                bool eligible = wasGrounded && verticalDisplacement <= 0.0F && carried.Controller.Lock() == character &&
                                entity.Parent().Id() == carried.RiderParent &&
                                transform->PresentationResetRevision() == carried.RiderResetRevision &&
                                length(subtract(start, carried.ResolvedRiderPosition)) < 0.00001F && support &&
                                support.ActiveInHierarchy() && collider && collider->Enabled() &&
                                !collider->Trigger() && carried.Collider.Lock() == collider &&
                                support.Parent().Id() == carried.SupportParent && supportTransform &&
                                supportTransform->PresentationResetRevision() == carried.SupportResetRevision &&
                                supportState != PhysicsBodies.end() && supportState->second.HasDefinition &&
                                supportState->second.Body;
                if (eligible)
                {
                    auto previousDefinition = carried.Definition;
                    auto currentDefinition = supportState->second.Definition;
                    // Static authored motion rebuilds the backend body. Preserve component identity while
                    // still rejecting shape/filter/material changes and actual component replacement.
                    previousDefinition.Position = currentDefinition.Position;
                    previousDefinition.Rotation = currentDefinition.Rotation;
                    eligible = SamePhysicsDefinition(previousDefinition, currentDefinition);
                    if (eligible)
                    {
                        carryTarget = Math::TransformPoint(
                            PhysicsRelativeTransform(*supportTransform, *PhysicsWorldService), carried.LocalAnchor);
                    }
                }
                if (!eligible)
                    carried = {};
            }
            const auto padding = std::min(character->SkinWidth(), state.Definition.Radius * 0.5F);
            const auto castRadius = state.Definition.Radius - padding;
            const auto castHeight = state.Definition.Height - padding * 2.0F;
            Vector3 current = start;
            std::vector<Vector3> movementPath{start};
            const auto advance = [&](const Vector3 next)
            {
                if (next != current)
                {
                    movementPath.push_back(next);
                    current = next;
                }
            };
            const auto cast = [&](const Vector3 origin, const Vector3 movement) -> std::optional<PhysicsQueryHit>
            {
                if (!hasResolvableDisplacement(movement))
                    return std::nullopt;
                return PhysicsWorldService->CastCapsule(
                    {.Origin = origin,
                     .Rotation = rotation,
                     .Radius = castRadius,
                     .Height = castHeight,
                     // The query shape is inset by the skin. Sweep far enough to
                     // preserve that clearance even for sub-skin movement steps.
                     .Displacement = multiply(movement, 1.0F + padding / length(movement)),
                     .Mask = character->Mask(),
                     .IncludeTriggers = false,
                     .Layer = character->Layer(),
                     .IgnoreBody = state.Body});
            };

            // A spawn or teleport can put the inset capsule on or slightly inside
            // the floor. Its zero-distance horizontal sweep then repeatedly hits
            // that same floor without making progress. Recover only shallow,
            // walkable contact, with an unobstructed upward path and a bounded
            // downward probe; this must not become an extra step or ceiling bypass.
            const auto recoveryHeight = std::max(0.002F, padding * 2.0F);
            if (const auto support = cast(current, multiply(up, -recoveryHeight));
                support && support->Distance < padding - 0.0001F && dot(support->Normal, up) >= slopeNormal)
            {
                const auto upward = multiply(up, recoveryHeight);
                if (!cast(current, upward))
                {
                    const auto elevated = add(current, upward);
                    if (const auto landing = cast(elevated, multiply(up, -recoveryHeight));
                        landing && dot(landing->Normal, up) >= slopeNormal && landing->Distance >= padding)
                    {
                        const auto correction = recoveryHeight - (landing->Distance - padding);
                        if (correction > 0.0001F && correction <= recoveryHeight)
                            advance(add(current, multiply(up, correction)));
                    }
                }
            }
            const auto moveAndSlide = [&](Vector3 movement, const bool slideAlongSurface)
            {
                for (std::size_t iteration = 0; iteration < 4; ++iteration)
                {
                    if (!hasResolvableDisplacement(movement))
                        break;
                    const auto movementLength = length(movement);
                    const auto hit = cast(current, movement);
                    if (!hit)
                    {
                        advance(add(current, movement));
                        break;
                    }
                    const auto safeDistance = std::max(0.0F, hit->Distance - padding);
                    const auto safeFraction = std::clamp(safeDistance / movementLength, 0.0F, 1.0F);
                    advance(add(current, multiply(movement, safeFraction)));
                    movement = multiply(movement, 1.0F - safeFraction);
                    movement = Detail::ResolveCharacterCollisionRemainder(movement, hit->Normal, slideAlongSurface);
                }
            };

            // Query order is parent-first. Inherit the nearest planned ancestor's accepted
            // world translation before resolving this controller's own commands.
            for (auto ancestor = entity.Parent(); ancestor; ancestor = ancestor.Parent())
            {
                const auto parentPlan = std::ranges::find(prepared, ancestor.Id(), &PreparedCharacterMovement::Entity);
                if (parentPlan == prepared.end())
                {
                    continue;
                }
                const auto inherited = Math::RelativeFloat(parentPlan->WorldPosition, parentPlan->OriginalWorldPosition,
                                                           MaximumPhysicsPositionErrorMetres);
                moveAndSlide(inherited, true);
                break;
            }

            // Carry is collision-resolved separately from caller input. Recovery may already have
            // supplied upward support motion, so sweep toward the anchor rather than adding it twice.
            if (carryTarget)
                moveAndSlide(subtract(*carryTarget, current), true);
            const auto horizontal = subtract(displacement, multiply(up, verticalDisplacement));
            bool stepped = false;
            if (wasGrounded && character->StepHeight() > 0.0F && hasResolvableDisplacement(horizontal))
            {
                const auto obstruction = cast(current, horizontal);
                if (obstruction && dot(obstruction->Normal, up) < slopeNormal)
                {
                    const auto upwardDistance = character->StepHeight() + padding;
                    const auto upward = multiply(up, upwardDistance);
                    if (!cast(current, upward))
                    {
                        const auto elevated = add(current, upward);
                        if (!cast(elevated, horizontal))
                        {
                            const auto forward = add(elevated, horizontal);
                            const auto downward = multiply(up, -(upwardDistance + padding + 0.05F));
                            const auto landing = cast(forward, downward);
                            if (landing)
                            {
                                const auto normal = CharacterSupportNormal(*PhysicsWorldService, *landing, slopeNormal,
                                                                           character->Mask(), character->Layer(), up);
                                const auto foot = subtract(current, multiply(up, state.Definition.Height * 0.5F));
                                const auto rise = dot(subtract(landing->Position, foot), up);
                                if (dot(normal, up) >= slopeNormal && rise >= -0.0001F &&
                                    rise <= character->StepHeight() + 0.0001F)
                                {
                                    const auto downDistance = std::max(0.0F, landing->Distance - padding);
                                    advance(elevated);
                                    advance(forward);
                                    advance(add(forward, multiply(up, -downDistance)));
                                    stepped = true;
                                }
                            }
                        }
                    }
                }
            }
            if (!stepped)
                moveAndSlide(horizontal, true);
            moveAndSlide(multiply(up, verticalDisplacement), false);

            if (wasGrounded && verticalDisplacement <= 0.0F)
            {
                const auto snapDistance = character->StepHeight() + padding + 0.05F;
                const auto landing = cast(current, multiply(up, -snapDistance));
                if (landing &&
                    Detail::ShouldSnapCharacterToGround(true, verticalDisplacement, landing->Normal, slopeNormal, up))
                {
                    const auto downDistance = std::max(0.0F, landing->Distance - padding);
                    advance(add(current, multiply(up, -downDistance)));
                }
            }

            const auto applied = subtract(current, start);
            state.CharacterVelocity = deltaSeconds > 0.0F ? multiply(applied, 1.0F / deltaSeconds) : Vector3{};
            const auto worldPosition = transform->WorldPositionPrecise();
            prepared.push_back({entity.Id(),
                                character,
                                transform,
                                std::move(state),
                                input,
                                current,
                                rotation,
                                {worldPosition.X + applied.X, worldPosition.Y + applied.Y, worldPosition.Z + applied.Z},
                                applied != Vector3{},
                                Runtime->PhysicsRevision(),
                                worldPosition,
                                transform->WorldRotation(),
                                transform->PresentationResetRevision(),
                                std::move(movementPath)});
        }
        return prepared;
    }

    void
    SceneRuntimeSession::Impl::ResolveCharacterMovementPaths(const std::span<PreparedCharacterMovement> prepared) const
    {
        std::size_t remainingQueries = 4095;
        const auto pathsAreClear =
            [&](const PreparedCharacterMovement& character, const PreparedCharacterMovement& obstacle)
        {
            for (std::size_t segment = 1; segment < character.Path.size(); ++segment)
            {
                PhysicsCapsuleOverlapQuery capsule;
                capsule.Center = character.Path[segment - 1];
                capsule.Rotation = character.Rotation;
                const float padding =
                    std::min(character.Controller->SkinWidth(), character.State.Definition.Radius * 0.5F);
                capsule.Radius = character.State.Definition.Radius - padding;
                capsule.Height = character.State.Definition.Height - padding * 2.0F;
                capsule.Mask = character.Controller->Mask();
                capsule.Layer = character.Controller->Layer();
                capsule.IgnoreBody = character.State.Body;
                const auto path = Detail::MakePlanetCharacterKinematicPath(capsule, capsule.Center,
                                                                           character.Path[segment], capsule.Rotation);
                // Test every obstacle segment independently: relative scheduling of controller
                // commands is unspecified, so clearance must hold throughout both swept regions.
                for (std::size_t otherSegment = 1; otherSegment < obstacle.Path.size(); ++otherSegment)
                {
                    PhysicsCapsuleOverlapQuery otherPose;
                    otherPose.Center = obstacle.Path[otherSegment - 1];
                    otherPose.Rotation = obstacle.Rotation;
                    const auto otherPath = Detail::MakePlanetCharacterKinematicPath(
                        otherPose, otherPose.Center, obstacle.Path[otherSegment], otherPose.Rotation);
                    struct PairInterval
                    {
                        double FirstBegin;
                        double FirstEnd;
                        double SecondBegin;
                        double SecondEnd;
                        std::size_t Depth;
                    };
                    std::array<PairInterval, 17> pending{};
                    pending[0] = {0.0, 1.0, 0.0, 1.0, 0};
                    std::size_t count = 1;
                    while (count != 0)
                    {
                        const auto interval = pending[--count];
                        const auto firstBounds = path(interval.FirstBegin, interval.FirstEnd);
                        const auto secondBounds = otherPath(interval.SecondBegin, interval.SecondEnd);
                        const auto relative = Detail::PlanetCharacterRelativeInterval(
                            firstBounds, secondBounds, obstacle.Path.front(), obstacle.Rotation);
                        const auto envelope = Detail::PlanetCharacterPoseEnvelope(
                            relative.Midpoint, relative.AngularExcursion, relative.CenterExcursion);
                        if (remainingQueries == 0)
                        {
                            return false;
                        }
                        --remainingQueries;
                        if (!Detail::PhysicsFrameAccess::HasCapsuleContact(*PhysicsWorldService, envelope,
                                                                           obstacle.State.Body, {}))
                        {
                            continue;
                        }
                        if (interval.Depth == 16)
                        {
                            return false;
                        }
                        auto left = interval;
                        auto right = interval;
                        ++left.Depth;
                        ++right.Depth;
                        if (firstBounds.CenterExcursion >= secondBounds.CenterExcursion)
                        {
                            const double midpoint = (interval.FirstBegin + interval.FirstEnd) * 0.5;
                            left.FirstEnd = midpoint;
                            right.FirstBegin = midpoint;
                        }
                        else
                        {
                            const double midpoint = (interval.SecondBegin + interval.SecondEnd) * 0.5;
                            left.SecondEnd = midpoint;
                            right.SecondBegin = midpoint;
                        }
                        pending[count++] = right;
                        pending[count++] = left;
                    }
                }
            }
            return true;
        };
        const auto blockMovement = [&](PreparedCharacterMovement& movement)
        {
            const auto& original = PhysicsBodies.at(movement.Entity);
            movement.Position = movement.Path.front();
            movement.WorldPosition = movement.OriginalWorldPosition;
            movement.Moved = false;
            movement.State.CharacterVelocity = {};
            movement.State.CharacterSupport = original.CharacterSupport;
            movement.State.CharacterRequestedVerticalDisplacement = 0.0F;
            movement.Path.resize(1);
        };

        const auto blockHierarchy = [&](const EntityId root)
        {
            for (auto& movement : prepared)
            {
                for (auto entity = Runtime->FindEntity(movement.Entity); entity; entity = entity.Parent())
                {
                    if (entity.Id() == root)
                    {
                        blockMovement(movement);
                        break;
                    }
                }
            }
        };

        for (std::size_t first = 0; first < prepared.size(); ++first)
        {
            auto& character = prepared[first];
            for (std::size_t second = first + 1; second < prepared.size(); ++second)
            {
                auto& obstacle = prepared[second];
                if ((character.Controller->Mask() & obstacle.Controller->Layer()) == 0 ||
                    (obstacle.Controller->Mask() & character.Controller->Layer()) == 0)
                {
                    continue;
                }
                if (!pathsAreClear(character, obstacle))
                {
                    // Each plan already cleared every other character's original native pose.
                    // Blocking a pair therefore cannot invalidate any previously accepted path.
                    // The normal commit consumes attempted input without faulting the scene.
                    blockHierarchy(character.Entity);
                    blockHierarchy(obstacle.Entity);
                }
            }
        }
    }

    void SceneRuntimeSession::Impl::CommitCharacterMovement(
        const std::span<const PreparedCharacterMovement> prepared,
        const std::span<const Detail::AuthoredPhysicsBodyUpdate> frameTargets)
    {
        std::vector<Vector3d> localPositions;
        std::vector<Detail::AuthoredPhysicsBodyUpdate> nativeUpdates;
        localPositions.reserve(prepared.size());
        nativeUpdates.reserve(prepared.size() + frameTargets.size());
        for (const auto& movement : prepared)
        {
            Detail::CharacterMovementAccess::Validate(*movement.Controller, movement.Input);
            const auto entity = Runtime->FindEntity(movement.Entity);
            if (Runtime->PhysicsRevision() != movement.ScenePhysicsRevision || !entity ||
                entity.GetComponent<CharacterControllerComponent>() != movement.Controller ||
                entity.GetComponent<TransformComponent>() != movement.Transform ||
                movement.Transform->WorldPositionPrecise() != movement.OriginalWorldPosition ||
                movement.Transform->WorldRotation() != movement.OriginalRotation ||
                movement.Transform->PresentationResetRevision() != movement.OriginalPresentationRevision)
            {
                throw std::logic_error("Prepared character authoring changed before commit.");
            }
            const auto live = PhysicsBodies.find(movement.Entity);
            if (live == PhysicsBodies.end() || live->second.Body != movement.State.Body ||
                live->second.Generation != movement.State.Generation ||
                !PhysicsWorldService->TryGetBody(movement.State.Body))
            {
                throw std::logic_error("Prepared character movement body changed before commit.");
            }
            if (live->second.Definition.Motion != PhysicsMotionType::Kinematic)
            {
                throw std::logic_error("Prepared character body is no longer kinematic.");
            }
            const auto parent = movement.Transform->Parent();
            const auto parentTransform = parent ? parent.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
            auto parentRelativeTarget = movement.WorldPosition;
            for (auto ancestor = parent; ancestor; ancestor = ancestor.Parent())
            {
                const auto parentPlan = std::ranges::find(prepared, ancestor.Id(), &PreparedCharacterMovement::Entity);
                if (parentPlan != prepared.end())
                {
                    parentRelativeTarget.X -= parentPlan->WorldPosition.X - parentPlan->OriginalWorldPosition.X;
                    parentRelativeTarget.Y -= parentPlan->WorldPosition.Y - parentPlan->OriginalWorldPosition.Y;
                    parentRelativeTarget.Z -= parentPlan->WorldPosition.Z - parentPlan->OriginalWorldPosition.Z;
                    break;
                }
            }
            const auto localPosition = parentTransform
                                           ? parentTransform->InverseTransformPointPrecise(parentRelativeTarget)
                                           : parentRelativeTarget;
            if (!Math::IsFinite(localPosition))
            {
                throw std::invalid_argument("Prepared character parent-space position is not finite.");
            }
            (void)Math::Normalize(movement.Rotation);
            if (!Math::IsFinite(movement.Position) || !Math::IsFinite(movement.Rotation))
            {
                throw std::invalid_argument("Prepared character pose is not finite.");
            }
            localPositions.push_back(localPosition);
            if (movement.Moved)
            {
                auto native = PhysicsWorldService->TryGetBody(movement.State.Body);
                if (!native)
                {
                    throw std::logic_error("Prepared character native pose disappeared before commit.");
                }
                native->Position = movement.Position;
                native->Rotation = movement.Rotation;
                native->Sleeping = false;
                nativeUpdates.push_back({movement.State.Body, native, {}, {}});
            }
        }
        // A character's accepted pose is the start of its frame motion. Merge the target
        // into that same update so native velocity validation sees the staged start pose.
        for (const auto& target : frameTargets)
        {
            if (!target.KinematicTarget || target.State || target.MotionSettings || target.UseGravity)
            {
                throw std::invalid_argument("Frame movement requires target-only body updates.");
            }
            const auto existing =
                std::ranges::find(nativeUpdates, target.Body, &Detail::AuthoredPhysicsBodyUpdate::Body);
            if (existing == nativeUpdates.end())
            {
                nativeUpdates.push_back(target);
                continue;
            }
            if (existing->KinematicTarget)
            {
                throw std::invalid_argument("Frame movement contains duplicate body targets.");
            }
            existing->KinematicTarget = target.KinematicTarget;
        }
        std::vector<Vector3> resolvedVelocities;
        resolvedVelocities.reserve(prepared.size());
        for (const auto& movement : prepared)
        {
            auto velocity = movement.State.CharacterVelocity;
            const auto update =
                std::ranges::find(nativeUpdates, movement.State.Body, &Detail::AuthoredPhysicsBodyUpdate::Body);
            if (update != nativeUpdates.end() && update->KinematicTarget)
            {
                const auto& target = *update->KinematicTarget;
                if (!std::isfinite(target.DeltaSeconds) || target.DeltaSeconds <= 0.0F)
                {
                    throw std::invalid_argument("Character frame movement requires a positive finite duration.");
                }
                // Use physics-local coordinates so distant world origins do not erase the
                // carried displacement. Gameplay observes both accepted input and frame motion.
                velocity.X += (target.Position.X - movement.Position.X) / target.DeltaSeconds;
                velocity.Y += (target.Position.Y - movement.Position.Y) / target.DeltaSeconds;
                velocity.Z += (target.Position.Z - movement.Position.Z) / target.DeltaSeconds;
            }
            if (!Math::IsFinite(velocity))
            {
                throw std::out_of_range("Resolved character velocity is not finite.");
            }
            resolvedVelocities.push_back(velocity);
        }
        // Refresh traversal storage before native writes. Position changes only invalidate cached
        // matrices, not topology, so the subsequent local setters do not allocate hierarchy storage.
        (void)Runtime->Entities();
        Detail::PhysicsFrameAccess::ApplyAuthoredBodyUpdates(*PhysicsWorldService, nativeUpdates);
        std::size_t index = 0;
        for (const auto& movement : prepared)
        {
            auto& state = PhysicsBodies.at(movement.Entity);
            movement.Transform->SetLocalPositionPrecise(localPositions[index++]);
            if (movement.Moved)
            {
                state.Definition.Position = movement.Position;
                state.Definition.Rotation = movement.Rotation;
            }
            state.CharacterVelocity = resolvedVelocities[index - 1];
            state.CharacterSupport = movement.State.CharacterSupport;
            state.CharacterRequestedVerticalDisplacement = movement.State.CharacterRequestedVerticalDisplacement;
            Detail::CharacterMovementAccess::Commit(*movement.Controller, movement.Input);
        }
    }

    void SceneRuntimeSession::Impl::UpdateCharacterGrounding()
    {
        constexpr float Pi = 3.14159265358979323846F;
        for (const auto& entity : Runtime->Query<CharacterControllerComponent>())
        {
            const auto character = entity.GetComponent<CharacterControllerComponent>();
            const auto transform = entity.GetComponent<TransformComponent>();
            const auto state = PhysicsBodies.find(entity.Id());
            if (!character || !character->Enabled() || !transform || state == PhysicsBodies.end() ||
                !state->second.Body || state->second.Generation == 0)
            {
                continue;
            }

            Vector3 worldPosition;
            Quaternion worldRotation;
            Vector3 worldScale;
            if (!Math::DecomposeTransform(PhysicsRelativeTransform(*transform, *PhysicsWorldService), worldPosition,
                                          worldRotation, worldScale))
                continue;
            bool hasWalkableHit = false;
            const auto up = CharacterUp(worldRotation);
            Vector3 normal = up;
            const auto minimumNormal = std::cos(character->MaximumSlopeDegrees() * Pi / 180.0F);
            const auto& definition = state->second.Definition;
            const auto padding = std::min(character->SkinWidth(), definition.Radius * 0.5F);
            const auto hit = PhysicsWorldService->CastCapsule(
                {.Origin = worldPosition,
                 .Rotation = worldRotation,
                 .Radius = definition.Radius - padding,
                 .Height = definition.Height - padding * 2.0F,
                 // Step following is handled by movement; airborne characters must reach contact first.
                 .Displacement = {-up.X * (padding + 0.002F), -up.Y * (padding + 0.002F), -up.Z * (padding + 0.002F)},
                 .Mask = character->Mask(),
                 .IncludeTriggers = false,
                 .Layer = character->Layer(),
                 .IgnoreBody = state->second.Body});
            if (hit)
            {
                normal = CharacterSupportNormal(*PhysicsWorldService, *hit, minimumNormal, character->Mask(),
                                                character->Layer(), up);
                hasWalkableHit = CharacterDot(normal, up) >= minimumNormal;
            }
            const auto previous = character->RuntimeState();
            const bool previousSupportWalkable =
                previous.Grounded && CharacterDot(previous.GroundNormal, up) >= minimumNormal;
            const bool grounded = Detail::ResolveCharacterGrounded(hasWalkableHit, previousSupportWalkable,
                                                                   state->second.CharacterRequestedVerticalDisplacement,
                                                                   state->second.CharacterMissedWalkableFrames);
            if (grounded && !hasWalkableHit)
                normal = previous.GroundNormal;
            state->second.CharacterSupport = {};
            if (grounded && hasWalkableHit)
            {
                const auto supportId = EntityForBody(hit->Body);
                const auto support = supportId ? Runtime->FindEntity(*supportId) : Entity{};
                const auto collider = support ? support.GetComponent<ColliderComponent>() : Ref<ColliderComponent>{};
                const auto supportTransform =
                    support ? support.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
                if (collider && collider->Enabled() && !collider->Trigger() && supportTransform)
                {
                    const auto& supportState = PhysicsBodies.at(*supportId);
                    auto& carried = state->second.CharacterSupport;
                    carried.Entity = *supportId;
                    carried.SupportParent = support.Parent().Id();
                    carried.RiderParent = entity.Parent().Id();
                    carried.Collider = collider;
                    carried.Controller = character;
                    carried.Definition = supportState.Definition;
                    // A dynamic support advances after character movement. Keep the movement-time
                    // reference so its just-resolved motion is carried on the next fixed tick.
                    // Frame transport already moves both the rider and its support. Anchoring
                    // against the old support pose would apply that rotation a second time.
                    carried.ReferenceWorld =
                        FrameDrivenKinematics && supportState.Definition.Motion != PhysicsMotionType::Dynamic
                            ? PhysicsRelativeTransform(*supportTransform, *PhysicsWorldService)
                            : supportState.MovementWorld;
                    carried.LocalAnchor = Math::TransformPoint(Math::Inverse(carried.ReferenceWorld), worldPosition);
                    carried.ResolvedRiderPosition = worldPosition;
                    carried.RiderResetRevision = transform->PresentationResetRevision();
                    carried.SupportResetRevision = supportTransform->PresentationResetRevision();
                }
            }
            character->ApplyRuntimeState(state->second.Generation, grounded, normal, state->second.CharacterVelocity);
        }
    }

    std::optional<EntityId> SceneRuntimeSession::Impl::EntityForBody(const PhysicsBodyId body) const noexcept
    {
        const auto found =
            std::ranges::find_if(PhysicsBodies, [body](const auto& item) { return item.second.Body == body; });
        return found == PhysicsBodies.end() ? std::nullopt : std::optional(found->first);
    }

    void SceneRuntimeSession::Impl::PullDynamicBodies()
    {
        for (auto& [entityId, runtime] : PhysicsBodies)
        {
            if (!runtime.Body || runtime.Definition.Motion != PhysicsMotionType::Dynamic)
                continue;
            const auto body = PhysicsWorldService->TryGetBody(runtime.Body);
            const auto entity = body ? Runtime->FindEntity(entityId) : Entity{};
            const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
            if (!body || !transform)
                continue;
            const auto centerTransform = Math::ComposeTransform({}, body->Rotation, runtime.WorldScale);
            const auto centerOffset = Math::TransformDirection(centerTransform, runtime.ColliderCenter);
            const Vector3 origin{body->Position.X - centerOffset.X, body->Position.Y - centerOffset.Y,
                                 body->Position.Z - centerOffset.Z};
            const auto worldPosition = PhysicsWorldService->ToWorldPosition(origin);
            const auto world = Math::ComposeTransform({}, body->Rotation, runtime.WorldScale);
            Matrix4 local = world;
            if (const auto parent = entity.Parent())
            {
                if (const auto parentTransform = parent.GetComponent<TransformComponent>())
                {
                    const auto parentLinear =
                        parentTransform->WorldRelativeMatrix(parentTransform->WorldPositionPrecise(), 0.0);
                    local = Math::Multiply(Math::Inverse(parentLinear), world);
                }
            }
            Vector3 localPosition;
            Quaternion localRotation;
            Vector3 localScale;
            if (!Math::DecomposeTransform(local, localPosition, localRotation, localScale))
                throw std::runtime_error("Dynamic physics body produced a non-decomposable Transform.");
            transform->SetWorldPositionPrecise(worldPosition);
            transform->SetLocalRotation(localRotation);
            if (const auto rigidBody = entity.GetComponent<RigidBodyComponent>();
                rigidBody && rigidBody->LinearVelocity() != body->LinearVelocity)
            {
                rigidBody->SetLinearVelocity(body->LinearVelocity);
            }
            runtime.Definition.Position = body->Position;
            runtime.Definition.Rotation = body->Rotation;
            runtime.Definition.LinearVelocity = body->LinearVelocity;
        }
    }

    void SceneRuntimeSession::Impl::DispatchPhysicsContacts()
    {
        for (const auto& event : PhysicsWorldService->DrainContactEvents())
        {
            const auto first = EntityForBody(event.First);
            const auto second = EntityForBody(event.Second);
            if (!first || !second)
                continue;
            const auto phase = event.Phase == ContactPhase::Enter  ? PhysicsContactPhase::Enter
                               : event.Phase == ContactPhase::Stay ? PhysicsContactPhase::Stay
                                                                   : PhysicsContactPhase::Exit;
            const auto precisePoint = PhysicsWorldService->ToWorldPosition(event.Point);
            const auto legacyPoint = Math::RelativeFloat(precisePoint, {}, std::numeric_limits<double>::max());
            Runtime->DispatchPhysicsContact(
                *first, phase, {*second, legacyPoint, event.Normal, event.Impulse, event.Trigger, precisePoint});
            Runtime->DispatchPhysicsContact(*second, phase,
                                            {*first,
                                             legacyPoint,
                                             {-event.Normal.X, -event.Normal.Y, -event.Normal.Z},
                                             event.Impulse,
                                             event.Trigger,
                                             precisePoint});
        }
    }

    void SceneRuntimeSession::Impl::ValidateFrameMotion(
        const Detail::SceneFrameMotion& motion, const float deltaSeconds,
        const std::span<const PreparedCharacterMovement> preparedCharacters) const
    {
        if (!PhysicsWorldService)
        {
            throw std::logic_error("Frame motion requires an active physics world.");
        }
        const auto* frameMotion = &motion;
        const auto rotation = frameMotion->NextRotation.Compose(frameMotion->PreviousRotation.Inverse());
        const auto movePosition = [&](const Vector3d position)
        {
            const Detail::SectorPosition source(frameMotion->PreviousOrigin.Identity(), {},
                                                {position.X, position.Y, position.Z});
            const auto local = source.RelativeTo(frameMotion->PreviousOrigin);
            const auto target = frameMotion->NextOrigin.Translated(rotation.Apply(local));
            const Detail::SectorPosition root(target.Identity(), {}, {});
            const auto value = target.RelativeTo(root);
            for (const auto error : root.Translated(value).RelativeTo(target))
            {
                if (std::abs(error) > MaximumPhysicsPositionErrorMetres)
                {
                    throw std::out_of_range("Frame motion exceeds root-coordinate precision.");
                }
            }
            return Vector3d{value[0], value[1], value[2]};
        };
        const auto moveRotation = [&](const Quaternion value)
        {
            const auto target = rotation.Compose(Detail::FrameRotation::Normalize(value.X, value.Y, value.Z, value.W));
            const auto& components = target.Components();
            return Quaternion{static_cast<float>(components[0]), static_cast<float>(components[1]),
                              static_cast<float>(components[2]), static_cast<float>(components[3])};
        };
        for (const auto& entity : Runtime->Entities())
        {
            const auto rigid = entity.GetComponent<RigidBodyComponent>();
            if (entity.Parent() || (rigid && rigid->Enabled() && rigid->Motion() == PhysicsMotionType::Dynamic))
            {
                continue;
            }
            const auto transform = entity.GetComponent<TransformComponent>();
            if (transform)
            {
                (void)movePosition(transform->WorldPositionPrecise());
                (void)moveRotation(transform->WorldRotation());
            }
        }
        std::vector<Detail::PlanetCharacterMovingObstacle> obstacles;
        std::vector<PhysicsBodyId> movingBodies;
        std::vector<Detail::PlanetCharacterFramePath> characters;
        bool hasDynamicBodies = false;
        for (const auto& [entity, body] : PhysicsBodies)
        {
            hasDynamicBodies = hasDynamicBodies || (body.Body && body.Definition.Motion == PhysicsMotionType::Dynamic);
            if (!body.Body || body.Definition.Motion == PhysicsMotionType::Dynamic)
            {
                continue;
            }
            auto current = PhysicsWorldService->TryGetBody(body.Body);
            if (!current)
            {
                throw std::logic_error("Frame preflight lost a kinematic body.");
            }
            PhysicsCapsuleOverlapQuery nativeReference;
            nativeReference.Center = current->Position;
            nativeReference.Rotation = current->Rotation;
            const auto prepared = std::ranges::find(preparedCharacters, entity, &PreparedCharacterMovement::Entity);
            Vector3 stagedOffset;
            if (prepared != preparedCharacters.end())
            {
                stagedOffset = {prepared->Position.X - current->Position.X, prepared->Position.Y - current->Position.Y,
                                prepared->Position.Z - current->Position.Z};
                current->Position = prepared->Position;
                current->Rotation = prepared->Rotation;
            }
            const auto position = movePosition(PhysicsWorldService->ToWorldPosition(current->Position));
            Detail::PhysicsFrameAccess::ValidateKinematicMove(
                *PhysicsWorldService, body.Body,
                PhysicsWorldService->ToLocalPosition(position, MaximumPhysicsPositionErrorMetres),
                moveRotation(current->Rotation), deltaSeconds);
            auto center = Detail::PhysicsFrameAccess::TryGetCenterOfMassPosition(*PhysicsWorldService, body.Body);
            if (!center)
            {
                throw std::logic_error("Frame preflight lost a body center of mass.");
            }
            center->X += stagedOffset.X;
            center->Y += stagedOffset.Y;
            center->Z += stagedOffset.Z;
            const auto targetPosition =
                PhysicsWorldService->ToLocalPosition(position, MaximumPhysicsPositionErrorMetres);
            const auto offset = rotation.Apply({static_cast<double>(center->X) - current->Position.X,
                                                static_cast<double>(center->Y) - current->Position.Y,
                                                static_cast<double>(center->Z) - current->Position.Z});
            const auto targetCenter = Math::RelativeFloat(
                {targetPosition.X + offset[0], targetPosition.Y + offset[1], targetPosition.Z + offset[2]}, {},
                MaximumPhysicsPositionErrorMetres);
            PhysicsCapsuleOverlapQuery pose;
            pose.Center = current->Position;
            pose.Rotation = current->Rotation;
            const auto path =
                Detail::MakePlanetCharacterKinematicPath(pose, *center, targetCenter, moveRotation(current->Rotation));
            obstacles.push_back({body.Body, path, nativeReference});
            movingBodies.push_back(body.Body);
            const auto runtimeEntity = Runtime->FindEntity(entity);
            const auto controller = runtimeEntity ? runtimeEntity.GetComponent<CharacterControllerComponent>()
                                                  : Ref<CharacterControllerComponent>{};
            if (controller && controller->Enabled())
            {
                auto characterPath = path;
                const auto padding = std::min(controller->SkinWidth(), body.Definition.Radius * 0.5F);
                characterPath.Start.Radius = body.Definition.Radius - padding;
                characterPath.Start.Height = body.Definition.Height - padding * 2.0F;
                characterPath.Start.Mask = controller->Mask();
                characterPath.Start.Layer = controller->Layer();
                characterPath.Start.IgnoreBody = body.Body;
                characters.push_back(characterPath);
            }
        }
        if (!characters.empty() && hasDynamicBodies)
        {
            // Development gate: solver impulses/position correction require a separate motion bound.
            throw std::invalid_argument("Character frame preflight has no dynamic-contact motion bound yet.");
        }
        for (const auto& character : characters)
        {
            if (!Detail::PlanetCharacterMovingClearance(*PhysicsWorldService, character, obstacles, movingBodies))
            {
                throw std::runtime_error("Character frame motion has no collision-free swept clearance.");
            }
        }
    }

    void SceneRuntimeSession::Impl::StepPhysics(const float deltaSeconds)
    {
        const double nextWaterTime = WaterTimeSeconds + static_cast<double>(deltaSeconds);
        if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F || deltaSeconds > 1.0F ||
            !std::isfinite(nextWaterTime))
        {
            throw std::invalid_argument("Water simulation requires a finite fixed step in (0, 1].");
        }
        // Revalidate after callbacks, before consuming movement or changing native bodies.
        if (PendingFrameMotion)
        {
            ValidateFrameMotion(*PendingFrameMotion, deltaSeconds);
        }
        const auto frameMotion = PendingFrameMotion;
        if (!PhysicsWorldService)
        {
            if (frameMotion)
            {
                throw std::logic_error("Frame following currently requires an active physics world.");
            }
            WaterTimeSeconds = nextWaterTime;
            PublishWaterSimulationTime();
            return;
        }
        Detail::PhysicsFrameAccess::ValidateStepClock(*PhysicsWorldService, deltaSeconds);
        // Gameplay may move or disable a support this tick. Movement and grounding
        // must query the same current authored geometry, including kinematic targets.
        SynchronizePhysicsBodies();
        for (auto& [id, state] : PhysicsBodies)
        {
            const auto entity = Runtime->FindEntity(id);
            const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
            if (transform)
                state.MovementWorld = PhysicsRelativeTransform(*transform, *PhysicsWorldService);
            const auto character =
                entity ? entity.GetComponent<CharacterControllerComponent>() : Ref<CharacterControllerComponent>{};
            if (!character || !character->Enabled())
                state.CharacterSupport = {};
        }
        auto preparedCharacters = PrepareCharacterMovement(deltaSeconds);
        ResolveCharacterMovementPaths(preparedCharacters);
        if (frameMotion)
        {
            ValidateFrameMotion(*frameMotion, deltaSeconds, preparedCharacters);
        }
        struct RootTarget
        {
            Ref<TransformComponent> Transform;
            Vector3d Position;
            Quaternion Rotation;
        };
        struct BodyTarget
        {
            PhysicsBodyId Body;
            Vector3 Position;
            Quaternion Rotation;
            PhysicsRuntimeState* Runtime = nullptr;
        };
        std::vector<RootTarget> rootTargets;
        std::vector<BodyTarget> bodyTargets;
        if (frameMotion)
        {
            const auto rotation = frameMotion->NextRotation.Compose(frameMotion->PreviousRotation.Inverse());
            const auto movePosition = [&](const Vector3d position)
            {
                const Detail::SectorPosition source(frameMotion->PreviousOrigin.Identity(), {},
                                                    {position.X, position.Y, position.Z});
                const auto local = source.RelativeTo(frameMotion->PreviousOrigin);
                const auto target = frameMotion->NextOrigin.Translated(rotation.Apply(local));
                const Detail::SectorPosition root(target.Identity(), {}, {});
                const auto value = target.RelativeTo(root);
                for (const auto error : root.Translated(value).RelativeTo(target))
                {
                    if (std::abs(error) > MaximumPhysicsPositionErrorMetres)
                    {
                        throw std::out_of_range("Frame motion exceeds root-coordinate precision.");
                    }
                }
                return Vector3d{value[0], value[1], value[2]};
            };
            const auto moveRotation = [&](const Quaternion value)
            {
                const auto target =
                    rotation.Compose(Detail::FrameRotation::Normalize(value.X, value.Y, value.Z, value.W));
                const auto& components = target.Components();
                return Quaternion{static_cast<float>(components[0]), static_cast<float>(components[1]),
                                  static_cast<float>(components[2]), static_cast<float>(components[3])};
            };
            for (const auto& entity : Runtime->Entities())
            {
                const auto collider = entity.GetComponent<ColliderComponent>();
                const auto character = entity.GetComponent<CharacterControllerComponent>();
                const auto rigid = entity.GetComponent<RigidBodyComponent>();
                if (entity.Parent() && ((character && character->Enabled()) || (collider && collider->Enabled())))
                {
                    throw std::invalid_argument("Unsupported physics was added to a frame-following scene.");
                }
                if (entity.Parent() || (rigid && rigid->Enabled() && rigid->Motion() == PhysicsMotionType::Dynamic))
                {
                    continue;
                }
                const auto transform = entity.GetComponent<TransformComponent>();
                if (transform)
                {
                    const auto prepared =
                        std::ranges::find(preparedCharacters, entity.Id(), &PreparedCharacterMovement::Entity);
                    const auto position = prepared == preparedCharacters.end() ? transform->WorldPositionPrecise()
                                                                               : prepared->WorldPosition;
                    const auto targetPosition = movePosition(position);
                    const auto targetRotation = Math::Normalize(moveRotation(transform->WorldRotation()));
                    if (!Math::IsFinite(targetPosition) || !Math::IsFinite(targetRotation))
                    {
                        throw std::out_of_range("Frame motion produced a non-finite root pose.");
                    }
                    rootTargets.push_back({transform, targetPosition, targetRotation});
                }
            }
            for (auto& [entity, body] : PhysicsBodies)
            {
                (void)entity;
                if (!body.Body || body.Definition.Motion == PhysicsMotionType::Dynamic)
                {
                    continue;
                }
                auto current = PhysicsWorldService->TryGetBody(body.Body);
                if (!current)
                {
                    throw std::logic_error("Frame motion lost a kinematic body.");
                }
                const auto prepared = std::ranges::find(preparedCharacters, entity, &PreparedCharacterMovement::Entity);
                if (prepared != preparedCharacters.end())
                {
                    current->Position = prepared->Position;
                    current->Rotation = prepared->Rotation;
                }
                const auto position = movePosition(PhysicsWorldService->ToWorldPosition(current->Position));
                bodyTargets.push_back(
                    {body.Body, PhysicsWorldService->ToLocalPosition(position, MaximumPhysicsPositionErrorMetres),
                     moveRotation(current->Rotation), &body});
            }
            std::vector<Detail::AuthoredPhysicsBodyUpdate> frameTargets;
            frameTargets.reserve(bodyTargets.size());
            for (const auto& target : bodyTargets)
            {
                frameTargets.push_back(
                    {target.Body,
                     {},
                     {},
                     {},
                     Detail::PhysicsKinematicTarget{target.Position, target.Rotation, deltaSeconds}});
            }
            CommitCharacterMovement(preparedCharacters, frameTargets);
            PendingFrameMotion.reset();
            FrameDrivenKinematics = !bodyTargets.empty();
        }
        else
        {
            CommitCharacterMovement(preparedCharacters);
            // Non-frame movement can move descendant colliders through authored parenting.
            SynchronizePhysicsBodies();
            if (FrameDrivenKinematics)
            {
                for (const auto& [entity, body] : PhysicsBodies)
                {
                    (void)entity;
                    if (body.Body && body.Definition.Motion == PhysicsMotionType::Kinematic)
                    {
                        const auto current = PhysicsWorldService->TryGetBody(body.Body);
                        if (current)
                        {
                            Detail::PhysicsFrameAccess::MoveKinematic(
                                *PhysicsWorldService, body.Body, current->Position, current->Rotation, deltaSeconds);
                        }
                    }
                }
                FrameDrivenKinematics = false;
            }
        }
        ApplyWaterForces(deltaSeconds, frameMotion);
        PhysicsWorldService->Step(deltaSeconds);
        WaterTimeSeconds = nextWaterTime;
        PublishWaterSimulationTime();
        for (const auto& target : bodyTargets)
        {
            target.Runtime->Definition.Position = target.Position;
            target.Runtime->Definition.Rotation = target.Rotation;
        }
        for (const auto& target : rootTargets)
        {
            // These targets were collected only for roots, and validated before native writes.
            // No parent-space conversion may be introduced after the physics step.
            target.Transform->SetLocalPositionPrecise(target.Position);
            target.Transform->SetLocalRotation(target.Rotation);
        }
        PullDynamicBodies();
        UpdateCharacterGrounding();
        DispatchPhysicsContacts();
        CapturePhysicsPresentationSamples();
    }

    void SceneRuntimeSession::Impl::CapturePhysicsPresentationSamples()
    {
        if (!Runtime)
            return;
        for (auto& [entityId, state] : PhysicsBodies)
        {
            const auto entity = Runtime->FindEntity(entityId);
            const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
            if (!transform)
                continue;
            const auto position = transform->WorldPositionPrecise();
            const auto current = transform->WorldRelativeMatrix(position, 0.0);
            const auto resetRevision = transform->PresentationResetRevision();
            if (!state.HasPresentationSamples || state.PresentationResetRevision != resetRevision)
            {
                state.PreviousPresentationWorld = current;
                state.CurrentPresentationWorld = current;
                state.PreviousPresentationPosition = position;
                state.CurrentPresentationPosition = position;
                state.PresentationResetRevision = resetRevision;
                state.HasPresentationSamples = true;
            }
            else
            {
                state.PreviousPresentationWorld = state.CurrentPresentationWorld;
                state.CurrentPresentationWorld = current;
                state.PreviousPresentationPosition = state.CurrentPresentationPosition;
                state.CurrentPresentationPosition = position;
            }
        }
    }

    void SceneRuntimeSession::Impl::ApplyPhysicsPresentationInterpolation(const float alpha)
    {
        if (!Runtime)
            return;
        const auto amount = std::clamp(alpha, 0.0F, 1.0F);
        for (auto& [entityId, state] : PhysicsBodies)
        {
            if (!state.HasPresentationSamples)
                continue;
            const auto entity = Runtime->FindEntity(entityId);
            const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
            if (!transform)
                continue;
            const auto character = entity.GetComponent<CharacterControllerComponent>();
            const auto rigidBody = entity.GetComponent<RigidBodyComponent>();
            if (!character && (!rigidBody || rigidBody->Motion() != PhysicsMotionType::Dynamic))
                continue;
            // A teleport must remain immediate even if no fixed tick has captured it yet.
            if (state.PresentationResetRevision != transform->PresentationResetRevision())
                continue;
            const auto previous = state.PreviousPresentationPosition;
            const auto current = state.CurrentPresentationPosition;
            const Vector3d position{previous.X + (current.X - previous.X) * amount,
                                    previous.Y + (current.Y - previous.Y) * amount,
                                    previous.Z + (current.Z - previous.Z) * amount};
            if (character)
            {
                transform->SetRuntimePresentationWorldPositionPrecise(position);
                continue;
            }
            Vector3 previousPosition;
            Vector3 previousScale;
            Quaternion previousRotation;
            Vector3 currentPosition;
            Vector3 currentScale;
            Quaternion currentRotation;
            if (!Math::DecomposeTransform(state.PreviousPresentationWorld, previousPosition, previousRotation,
                                          previousScale) ||
                !Math::DecomposeTransform(state.CurrentPresentationWorld, currentPosition, currentRotation,
                                          currentScale))
            {
                transform->SetRuntimePresentationWorldMatrix(state.CurrentPresentationWorld,
                                                             state.CurrentPresentationPosition);
                continue;
            }
            const Vector3 scale{previousScale.X + (currentScale.X - previousScale.X) * amount,
                                previousScale.Y + (currentScale.Y - previousScale.Y) * amount,
                                previousScale.Z + (currentScale.Z - previousScale.Z) * amount};
            transform->SetRuntimePresentationWorldMatrix(
                Math::ComposeTransform({}, RiggingDetail::Nlerp(previousRotation, currentRotation, amount), scale),
                position);
        }
    }

    void SceneRuntimeSession::Impl::ClearPhysics() noexcept
    {
        PendingFrameMotion.reset();
        FrameDrivenKinematics = false;
        FrameFollowing = false;
        FrameSupportBodies.clear();
        PhysicsBodies.clear();
        PhysicsQueryRevision = 0;
        WaterCaptureEpoch = 0;
        WaterTimeSeconds = 0.0;
        if (PhysicsWorldService)
        {
            try
            {
                PhysicsWorldService->Close();
            }
            catch (...)
            {
            }
            PhysicsWorldService.Reset();
        }
    }
} // namespace Keire
