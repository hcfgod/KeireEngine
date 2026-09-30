#include "KeireInternal/Animation/RiggingMath.h"
#include "KeireInternal/Scenes/SceneRuntimeSessionImpl.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <exception>

namespace Keire
{
    [[nodiscard]] std::string SceneRuntimeSession::Impl::ApplyFootGrounding(
        const Entity& entity, const SkeletonAsset& skeleton, const AnimatorFootGroundingSettings& settings,
        const float runtimeWeight, const AssetId skinnedMesh, const float deltaSeconds,
        std::span<BoneTransform> localPose, const std::map<std::string, std::uint32_t, std::less<>>& indices,
        const std::map<RigBoneSemantic, std::uint32_t>& semantics, AnimationRuntimeState& runtimeState,
        const std::optional<float> horizontalPelvisRatio, const std::optional<float> maximumFootRotationDegrees,
        const std::optional<std::array<float, 2>> proceduralFootWeights,
        const std::optional<float> unsupportedFootDropRatio)
    {
        const auto clearContacts = [&]
        {
            runtimeState.LeftFootIkState = {};
            runtimeState.RightFootIkState = {};
            runtimeState.LeftFootGroundingSmoothingState = {};
            runtimeState.RightFootGroundingSmoothingState = {};
            runtimeState.LeftFootPlantState = {};
            runtimeState.RightFootPlantState = {};
            runtimeState.UnreachableFootCount = 0;
        };
        if (!settings.Enabled || runtimeWeight <= std::numeric_limits<float>::epsilon())
        {
            clearContacts();
            return {};
        }
        if (!PhysicsWorldService)
        {
            clearContacts();
            return "Foot grounding requires an active physics world.";
        }
        const auto transform = entity.GetComponent<TransformComponent>();
        if (!transform)
        {
            clearContacts();
            return "Foot grounding requires an Animator world transform.";
        }

        const auto bone = [&](const std::string_view name, const RigBoneSemantic semantic)
        { return ResolveIkBone(indices, semantics, settings.AutomaticBoneMapping, name, semantic); };
        const auto pelvis = bone(settings.Pelvis, RigBoneSemantic::Pelvis);
        const std::array chains{std::array{bone(settings.LeftUpperLeg, RigBoneSemantic::LeftUpperLeg),
                                           bone(settings.LeftLowerLeg, RigBoneSemantic::LeftLowerLeg),
                                           bone(settings.LeftFoot, RigBoneSemantic::LeftFoot)},
                                std::array{bone(settings.RightUpperLeg, RigBoneSemantic::RightUpperLeg),
                                           bone(settings.RightLowerLeg, RigBoneSemantic::RightLowerLeg),
                                           bone(settings.RightFoot, RigBoneSemantic::RightFoot)}};
        if (!pelvis || std::ranges::any_of(
                           chains, [](const auto& chain)
                           { return std::ranges::any_of(chain, [](const auto value) { return !value.has_value(); }); }))
        {
            clearContacts();
            return "Foot grounding references one or more unavailable skeleton bones.";
        }

        Matrix4 worldToModel;
        try
        {
            worldToModel = Math::Inverse(transform->WorldMatrix());
        }
        catch (const std::exception&)
        {
            clearContacts();
            return "Foot grounding could not invert the Animator world transform.";
        }
        const auto modelToWorld = transform->WorldMatrix();
        ModelBoneMatrices(skeleton, localPose, runtimeState.ModelMatrixScratch);
        const auto& modelBones = runtimeState.ModelMatrixScratch;
        const auto leftHipPosition = Math::TransformPoint(modelBones[*chains[0][0]], {});
        const auto rightHipPosition = Math::TransformPoint(modelBones[*chains[1][0]], {});
        const auto gravityUpModel = Math::TransformDirection(worldToModel, {0.0F, 1.0F, 0.0F});
        const auto leftKneePosition = Math::TransformPoint(modelBones[*chains[0][1]], {});
        const auto leftFootPosition = Math::TransformPoint(modelBones[*chains[0][2]], {});
        const auto rightKneePosition = Math::TransformPoint(modelBones[*chains[1][1]], {});
        const auto rightFootPosition = Math::TransformPoint(modelBones[*chains[1][2]], {});
        const auto kneeReference = Detail::OrientBipedKneeReference(
            Detail::AutomaticBipedKneeReference(leftHipPosition, rightHipPosition, gravityUpModel), leftHipPosition,
            leftKneePosition, leftFootPosition, rightHipPosition, rightKneePosition, rightFootPosition);
        auto characterPhysicsRoot = Detail::FindCharacterControllerRoot(entity);
        if (!characterPhysicsRoot)
        {
            for (auto current = entity; current; current = current.Parent())
            {
                if (PhysicsBodies.contains(current.Id()))
                {
                    characterPhysicsRoot = current;
                    break;
                }
            }
        }
        if (!characterPhysicsRoot)
            characterPhysicsRoot = entity;

        runtimeState.CharacterBodyScratch.clear();
        auto queryLayer = 1U;
        bool hasQueryLayer = false;
        for (const auto& [bodyEntityId, physics] : PhysicsBodies)
        {
            const auto bodyEntity = Runtime->FindEntity(bodyEntityId);
            if (!Detail::IsSameOrDescendantOf(bodyEntity, characterPhysicsRoot))
                continue;
            runtimeState.CharacterBodyScratch.push_back(physics.Body);
            if (!hasQueryLayer && bodyEntity == characterPhysicsRoot && physics.HasDefinition)
            {
                queryLayer = physics.Definition.Layer;
                hasQueryLayer = true;
            }
        }

        const auto queryCollisionMask = PhysicsService->CollisionMatrix()[std::countr_zero(queryLayer)];
        auto& request = runtimeState.FootGroundingRequestCache;
        request.Pelvis = pelvis;
        request.Torso.reset();
        request.Contacts.clear();
        request.FootHeight = 0.0F;
        request.PelvisWeight = settings.Weight * runtimeWeight;
        request.MaximumPelvisAdjustment =
            Detail::WorldVerticalDistanceToModel(worldToModel, settings.MaximumPelvisAdjustment);
        request.MaximumHorizontalPelvisAdjustment = 0.0F;
        request.PelvisSupportRadius = 0.0F;
        request.PelvisRotationWeight = 0.0F;
        request.MaximumPelvisRotationDegrees = 0.0F;
        request.PositionTolerance = Detail::WorldVerticalDistanceToModel(worldToModel, 0.01F);
        float totalLegLength = 0.0F;
        std::size_t legCount = 0;
        std::array<bool, 2> unsupportedFeet{};
        Ref<const SkinnedMeshAsset> skin;
        Ref<const MeshAsset> skinMesh;
        if (Assets && skinnedMesh)
        {
            const auto skinHandle = Assets->Load<SkinnedMeshAsset>(skinnedMesh, AssetPriority::High);
            skin = skinHandle.TryGetLoaded();
            if (skin)
            {
                const auto meshHandle = Assets->Load<MeshAsset>(skin->Mesh(), AssetPriority::High);
                skinMesh = meshHandle.TryGetLoaded();
                if (skinMesh && (runtimeState.FootClearanceMesh != skin->Mesh() ||
                                 runtimeState.FootClearanceSkinRevision != skinHandle.Revision() ||
                                 runtimeState.FootClearanceMeshRevision != meshHandle.Revision()))
                {
                    runtimeState.FootClearanceMesh = skin->Mesh();
                    runtimeState.FootClearanceSkinRevision = skinHandle.Revision();
                    runtimeState.FootClearanceMeshRevision = meshHandle.Revision();
                    runtimeState.FootMeshClearances.clear();
                    runtimeState.FootToeBones.clear();
                }
            }
        }
        const auto distance = [](const Vector3 left, const Vector3 right)
        {
            const auto x = right.X - left.X;
            const auto y = right.Y - left.Y;
            const auto z = right.Z - left.Z;
            return std::sqrt(x * x + y * y + z * z);
        };
        for (std::size_t chainIndex = 0; chainIndex < chains.size(); ++chainIndex)
        {
            const auto& chain = chains[chainIndex];
            const auto footPosition = Math::TransformPoint(modelBones[*chain[2]], {});
            const auto footWorld = Math::TransformPoint(modelToWorld, footPosition);
            const auto upperWorld = Math::TransformPoint(modelToWorld, Math::TransformPoint(modelBones[*chain[0]], {}));
            const auto lowerWorld = Math::TransformPoint(modelToWorld, Math::TransformPoint(modelBones[*chain[1]], {}));
            const auto legLength = distance(upperWorld, lowerWorld) + distance(lowerWorld, footWorld);
            const auto upperModel = Math::TransformPoint(modelBones[*chain[0]], {});
            const auto lowerModel = Math::TransformPoint(modelBones[*chain[1]], {});
            totalLegLength += distance(upperModel, lowerModel) + distance(lowerModel, footPosition);
            ++legCount;
            auto& plantRuntime = chainIndex == 0 ? runtimeState.LeftFootPlantState : runtimeState.RightFootPlantState;
            auto& plantState = plantRuntime.Plant;
            auto& stability = chainIndex == 0 ? runtimeState.LeftFootIkState : runtimeState.RightFootIkState;
            auto& smoothing = chainIndex == 0 ? runtimeState.LeftFootGroundingSmoothingState
                                              : runtimeState.RightFootGroundingSmoothingState;
            const auto chainRuntimeWeight =
                proceduralFootWeights ? std::clamp((*proceduralFootWeights)[chainIndex], 0.0F, 1.0F) : 1.0F;
            if (chainRuntimeWeight <= std::numeric_limits<float>::epsilon())
            {
                plantRuntime = {};
                stability = {};
                smoothing = {};
                continue;
            }
            bool forcePlantCandidate = false;
            bool resolvedLockedSupport = settings.LockPlantedFeet && plantState.Locked;
            if (resolvedLockedSupport && plantRuntime.Support)
            {
                const auto supportEntity = Runtime->FindEntity(*plantRuntime.Support);
                const auto supportBody = PhysicsBodies.find(*plantRuntime.Support);
                const bool hasSupportBody = supportBody != PhysicsBodies.end() && supportBody->second.Body &&
                                            supportBody->second.HasDefinition &&
                                            !supportBody->second.Definition.Trigger &&
                                            (supportBody->second.Definition.Layer & settings.CollisionMask) != 0 &&
                                            (supportBody->second.Definition.Layer & queryCollisionMask) != 0 &&
                                            (supportBody->second.Definition.Mask & queryLayer) != 0;
                const auto supportTransform = supportEntity && hasSupportBody
                                                  ? supportEntity.GetComponent<TransformComponent>()
                                                  : Ref<TransformComponent>{};
                const auto supportContact = supportTransform
                                                ? Detail::ResolveFootPlantSupportAnchor(supportTransform->WorldMatrix(),
                                                                                        plantRuntime.SupportAnchor)
                                                : std::nullopt;
                const auto supportSurface =
                    supportTransform ? Detail::ResolveFootPlantSupportAnchor(supportTransform->WorldMatrix(),
                                                                             plantRuntime.SupportSurfaceAnchor)
                                     : std::nullopt;
                if (supportContact && supportSurface)
                {
                    // Filter contact changes in support space: world-space smoothing otherwise
                    // follows rising platforms immediately but lags behind their descent and rotation.
                    if (smoothing.HasContact && plantRuntime.SmoothedSupportAnchor)
                    {
                        const auto carried = Detail::ResolveFootPlantSupportAnchor(supportTransform->WorldMatrix(),
                                                                                   *plantRuntime.SmoothedSupportAnchor);
                        if (carried)
                        {
                            smoothing.Position = carried->Position;
                            smoothing.Normal = carried->Normal;
                        }
                    }
                    plantState.Position = supportContact->Position;
                    plantState.Normal = supportContact->Normal;
                    plantRuntime.SurfacePosition = supportSurface->Position;
                    plantRuntime.SurfaceNormal = supportSurface->Normal;
                }
                else
                {
                    Log::GetCoreLogger().Write(LogLevel::Trace,
                                               LogMessage("[IK] entity={} foot={} support-lost support={}", entity.Id(),
                                                          chainIndex, *plantRuntime.Support));
                    plantRuntime = {};
                    resolvedLockedSupport = false;
                    forcePlantCandidate = true;
                }
            }
            const Vector3 origin{footWorld.X, footWorld.Y + settings.RaycastHeight, footWorld.Z};
            auto raycastDistance = settings.RaycastDistance;
            if (settings.AutomaticRaycastDistance)
            {
                raycastDistance = std::max(raycastDistance, legLength * 1.1F);
            }
            const auto hits = PhysicsWorldService->RayCast({.Origin = origin,
                                                            .Direction = {0.0F, -1.0F, 0.0F},
                                                            .MaximumDistance = settings.RaycastHeight + raycastDistance,
                                                            .Mask = settings.CollisionMask,
                                                            .IncludeTriggers = false,
                                                            .Layer = queryLayer});
            const auto hit = std::ranges::find_if(
                hits,
                [&](const auto& candidate)
                {
                    if (std::ranges::find(runtimeState.CharacterBodyScratch, candidate.Body) !=
                        runtimeState.CharacterBodyScratch.end())
                        return false;
                    const auto normalLength =
                        std::sqrt(candidate.Normal.X * candidate.Normal.X + candidate.Normal.Y * candidate.Normal.Y +
                                  candidate.Normal.Z * candidate.Normal.Z);
                    if (normalLength <= 0.000001F)
                        return false;
                    const auto normalY = std::clamp(candidate.Normal.Y / normalLength, -1.0F, 1.0F);
                    const auto slope = std::acos(normalY) * 57.2957795131F;
                    return slope <= settings.MaximumSlopeDegrees;
                });
            std::optional<Detail::ModelFootGroundContact> contact;
            std::optional<Vector3> footTarget;
            std::optional<Vector3> targetNormalWorld;
            std::optional<Vector3> clearanceWorld;
            std::optional<Vector3> clearanceNormalWorld;
            if (resolvedLockedSupport && hit != hits.end() &&
                Detail::ShouldReplaceAutomaticFootSupport(plantRuntime.SurfacePosition, plantRuntime.SurfaceNormal,
                                                          hit->Position))
            {
                Log::GetCoreLogger().Write(
                    LogLevel::Trace,
                    LogMessage("[IK] entity={} foot={} support-replaced surfaceY={} candidateY={}", entity.Id(),
                               chainIndex, plantRuntime.SurfacePosition.Y, hit->Position.Y));
                plantRuntime = {};
                resolvedLockedSupport = false;
                forcePlantCandidate = true;
            }
            if (resolvedLockedSupport)
            {
                // Contact can be acquired before the authored foot reaches its lowest point.
                // Retain that low point so acquisition timing cannot suppress the next lift.
                if (Detail::IkDot(Detail::IkSubtract(footWorld, plantRuntime.AnimationPlantPosition),
                                  plantRuntime.ReleaseNormal) < 0.0F)
                {
                    plantRuntime.AnimationPlantPosition = footWorld;
                }
                const auto animationReleased = Detail::ShouldReleaseAutomaticFootPlant(
                    footWorld, plantRuntime.AnimationPlantPosition, plantRuntime.ReleaseNormal, legLength,
                    settings.ReleaseDistance);
                const auto supportNeedsReanchor =
                    plantRuntime.Support && Detail::ShouldReanchorMovingFootSupport(
                                                plantState.Position, plantRuntime.ReleasePosition,
                                                plantRuntime.ReleaseNormal, legLength, settings.ReleaseDistance);
                const auto outsideReach =
                    distance(upperWorld, plantState.Position) > legLength + settings.MaximumPelvisAdjustment;
                if (!animationReleased && !supportNeedsReanchor && !outsideReach)
                {
                    contact =
                        Detail::ToModelFootGroundContact(worldToModel, plantState.Position, plantState.Normal, 0.0F);
                    footTarget = Math::TransformPoint(worldToModel, plantState.Position);
                    targetNormalWorld = plantState.Normal;
                    if (contact)
                        contact->Normal = Math::TransformDirection(worldToModel, plantState.Normal);
                }
                else
                {
                    Log::GetCoreLogger().Write(
                        LogLevel::Trace,
                        LogMessage("[IK] entity={} foot={} released lift={} supportTravel={} outsideReach={} "
                                   "target=({},{},{}) legLength={}",
                                   entity.Id(), chainIndex, animationReleased, supportNeedsReanchor, outsideReach,
                                   plantState.Position.X, plantState.Position.Y, plantState.Position.Z, legLength));
                    forcePlantCandidate = !animationReleased && (supportNeedsReanchor || outsideReach);
                    const auto animationPlantPosition = plantRuntime.AnimationPlantPosition;
                    const auto releaseNormal = plantRuntime.ReleaseNormal;
                    plantRuntime = {};
                    if (animationReleased)
                    {
                        plantRuntime.AnimationPlantPosition = animationPlantPosition;
                        plantRuntime.ReleaseNormal = releaseNormal;
                        plantRuntime.AwaitingAnimationPlant = true;
                    }
                    resolvedLockedSupport = false;
                }
            }

            if (!footTarget)
            {
                if (hit == hits.end())
                {
                    plantRuntime = {};
                    unsupportedFeet[chainIndex] = unsupportedFootDropRatio.has_value();
                }
                else
                {
                    contact = Detail::ToModelFootGroundContact(worldToModel, hit->Position, hit->Normal, 0.0F);
                    if (!contact)
                        continue;
                    const auto minimumClearance =
                        Detail::WorldSurfaceDistanceToModel(worldToModel, hit->Normal, settings.FootOffset);
                    if (!minimumClearance)
                        continue;
                    auto soleClearance = Detail::FootBoneBindSurfaceClearance(skeleton, *chain[2]);
                    if (!soleClearance)
                        continue;
                    auto cachedMeshClearance = runtimeState.FootMeshClearances.find(*chain[2]);
                    if (cachedMeshClearance == runtimeState.FootMeshClearances.end() && skin && skinMesh)
                    {
                        cachedMeshClearance =
                            runtimeState.FootMeshClearances
                                .emplace(*chain[2],
                                         Detail::FootMeshBindSurfaceClearance(skeleton, *skin, *skinMesh, *chain[2]))
                                .first;
                    }
                    if (cachedMeshClearance != runtimeState.FootMeshClearances.end() && cachedMeshClearance->second)
                        *soleClearance = std::max(*soleClearance, *cachedMeshClearance->second);
                    footTarget = Detail::FootTargetAboveSurface(*contact, *soleClearance, *minimumClearance);
                    if (!footTarget)
                        continue;
                    const auto candidateWorld = Math::TransformPoint(modelToWorld, *footTarget);
                    const auto candidateNormal = Detail::IkNormalize(hit->Normal);
                    clearanceWorld = candidateWorld;
                    clearanceNormalWorld = candidateNormal;
                    targetNormalWorld = candidateNormal;
                    if (settings.LockPlantedFeet)
                    {
                        const auto wasLocked = plantState.Locked;
                        const auto separation =
                            Detail::IkDot(Detail::IkSubtract(footWorld, candidateWorld), candidateNormal);
                        // Terrain correction must not raise the animation's lift threshold or
                        // immediately reacquire a foot that is still in its swing phase.
                        if (plantRuntime.AwaitingAnimationPlant &&
                            Detail::IkDot(Detail::IkSubtract(footWorld, plantRuntime.AnimationPlantPosition),
                                          plantRuntime.ReleaseNormal) <= settings.PlantDistance)
                        {
                            plantRuntime.AwaitingAnimationPlant = false;
                        }
                        const auto forced = forcePlantCandidate || separation < -settings.ReleaseDistance;
                        if (forced)
                        {
                            if (!Detail::ForceAutomaticFootPlant(candidateWorld, candidateNormal, plantState))
                                continue;
                        }
                        else if (!plantRuntime.AwaitingAnimationPlant)
                        {
                            (void)Detail::UpdateAutomaticFootPlant(footWorld, candidateWorld, candidateNormal,
                                                                   legLength, settings.PlantDistance,
                                                                   settings.ReleaseDistance, plantState);
                        }
                        *footTarget = Math::TransformPoint(worldToModel,
                                                           plantState.Locked ? plantState.Position : candidateWorld);
                        if (plantState.Locked)
                        {
                            targetNormalWorld = plantState.Normal;
                            contact->Normal = Math::TransformDirection(worldToModel, plantState.Normal);
                            if (!wasLocked || forcePlantCandidate)
                            {
                                Log::GetCoreLogger().Write(
                                    LogLevel::Trace,
                                    LogMessage("[IK] entity={} foot={} planted forced={} separation={} "
                                               "target=({},{},{})",
                                               entity.Id(), chainIndex, forced, separation, plantState.Position.X,
                                               plantState.Position.Y, plantState.Position.Z));
                                plantRuntime.ReleasePosition = plantState.Position;
                                plantRuntime.ReleaseNormal = plantState.Normal;
                                plantRuntime.AnimationPlantPosition = footWorld;
                                plantRuntime.AwaitingAnimationPlant = false;
                            }
                            plantRuntime.SurfacePosition = hit->Position;
                            plantRuntime.SurfaceNormal = candidateNormal;
                            if (const auto support = EntityForBody(hit->Body))
                            {
                                const auto supportEntity = Runtime->FindEntity(*support);
                                const auto supportTransform = supportEntity
                                                                  ? supportEntity.GetComponent<TransformComponent>()
                                                                  : Ref<TransformComponent>{};
                                const auto supportAnchor =
                                    supportTransform
                                        ? Detail::CaptureFootPlantSupportAnchor(supportTransform->WorldMatrix(),
                                                                                plantState.Position, plantState.Normal)
                                        : std::nullopt;
                                const auto supportSurfaceAnchor =
                                    supportTransform
                                        ? Detail::CaptureFootPlantSupportAnchor(supportTransform->WorldMatrix(),
                                                                                hit->Position, candidateNormal)
                                        : std::nullopt;
                                if (supportAnchor && supportSurfaceAnchor)
                                {
                                    plantRuntime.Support = *support;
                                    plantRuntime.SupportAnchor = *supportAnchor;
                                    plantRuntime.SupportSurfaceAnchor = *supportSurfaceAnchor;
                                }
                                else
                                {
                                    plantRuntime.Support.reset();
                                }
                            }
                            else
                            {
                                plantRuntime.Support.reset();
                            }
                        }
                        else
                        {
                            footTarget.reset();
                            contact.reset();
                            targetNormalWorld.reset();
                        }
                    }
                    else
                    {
                        plantRuntime = {};
                    }
                }
            }

            std::optional<Vector3> desiredWorldTarget;
            if (footTarget && contact && targetNormalWorld)
                desiredWorldTarget = Math::TransformPoint(modelToWorld, *footTarget);
            auto smoothed = Detail::UpdateAutomaticFootGroundingSmoothing(
                desiredWorldTarget, desiredWorldTarget ? targetNormalWorld : std::nullopt, footWorld, deltaSeconds,
                settings.ResponseTime, smoothing);
            if (clearanceWorld && clearanceNormalWorld)
            {
                // Swing-phase locking is suppressed, but collision clearance is not. Correct only
                // normal penetration without creating an anchor or adding pelvis-support influence.
                const auto position = smoothed ? smoothed->Position : footWorld;
                const auto penetration =
                    Detail::IkDot(Detail::IkSubtract(*clearanceWorld, position), *clearanceNormalWorld);
                if (penetration > 0.0F)
                {
                    if (!smoothed)
                        smoothed = Detail::AutomaticFootGroundingTarget{position, *clearanceNormalWorld, 0.0F};
                    smoothed->Position = {position.X + clearanceNormalWorld->X * penetration,
                                          position.Y + clearanceNormalWorld->Y * penetration,
                                          position.Z + clearanceNormalWorld->Z * penetration};
                }
            }
            plantRuntime.SmoothedSupportAnchor.reset();
            if (smoothing.HasContact && plantState.Locked && plantRuntime.Support)
            {
                const auto supportEntity = Runtime->FindEntity(*plantRuntime.Support);
                const auto supportTransform =
                    supportEntity ? supportEntity.GetComponent<TransformComponent>() : Ref<TransformComponent>{};
                if (supportTransform)
                {
                    plantRuntime.SmoothedSupportAnchor = Detail::CaptureFootPlantSupportAnchor(
                        supportTransform->WorldMatrix(), smoothing.Position, smoothing.Normal);
                }
            }
            if (!smoothed)
            {
                if (!unsupportedFeet[chainIndex] && settings.KneeStability <= 0.0F)
                    stability = {};
                continue;
            }
            unsupportedFeet[chainIndex] = false;
            auto limitedNormal = smoothed->Normal;
            if (maximumFootRotationDegrees)
            {
                limitedNormal = Detail::IkNormalize(limitedNormal);
                const auto slope = std::acos(std::clamp(limitedNormal.Y, -1.0F, 1.0F)) * 57.2957795131F;
                if (slope > *maximumFootRotationDegrees)
                {
                    const auto horizontalLength =
                        std::sqrt(limitedNormal.X * limitedNormal.X + limitedNormal.Z * limitedNormal.Z);
                    if (horizontalLength > 0.000001F)
                    {
                        const auto radians = *maximumFootRotationDegrees * 0.0174532925199F;
                        const auto sine = std::sin(radians);
                        limitedNormal = {limitedNormal.X / horizontalLength * sine, std::cos(radians),
                                         limitedNormal.Z / horizontalLength * sine};
                    }
                }
            }
            contact = Detail::ToModelFootGroundContact(worldToModel, smoothed->Position, limitedNormal, 0.0F);
            if (!contact)
            {
                smoothing = {};
                stability = {};
                continue;
            }
            footTarget = Math::TransformPoint(worldToModel, smoothed->Position);
            const auto upperLeg = Math::TransformPoint(modelBones[*chain[0]], {});
            const auto knee = Math::TransformPoint(modelBones[*chain[1]], {});
            const auto pole =
                Detail::StableAutomaticLimbPole(upperLeg, knee, footPosition, *footTarget, kneeReference, deltaSeconds,
                                                settings.ResponseTime, settings.KneeStability, stability);
            FootGroundContact grounded{*chain[0],
                                       *chain[1],
                                       *chain[2],
                                       *footTarget,
                                       contact->Normal,
                                       pole,
                                       settings.Weight * runtimeWeight * chainRuntimeWeight,
                                       settings.RotationWeight * runtimeWeight * chainRuntimeWeight * smoothed->Blend};
            auto toe = runtimeState.FootToeBones.find(*chain[2]);
            if (toe == runtimeState.FootToeBones.end())
            {
                toe = runtimeState.FootToeBones
                          .emplace(*chain[2], Detail::AutomaticFootToeBone(skeleton, skin.Get(), *chain[2]))
                          .first;
            }
            grounded.Toe = toe->second;
            grounded.SupportWeight = smoothed->Blend;
            grounded.SupportPosition = Math::TransformPoint(worldToModel, smoothing.Position);
            request.Contacts.push_back(grounded);
        }
        if (!request.Contacts.empty())
        {
            if (legCount != 0 && settings.LockPlantedFeet)
            {
                const auto averageLegLength = totalLegLength / static_cast<float>(legCount);
                // Clip playback owns horizontal body motion. Rebalancing toward locked feet would drag
                // an in-place animation backward at each support transfer. Procedural motion opts in explicitly.
                request.MaximumHorizontalPelvisAdjustment = averageLegLength * horizontalPelvisRatio.value_or(0.0F);
                request.PelvisSupportRadius = averageLegLength * 0.025F;
                const auto chest = semantics.find(RigBoneSemantic::Chest);
                const auto spine = semantics.find(RigBoneSemantic::Spine);
                if (chest != semantics.end() && Detail::IsBoneInSubtree(skeleton, chest->second, *pelvis) &&
                    chest->second != *pelvis)
                    request.Torso = chest->second;
                else if (spine != semantics.end() && Detail::IsBoneInSubtree(skeleton, spine->second, *pelvis) &&
                         spine->second != *pelvis)
                    request.Torso = spine->second;
                if (request.Torso)
                {
                    request.PelvisRotationWeight = settings.Weight * settings.LeanCorrectionWeight;
                    request.MaximumPelvisRotationDegrees = settings.MaximumLeanCorrectionDegrees;
                }
            }
            // Keep swing endpoints in sampled model space while the pelvis follows the planted leg.
            // Otherwise acquiring a contact abruptly removes the pelvis displacement inherited by that foot.
            for (std::size_t chainIndex = 0; chainIndex < chains.size(); ++chainIndex)
            {
                const auto& chain = chains[chainIndex];
                if (std::ranges::any_of(request.Contacts, [&](const auto& value) { return value.Foot == *chain[2]; }))
                    continue;
                const auto chainWeight =
                    proceduralFootWeights ? std::clamp((*proceduralFootWeights)[chainIndex], 0.0F, 1.0F) : 1.0F;
                if (chainWeight <= std::numeric_limits<float>::epsilon())
                    continue;
                const auto foot = Math::TransformPoint(modelBones[*chain[2]], {});
                const auto knee = Math::TransformPoint(modelBones[*chain[1]], {});
                const auto upperLeg = Math::TransformPoint(modelBones[*chain[0]], {});
                auto& stability = chainIndex == 0 ? runtimeState.LeftFootIkState : runtimeState.RightFootIkState;
                const auto pole =
                    settings.KneeStability > 0.0F
                        ? Detail::StableAutomaticLimbPole(upperLeg, knee, foot, foot, kneeReference, deltaSeconds,
                                                          settings.ResponseTime, settings.KneeStability, stability)
                        : knee;
                FootGroundContact swing{*chain[0],
                                        *chain[1],
                                        *chain[2],
                                        foot,
                                        gravityUpModel,
                                        pole,
                                        settings.Weight * runtimeWeight * chainWeight,
                                        0.0F};
                swing.SupportWeight = 0.0F;
                request.Contacts.push_back(swing);
            }
            const auto solved = SolveFootGrounding(skeleton, localPose, request);
            if (!solved)
                return "Foot grounding could not solve the configured leg chains.";
            if (runtimeState.UnreachableFootCount != solved->UnreachableFeet)
            {
                Log::GetCoreLogger().Write(
                    LogLevel::Trace,
                    LogMessage("[IK] entity={} reach-limit feet={} maximumError={} pelvisAdjustment={}", entity.Id(),
                               solved->UnreachableFeet, solved->MaximumPositionError, solved->PelvisAdjustment));
                runtimeState.UnreachableFootCount = solved->UnreachableFeet;
            }
            if (solved->UnreachableFeet != 0)
                return "Foot grounding reached the configured pelvis/leg limit for " +
                       std::to_string(solved->UnreachableFeet) + " foot target(s).";
        }
        else if (runtimeState.UnreachableFootCount != 0)
        {
            Log::GetCoreLogger().Write(
                LogLevel::Trace, LogMessage("[IK] entity={} reach-limit cleared reason=no-contacts", entity.Id()));
            runtimeState.UnreachableFootCount = 0;
        }
        // Stabilization must also span frames without ground contacts. Keep authored endpoints and
        // pelvis motion; only the knee's bend plane is corrected, with no support or foot rotation.
        if (request.Contacts.empty() && settings.KneeStability > 0.0F)
        {
            for (std::size_t chainIndex = 0; chainIndex < chains.size(); ++chainIndex)
            {
                if (unsupportedFootDropRatio && unsupportedFeet[chainIndex])
                    continue;
                const auto& chain = chains[chainIndex];
                const auto chainWeight =
                    proceduralFootWeights ? std::clamp((*proceduralFootWeights)[chainIndex], 0.0F, 1.0F) : 1.0F;
                if (chainWeight <= std::numeric_limits<float>::epsilon())
                    continue;
                ModelBoneMatrices(skeleton, localPose, runtimeState.ModelMatrixScratch);
                const auto& matrices = runtimeState.ModelMatrixScratch;
                const auto upperLeg = Math::TransformPoint(matrices[*chain[0]], {});
                const auto knee = Math::TransformPoint(matrices[*chain[1]], {});
                const auto foot = Math::TransformPoint(matrices[*chain[2]], {});
                auto& stability = chainIndex == 0 ? runtimeState.LeftFootIkState : runtimeState.RightFootIkState;
                const auto pole =
                    Detail::StableAutomaticLimbPole(upperLeg, knee, foot, foot, kneeReference, deltaSeconds,
                                                    settings.ResponseTime, settings.KneeStability, stability);
                const auto axis = Detail::IkNormalize(Detail::IkSubtract(foot, upperLeg));
                const auto sampledBend = Detail::IkProjectOntoPlane(Detail::IkSubtract(knee, upperLeg), axis);
                if (Detail::IkDot(sampledBend, Detail::IkSubtract(pole, upperLeg)) >= 0.0F)
                    continue;
                const auto sampledFootMatrix = matrices[*chain[2]];
                // Rotate the existing bend around its endpoint axis. Re-solving near extension
                // introduces a minimum bend that pops away when the authored bend becomes valid.
                const auto from = Detail::IkNormalize(sampledBend);
                const auto to =
                    Detail::IkNormalize(Detail::IkProjectOntoPlane(Detail::IkSubtract(pole, upperLeg), axis));
                const auto angle = std::atan2(Detail::IkDot(axis, Detail::IkCross(from, to)),
                                              std::clamp(Detail::IkDot(from, to), -1.0F, 1.0F));
                const auto sine = std::sin(angle * 0.5F);
                const Quaternion rotation{axis.X * sine, axis.Y * sine, axis.Z * sine, std::cos(angle * 0.5F)};
                const auto originalRootRotation = localPose[*chain[0]].Rotation;
                const auto originalFootRotation = localPose[*chain[2]].Rotation;
                const auto restoreFootOrientation = [&]
                {
                    try
                    {
                        // Preserve the full parent basis: extracting model rotations loses shear
                        // introduced by a nonuniformly scaled ancestor during a partial correction.
                        const auto corrected = RiggingDetail::WorldMatrices(skeleton, localPose);
                        const auto parent = skeleton.Bones()[*chain[2]].Parent;
                        if (parent < 0)
                            return false;
                        const auto desiredLocal = Math::Multiply(
                            Math::Inverse(corrected[static_cast<std::size_t>(parent)]), sampledFootMatrix);
                        Vector3 translation, scale;
                        Quaternion localRotation;
                        if (!Math::DecomposeTransform(desiredLocal, translation, localRotation, scale))
                            return false;
                        localPose[*chain[2]].Rotation = localRotation;
                        return true;
                    }
                    catch (const std::exception&)
                    {
                        return false;
                    }
                };
                if (!RiggingDetail::ApplyBoneModelRotationDelta(skeleton, localPose, *chain[0], rotation,
                                                                settings.Weight * runtimeWeight * chainWeight) ||
                    !restoreFootOrientation())
                {
                    localPose[*chain[0]].Rotation = originalRootRotation;
                    localPose[*chain[2]].Rotation = originalFootRotation;
                    return "Foot grounding could not stabilize the unsupported leg.";
                }
            }
        }
        if (unsupportedFootDropRatio)
        {
            for (std::size_t chainIndex = 0; chainIndex < chains.size(); ++chainIndex)
            {
                if (!unsupportedFeet[chainIndex])
                    continue;
                const auto& chain = chains[chainIndex];
                ModelBoneMatrices(skeleton, localPose, runtimeState.ModelMatrixScratch);
                const auto& matrices = runtimeState.ModelMatrixScratch;
                const auto upperLeg = Math::TransformPoint(matrices[*chain[0]], {});
                const auto knee = Math::TransformPoint(matrices[*chain[1]], {});
                const auto foot = Math::TransformPoint(matrices[*chain[2]], {});
                const auto target = Detail::ProceduralUnsupportedFootTarget(
                    foot, gravityUpModel, distance(upperLeg, knee) + distance(knee, foot), *unsupportedFootDropRatio);
                auto& stability = chainIndex == 0 ? runtimeState.LeftFootIkState : runtimeState.RightFootIkState;
                const auto pole =
                    Detail::StableAutomaticLimbPole(upperLeg, knee, foot, target, kneeReference, deltaSeconds,
                                                    settings.ResponseTime, settings.KneeStability, stability);
                const auto footWeights = proceduralFootWeights.value_or(std::array{1.0F, 1.0F});
                const auto weight = settings.Weight * runtimeWeight * std::clamp(footWeights[chainIndex], 0.0F, 1.0F);
                (void)SolveTwoBoneIkCached(skeleton, localPose, {*chain[0], *chain[1], *chain[2], target, pole, weight},
                                           runtimeState.ModelMatrixScratch);
            }
        }
        return {};
    }

