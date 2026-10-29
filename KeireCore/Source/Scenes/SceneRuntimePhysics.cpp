#include "KeireInternal/Scenes/SceneRuntimeSessionImpl.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>

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
            if (state.Definition.MotionSettings != definition->MotionSettings)
            {
                PhysicsWorldService->SetMotionSettings(state.Body, definition->MotionSettings);
                state.Definition.MotionSettings = definition->MotionSettings;
            }
            if (definition->Motion != PhysicsMotionType::Static &&
                state.Definition.LinearVelocity != definition->LinearVelocity)
            {
                auto pose = PhysicsWorldService->TryGetBody(state.Body);
                if (!pose)
                {
                    throw std::runtime_error("Authored physics velocity targets an unavailable body.");
                }
                pose->LinearVelocity = definition->LinearVelocity;
                pose->Sleeping = false;
                PhysicsWorldService->SetBodyState(state.Body, *pose);
            }
            if (state.Definition.UseGravity != definition->UseGravity)
            {
                PhysicsWorldService->SetGravityEnabled(state.Body, definition->UseGravity);
            }
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
                            current = add(current, multiply(up, correction));
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
                                    current = add(forward, multiply(up, -downDistance));
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
                    current = add(current, multiply(up, -downDistance));
                }
            }

            const auto applied = subtract(current, start);
            state.CharacterVelocity = deltaSeconds > 0.0F ? multiply(applied, 1.0F / deltaSeconds) : Vector3{};
            if (applied != Vector3{})
            {
                MoveTransformInWorld(*transform, applied);
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
                state.MovementWorld = PhysicsRelativeTransform(*transform, *PhysicsWorldService);
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
