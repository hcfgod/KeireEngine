#include "Keire/Animation/RiggingSystem.h"

#include "KeireInternal/Animation/RiggingMath.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <ranges>
#include <set>
#include <vector>

namespace Keire
{
    using RiggingDetail::Add;
    using RiggingDetail::ApplyBoneModelRotationDelta;
    using RiggingDetail::Conjugate;
    using RiggingDetail::Epsilon;
    using RiggingDetail::FromTo;
    using RiggingDetail::IsDescendantOf;
    using RiggingDetail::Length;
    using RiggingDetail::MatrixRotation;
    using RiggingDetail::Multiply;
    using RiggingDetail::Nlerp;
    using RiggingDetail::Normalize;
    using RiggingDetail::ProjectOntoPlane;
    using RiggingDetail::Rotate;
    using RiggingDetail::SetBoneModelRotation;
    using RiggingDetail::Subtract;
    using RiggingDetail::WorldMatrices;

    namespace
    {
        [[nodiscard]] bool IsValidIkPose(const std::span<const BoneTransform> pose)
        {
            return std::ranges::all_of(pose,
                                       [](const BoneTransform& bone)
                                       {
                                           const auto rotationLength = Math::Length(bone.Rotation);
                                           return Math::IsFinite(bone.Translation) && Math::IsFinite(bone.Scale) &&
                                                  Math::IsFinite(bone.Rotation) && std::isfinite(rotationLength) &&
                                                  rotationLength > Epsilon;
                                       });
        }

        bool SolveTwoBoneIkWorking(const SkeletonAsset& skeleton, const std::span<BoneTransform> localPose,
                                   const TwoBoneIkRequest& request, float* reachError = nullptr)
        {
            if (localPose.size() != skeleton.Bones().size() || request.Root >= localPose.size() ||
                request.Middle >= localPose.size() || request.End >= localPose.size() ||
                !IsDescendantOf(skeleton, request.Middle, request.Root) ||
                !IsDescendantOf(skeleton, request.End, request.Middle) || !Math::IsFinite(request.Target) ||
                !Math::IsFinite(request.Pole) || !std::isfinite(request.Weight) ||
                (request.EndRotation &&
                 (!Math::IsFinite(*request.EndRotation) || Math::Length(*request.EndRotation) <= Epsilon)) ||
                !std::isfinite(request.EndRotationWeight))
            {
                return false;
            }
            const auto weight = std::clamp(request.Weight, 0.0F, 1.0F);
            const auto endRotationWeight = std::clamp(request.EndRotationWeight, 0.0F, 1.0F);
            if (weight <= 0.0F)
            {
                if (request.EndRotation && endRotationWeight > 0.0F)
                    return SetBoneModelRotation(skeleton, localPose, request.End, *request.EndRotation,
                                                endRotationWeight);
                return true;
            }

            auto world = WorldMatrices(skeleton, localPose);
            auto rootPosition = Math::TransformPoint(world[request.Root], {});
            auto middlePosition = Math::TransformPoint(world[request.Middle], {});
            auto endPosition = Math::TransformPoint(world[request.End], {});
            const auto upperLength = Length(Subtract(middlePosition, rootPosition));
            const auto lowerLength = Length(Subtract(endPosition, middlePosition));
            if (upperLength <= Epsilon || lowerLength <= Epsilon)
                return false;

            const auto requestedDelta = Subtract(request.Target, rootPosition);
            const auto requestedDistance = Length(requestedDelta);
            auto targetDelta = requestedDelta;
            if (requestedDistance <= Epsilon)
            {
                targetDelta = Subtract(endPosition, rootPosition);
                if (Length(targetDelta) <= Epsilon)
                    targetDelta = Subtract(middlePosition, rootPosition);
            }
            const auto singularityMargin = std::min(std::max((upperLength + lowerLength) * 0.0025F, Epsilon),
                                                    std::min(upperLength, lowerLength) * 0.25F);
            const auto targetDistance =
                std::clamp(requestedDistance, std::abs(upperLength - lowerLength) + singularityMargin,
                           upperLength + lowerLength - singularityMargin);
            if (reachError)
                *reachError = std::abs(requestedDistance - targetDistance);
            const auto forward = Normalize(targetDelta);
            auto bendVector = ProjectOntoPlane(Subtract(request.Pole, rootPosition), forward);
            if (Length(bendVector) <= Epsilon)
                bendVector = ProjectOntoPlane(Subtract(middlePosition, rootPosition), forward);
            if (Length(bendVector) <= Epsilon)
            {
                const auto fallback =
                    std::abs(forward.Y) < 0.95F ? Vector3{0.0F, 1.0F, 0.0F} : Vector3{0.0F, 0.0F, 1.0F};
                bendVector = ProjectOntoPlane(fallback, forward);
            }
            const auto bend = Normalize(bendVector);
            const auto projected =
                (upperLength * upperLength + targetDistance * targetDistance - lowerLength * lowerLength) /
                (2.0F * targetDistance);
            const auto height = std::sqrt(std::max(0.0F, upperLength * upperLength - projected * projected));
            const auto desiredMiddle = Add(rootPosition, Add(Multiply(forward, projected), Multiply(bend, height)));

            const auto rootDelta =
                FromTo(Subtract(middlePosition, rootPosition), Subtract(desiredMiddle, rootPosition));
            if (!ApplyBoneModelRotationDelta(skeleton, localPose, request.Root, rootDelta, weight))
                return false;

            world = WorldMatrices(skeleton, localPose);
            middlePosition = Math::TransformPoint(world[request.Middle], {});
            endPosition = Math::TransformPoint(world[request.End], {});
            const auto reachableTarget = Add(rootPosition, Multiply(forward, targetDistance));
            const auto middleDelta =
                FromTo(Subtract(endPosition, middlePosition), Subtract(reachableTarget, middlePosition));
            if (!ApplyBoneModelRotationDelta(skeleton, localPose, request.Middle, middleDelta, weight))
                return false;
            if (request.EndRotation && endRotationWeight > 0.0F &&
                !SetBoneModelRotation(skeleton, localPose, request.End, *request.EndRotation, endRotationWeight))
            {
                return false;
            }
            return true;
        }