    void SceneRuntimeSession::Impl::ApplyRootMotion(const Entity& entity, const AnimatorSample& sample,
                                                    AnimatorComponent& animator)
    {
        if (!animator.ApplyRootMotion())
            return;
        const auto body = entity.GetComponent<RigidBodyComponent>();
        if (body && body->Motion() == PhysicsMotionType::Dynamic)
        {
            animator.SetRuntimeDiagnostic(
                "Root motion is disabled because the Animator shares an entity with a dynamic rigid body.");
            return;
        }
        bool routedToCharacter = false;
        if (const auto character = entity.GetComponent<CharacterControllerComponent>();
            character && character->Enabled())
        {
            (void)character->QueueDesiredMovement(sample.RootMotion);
            routedToCharacter = true;
        }
        const auto transform = entity.GetComponent<TransformComponent>();
        if (!transform)
            return;
        const auto position = transform->LocalPosition();
        if (!routedToCharacter)
        {
            transform->SetLocalPosition(
                {position.X + sample.RootMotion.X, position.Y + sample.RootMotion.Y, position.Z + sample.RootMotion.Z});
        }
        if (sample.RootRotation != Quaternion{})
        {
            const auto current = Math::ComposeTransform({}, transform->LocalRotation(), {1.0F, 1.0F, 1.0F});
            const auto delta = Math::ComposeTransform({}, sample.RootRotation, {1.0F, 1.0F, 1.0F});
            Vector3 ignoredPosition;
            Vector3 ignoredScale;
            Quaternion rotation;
            if (Math::DecomposeTransform(Math::Multiply(current, delta), ignoredPosition, rotation, ignoredScale))
                transform->SetLocalRotation(rotation);
        }
    }

