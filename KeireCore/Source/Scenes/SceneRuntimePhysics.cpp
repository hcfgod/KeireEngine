#include "KeireInternal/Scenes/SceneRuntimeSessionImpl.h"

namespace Keire
{
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
        const bool transformMatches = first.Motion != PhysicsMotionType::Static ||
                                      (first.Position == second.Position && first.Rotation == second.Rotation);
        return transformMatches && first.Motion == second.Motion && first.Shape == second.Shape &&
               first.LinearVelocity == second.LinearVelocity && first.HalfExtent == second.HalfExtent &&
               first.Radius == second.Radius && first.Height == second.Height && first.Mass == second.Mass &&
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
        const auto rigidBody = entity.GetComponent<RigidBodyComponent>();

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

    void SceneRuntimeSession::Impl::SynchronizePhysicsBodies()
    {
        if (!PhysicsWorldService || !Runtime)
            return;
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
                continue;
            }
            if (!state.Body || !state.HasDefinition || !SamePhysicsDefinition(state.Definition, *definition))
            {
                if (state.Body)
                    PhysicsWorldService->DestroyBody(state.Body);
                state.Body = PhysicsWorldService->CreateBody(*definition);
                state.CharacterRequestedVerticalDisplacement = 0.0F;
                state.CharacterMissedWalkableFrames = 0;
                ++state.Generation;
                if (state.Generation == 0)
                    state.Generation = 1;
            }
            else if (definition->Motion == PhysicsMotionType::Kinematic)
            {
                PhysicsWorldService->SetKinematicTarget(state.Body, definition->Position, definition->Rotation);
                if (definition->UseGravity != state.Definition.UseGravity)
                    PhysicsWorldService->SetGravityEnabled(state.Body, definition->UseGravity);
            }
            state.Definition = *definition;
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
            if (displacement == Vector3{})
            {
                state.CharacterVelocity = {};
                continue;
            }

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

            const auto padding = std::min(character->SkinWidth(), state.Definition.Radius * 0.5F);
            const auto castRadius = state.Definition.Radius - padding;
            const auto castHeight = state.Definition.Height - padding * 2.0F;
            const auto slopeNormal = std::cos(character->MaximumSlopeDegrees() * 3.14159265358979323846F / 180.0F);
            Vector3 current = start;
            const auto cast = [&](const Vector3 origin, const Vector3 movement) -> std::optional<PhysicsQueryHit>
            {
                if (!hasResolvableDisplacement(movement))
                    return std::nullopt;
                return PhysicsWorldService->CastCapsule({.Origin = origin,
                                                         .Rotation = rotation,
                                                         .Radius = castRadius,
                                                         .Height = castHeight,
                                                         .Displacement = movement,
                                                         .Mask = character->Mask(),
                                                         .IncludeTriggers = false,
                                                         .Layer = character->Layer(),
                                                         .IgnoreBody = state.Body});
            };
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
                            if (landing && landing->Normal.Y >= slopeNormal)
                            {
                                const auto downDistance = std::max(0.0F, landing->Distance - padding);
                                current = add(forward, {0.0F, -downDistance, 0.0F});
                                stepped = true;
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
                 .Displacement = {0.0F, -(character->StepHeight() + padding + 0.05F), 0.0F},
                 .Mask = character->Mask(),
                 .IncludeTriggers = false,
                 .Layer = character->Layer(),
                 .IgnoreBody = state->second.Body});
            if (hit && hit->Normal.Y >= minimumNormal)
            {
                hasWalkableHit = true;
                normal = hit->Normal;
            }
            const auto previous = character->RuntimeState();
            const bool grounded = Detail::ResolveCharacterGrounded(hasWalkableHit, previous.Grounded,
                                                                   state->second.CharacterRequestedVerticalDisplacement,
                                                                   state->second.CharacterMissedWalkableFrames);
            if (grounded && !hasWalkableHit)
                normal = previous.GroundNormal;
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
        ApplyCharacterMovement(deltaSeconds);
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
