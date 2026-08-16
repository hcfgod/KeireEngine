#include "Keire/Animation/RiggingSystem.h"

#include "KeireInternal/Animation/RiggingMath.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
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

    bool SolveTwoBoneIk(const SkeletonAsset& skeleton, const std::span<BoneTransform> localPose,
                        const TwoBoneIkRequest& request)
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
                return SetBoneModelRotation(skeleton, localPose, request.End, *request.EndRotation, endRotationWeight);
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
        const auto forward = Normalize(targetDelta);
        auto bendVector = ProjectOntoPlane(Subtract(request.Pole, rootPosition), forward);
        if (Length(bendVector) <= Epsilon)
            bendVector = ProjectOntoPlane(Subtract(middlePosition, rootPosition), forward);
        if (Length(bendVector) <= Epsilon)
        {
            const auto fallback = std::abs(forward.Y) < 0.95F ? Vector3{0.0F, 1.0F, 0.0F} : Vector3{0.0F, 0.0F, 1.0F};
            bendVector = ProjectOntoPlane(fallback, forward);
        }
        const auto bend = Normalize(bendVector);
        const auto projected =
            (upperLength * upperLength + targetDistance * targetDistance - lowerLength * lowerLength) /
            (2.0F * targetDistance);
        const auto height = std::sqrt(std::max(0.0F, upperLength * upperLength - projected * projected));
        const auto desiredMiddle = Add(rootPosition, Add(Multiply(forward, projected), Multiply(bend, height)));

        const auto rootDelta = FromTo(Subtract(middlePosition, rootPosition), Subtract(desiredMiddle, rootPosition));
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

    bool SolveFabrikIk(const SkeletonAsset& skeleton, const std::span<BoneTransform> localPose,
                       const FabrikIkRequest& request)
    {
        if (localPose.size() != skeleton.Bones().size() || request.Chain.size() < 2 || request.MaximumIterations == 0 ||
            request.MaximumIterations > 1024 || request.Tolerance <= 0.0F || !std::isfinite(request.Tolerance) ||
            !std::isfinite(request.Weight) || !Math::IsFinite(request.Target))
        {
            return false;
        }
        for (std::size_t index = 0; index < request.Chain.size(); ++index)
        {
            if (request.Chain[index] >= localPose.size())
                return false;
            if (index > 0 &&
                skeleton.Bones()[request.Chain[index]].Parent != static_cast<std::int32_t>(request.Chain[index - 1]))
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

        const auto root = positions.front();
        if (Length(Subtract(request.Target, root)) >= totalLength)
        {
            const auto direction = Normalize(Subtract(request.Target, root));
            for (std::size_t index = 1; index < positions.size(); ++index)
                positions[index] = Add(positions[index - 1], Multiply(direction, lengths[index - 1]));
        }
        else
        {
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

        const auto weight = std::clamp(request.Weight, 0.0F, 1.0F);
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

    std::optional<FootGroundingResult> SolveFootGrounding(const SkeletonAsset& skeleton,
                                                          const std::span<BoneTransform> localPose,
                                                          const FootGroundingRequest& request)
    {
        if (localPose.size() != skeleton.Bones().size() || request.Contacts.empty() || request.Contacts.size() > 16 ||
            (request.Pelvis && *request.Pelvis >= localPose.size()) || !std::isfinite(request.FootHeight) ||
            request.FootHeight < 0.0F || !std::isfinite(request.PelvisWeight) || request.PelvisWeight < 0.0F ||
            request.PelvisWeight > 1.0F || !std::isfinite(request.MaximumPelvisAdjustment) ||
            request.MaximumPelvisAdjustment < 0.0F || !std::isfinite(request.MaximumHorizontalPelvisAdjustment) ||
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
                Length(contact.Normal) <= Epsilon || !Math::IsFinite(contact.Pole) || !std::isfinite(contact.Weight) ||
                contact.Weight < 0.0F || contact.Weight > 1.0F || !std::isfinite(contact.RotationWeight) ||
                contact.RotationWeight < 0.0F || contact.RotationWeight > 1.0F ||
                (contact.Toe &&
                 (*contact.Toe >= localPose.size() || !IsDescendantOf(skeleton, *contact.Toe, contact.Foot) ||
                  !toes.insert(*contact.Toe).second)))
                return std::nullopt;
        }

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
        for (const auto& contact : request.Contacts)
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
        if (request.Pelvis && request.PelvisWeight > 0.0F)
        {
            if (request.Torso && request.PelvisRotationWeight > 0.0F && request.MaximumPelvisRotationDegrees > 0.0F)
            {
                const auto sampledPelvis = Math::TransformPoint(sampledWorld[*request.Pelvis], {});
                const auto sampledTorso = Math::TransformPoint(sampledWorld[*request.Torso], {});
                const auto bindPelvis = Math::TransformPoint(bindWorld[*request.Pelvis], {});
                const auto bindTorso = Math::TransformPoint(bindWorld[*request.Torso], {});
                Vector3 averageNormal;
                for (const auto& contact : request.Contacts)
                    averageNormal = Add(averageNormal, Normalize(contact.Normal));
                averageNormal = Normalize(averageNormal);
                const auto slopeRotation = FromTo({0.0F, 1.0F, 0.0F}, averageNormal);
                const auto desiredTorsoDirection = Rotate(slopeRotation, Normalize(Subtract(bindTorso, bindPelvis)));
                auto correction = FromTo(Subtract(sampledTorso, sampledPelvis), desiredTorsoDirection);
                correction = Normalize(correction);
                const auto angleRadians = 2.0F * std::acos(std::clamp(std::abs(correction.W), 0.0F, 1.0F));
                constexpr float RadiansPerDegree = 0.01745329251994329577F;
                const auto maximumRadians = request.MaximumPelvisRotationDegrees * RadiansPerDegree;
                if (angleRadians > Epsilon)
                {
                    correction = Nlerp({}, correction, std::min(1.0F, maximumRadians / angleRadians));
                    if (!ApplyBoneModelRotationDelta(skeleton, working, *request.Pelvis, correction,
                                                     request.PelvisRotationWeight))
                    {
                        return std::nullopt;
                    }
                    result.PelvisRotationAdjustmentDegrees =
                        std::min(angleRadians, maximumRadians) / RadiansPerDegree * request.PelvisRotationWeight;
                }
            }

            const auto world = WorldMatrices(skeleton, working);
            float requestedAdjustment = 0.0F;
            for (const auto& contact : request.Contacts)
            {
                const auto current = Math::TransformPoint(world[contact.Foot], {});
                const auto target = Add(contact.Position, Multiply(Normalize(contact.Normal), request.FootHeight));
                requestedAdjustment = std::min(requestedAdjustment, target.Y - current.Y);
            }
            result.PelvisAdjustment =
                std::clamp(requestedAdjustment, -request.MaximumPelvisAdjustment, 0.0F) * request.PelvisWeight;

            if (!request.Contacts.empty() && request.MaximumHorizontalPelvisAdjustment > 0.0F)
            {
                Vector3 bindFootCenter;
                Vector3 targetFootCenter;
                for (const auto& contact : request.Contacts)
                {
                    bindFootCenter = Add(bindFootCenter, Math::TransformPoint(bindWorld[contact.Foot], {}));
                    targetFootCenter =
                        Add(targetFootCenter,
                            Add(contact.Position, Multiply(Normalize(contact.Normal), request.FootHeight)));
                }
                const auto inverseContactCount = 1.0F / static_cast<float>(request.Contacts.size());
                bindFootCenter = Multiply(bindFootCenter, inverseContactCount);
                targetFootCenter = Multiply(targetFootCenter, inverseContactCount);
                const auto bindPelvis = Math::TransformPoint(bindWorld[*request.Pelvis], {});
                const auto currentPelvis = Math::TransformPoint(world[*request.Pelvis], {});
                const auto desiredPelvis = Add(targetFootCenter, Subtract(bindPelvis, bindFootCenter));
                const Vector3 towardBindNeutral{desiredPelvis.X - currentPelvis.X, 0.0F,
                                                desiredPelvis.Z - currentPelvis.Z};
                const auto distance = Length(towardBindNeutral);
                if (distance > request.PelvisSupportRadius)
                {
                    const auto correction =
                        std::min(distance - request.PelvisSupportRadius, request.MaximumHorizontalPelvisAdjustment) *
                        request.PelvisWeight;
                    result.HorizontalPelvisAdjustment = Multiply(Normalize(towardBindNeutral), correction);
                }
            }

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

        for (std::size_t contactIndex = 0; contactIndex < request.Contacts.size(); ++contactIndex)
        {
            const auto& contact = request.Contacts[contactIndex];
            const auto normal = Normalize(contact.Normal);
            const auto target = Add(contact.Position, Multiply(normal, request.FootHeight));
            if (!SolveTwoBoneIk(
                    skeleton, working,
                    {contact.UpperLeg, contact.LowerLeg, contact.Foot, target, contact.Pole, contact.Weight}))
                return std::nullopt;
            const auto surfaceAlignment = FromTo(sampledSoleNormals[contactIndex], normal);
            const auto desiredFootRotation = Multiply(surfaceAlignment, sampledFootRotations[contactIndex]);
            if (!SetBoneModelRotation(skeleton, working, contact.Foot, desiredFootRotation,
                                      contact.Weight * contact.RotationWeight))
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
            if (positionError > request.PositionTolerance)
                ++result.UnreachableFeet;
            ++result.SolvedFeet;
        }

        std::ranges::copy(working, localPose.begin());
        return result;
    }
} // namespace Keire