    [[nodiscard]] bool SceneRuntimeSession::Impl::PrepareProceduralAnimator(
        const Entity& entity, AnimatorComponent& animator, AnimationRuntimeState& state,
        Ref<const SkeletonAsset>& skeleton, Ref<const ProceduralMotionProfileAsset>& profile)
    {
        auto targetSkeleton = animator.Skeleton();
        if (animator.SkinnedMesh())
        {
            const auto skin =
                Assets->Load<SkinnedMeshAsset>(animator.SkinnedMesh(), AssetPriority::High).TryGetLoaded();
            if (!skin)
            {
                animator.SetRuntimeDiagnostic("Procedural Animator is waiting for its skinned mesh to load.");
                return false;
            }
            targetSkeleton = skin->Skeleton();
            if (!targetSkeleton)
            {
                animator.SetRuntimeDiagnostic("The assigned skinned mesh does not reference a skeleton.");
                return false;
            }
            if (animator.Skeleton() != targetSkeleton)
                animator.SetSkeleton(targetSkeleton);
        }

        const bool changed = state.PoseSource != animator.PoseSource() || state.Skeleton != targetSkeleton ||
                             state.Skin != animator.SkinnedMesh() ||
                             state.ProceduralProfile != animator.ProceduralProfile() ||
                             state.RigDefinition != animator.RigDefinition();
        if (changed)
        {
            state = {};
            state.PoseSource = AnimatorPoseSource::ProceduralHumanoid;
            state.Skeleton = targetSkeleton;
            state.Skin = animator.SkinnedMesh();
            state.ProceduralProfile = animator.ProceduralProfile();
            state.RigDefinition = animator.RigDefinition();
            if (state.Skeleton)
                state.SkeletonHandle = Assets->Load<SkeletonAsset>(state.Skeleton, AssetPriority::High);
            if (state.ProceduralProfile)
            {
                state.ProceduralProfileHandle =
                    Assets->Load<ProceduralMotionProfileAsset>(state.ProceduralProfile, AssetPriority::High);
            }
            if (state.RigDefinition)
                state.RigDefinitionHandle = Assets->Load<RigDefinitionAsset>(state.RigDefinition, AssetPriority::High);
            animator.SetRuntimeDiagnostic({});
        }
        if (!state.Skeleton || !state.ProceduralProfile || !state.RigDefinition)
        {
            animator.SetRuntimeDiagnostic(
                "Procedural Humanoid requires skeleton, motion profile, and Rig Definition assets.");
            return false;
        }
        skeleton = state.SkeletonHandle.TryGetLoaded();
        profile = state.ProceduralProfileHandle.TryGetLoaded();
        const auto rig = state.RigDefinitionHandle.TryGetLoaded();
        if (!skeleton || !profile || !rig)
        {
            animator.SetRuntimeDiagnostic("Procedural Animator is waiting for its profile and rig dependencies.");
            return false;
        }

        const auto skeletonRevision = state.SkeletonHandle.Revision();
        const auto profileRevision = state.ProceduralProfileHandle.Revision();
        const auto rigRevision = state.RigDefinitionHandle.Revision();
        if (!state.ProceduralInitialized || state.SkeletonRevision != skeletonRevision ||
            state.ProceduralProfileRevision != profileRevision || state.RigDefinitionRevision != rigRevision)
        {
            state.BoneIndices.clear();
            state.SemanticBoneIndices.clear();
            for (std::uint32_t index = 0; index < skeleton->Bones().size(); ++index)
                state.BoneIndices.emplace(skeleton->Bones()[index].Name, index);
            for (const auto& bone : rig->Definition().Bones)
            {
                const auto found = state.BoneIndices.find(bone.Name);
                if (bone.Semantic != RigBoneSemantic::None && found != state.BoneIndices.end())
                    state.SemanticBoneIndices.emplace(bone.Semantic, found->second);
            }
            constexpr std::array required{
                RigBoneSemantic::Pelvis,        RigBoneSemantic::Spine,     RigBoneSemantic::LeftUpperArm,
                RigBoneSemantic::LeftLowerArm,  RigBoneSemantic::LeftHand,  RigBoneSemantic::RightUpperArm,
                RigBoneSemantic::RightLowerArm, RigBoneSemantic::RightHand, RigBoneSemantic::LeftUpperLeg,
                RigBoneSemantic::LeftLowerLeg,  RigBoneSemantic::LeftFoot,  RigBoneSemantic::RightUpperLeg,
                RigBoneSemantic::RightLowerLeg, RigBoneSemantic::RightFoot};
            const auto missing = std::ranges::find_if(required, [&](const auto semantic)
                                                      { return !state.SemanticBoneIndices.contains(semantic); });
            if (missing != required.end())
            {
                animator.SetRuntimeDiagnostic("Procedural Humanoid rig is missing semantic bone '" +
                                              std::string(RigBoneSemanticName(*missing)) + "'.");
                return false;
            }
            state.BindProceduralPose.clear();
            state.BindProceduralPose.reserve(skeleton->Bones().size());
            for (const auto& bone : skeleton->Bones())
                state.BindProceduralPose.push_back(bone.BindPose);
            state.PreviousProceduralPose = state.BindProceduralPose;
            state.CurrentProceduralPose = state.BindProceduralPose;
            state.TargetProceduralPose = state.BindProceduralPose;
            state.PublishedProceduralPose = state.BindProceduralPose;
            ModelBoneMatrices(*skeleton, state.BindProceduralPose, state.BindModelMatrices);
            state.ModelMatrixScratch.resize(skeleton->Bones().size());
            state.PublishedModelMatrices.resize(skeleton->Bones().size());
            state.SkinPaletteCache.resize(skeleton->Bones().size());
            state.CharacterBodyScratch.reserve(4);
            state.FootGroundingRequestCache.Contacts.reserve(2);
            state.ProceduralDebugSnapshots = {std::make_shared<AnimatorDebugSnapshot>(),
                                              std::make_shared<AnimatorDebugSnapshot>()};
            for (auto& snapshot : state.ProceduralDebugSnapshots)
            {
                snapshot->Pose.resize(skeleton->Bones().size());
                for (std::size_t index = 0; index < skeleton->Bones().size(); ++index)
                {
                    snapshot->Pose[index].Name = skeleton->Bones()[index].Name;
                    snapshot->Pose[index].Parent = skeleton->Bones()[index].Parent;
                }
            }
            state.ProceduralDebugRevision = 0;
            state.SkeletonRevision = skeletonRevision;
            state.ProceduralProfileRevision = profileRevision;
            state.RigDefinitionRevision = rigRevision;
            state.ProceduralInitialized = true;
            state.PreviousGrounded = true;
            state.PreviousProceduralState = ProceduralMotionState::Idle;
            state.FilteredHorizontalVelocity = {};
            state.FilteredFacingWorldDirection = {0.0F, 0.0F, 1.0F};
            state.PreviousRootForward = {};
            state.HasPreviousRootForward = false;
            state.PreLandingAmount = 0.0F;
            state.LeftFootPlantState = {};
            state.RightFootPlantState = {};
            if (skeletonRevision > 1 || profileRevision > 1 || rigRevision > 1)
            {
                animator.SetRuntimeDiagnostic(
                    "Procedural profile or rig reload reset planted contacts and pose interpolation safely.");
            }
        }
        (void)entity;
        return true;
    }

