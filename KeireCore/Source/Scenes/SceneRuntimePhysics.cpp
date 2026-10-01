#include "KeireInternal/Scenes/SceneRuntimeSessionImpl.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Keire
{
    namespace
    {
        // A capsule's rounded bottom reports a contact normal across a box edge, not the top face normal.
        // Only recover the face normal when a tiny local probe confirms the SAME body's surface at that edge.
        [[nodiscard]] Vector3 CharacterSupportNormal(PhysicsWorld& world, const PhysicsQueryHit& hit,
                                                     const float minimumNormal, const std::uint32_t mask,
                                                     const std::uint32_t layer)
        {
            if (hit.Normal.Y >= minimumNormal)
                return hit.Normal;
            const auto planar = std::sqrt(hit.Normal.X * hit.Normal.X + hit.Normal.Z * hit.Normal.Z);
            if (planar <= 0.000001F)
                return hit.Normal;
            constexpr float inset = 0.001F;
            const Vector3 probe{hit.Position.X - hit.Normal.X / planar * inset, hit.Position.Y + 0.01F,
                                hit.Position.Z - hit.Normal.Z / planar * inset};
            const auto surfaces = world.RayCast({.Origin = probe,
                                                 .Direction = {0.0F, -1.0F, 0.0F},
                                                 .MaximumDistance = 0.02F,
                                                 .Mask = mask,
                                                 .IncludeTriggers = false,
                                                 .Layer = layer});
            for (const auto& surface : surfaces)
            {
                if (surface.Body == hit.Body && surface.Normal.Y >= minimumNormal &&
                    std::abs(surface.Position.Y - hit.Position.Y) <= 0.002F)
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
        return first.Motion == second.Motion && first.Shape == second.Shape &&
               first.LinearVelocity == second.LinearVelocity && shapeSizeMatches && first.Mass == second.Mass &&
               first.Layer == second.Layer && first.Mask == second.Mask && first.Trigger == second.Trigger &&
               first.Continuous == second.Continuous && first.UseGravity == second.UseGravity &&
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
        if (!Math::DecomposeTransform(transform->WorldMatrix(), worldPosition, worldRotation, worldScale))
            throw std::runtime_error("Physics body Transform cannot be decomposed.");
        const Vector3 absoluteScale{std::abs(worldScale.X), std::abs(worldScale.Y), std::abs(worldScale.Z)};

        PhysicsBodyDefinition definition;
        definition.Motion =
            useCharacter ? PhysicsMotionType::Kinematic : (rigidBody ? rigidBody->Motion() : PhysicsMotionType::Static);
        definition.Shape = useCharacter ? ColliderShape::Capsule : collider->Shape();
        definition.Position =
            useCharacter ? worldPosition : Math::TransformPoint(transform->WorldMatrix(), collider->Center());
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
        if (!PhysicsService || !Runtime)
            return;
        PhysicsWorldService = PhysicsService->CreateWorld();
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
        std::set<EntityId> seen;
        for (const auto entityId : candidates)
        {
            const auto entity = Runtime->FindEntity(entityId);
            seen.emplace(entityId);
            // Queries expose authored support geometry, not a partially advanced controller or dynamic body.
            // The normal fixed-step pass is unconditional and remains responsible for those definitions/assets.
            const auto rigid = entity.GetComponent<RigidBodyComponent>();
            const auto character = entity.GetComponent<CharacterControllerComponent>();
            const auto collider = entity.GetComponent<ColliderComponent>();
            const bool characterEnabled = character && character->Enabled();
            const bool eligible = entity.ActiveInHierarchy() && entity.GetComponent<TransformComponent>() &&
                                  (characterEnabled || (collider && collider->Enabled()));
            if (queryOnly && eligible &&
                (characterEnabled || (rigid && rigid->Enabled() && rigid->Motion() == PhysicsMotionType::Dynamic)))
                continue;
            auto& state = PhysicsBodies[entityId];
            const auto definition = BuildPhysicsDefinition(entity, state);
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
                // The guard borrows this session's live world only for the installation transaction.
                struct Replacement final
                {
                    PhysicsWorld& World;
                    PhysicsBodyId Body;
                    ~Replacement() noexcept
                    {
                        if (Body)
                        {
                            try
                            {
                                World.DestroyBody(Body);
                            }
                            catch (...)
                            {
                            } // Preserve an installation failure during rollback.
                        }
                    }
                } replacement{*PhysicsWorldService, PhysicsWorldService->CreateBody(*definition)};
                if (state.Body)
                    PhysicsWorldService->DestroyBody(state.Body);
                state.Body = replacement.Body;
                replacement.Body = {};
                state.Definition = *definition;
                state.CharacterSupport = {};
                state.CharacterRequestedVerticalDisplacement = 0.0F;
                state.CharacterMissedWalkableFrames = 0;
                ++state.Generation;
                if (state.Generation == 0)
                    state.Generation = 1;
            }
            else if (definition->Motion == PhysicsMotionType::Kinematic)
            {
                if (state.Definition.Position != definition->Position ||
                    state.Definition.Rotation != definition->Rotation)
                    PhysicsWorldService->SetKinematicTarget(state.Body, definition->Position, definition->Rotation);
                if (definition->UseGravity != state.Definition.UseGravity)
                    PhysicsWorldService->SetGravityEnabled(state.Body, definition->UseGravity);
            }
            else if (definition->Motion == PhysicsMotionType::Static &&
                     (state.Definition.Position != definition->Position ||
                      state.Definition.Rotation != definition->Rotation))
            {
                auto pose = PhysicsWorldService->TryGetBody(state.Body);
                if (!pose)
                    throw std::runtime_error("Authored physics body is unavailable.");
                pose->Position = definition->Position;
                pose->Rotation = definition->Rotation;
                PhysicsWorldService->SetBodyState(state.Body, *pose);
            }
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

    void SceneRuntimeSession::Impl::MoveTransformInWorld(const Entity& entity, TransformComponent& transform,
                                                         const Vector3 displacement)
    {
        Vector3 worldPosition;
        Quaternion worldRotation;
        Vector3 worldScale;
        if (!Math::DecomposeTransform(transform.WorldMatrix(), worldPosition, worldRotation, worldScale))
            throw std::runtime_error("Character Controller Transform cannot be decomposed.");
        worldPosition = {worldPosition.X + displacement.X, worldPosition.Y + displacement.Y,
                         worldPosition.Z + displacement.Z};
        auto local = Math::ComposeTransform(worldPosition, worldRotation, worldScale);
        if (const auto parent = entity.Parent())
        {
            if (const auto parentTransform = parent.GetComponent<TransformComponent>())
                local = Math::Multiply(Math::Inverse(parentTransform->WorldMatrix()), local);
        }
        Vector3 localPosition;
        Quaternion localRotation;
        Vector3 localScale;
        if (!Math::DecomposeTransform(local, localPosition, localRotation, localScale))
            throw std::runtime_error("Character Controller produced a non-decomposable local Transform.");
        transform.SetLocalPosition(localPosition);
    }

    void SceneRuntimeSession::Impl::ApplyCharacterMovement(const float deltaSeconds)
    {
        for (const auto& entity : Runtime->Query<CharacterControllerComponent>())
        {
            const auto character = entity.GetComponent<CharacterControllerComponent>();
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!character || !character->Enabled() || !entity.ActiveInHierarchy() || !transform)
                continue;
            const auto displacement = character->ConsumeDesiredMovement();
            auto runtimeState = PhysicsBodies.find(entity.Id());
            if (runtimeState == PhysicsBodies.end() || !runtimeState->second.Body ||
                !runtimeState->second.HasDefinition)
            {
                continue;
            }
            auto& state = runtimeState->second;
            state.CharacterRequestedVerticalDisplacement = displacement.Y;
            Vector3 start;
            Quaternion rotation;
            Vector3 scale;
            if (!Math::DecomposeTransform(transform->WorldMatrix(), start, rotation, scale))
                throw std::runtime_error("Character Controller Transform cannot be decomposed.");

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

            std::optional<Vector3> carryTarget;
            auto& carried = state.CharacterSupport;
            if (carried.Entity)
            {
                const auto support = Runtime->FindEntity(carried.Entity);
                const auto collider = support ? support.GetComponent<ColliderComponent>() : Ref<ColliderComponent>{};
                const auto supportTransform =
                    support ? support.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
                const auto supportState = PhysicsBodies.find(carried.Entity);
                bool eligible = character->Grounded() && displacement.Y <= 0.0F &&
                                carried.Controller.Lock() == character && entity.Parent().Id() == carried.RiderParent &&
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
                        carryTarget = Math::TransformPoint(supportTransform->WorldMatrix(), carried.LocalAnchor);
                }
                if (!eligible)
                    carried = {};
            }
            const auto padding = std::min(character->SkinWidth(), state.Definition.Radius * 0.5F);
            const auto castRadius = state.Definition.Radius - padding;
            const auto castHeight = state.Definition.Height - padding * 2.0F;
            const auto slopeNormal = std::cos(character->MaximumSlopeDegrees() * 3.14159265358979323846F / 180.0F);
            Vector3 current = start;
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
            if (const auto support = cast(current, {0.0F, -recoveryHeight, 0.0F});
                support && support->Distance < padding - 0.0001F && support->Normal.Y >= slopeNormal)
            {
                const Vector3 upward{0.0F, recoveryHeight, 0.0F};
                if (!cast(current, upward))
                {
                    const auto elevated = add(current, upward);
                    if (const auto landing = cast(elevated, {0.0F, -recoveryHeight, 0.0F});
                        landing && landing->Normal.Y >= slopeNormal && landing->Distance >= padding)
                    {
                        const auto correction = recoveryHeight - (landing->Distance - padding);
                        if (correction > 0.0001F && correction <= recoveryHeight)
                            current.Y += correction;
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
                        current = add(current, movement);
                        break;
                    }
                    const auto safeDistance = std::max(0.0F, hit->Distance - padding);
                    const auto safeFraction = std::clamp(safeDistance / movementLength, 0.0F, 1.0F);
                    current = add(current, multiply(movement, safeFraction));
                    movement = multiply(movement, 1.0F - safeFraction);
                    movement = Detail::ResolveCharacterCollisionRemainder(movement, hit->Normal, slideAlongSurface);
                }
            };

            // Carry is collision-resolved separately from caller input. Recovery may already have
            // supplied upward support motion, so sweep toward the anchor rather than adding it twice.
            if (carryTarget)
                moveAndSlide(subtract(*carryTarget, current), true);
            const Vector3 horizontal{displacement.X, 0.0F, displacement.Z};
            bool stepped = false;
            if (character->Grounded() && character->StepHeight() > 0.0F && hasResolvableDisplacement(horizontal))
            {
                const auto obstruction = cast(current, horizontal);
                if (obstruction && obstruction->Normal.Y < slopeNormal)
                {
                    const auto upwardDistance = character->StepHeight() + padding;
                    const Vector3 upward{0.0F, upwardDistance, 0.0F};
                    if (!cast(current, upward))
                    {
                        const auto elevated = add(current, upward);
                        if (!cast(elevated, horizontal))
                        {
                            const auto forward = add(elevated, horizontal);
                            const Vector3 downward{0.0F, -(upwardDistance + padding + 0.05F), 0.0F};
                            const auto landing = cast(forward, downward);
                            if (landing)
                            {
                                const auto normal = CharacterSupportNormal(*PhysicsWorldService, *landing, slopeNormal,
                                                                           character->Mask(), character->Layer());
                                const auto footHeight = current.Y - state.Definition.Height * 0.5F;
                                const auto rise = landing->Position.Y - footHeight;
                                if (normal.Y >= slopeNormal && rise >= -0.0001F &&
                                    rise <= character->StepHeight() + 0.0001F)
                                {
                                    const auto downDistance = std::max(0.0F, landing->Distance - padding);
                                    current = add(forward, {0.0F, -downDistance, 0.0F});
                                    stepped = true;
                                }
                            }
                        }
                    }
                }
            }
            if (!stepped)
                moveAndSlide(horizontal, true);
            moveAndSlide({0.0F, displacement.Y, 0.0F}, false);

            if (character->Grounded() && displacement.Y <= 0.0F)
            {
                const auto snapDistance = character->StepHeight() + padding + 0.05F;
                const auto landing = cast(current, {0.0F, -snapDistance, 0.0F});
                if (landing && Detail::ShouldSnapCharacterToGround(true, displacement.Y, landing->Normal, slopeNormal))
                {
                    const auto downDistance = std::max(0.0F, landing->Distance - padding);
                    current = add(current, {0.0F, -downDistance, 0.0F});
                }
            }

            const auto applied = subtract(current, start);
            state.CharacterVelocity = deltaSeconds > 0.0F ? multiply(applied, 1.0F / deltaSeconds) : Vector3{};
            if (applied != Vector3{})
            {
                MoveTransformInWorld(entity, *transform, applied);
                PhysicsWorldService->SetKinematicTarget(state.Body, current, rotation);
                state.Definition.Position = current;
                state.Definition.Rotation = rotation;
            }
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
            if (!Math::DecomposeTransform(transform->WorldMatrix(), worldPosition, worldRotation, worldScale))
                continue;
            bool hasWalkableHit = false;
            Vector3 normal{0.0F, 1.0F, 0.0F};
            const auto minimumNormal = std::cos(character->MaximumSlopeDegrees() * Pi / 180.0F);
            const auto& definition = state->second.Definition;
            const auto padding = std::min(character->SkinWidth(), definition.Radius * 0.5F);
            const auto hit = PhysicsWorldService->CastCapsule(
                {.Origin = worldPosition,
                 .Rotation = worldRotation,
                 .Radius = definition.Radius - padding,
                 .Height = definition.Height - padding * 2.0F,
                 // Step following is handled by movement; airborne characters must reach contact first.
                 .Displacement = {0.0F, -(padding + 0.002F), 0.0F},
                 .Mask = character->Mask(),
                 .IncludeTriggers = false,
                 .Layer = character->Layer(),
                 .IgnoreBody = state->second.Body});
            if (hit)
            {
                normal = CharacterSupportNormal(*PhysicsWorldService, *hit, minimumNormal, character->Mask(),
                                                character->Layer());
                hasWalkableHit = normal.Y >= minimumNormal;
            }
            const auto previous = character->RuntimeState();
            const bool grounded = Detail::ResolveCharacterGrounded(hasWalkableHit, previous.Grounded,
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
                    carried.ReferenceWorld = supportState.MovementWorld;
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
            const auto world = Math::ComposeTransform(origin, body->Rotation, runtime.WorldScale);
            Matrix4 local = world;
            if (const auto parent = entity.Parent())
            {
                if (const auto parentTransform = parent.GetComponent<TransformComponent>())
                    local = Math::Multiply(Math::Inverse(parentTransform->WorldMatrix()), world);
            }
            Vector3 localPosition;
            Quaternion localRotation;
            Vector3 localScale;
            if (!Math::DecomposeTransform(local, localPosition, localRotation, localScale))
                throw std::runtime_error("Dynamic physics body produced a non-decomposable Transform.");
            transform->SetLocalPosition(localPosition);
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
            Runtime->DispatchPhysicsContact(*first, phase,
                                            {*second, event.Point, event.Normal, event.Impulse, event.Trigger});
            Runtime->DispatchPhysicsContact(*second, phase,
                                            {*first,
                                             event.Point,
                                             {-event.Normal.X, -event.Normal.Y, -event.Normal.Z},
                                             event.Impulse,
                                             event.Trigger});
        }
    }

    void SceneRuntimeSession::Impl::StepPhysics(const float deltaSeconds)
    {
        if (!PhysicsWorldService)
            return;
        // Gameplay may move or disable a support this tick. Movement and grounding
        // must query the same current authored geometry, including kinematic targets.
        SynchronizePhysicsBodies();
        for (auto& [id, state] : PhysicsBodies)
        {
            const auto entity = Runtime->FindEntity(id);
            const auto transform = entity ? entity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
            if (transform)
                state.MovementWorld = transform->WorldMatrix();
            const auto character =
                entity ? entity.GetComponent<CharacterControllerComponent>() : Ref<CharacterControllerComponent>{};
            if (!character || !character->Enabled())
                state.CharacterSupport = {};
        }
        ApplyCharacterMovement(deltaSeconds);
        // Controller movement also moves descendant colliders through the Transform hierarchy.
        // Refresh those bodies before contact generation, as the original post-movement pass did.
        SynchronizePhysicsBodies();
        PhysicsWorldService->Step(deltaSeconds);
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
            const auto current = transform->WorldMatrix();
            const auto resetRevision = transform->PresentationResetRevision();
            if (!state.HasPresentationSamples || state.PresentationResetRevision != resetRevision)
            {
                state.PreviousPresentationWorld = current;
                state.CurrentPresentationWorld = current;
                state.PresentationResetRevision = resetRevision;
                state.HasPresentationSamples = true;
            }
            else
            {
                state.PreviousPresentationWorld = state.CurrentPresentationWorld;
                state.CurrentPresentationWorld = current;
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
            if (character)
            {
                const auto& previous = state.PreviousPresentationWorld.Elements;
                const auto& current = state.CurrentPresentationWorld.Elements;
                transform->SetRuntimePresentationWorldPosition({previous[12] + (current[12] - previous[12]) * amount,
                                                                previous[13] + (current[13] - previous[13]) * amount,
                                                                previous[14] + (current[14] - previous[14]) * amount});
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
                transform->SetRuntimePresentationWorldMatrix(state.CurrentPresentationWorld);
                continue;
            }
            const Vector3 position{previousPosition.X + (currentPosition.X - previousPosition.X) * amount,
                                   previousPosition.Y + (currentPosition.Y - previousPosition.Y) * amount,
                                   previousPosition.Z + (currentPosition.Z - previousPosition.Z) * amount};
            const Vector3 scale{previousScale.X + (currentScale.X - previousScale.X) * amount,
                                previousScale.Y + (currentScale.Y - previousScale.Y) * amount,
                                previousScale.Z + (currentScale.Z - previousScale.Z) * amount};
            transform->SetRuntimePresentationWorldMatrix(Math::ComposeTransform(
                position, RiggingDetail::Nlerp(previousRotation, currentRotation, amount), scale));
        }
    }

    void SceneRuntimeSession::Impl::ClearPhysics() noexcept
    {
        PhysicsBodies.clear();
        PhysicsQueryRevision = 0;
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