        bool SolveFabrikIkWorking(const SkeletonAsset& skeleton, const std::span<BoneTransform> localPose,
                                  const FabrikIkRequest& request)
        {
            if (localPose.size() != skeleton.Bones().size() || request.Chain.size() < 2 ||
                request.MaximumIterations == 0 || request.MaximumIterations > 1024 || request.Tolerance <= 0.0F ||
                !std::isfinite(request.Tolerance) || !std::isfinite(request.Weight) || !Math::IsFinite(request.Target))
            {
                return false;
            }
            for (std::size_t index = 0; index < request.Chain.size(); ++index)
            {
                if (request.Chain[index] >= localPose.size())
                    return false;
                if (index > 0 && skeleton.Bones()[request.Chain[index]].Parent !=
                                     static_cast<std::int32_t>(request.Chain[index - 1]))
                    return false;
            }

            const auto world = WorldMatrices(skeleton, localPose);
            std::vector<Vector3> positions(request.Chain.size());
            std::vector<float> lengths(request.Chain.size() - 1);
            float totalLength = 0.0F;
            for (std::size_t index = 0; index < request.Chain.size(); ++index)
            {
                positions[index] = Math::TransformPoint(world[request.Chain[index]], {});
                if (index > 0)
                {
                    lengths[index - 1] = Length(Subtract(positions[index], positions[index - 1]));
                    if (lengths[index - 1] <= Epsilon)
                        return false;
                    totalLength += lengths[index - 1];
                }
            }

            const auto weight = std::clamp(request.Weight, 0.0F, 1.0F);
            if (weight <= 0.0F)
                return true;

            const auto root = positions.front();
            if (Length(Subtract(request.Target, root)) >= totalLength)
            {
                const auto direction = Normalize(Subtract(request.Target, root));
                for (std::size_t index = 1; index < positions.size(); ++index)
                    positions[index] = Add(positions[index - 1], Multiply(direction, lengths[index - 1]));
            }
            else
            {
                // Collinear projection cannot create a bend toward an interior target. Seed only this
                // singular configuration, using the root's frame so rotated rigs choose the same bend side.
                if (positions.size() > 2 && Length(Subtract(positions.back(), request.Target)) > request.Tolerance)
                {
                    const auto axis = Normalize(Subtract(positions.back(), root));
                    const auto collinearTolerance = std::max(Epsilon, totalLength * 0.00001F);
                    bool collinear =
                        Length(ProjectOntoPlane(Subtract(request.Target, root), axis)) <= collinearTolerance;
                    for (std::size_t index = 1; index + 1 < positions.size(); ++index)
                        collinear = collinear && Length(ProjectOntoPlane(Subtract(positions[index], root), axis)) <=
                                                     collinearTolerance;
                    if (collinear)
                    {
                        Quaternion rotation;
                        if (!MatrixRotation(world[request.Chain.front()], rotation))
                            return false;
                        auto bend = ProjectOntoPlane(Rotate(rotation, {1.0F, 0.0F, 0.0F}), axis);
                        if (Length(bend) <= Epsilon)
                            bend = ProjectOntoPlane(Rotate(rotation, {0.0F, 0.0F, 1.0F}), axis);
                        bend = Multiply(Normalize(bend), *std::ranges::min_element(lengths) * 0.1F);
                        for (std::size_t index = 1; index + 1 < positions.size(); ++index)
                            positions[index] = Add(positions[index], bend);
                    }
                }
                for (std::uint32_t iteration = 0; iteration < request.MaximumIterations; ++iteration)
                {
                    positions.back() = request.Target;
                    for (std::size_t index = positions.size() - 1; index > 0; --index)
                    {
                        const auto direction = Normalize(Subtract(positions[index - 1], positions[index]));
                        positions[index - 1] = Add(positions[index], Multiply(direction, lengths[index - 1]));
                    }
                    positions.front() = root;
                    for (std::size_t index = 1; index < positions.size(); ++index)
                    {
                        const auto direction = Normalize(Subtract(positions[index], positions[index - 1]));
                        positions[index] = Add(positions[index - 1], Multiply(direction, lengths[index - 1]));
                    }
                    if (Length(Subtract(positions.back(), request.Target)) <= request.Tolerance)
                        break;
                }
            }

            for (std::size_t index = 0; index + 1 < request.Chain.size(); ++index)
            {
                const auto bone = request.Chain[index];
                const auto currentWorld = WorldMatrices(skeleton, localPose);
                const auto current = Math::TransformPoint(currentWorld[bone], {});
                const auto child = Math::TransformPoint(currentWorld[request.Chain[index + 1]], {});
                const auto delta = FromTo(Subtract(child, current), Subtract(positions[index + 1], positions[index]));
                if (!ApplyBoneModelRotationDelta(skeleton, localPose, bone, delta, weight))
                    return false;
            }
            return true;
        }

    } // namespace