    [[nodiscard]] float SceneRuntimeSession::Impl::VectorLength(const Vector3 value) noexcept
    {
        return std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
    }

    [[nodiscard]] Vector3 SceneRuntimeSession::Impl::NormalizeHorizontal(const Vector3 value,
                                                                         const Vector3 fallback) noexcept
    {
        const auto length = std::sqrt(value.X * value.X + value.Z * value.Z);
        return length > 0.000001F ? Vector3{value.X / length, 0.0F, value.Z / length} : fallback;
    }

    [[nodiscard]] bool SceneRuntimeSession::Impl::CrossedPhase(const float previous, const float current,
                                                               const float target) noexcept
    {
        return current >= previous ? previous < target && current >= target : previous < target || current >= target;
    }

    [[nodiscard]] float SceneRuntimeSession::Impl::SignedHorizontalAngleDegrees(const Vector3 from,
                                                                                const Vector3 to) noexcept
    {
        const auto first = NormalizeHorizontal(from);
        const auto second = NormalizeHorizontal(to);
        if (VectorLength(first) <= 0.000001F || VectorLength(second) <= 0.000001F)
            return 0.0F;
        const auto cosine = std::clamp(first.X * second.X + first.Z * second.Z, -1.0F, 1.0F);
        const auto angle = std::acos(cosine) * 57.2957795131F;
        return first.X * second.Z - first.Z * second.X < 0.0F ? -angle : angle;
    }

