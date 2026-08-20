#include "KeireInternal/Scenes/SceneRuntimeSessionImpl.h"

#include <cmath>

namespace Keire
{
    void SceneRuntimeSession::Impl::AdvanceProceduralAnimation(const float deltaSeconds)
    {
        if (!Assets || !Runtime || deltaSeconds <= 0.0F)
            return;
        for (const auto& entity : Runtime->Query<AnimatorComponent>())
        {
            const auto animator = entity.GetComponent<AnimatorComponent>();
            if (!animator || animator->PoseSource() != AnimatorPoseSource::ProceduralHumanoid ||
                !entity.ActiveInHierarchy() || !animator->Enabled())
            {
                continue;
            }
            auto& statePointer = Animators[entity.Id()];
            if (!statePointer)
                statePointer = std::make_unique<AnimationRuntimeState>();
            auto& state = *statePointer;
            Ref<const SkeletonAsset> skeleton;
            Ref<const ProceduralMotionProfileAsset> profileAsset;
            if (!PrepareProceduralAnimator(entity, *animator, state, skeleton, profileAsset))
                continue;
            const auto& profile = profileAsset->Profile();
            animator->SetRuntimeDiagnostic({});
            state.PreviousProceduralPose = state.CurrentProceduralPose;
            state.PreviousGaitPhase = state.GaitPhase;
            state.ProceduralIntent = animator->ConsumeProceduralLocomotionIntent();
            state.ProceduralTime += deltaSeconds;
            ++state.ProceduralTick;

            const auto characterRoot = Detail::FindCharacterControllerRoot(entity);
            const auto character = characterRoot ? characterRoot.GetComponent<CharacterControllerComponent>()
                                                 : entity.GetComponent<CharacterControllerComponent>();
            const auto characterState = character ? character->RuntimeState() : CharacterControllerRuntimeState{};
            const auto grounded = character ? characterState.Grounded : false;
            const auto velocity = character ? characterState.Velocity : state.ProceduralIntent.DesiredWorldVelocity;
            const Vector3 horizontalVelocity{velocity.X, 0.0F, velocity.Z};
            const auto speed = VectorLength(horizontalVelocity);
            const auto previousFilteredVelocity = state.FilteredHorizontalVelocity;
            const auto velocityBlend = Detail::ProceduralResponseBlend(deltaSeconds, profile.VelocityResponseTime);
            state.FilteredHorizontalVelocity = {
                previousFilteredVelocity.X + (horizontalVelocity.X - previousFilteredVelocity.X) * velocityBlend, 0.0F,
                previousFilteredVelocity.Z + (horizontalVelocity.Z - previousFilteredVelocity.Z) * velocityBlend};
            const auto poseSpeed = VectorLength(state.FilteredHorizontalVelocity);
            state.HorizontalAcceleration =
                VectorLength(RiggingDetail::Subtract(state.FilteredHorizontalVelocity, previousFilteredVelocity)) /
                deltaSeconds;
            Vector3 rootForward{0.0F, 0.0F, 1.0F};
            const auto rootTransform = characterRoot ? characterRoot.GetComponent<TransformComponent>()
                                                     : entity.GetComponent<TransformComponent>();
            if (rootTransform)
                rootForward = NormalizeHorizontal(Math::TransformDirection(rootTransform->WorldMatrix(), rootForward),
                                                  rootForward);
            if (!state.HasPreviousRootForward)
                state.FilteredFacingWorldDirection = rootForward;
            const auto requestedFacing = NormalizeHorizontal(state.ProceduralIntent.FacingWorldDirection);
            const auto hasRequestedFacing = VectorLength(requestedFacing) > 0.000001F;
            const auto facingTarget = hasRequestedFacing ? requestedFacing : rootForward;
            state.FilteredFacingWorldDirection =
                RespondHorizontalDirection(state.FilteredFacingWorldDirection, facingTarget,
                                           Detail::ProceduralResponseBlend(deltaSeconds, profile.FacingResponseTime));
            const auto facingError = SignedHorizontalAngleDegrees(rootForward, state.FilteredFacingWorldDirection);
            const auto rootAngularVelocity =
                state.HasPreviousRootForward
                    ? SignedHorizontalAngleDegrees(state.PreviousRootForward, rootForward) / deltaSeconds
                    : 0.0F;
            state.RootAngularVelocityDegrees =
                hasRequestedFacing
                    ? std::clamp(facingError / std::max(profile.FacingResponseTime, deltaSeconds), -360.0F, 360.0F)
                    : rootAngularVelocity;
            const auto turningInPlace = grounded && speed <= profile.MinimumMovementSpeed &&
                                        (std::abs(facingError) >= profile.TurnInPlaceThresholdDegrees ||
                                         std::abs(rootAngularVelocity) >= profile.TurnInPlaceThresholdDegrees);
            const auto justLanded = grounded && !state.PreviousGrounded;
            const auto confirmedTakeoff =
                !grounded && state.PreviousGrounded && (state.ProceduralIntent.JumpRequested || velocity.Y > 0.1F);
            auto motionState = state.ProceduralState.State;
            if (justLanded)
            {
                motionState = ProceduralMotionState::Landing;
                state.LandingElapsed = 0.0F;
                state.ProceduralState.LandingIntensity =
                    std::clamp(-state.PreviousVerticalSpeed / profile.MaximumLandingSpeed, 0.0F, 1.0F);
                Runtime->DispatchProceduralMotionEvent(entity.Id(),
                                                       {ProceduralMotionEventType::Land, ProceduralFootSide::None,
                                                        motionState, 0.0F, state.ProceduralState.LandingIntensity});
                Runtime->DispatchAnimationEvent(
                    entity.Id(), {"Procedural.Land", 0.0F, 0, state.ProceduralState.LandingIntensity, {}});
            }
            else if (!grounded)
            {
                if (confirmedTakeoff)
                {
                    motionState = ProceduralMotionState::Takeoff;
                    state.ApexSent = false;
                    Runtime->DispatchProceduralMotionEvent(
                        entity.Id(),
                        {ProceduralMotionEventType::Takeoff, ProceduralFootSide::None, motionState, 0.0F, 1.0F});
                    Runtime->DispatchAnimationEvent(entity.Id(), {"Procedural.Takeoff", 0.0F, 0, 1.0F, {}});
                }
                else if (velocity.Y > 0.08F)
                {
                    motionState = ProceduralMotionState::Rising;
                }
                else
                {
                    motionState = ProceduralMotionState::Falling;
                    if (!state.ApexSent && state.PreviousVerticalSpeed > 0.0F)
                    {
                        state.ApexSent = true;
                        Runtime->DispatchProceduralMotionEvent(
                            entity.Id(),
                            {ProceduralMotionEventType::Apex, ProceduralFootSide::None, motionState, 0.0F, 1.0F});
                        Runtime->DispatchAnimationEvent(entity.Id(), {"Procedural.Apex", 0.0F, 0, 1.0F, {}});
                    }
                }
            }
            else if (motionState == ProceduralMotionState::Landing &&
                     state.LandingElapsed < profile.LandingRecoveryTime)
            {
                state.LandingElapsed += deltaSeconds;
            }
            else if (speed > profile.MinimumMovementSpeed)
            {
                motionState = ProceduralMotionState::Locomotion;
                state.ProceduralState.LandingIntensity = 0.0F;
            }
            else if (turningInPlace)
            {
                motionState = ProceduralMotionState::TurnInPlace;
                state.ProceduralState.LandingIntensity = 0.0F;
            }
            else
            {
                motionState = ProceduralMotionState::Idle;
                state.ProceduralState.LandingIntensity = 0.0F;
                if (state.PreviousProceduralState == ProceduralMotionState::Locomotion)
                    state.StopSettleRemaining = profile.StopSettleTime;
            }
            if (motionState == ProceduralMotionState::Takeoff && !confirmedTakeoff)
                motionState = velocity.Y > 0.08F ? ProceduralMotionState::Rising : ProceduralMotionState::Falling;
            if (motionState != state.ProceduralState.State)
            {
                Runtime->DispatchProceduralMotionEvent(entity.Id(),
                                                       {ProceduralMotionEventType::StateChanged,
                                                        ProceduralFootSide::None, motionState, state.GaitPhase, 0.0F});
                Runtime->DispatchAnimationEvent(entity.Id(), {"Procedural.StateChanged", 0.0F,
                                                              static_cast<std::int32_t>(motionState), 0.0F,
                                                              std::string(ProceduralMotionStateName(motionState))});
            }

            auto quality = animator->ProceduralQuality();
            if (quality == ProceduralMotionQuality::Auto)
            {
                quality = ProceduralMotionQuality::High;
                const auto animatorTransform = entity.GetComponent<TransformComponent>();
                if (animatorTransform)
                {
                    auto closestSquared = std::numeric_limits<float>::max();
                    for (const auto& cameraEntity : Runtime->Query<CameraComponent>())
                    {
                        const auto camera = cameraEntity.GetComponent<CameraComponent>();
                        const auto cameraTransform = cameraEntity.GetComponent<TransformComponent>();
                        if (!camera || !camera->Primary() || !cameraTransform || !cameraEntity.ActiveInHierarchy())
                        {
                            continue;
                        }
                        const auto delta = RiggingDetail::Subtract(cameraTransform->WorldPosition(),
                                                                   animatorTransform->WorldPosition());
                        closestSquared = std::min(closestSquared, RiggingDetail::Dot(delta, delta));
                    }
                    if (closestSquared > 50.0F * 50.0F)
                        quality = ProceduralMotionQuality::Low;
                    else if (closestSquared > 20.0F * 20.0F)
                        quality = ProceduralMotionQuality::Medium;
                }
            }
            state.ProceduralState.State = motionState;
            state.ProceduralState.Quality = quality;
            state.ProceduralState.ActualWorldVelocity = velocity;
            state.ProceduralState.GroundNormal = characterState.GroundNormal;
            state.ProceduralState.Speed = speed;
            state.ProceduralState.VerticalSpeed = velocity.Y;
            state.ProceduralState.Grounded = grounded;
            state.PreLandingAmount = motionState == ProceduralMotionState::Falling
                                         ? PreLandingAmount(entity, characterRoot, character, velocity, profile, state)
                                         : 0.0F;

            auto phaseRate = 0.0F;
            if (motionState == ProceduralMotionState::Locomotion)
            {
                phaseRate =
                    Detail::ProceduralGaitPhaseRate(speed, state.ProceduralIntent.RunBlend, profile.WalkSpeed,
                                                    profile.SprintSpeed, profile.WalkCadence, profile.SprintCadence);
                state.GaitPhase += deltaSeconds * phaseRate;
                state.GaitPhase -= std::floor(state.GaitPhase);
            }
            else if (motionState == ProceduralMotionState::TurnInPlace)
            {
                state.GaitPhase +=
                    deltaSeconds * std::abs(state.RootAngularVelocityDegrees) / std::max(profile.TurnStepDegrees, 1.0F);
                state.GaitPhase -= std::floor(state.GaitPhase);
            }
            else if (motionState == ProceduralMotionState::Idle && state.GaitPhase != 0.0F)
            {
                state.StopSettleRemaining = std::max(0.0F, state.StopSettleRemaining - deltaSeconds);
                const auto settle = profile.StopSettleTime <= 0.0F ? 1.0F : deltaSeconds / profile.StopSettleTime;
                const auto nearest = state.GaitPhase < 0.5F ? 0.0F : 1.0F;
                state.GaitPhase += (nearest - state.GaitPhase) * std::clamp(settle, 0.0F, 1.0F);
                if (state.GaitPhase >= 0.999F || state.GaitPhase <= 0.001F)
                    state.GaitPhase = 0.0F;
            }
            state.ProceduralState.GaitPhase = state.GaitPhase;

            const auto solveInterval = quality == ProceduralMotionQuality::Low      ? std::uint64_t{4}
                                       : quality == ProceduralMotionQuality::Medium ? std::uint64_t{2}
                                                                                    : std::uint64_t{1};
            if ((state.ProceduralTick - 1U) % solveInterval != 0U)
            {
                animator->SetRuntimeProceduralState(state.ProceduralState);
                DispatchProceduralFootEvents(entity, state, state.ProceduralState);
                state.PreviousGrounded = grounded;
                state.PreviousVerticalSpeed = velocity.Y;
                state.PreviousProceduralState = motionState;
                state.PreviousRootForward = rootForward;
                state.HasPreviousRootForward = true;
                continue;
            }

            auto& pose = state.TargetProceduralPose;
            std::ranges::copy(state.BindProceduralPose, pose.begin());
            const auto semantic = [&](const RigBoneSemantic value) { return state.SemanticBoneIndices.at(value); };
            const auto pelvis = semantic(RigBoneSemantic::Pelvis);
            const auto leftUpper = semantic(RigBoneSemantic::LeftUpperLeg);
            const auto leftLower = semantic(RigBoneSemantic::LeftLowerLeg);
            const auto leftFoot = semantic(RigBoneSemantic::LeftFoot);
            const auto rightUpper = semantic(RigBoneSemantic::RightUpperLeg);
            const auto rightLower = semantic(RigBoneSemantic::RightLowerLeg);
            const auto rightFoot = semantic(RigBoneSemantic::RightFoot);

            const auto& bindMatrices = state.BindModelMatrices;
            const auto position = [&](const std::uint32_t bone)
            { return Math::TransformPoint(bindMatrices[bone], {}); };
            const auto distance = [&](const std::uint32_t first, const std::uint32_t second)
            { return VectorLength(RiggingDetail::Subtract(position(first), position(second))); };
            const auto leftLegLength = distance(leftUpper, leftLower) + distance(leftLower, leftFoot);
            const auto rightLegLength = distance(rightUpper, rightLower) + distance(rightLower, rightFoot);
            const auto legLength = std::max((leftLegLength + rightLegLength) * 0.5F, 0.001F);
            const auto locomotionWeight =
                motionState == ProceduralMotionState::Locomotion
                    ? Detail::ProceduralLocomotionPoseWeight(poseSpeed, profile.MinimumMovementSpeed, profile.WalkSpeed)
                : motionState == ProceduralMotionState::TurnInPlace
                    ? std::clamp(std::abs(state.RootAngularVelocityDegrees) / 180.0F, 0.25F, 1.0F)
                : motionState == ProceduralMotionState::Idle && profile.StopSettleTime > 0.0F
                    ? std::clamp(state.StopSettleRemaining / profile.StopSettleTime, 0.0F, 1.0F)
                    : 0.0F;
            const auto bob =
                profile.PelvisMotion.Evaluate(state.GaitPhase) * profile.PelvisBobRatio * legLength * locomotionWeight;
            const auto sway =
                std::sin(state.GaitPhase * 6.28318530718F) * profile.PelvisSwayRatio * legLength * locomotionWeight;
            pose[pelvis].Translation.X += sway;
            pose[pelvis].Translation.Y +=
                bob - profile.CrouchDepthRatio * legLength * state.ProceduralIntent.CrouchAmount;
            const auto facingYaw = std::clamp(facingError, -profile.TurnStepDegrees, profile.TurnStepDegrees);
            pose[pelvis].Rotation = RiggingDetail::Normalize(RiggingDetail::Multiply(
                pose[pelvis].Rotation, Math::EulerDegreesToQuaternion({0.0F, facingYaw, 0.0F})));
            if (motionState == ProceduralMotionState::Landing)
            {
                const auto recovery = std::clamp(state.LandingElapsed / profile.LandingRecoveryTime, 0.0F, 1.0F);
                pose[pelvis].Translation.Y -= profile.LandingCompression.Evaluate(recovery) *
                                              profile.LandingCompressionRatio * legLength *
                                              state.ProceduralState.LandingIntensity;
            }
            else if (motionState == ProceduralMotionState::Takeoff)
            {
                pose[pelvis].Translation.Y -= profile.TakeoffCompressionRatio * legLength;
            }
            else if (!grounded)
            {
                const auto airborneAmount = velocity.Y > 0.0F ? std::clamp(1.0F - velocity.Y / 8.0F, 0.0F, 1.0F) : 1.0F;
                pose[pelvis].Translation.Y -= profile.AirborneTuck.Evaluate(airborneAmount) *
                                              profile.AirborneTuckRatio * legLength * 0.25F *
                                              (1.0F - state.PreLandingAmount);
            }

            const auto animatorTransform = entity.GetComponent<TransformComponent>();
            Matrix4 worldToModel;
            try
            {
                worldToModel = Math::Inverse(animatorTransform->WorldMatrix());
            }
            catch (const std::exception&)
            {
                animator->SetRuntimeDiagnostic("Procedural Animator world transform is not invertible.");
                continue;
            }
            auto localMotion = Math::TransformDirection(worldToModel, state.FilteredHorizontalVelocity);
            localMotion.Y = 0.0F;
            const auto modelSpeed = VectorLength(localMotion);
            localMotion = motionState == ProceduralMotionState::TurnInPlace
                              ? Vector3{state.RootAngularVelocityDegrees < 0.0F ? -1.0F : 1.0F, 0.0F, 0.0F}
                              : NormalizeHorizontal(localMotion, {0.0F, 0.0F, 1.0F});
            const auto directionalRatio = Detail::ProceduralDirectionalStrideRatio(
                localMotion, profile.LateralStrideRatio, profile.BackwardStrideRatio);
            const auto stride = motionState == ProceduralMotionState::Locomotion
                                    ? Detail::ProceduralStrideLength(
                                          modelSpeed, phaseRate, legLength, profile.StrideLengthRatio, directionalRatio,
                                          state.ProceduralIntent.RunBlend, profile.WalkSpeed, profile.SprintSpeed,
                                          profile.WalkCadence, profile.SprintCadence)
                                    : legLength * profile.StrideLengthRatio * directionalRatio * locomotionWeight;

            const auto modelRight = RiggingDetail::Normalize(
                RiggingDetail::Subtract(position(rightUpper), position(leftUpper)), {1.0F, 0.0F, 0.0F});
            const auto solveLeg = [&](const std::uint32_t upper, const std::uint32_t lower, const std::uint32_t foot,
                                      const float offset, const float side)
            {
                auto phase = state.GaitPhase + offset;
                phase -= std::floor(phase);
                ModelBoneMatrices(*skeleton, pose, state.ModelMatrixScratch);
                const auto& matrices = state.ModelMatrixScratch;
                const auto currentPelvis = Math::TransformPoint(matrices[pelvis], {});
                const auto currentFoot = Math::TransformPoint(matrices[foot], {});
                auto target = currentFoot;
                if (grounded)
                {
                    const auto travel = profile.StrideTravel.Evaluate(phase) * stride * 0.5F;
                    target.X += localMotion.X * travel;
                    target.Z += localMotion.Z * travel;
                    target.Y +=
                        profile.FootLift.Evaluate(phase) * profile.StepClearanceRatio * legLength * locomotionWeight;
                }
                else
                {
                    const auto verticalPhase =
                        velocity.Y > 0.0F ? std::clamp(1.0F - velocity.Y / 8.0F, 0.0F, 1.0F) : 1.0F;
                    const auto tuck = profile.AirborneTuck.Evaluate(verticalPhase) * profile.AirborneTuckRatio *
                                          (1.0F - state.PreLandingAmount) -
                                      profile.FallingExtensionRatio * state.PreLandingAmount;
                    target.Y += tuck * legLength;
                    target.Z += profile.AirborneTuckRatio * legLength * 0.22F;
                }
                const auto currentLateralOffset =
                    RiggingDetail::Dot(RiggingDetail::Subtract(currentFoot, currentPelvis), modelRight);
                target = RiggingDetail::Add(
                    target, RiggingDetail::Multiply(
                                modelRight, Detail::ProceduralFootLateralCorrection(currentLateralOffset, legLength,
                                                                                    profile.FootSpacingRatio, side)));
                const auto hip = Math::TransformPoint(matrices[upper], {});
                const auto knee = Math::TransformPoint(matrices[lower], {});
                const auto upperLength = VectorLength(RiggingDetail::Subtract(knee, hip));
                const auto lowerLength = VectorLength(RiggingDetail::Subtract(currentFoot, knee));
                auto hipToTarget = RiggingDetail::Subtract(target, hip);
                const auto targetDistance = VectorLength(hipToTarget);
                const auto distanceForBend = [&](const float degrees)
                {
                    const auto radians = degrees * 0.0174532925199F;
                    return std::sqrt(std::max(0.0F, upperLength * upperLength + lowerLength * lowerLength +
                                                        2.0F * upperLength * lowerLength * std::cos(radians)));
                };
                const auto minimumReach = distanceForBend(profile.MaximumKneeBendDegrees);
                const auto maximumReach = distanceForBend(profile.MinimumKneeBendDegrees);
                if (targetDistance > 0.000001F)
                {
                    const auto constrainedDistance = std::clamp(targetDistance, minimumReach, maximumReach);
                    hipToTarget = RiggingDetail::Multiply(hipToTarget, constrainedDistance / targetDistance);
                    target = RiggingDetail::Add(hip, hipToTarget);
                }
                const auto forward =
                    Vector3{hip.X + localMotion.X * legLength, knee.Y, hip.Z + localMotion.Z * legLength};
                (void)SolveTwoBoneIkCached(*skeleton, pose, {upper, lower, foot, target, forward, 1.0F},
                                           state.ModelMatrixScratch);
            };
            solveLeg(leftUpper, leftLower, leftFoot, 0.0F, -1.0F);
            solveLeg(rightUpper, rightLower, rightFoot, 0.5F, 1.0F);

            const auto rotateBone = [&](const RigBoneSemantic bone, const Vector3 degrees)
            {
                const auto found = state.SemanticBoneIndices.find(bone);
                if (found == state.SemanticBoneIndices.end())
                    return;
                pose[found->second].Rotation = RiggingDetail::Normalize(
                    RiggingDetail::Multiply(pose[found->second].Rotation, Math::EulerDegreesToQuaternion(degrees)));
            };
            const auto arm = profile.ArmSwing.Evaluate(state.GaitPhase) * profile.ArmSwingDegrees * locomotionWeight;
            const auto solveArm = [&](const RigBoneSemantic upperSemantic, const RigBoneSemantic lowerSemantic,
                                      const RigBoneSemantic handSemantic, const float side, const float swing)
            {
                const auto upperFound = state.SemanticBoneIndices.find(upperSemantic);
                const auto lowerFound = state.SemanticBoneIndices.find(lowerSemantic);
                const auto handFound = state.SemanticBoneIndices.find(handSemantic);
                if (upperFound == state.SemanticBoneIndices.end() || lowerFound == state.SemanticBoneIndices.end() ||
                    handFound == state.SemanticBoneIndices.end())
                {
                    return;
                }

                ModelBoneMatrices(*skeleton, pose, state.ModelMatrixScratch);
                const auto& matrices = state.ModelMatrixScratch;
                const auto shoulder = Math::TransformPoint(matrices[upperFound->second], {});
                const auto elbow = Math::TransformPoint(matrices[lowerFound->second], {});
                const auto hand = Math::TransformPoint(matrices[handFound->second], {});
                const auto upperLength = VectorLength(RiggingDetail::Subtract(elbow, shoulder));
                const auto lowerLength = VectorLength(RiggingDetail::Subtract(hand, elbow));
                if (upperLength <= 0.000001F || lowerLength <= 0.000001F)
                    return;

                const auto shoulderCenter =
                    RiggingDetail::Multiply(RiggingDetail::Add(position(semantic(RigBoneSemantic::LeftUpperArm)),
                                                               position(semantic(RigBoneSemantic::RightUpperArm))),
                                            0.5F);
                const auto modelUp = RiggingDetail::Normalize(
                    RiggingDetail::Subtract(shoulderCenter, Math::TransformPoint(matrices[pelvis], {})),
                    {0.0F, 1.0F, 0.0F});
                const auto geometry =
                    Detail::BuildProceduralArmGeometry(modelRight, modelUp, side, profile.ArmRestDropDegrees, swing,
                                                       profile.ElbowBendDegrees, upperLength, lowerLength);
                const auto target =
                    RiggingDetail::Add(shoulder, RiggingDetail::Multiply(geometry.TargetDirection, geometry.Reach));
                const auto pole =
                    RiggingDetail::Add(shoulder, RiggingDetail::Multiply(geometry.PoleDirection, upperLength));
                (void)SolveTwoBoneIkCached(
                    *skeleton, pose, {upperFound->second, lowerFound->second, handFound->second, target, pole, 1.0F},
                    state.ModelMatrixScratch);
            };
            if (state.SemanticBoneIndices.contains(RigBoneSemantic::LeftUpperArm) &&
                state.SemanticBoneIndices.contains(RigBoneSemantic::RightUpperArm))
            {
                solveArm(RigBoneSemantic::LeftUpperArm, RigBoneSemantic::LeftLowerArm, RigBoneSemantic::LeftHand, -1.0F,
                         arm);
                solveArm(RigBoneSemantic::RightUpperArm, RigBoneSemantic::RightLowerArm, RigBoneSemantic::RightHand,
                         1.0F, -arm);
            }
            const auto breathing = std::sin(state.ProceduralTime * profile.BreathingFrequency * 6.28318530718F) *
                                   profile.BreathingAmplitudeDegrees;
            const auto accelerationLean =
                std::clamp(state.HorizontalAcceleration / 30.0F, 0.0F, 1.0F) * profile.MaximumAccelerationLeanDegrees;
            const auto turnLean =
                std::clamp(state.RootAngularVelocityDegrees / 180.0F, -1.0F, 1.0F) * profile.MaximumTurnLeanDegrees;
            rotateBone(RigBoneSemantic::Spine, {breathing + accelerationLean, -arm * 0.12F, -turnLean});
            rotateBone(RigBoneSemantic::Chest,
                       {breathing * 0.5F,
                        arm * profile.SpineCounterRotationDegrees / std::max(profile.ArmSwingDegrees, 0.001F), 0.0F});
            auto lookDirection = NormalizeHorizontal(state.ProceduralIntent.LookWorldDirection, rootForward);
            const auto lookYaw = std::clamp(SignedHorizontalAngleDegrees(rootForward, lookDirection), -55.0F, 55.0F);
            rotateBone(RigBoneSemantic::Neck, {0.0F, lookYaw * 0.35F, 0.0F});
            rotateBone(RigBoneSemantic::Head, {0.0F, lookYaw * 0.65F, 0.0F});

            if (grounded && motionState != ProceduralMotionState::Takeoff)
            {
                AnimatorFootGroundingSettings grounding;
                grounding.Enabled = true;
                grounding.AutomaticBoneMapping = true;
                grounding.AutomaticRaycastDistance = false;
                grounding.LockPlantedFeet = true;
                grounding.Weight = 1.0F;
                grounding.RotationWeight = 1.0F;
                const auto characterHeight = character ? character->Height() : legLength * 2.0F;
                grounding.RaycastHeight = characterHeight * profile.ProbeHeightRatio;
                grounding.RaycastDistance = characterHeight * profile.ProbeDistanceRatio;
                grounding.FootOffset = characterHeight * profile.SoleOffsetRatio;
                grounding.MaximumPelvisAdjustment = characterHeight * profile.MaximumPelvisAdjustmentRatio;
                grounding.PlantDistance = characterHeight * profile.PlantDistanceRatio;
                grounding.ReleaseDistance = characterHeight * profile.ReleaseDistanceRatio;
                grounding.ResponseTime = profile.GroundingResponseTime;
                grounding.MaximumSlopeDegrees = profile.MaximumSlopeDegrees;
                grounding.CollisionMask = profile.CollisionMask;
                auto footWeights = std::array{1.0F, 1.0F};
                if (motionState == ProceduralMotionState::Locomotion ||
                    motionState == ProceduralMotionState::TurnInPlace)
                {
                    footWeights = {Detail::ProceduralFootGroundingWeight(state.GaitPhase),
                                   Detail::ProceduralFootGroundingWeight(state.GaitPhase + 0.5F)};
                }
                const auto diagnostic = ApplyFootGrounding(
                    entity, *skeleton, grounding, 1.0F, animator->SkinnedMesh(), deltaSeconds, pose, state.BoneIndices,
                    state.SemanticBoneIndices, state, profile.MaximumHorizontalPelvisAdjustmentRatio,
                    profile.MaximumAnkleSlopeDegrees, footWeights, 0.10F);
                if (!diagnostic.empty())
                    animator->SetRuntimeDiagnostic(diagnostic);
            }

            if (grounded && locomotionWeight > 0.0F)
            {
                const auto rollFoot = [&](const std::uint32_t foot, const float phaseOffset)
                {
                    const auto toe = state.FootToeBones.find(foot);
                    if (toe == state.FootToeBones.end() || !toe->second)
                        return;
                    auto phase = state.GaitPhase + phaseOffset;
                    phase -= std::floor(phase);
                    const auto roll = profile.FootRoll.Evaluate(phase) * 18.0F * locomotionWeight;
                    pose[*toe->second].Rotation = RiggingDetail::Normalize(RiggingDetail::Multiply(
                        pose[*toe->second].Rotation, Math::EulerDegreesToQuaternion({roll, 0.0F, 0.0F})));
                };
                rollFoot(leftFoot, 0.0F);
                rollFoot(rightFoot, 0.5F);
            }
            else if (Detail::ShouldResetProceduralFootContacts(grounded, motionState))
            {
                state.LeftFootIkState = {};
                state.RightFootIkState = {};
                state.LeftFootGroundingSmoothingState = {};
                state.RightFootGroundingSmoothingState = {};
                state.LeftFootPlantState = {};
                state.RightFootPlantState = {};
            }

            Runtime->DispatchAnimatorIk(entity.Id(), {.LayerWeight = 1.0F});
            const auto overrideDiagnostic = Detail::EvaluateIndependentAnimationIkPasses(
                [&] { return ApplyIkGoals(entity, *skeleton, *animator, pose, state.BoneIndices); },
                [&]
                {
                    return ApplyAuthoredArmIk(entity, *skeleton, *animator, pose, state.BoneIndices,
                                              state.SemanticBoneIndices, state);
                });
            if (!overrideDiagnostic.empty())
                animator->SetRuntimeDiagnostic(overrideDiagnostic);
            const auto poseBlend = Detail::ProceduralResponseBlend(deltaSeconds, profile.PoseResponseTime);
            for (std::size_t index = 0; index < pose.size(); ++index)
            {
                const auto& previous = state.PreviousProceduralPose[index];
                const auto& target = pose[index];
                auto& current = state.CurrentProceduralPose[index];
                current.Translation = {
                    previous.Translation.X + (target.Translation.X - previous.Translation.X) * poseBlend,
                    previous.Translation.Y + (target.Translation.Y - previous.Translation.Y) * poseBlend,
                    previous.Translation.Z + (target.Translation.Z - previous.Translation.Z) * poseBlend};
                current.Scale = {previous.Scale.X + (target.Scale.X - previous.Scale.X) * poseBlend,
                                 previous.Scale.Y + (target.Scale.Y - previous.Scale.Y) * poseBlend,
                                 previous.Scale.Z + (target.Scale.Z - previous.Scale.Z) * poseBlend};
                current.Rotation = RiggingDetail::Nlerp(previous.Rotation, target.Rotation, poseBlend);
            }
            state.ProceduralState.LeftFootPlanted = state.LeftFootPlantState.Plant.Locked;
            state.ProceduralState.RightFootPlanted = state.RightFootPlantState.Plant.Locked;
            animator->SetRuntimeProceduralState(state.ProceduralState);
            DispatchProceduralFootEvents(entity, state, state.ProceduralState);
            state.PreviousGrounded = grounded;
            state.PreviousVerticalSpeed = velocity.Y;
            state.PreviousProceduralState = motionState;
            state.PreviousRootForward = rootForward;
            state.HasPreviousRootForward = true;
        }
    }