    bool SolveTwoBoneIk(const SkeletonAsset& skeleton, const std::span<BoneTransform> localPose,
                        const TwoBoneIkRequest& request)
    {
        if (localPose.size() != skeleton.Bones().size() || !IsValidIkPose(localPose))
            return false;
        std::vector<BoneTransform> workingPose(localPose.begin(), localPose.end());
        if (!SolveTwoBoneIkWorking(skeleton, workingPose, request) || !IsValidIkPose(workingPose))
            return false;
        std::ranges::copy(workingPose, localPose.begin());
        return true;
    }

    bool SolveFabrikIk(const SkeletonAsset& skeleton, const std::span<BoneTransform> localPose,
                       const FabrikIkRequest& request)
    {
        if (localPose.size() != skeleton.Bones().size() || !IsValidIkPose(localPose))
            return false;
        std::vector<BoneTransform> workingPose(localPose.begin(), localPose.end());
        if (!SolveFabrikIkWorking(skeleton, workingPose, request) || !IsValidIkPose(workingPose))
            return false;
        std::ranges::copy(workingPose, localPose.begin());
        return true;
    }

    std::optional<FootGroundingResult> SolveFootGrounding(const SkeletonAsset& skeleton,
                                                          const std::span<BoneTransform> localPose,
                                                          const FootGroundingRequest& request)
    {
        if (localPose.size() != skeleton.Bones().size() || !IsValidIkPose(localPose) || request.Contacts.empty() ||
            request.Contacts.size() > 16 || (request.Pelvis && *request.Pelvis >= localPose.size()) ||
            !std::isfinite(request.FootHeight) || request.FootHeight < 0.0F || !std::isfinite(request.PelvisWeight) ||
            request.PelvisWeight < 0.0F || request.PelvisWeight > 1.0F ||
            !std::isfinite(request.MaximumPelvisAdjustment) || request.MaximumPelvisAdjustment < 0.0F ||
            !std::isfinite(request.MaximumHorizontalPelvisAdjustment) ||
            request.MaximumHorizontalPelvisAdjustment < 0.0F || !std::isfinite(request.PelvisSupportRadius) ||
            request.PelvisSupportRadius < 0.0F || (request.Torso && *request.Torso >= localPose.size()) ||
            (request.Torso && !request.Pelvis) || !std::isfinite(request.PelvisRotationWeight) ||
            request.PelvisRotationWeight < 0.0F || request.PelvisRotationWeight > 1.0F ||
            !std::isfinite(request.MaximumPelvisRotationDegrees) || request.MaximumPelvisRotationDegrees < 0.0F ||
            request.MaximumPelvisRotationDegrees > 180.0F || !std::isfinite(request.PositionTolerance) ||
            request.PositionTolerance <= 0.0F)
            return std::nullopt;
        if (request.Torso && !IsDescendantOf(skeleton, *request.Torso, *request.Pelvis))
            return std::nullopt;

        std::set<std::uint32_t> feet;
        std::set<std::uint32_t> toes;
        for (const auto& contact : request.Contacts)
        {
            if (contact.UpperLeg >= localPose.size() || contact.LowerLeg >= localPose.size() ||
                contact.Foot >= localPose.size() || !IsDescendantOf(skeleton, contact.LowerLeg, contact.UpperLeg) ||
                !IsDescendantOf(skeleton, contact.Foot, contact.LowerLeg) || !feet.insert(contact.Foot).second ||
                !Math::IsFinite(contact.Position) || !Math::IsFinite(contact.Normal) ||
                (contact.SupportPosition && !Math::IsFinite(*contact.SupportPosition)) ||
                Length(contact.Normal) <= Epsilon || !Math::IsFinite(contact.Pole) || !std::isfinite(contact.Weight) ||
                contact.Weight < 0.0F || contact.Weight > 1.0F || !std::isfinite(contact.RotationWeight) ||
                contact.RotationWeight < 0.0F || contact.RotationWeight > 1.0F ||
                !std::isfinite(contact.SupportWeight) || contact.SupportWeight < 0.0F || contact.SupportWeight > 1.0F ||
                (contact.Toe &&
                 (*contact.Toe >= localPose.size() || !IsDescendantOf(skeleton, *contact.Toe, contact.Foot) ||
                  !toes.insert(*contact.Toe).second)))
                return std::nullopt;
        }

        auto activeContacts = request.Contacts | std::views::filter([](const FootGroundContact& contact)
                                                                    { return contact.Weight > 0.0F; });
        const auto activeContactCount = std::ranges::distance(activeContacts);
        if (activeContactCount == 0)
            return FootGroundingResult{};

        std::vector<BoneTransform> working(localPose.begin(), localPose.end());
        const auto sampledWorld = WorldMatrices(skeleton, working);
        std::vector<BoneTransform> bindPose;
        bindPose.reserve(skeleton.Bones().size());
        std::ranges::transform(skeleton.Bones(), std::back_inserter(bindPose), &SkeletonBone::BindPose);
        const auto bindWorld = WorldMatrices(skeleton, bindPose);
        std::vector<Quaternion> sampledFootRotations;
        std::vector<Vector3> sampledSoleNormals;
        sampledFootRotations.reserve(request.Contacts.size());
        sampledSoleNormals.reserve(request.Contacts.size());
        for (const auto& contact : activeContacts)
        {
            Quaternion sampledRotation;
            Quaternion bindRotation;
            if (!MatrixRotation(sampledWorld[contact.Foot], sampledRotation) ||
                !MatrixRotation(bindWorld[contact.Foot], bindRotation))
                return std::nullopt;
            sampledFootRotations.push_back(sampledRotation);
            const auto bindToSampled = Multiply(Normalize(sampledRotation), Conjugate(Normalize(bindRotation)));
            sampledSoleNormals.push_back(Normalize(Rotate(bindToSampled, {0.0F, 1.0F, 0.0F})));
        }

        FootGroundingResult result;
        auto supportContacts =
            activeContacts | std::views::filter([](const FootGroundContact& contact)
                                                { return contact.Weight * contact.SupportWeight > 0.0F; });
        if (request.Pelvis && request.PelvisWeight > 0.0F && !supportContacts.empty())
        {
            float totalSupportWeight = 0.0F;
            float maximumSupportWeight = 0.0F;
            for (const auto& contact : supportContacts)
            {
                totalSupportWeight += contact.Weight * contact.SupportWeight;
                maximumSupportWeight = std::max(maximumSupportWeight, contact.Weight * contact.SupportWeight);
            }
            const auto pelvisBlend = request.PelvisWeight * maximumSupportWeight;
            if (request.Torso && request.PelvisRotationWeight > 0.0F && request.MaximumPelvisRotationDegrees > 0.0F)
            {
                const auto sampledPelvis = Math::TransformPoint(sampledWorld[*request.Pelvis], {});
                const auto sampledTorso = Math::TransformPoint(sampledWorld[*request.Torso], {});
                Vector3 averageNormal;
                for (const auto& contact : supportContacts)
                    averageNormal =
                        Add(averageNormal, Multiply(Normalize(contact.Normal),
                                                    contact.Weight * contact.SupportWeight / totalSupportWeight));
                averageNormal = Normalize(averageNormal);
                const auto slopeRotation = FromTo({0.0F, 1.0F, 0.0F}, averageNormal);
                // Terrain adds tilt to the authored lean; flat support must not straighten the animation.
                const auto desiredTorsoDirection =
                    Rotate(slopeRotation, Normalize(Subtract(sampledTorso, sampledPelvis)));
                auto correction = FromTo(Subtract(sampledTorso, sampledPelvis), desiredTorsoDirection);
                correction = Normalize(correction);
                const auto angleRadians = 2.0F * std::acos(std::clamp(std::abs(correction.W), 0.0F, 1.0F));
                constexpr float RadiansPerDegree = 0.01745329251994329577F;
                const auto maximumRadians = request.MaximumPelvisRotationDegrees * RadiansPerDegree;
                if (angleRadians > Epsilon)
                {
                    correction = Nlerp({}, correction, std::min(1.0F, maximumRadians / angleRadians));
                    if (!ApplyBoneModelRotationDelta(skeleton, working, *request.Pelvis, correction,
                                                     request.PelvisRotationWeight * pelvisBlend))
                    {
                        return std::nullopt;
                    }
                    result.PelvisRotationAdjustmentDegrees = std::min(angleRadians, maximumRadians) / RadiansPerDegree *
                                                             request.PelvisRotationWeight * pelvisBlend;
                }
            }

            const auto world = WorldMatrices(skeleton, working);
            if (request.MaximumHorizontalPelvisAdjustment > 0.0F)
            {
                // Preserve the authored stride unless the caller explicitly requests standing support centering.
                Vector3 sampledFootCenter;
                Vector3 targetFootCenter;
                for (const auto& contact : supportContacts)
                {
                    const auto normalizedWeight = contact.Weight * contact.SupportWeight / totalSupportWeight;
                    sampledFootCenter =
                        Add(sampledFootCenter,
                            Multiply(Math::TransformPoint(sampledWorld[contact.Foot], {}), normalizedWeight));
                    targetFootCenter =
                        Add(targetFootCenter, Multiply(Add(contact.SupportPosition.value_or(contact.Position),
                                                           Multiply(Normalize(contact.Normal), request.FootHeight)),
                                                       normalizedWeight));
                }
                const auto sampledPelvis = Math::TransformPoint(sampledWorld[*request.Pelvis], {});
                const auto currentPelvis = Math::TransformPoint(world[*request.Pelvis], {});
                const auto desiredPelvis = request.BalanceOverSupport
                                               ? targetFootCenter
                                               : Add(targetFootCenter, Subtract(sampledPelvis, sampledFootCenter));
                const Vector3 towardSupportedPose{desiredPelvis.X - currentPelvis.X, 0.0F,
                                                  desiredPelvis.Z - currentPelvis.Z};
                const auto distance = Length(towardSupportedPose);
                if (distance > request.PelvisSupportRadius)
                {
                    const auto correction =
                        std::min(distance - request.PelvisSupportRadius, request.MaximumHorizontalPelvisAdjustment) *
                        pelvisBlend;
                    result.HorizontalPelvisAdjustment = Multiply(Normalize(towardSupportedPose), correction);
                }
            }

            float requestedAdjustment = 0.0F;
            for (const auto& contact : supportContacts)
            {
                const auto upper = Math::TransformPoint(world[contact.UpperLeg], {});
                const auto lower = Math::TransformPoint(world[contact.LowerLeg], {});
                const auto foot = Math::TransformPoint(world[contact.Foot], {});
                const auto reach = Length(Subtract(lower, upper)) + Length(Subtract(foot, lower));
                const auto target = Add(contact.SupportPosition.value_or(contact.Position),
                                        Multiply(Normalize(contact.Normal), request.FootHeight));
                const auto movedUpper = Add(upper, result.HorizontalPelvisAdjustment);
                const auto dx = target.X - movedUpper.X;
                const auto dz = target.Z - movedUpper.Z;
                const auto verticalReach = std::sqrt(std::max(reach * reach - dx * dx - dz * dz, 0.0F));
                // A bent animated leg can reach below its sampled ankle without lowering the body.
                // Lower only enough to fit the target inside the leg's reach after horizontal correction.
                const auto correction =
                    std::clamp(target.Y + verticalReach - movedUpper.Y, -request.MaximumPelvisAdjustment, 0.0F) *
                    contact.Weight * contact.SupportWeight;
                requestedAdjustment = std::min(requestedAdjustment, correction);
            }
            result.PelvisAdjustment = requestedAdjustment * request.PelvisWeight;

            auto localAdjustment = Add(result.HorizontalPelvisAdjustment, {0.0F, result.PelvisAdjustment, 0.0F});
            const auto parent = skeleton.Bones()[*request.Pelvis].Parent;
            if (parent >= 0)
            {
                try
                {
                    localAdjustment = Math::TransformDirection(Math::Inverse(world[static_cast<std::size_t>(parent)]),
                                                               localAdjustment);
                }
                catch (const std::exception&)
                {
                    return std::nullopt;
                }
            }
            working[*request.Pelvis].Translation = Add(working[*request.Pelvis].Translation, localAdjustment);
        }

        std::size_t contactIndex = 0;
        for (const auto& contact : activeContacts)
        {
            const auto normal = Normalize(contact.Normal);
            const auto target = Add(contact.Position, Multiply(normal, request.FootHeight));
            float reachError = 0.0F;
            if (!SolveTwoBoneIkWorking(
                    skeleton, working,
                    {contact.UpperLeg, contact.LowerLeg, contact.Foot, target, contact.Pole, contact.Weight},
                    &reachError))
                return std::nullopt;
            const auto surfaceAlignment = FromTo(sampledSoleNormals[contactIndex], normal);
            const auto desiredFootRotation = Multiply(surfaceAlignment, sampledFootRotations[contactIndex]);
            // Leg IK changes the inherited ankle rotation. Terrain influence must blend from the
            // sampled model-space orientation, not from that solver-induced rotation.
            const auto blendedFootRotation =
                Nlerp(sampledFootRotations[contactIndex], desiredFootRotation, contact.Weight * contact.RotationWeight);
            if (!SetBoneModelRotation(skeleton, working, contact.Foot, blendedFootRotation, 1.0F))
                return std::nullopt;
            if (contact.Toe)
            {
                working[*contact.Toe].Rotation = Nlerp(working[*contact.Toe].Rotation, bindPose[*contact.Toe].Rotation,
                                                       contact.Weight * contact.RotationWeight);
            }
            const auto solvedWorld = WorldMatrices(skeleton, working);
            const auto solvedPosition = Math::TransformPoint(solvedWorld[contact.Foot], {});
            const auto positionError = Length(Subtract(solvedPosition, target));
            result.MaximumPositionError = std::max(result.MaximumPositionError, positionError);
            // Partial rotation blending intentionally leaves positional error even for a reachable target.
            const auto limitError = contact.Weight < 1.0F ? reachError : positionError;
            if (limitError > request.PositionTolerance)
                ++result.UnreachableFeet;
            ++result.SolvedFeet;
            ++contactIndex;
        }

        if (!IsValidIkPose(working))
            return std::nullopt;
        std::ranges::copy(working, localPose.begin());
        return result;
    }
} // namespace Keire