    [[nodiscard]] Vector3 SceneRuntimeSession::Impl::RespondHorizontalDirection(const Vector3 current,
                                                                                const Vector3 target,
                                                                                const float blend) noexcept
    {
        const auto from = NormalizeHorizontal(current, {0.0F, 0.0F, 1.0F});
        const auto to = NormalizeHorizontal(target, from);
        const auto radians = SignedHorizontalAngleDegrees(from, to) * std::clamp(blend, 0.0F, 1.0F) * 0.0174532925199F;
        const auto cosine = std::cos(radians);
        const auto sine = std::sin(radians);
        return NormalizeHorizontal({from.X * cosine - from.Z * sine, 0.0F, from.X * sine + from.Z * cosine}, to);
    }

    [[nodiscard]] float SceneRuntimeSession::Impl::PreLandingAmount(const Entity& entity, const Entity& characterRoot,
                                                                    const Ref<CharacterControllerComponent>& character,
                                                                    const Vector3 velocity,
                                                                    const ProceduralMotionProfile& profile,
                                                                    AnimationRuntimeState& state) const
    {
        if (!PhysicsWorldService || velocity.Y >= 0.0F || profile.PreLandingProbeTime <= 0.0F)
            return 0.0F;
        const auto transform = characterRoot ? characterRoot.GetComponent<TransformComponent>()
                                             : entity.GetComponent<TransformComponent>();
        if (!transform)
            return 0.0F;

        const auto downwardSpeed = -velocity.Y;
        const auto maximumClearance = downwardSpeed * profile.PreLandingProbeTime +
                                      0.5F * 9.81F * profile.PreLandingProbeTime * profile.PreLandingProbeTime;
        if (maximumClearance <= 0.000001F)
            return 0.0F;
        const auto bodyClearance = character ? character->Height() * 0.5F : 0.0F;

        state.CharacterBodyScratch.clear();
        const auto physicsRoot = characterRoot ? characterRoot : entity;
        for (const auto& [bodyEntityId, physics] : PhysicsBodies)
        {
            if (Detail::IsSameOrDescendantOf(Runtime->FindEntity(bodyEntityId), physicsRoot))
                state.CharacterBodyScratch.push_back(physics.Body);
        }

        const auto hits = PhysicsWorldService->RayCast({.Origin = transform->WorldPosition(),
                                                        .Direction = {0.0F, -1.0F, 0.0F},
                                                        .MaximumDistance = bodyClearance + maximumClearance,
                                                        .Mask = profile.CollisionMask,
                                                        .IncludeTriggers = false,
                                                        .Layer = character ? character->Layer() : 1U});
        const auto landing = std::ranges::find_if(
            hits,
            [&](const PhysicsQueryHit& hit)
            {
                if (std::ranges::find(state.CharacterBodyScratch, hit.Body) != state.CharacterBodyScratch.end())
                    return false;
                const auto normalLength = VectorLength(hit.Normal);
                if (normalLength <= 0.000001F)
                    return false;
                const auto slope = std::acos(std::clamp(hit.Normal.Y / normalLength, -1.0F, 1.0F)) * 57.2957795131F;
                return slope <= profile.MaximumSlopeDegrees;
            });
        if (landing == hits.end())
            return 0.0F;
        return Detail::ProceduralPreLandingAmount(std::max(0.0F, landing->Distance - bodyClearance), downwardSpeed,
                                                  profile.PreLandingProbeTime);
    }