    void SceneRuntimeSession::Impl::PublishProceduralAnimation(const Entity& entity, AnimatorComponent& animator,
                                                               AnimationRuntimeState& state)
    {
        const auto skeleton = state.SkeletonHandle.TryGetLoaded();
        if (!skeleton || state.CurrentProceduralPose.size() != skeleton->Bones().size())
            return;
        const auto alpha = std::clamp(PresentationInterpolationAlpha, 0.0F, 1.0F);
        auto& pose = state.PublishedProceduralPose;
        std::ranges::copy(state.CurrentProceduralPose, pose.begin());
        if (state.PreviousProceduralPose.size() == pose.size())
        {
            for (std::size_t index = 0; index < pose.size(); ++index)
            {
                const auto& previous = state.PreviousProceduralPose[index];
                const auto& current = state.CurrentProceduralPose[index];
                pose[index].Translation = {
                    previous.Translation.X + (current.Translation.X - previous.Translation.X) * alpha,
                    previous.Translation.Y + (current.Translation.Y - previous.Translation.Y) * alpha,
                    previous.Translation.Z + (current.Translation.Z - previous.Translation.Z) * alpha};
                pose[index].Scale = {previous.Scale.X + (current.Scale.X - previous.Scale.X) * alpha,
                                     previous.Scale.Y + (current.Scale.Y - previous.Scale.Y) * alpha,
                                     previous.Scale.Z + (current.Scale.Z - previous.Scale.Z) * alpha};
                pose[index].Rotation = RiggingDetail::Nlerp(previous.Rotation, current.Rotation, alpha);
            }
        }
        SkinPalette(*skeleton, pose, state.PublishedModelMatrices, state.SkinPaletteCache);
        animator.SetRuntimePose(std::string(ProceduralMotionStateName(state.ProceduralState.State)), state.GaitPhase,
                                true, state.SkinPaletteCache);
        animator.SetRuntimeDebugSnapshot(ProceduralDebugSnapshot(state, *skeleton, pose, state.PublishedModelMatrices));
        (void)entity;
    }

} // namespace Keire