    void SceneRuntimeSession::Impl::DispatchProceduralFootEvents(const Entity& entity, AnimationRuntimeState& state,
                                                                 const ProceduralLocomotionState& procedural)
    {
        if (!Runtime || (procedural.State != ProceduralMotionState::Locomotion &&
                         procedural.State != ProceduralMotionState::TurnInPlace))
            return;
        const auto emit = [&](const bool left, const bool plant)
        {
            const auto name = plant ? "Procedural.FootPlant" : "Procedural.FootLift";
            const auto& footState = left ? state.LeftFootPlantState : state.RightFootPlantState;
            AssetId physicsMaterial;
            if (footState.Support)
            {
                const auto supportBody = PhysicsBodies.find(*footState.Support);
                if (supportBody != PhysicsBodies.end())
                    physicsMaterial = supportBody->second.Material;
            }
            Runtime->DispatchProceduralMotionEvent(
                entity.Id(),
                {plant ? ProceduralMotionEventType::FootPlant : ProceduralMotionEventType::FootLift,
                 left ? ProceduralFootSide::Left : ProceduralFootSide::Right, procedural.State, procedural.GaitPhase,
                 std::clamp(procedural.Speed / 6.0F, 0.0F, 1.0F), footState.Plant.Position, footState.Plant.Normal,
                 footState.Support.value_or(EntityId{}), physicsMaterial});
            Runtime->DispatchAnimationEvent(entity.Id(),
                                            {name, procedural.GaitPhase, left ? 0 : 1, procedural.Speed, {}});
            if (plant)
            {
                Runtime->DispatchAnimationEvent(entity.Id(), {"Footstep",
                                                              procedural.GaitPhase,
                                                              left ? 0 : 1,
                                                              std::clamp(procedural.Speed / 6.0F, 0.0F, 1.0F),
                                                              {}});
            }
        };
        constexpr float liftPhase = 0.02F;
        constexpr float plantPhase = 0.50F;
        for (std::size_t foot = 0; foot < 2; ++foot)
        {
            const auto offset = foot == 0 ? 0.0F : 0.5F;
            auto previous = state.PreviousGaitPhase + offset;
            auto current = state.GaitPhase + offset;
            previous -= std::floor(previous);
            current -= std::floor(current);
            if (CrossedPhase(previous, current, liftPhase))
                emit(foot == 0, false);
            if (CrossedPhase(previous, current, plantPhase))
                emit(foot == 0, true);
        }
    }
} // namespace Keire
