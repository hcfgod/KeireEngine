#include "Keire/Animation/LimbRig.h"

#include "Keire/Animation/RiggingSystem.h"
#include "KeireInternal/Animation/RiggingMath.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <stdexcept>
#include <utility>

namespace Keire
{
    namespace
    {
        [[nodiscard]] bool ValidPose(const std::span<const BoneTransform> pose)
        {
            return std::ranges::all_of(pose,
                                       [](const BoneTransform& bone)
                                       {
                                           const auto length = Math::Length(bone.Rotation);
                                           return Math::IsFinite(bone.Translation) && Math::IsFinite(bone.Scale) &&
                                                  Math::IsFinite(bone.Rotation) && std::isfinite(length) &&
                                                  length > RiggingDetail::Epsilon;
                                       });
        }

        [[nodiscard]] float Distance(const Vector3 a, const Vector3 b)
        {
            return RiggingDetail::Length(RiggingDetail::Subtract(a, b));
        }

        [[nodiscard]] bool Related(const SkeletonAsset& skeleton, const std::uint32_t a, const std::uint32_t b)
        {
            return a == b || RiggingDetail::IsDescendantOf(skeleton, a, b) ||
                   RiggingDetail::IsDescendantOf(skeleton, b, a);
        }

        [[nodiscard]] bool BendReach(const float upper, const float lower, const LimbBendLimits limits, float& minimum,
                                     float& maximum)
        {
            const auto radius = [upper, lower](const float degrees)
            {
                const auto angle = static_cast<double>(degrees) * std::numbers::pi / 180.0;
                return static_cast<float>(
                    std::sqrt(std::max(0.0, static_cast<double>(upper) * upper + static_cast<double>(lower) * lower +
                                                2.0 * upper * lower * std::cos(angle))));
            };
            // Match the existing solver's singularity-safe reach interval. A constraint must be representable
            // by that solver; rejecting it is preferable to publishing a pose outside the promised limits.
            const auto margin =
                std::min(std::max((upper + lower) * 0.0025F, RiggingDetail::Epsilon), std::min(upper, lower) * 0.25F);
            minimum = std::max(radius(limits.MaximumDegrees), std::abs(upper - lower) + margin);
            maximum = std::min(radius(limits.MinimumDegrees), upper + lower - margin);
            return std::isfinite(minimum) && std::isfinite(maximum) && minimum <= maximum;
        }

        [[nodiscard]] float BendAngle(const Vector3 root, const Vector3 middle, const Vector3 end)
        {
            const auto upper = RiggingDetail::Normalize(RiggingDetail::Subtract(middle, root));
            const auto lower = RiggingDetail::Normalize(RiggingDetail::Subtract(end, middle));
            return static_cast<float>(std::acos(std::clamp(RiggingDetail::Dot(upper, lower), -1.0F, 1.0F)) * 180.0 /
                                      std::numbers::pi);
        }

        [[nodiscard]] bool SolveLimbTwoBone(const SkeletonAsset& skeleton, const ResolvedLimb& limb,
                                            const LimbTarget& target, std::span<BoneTransform> pose, bool& limited)
        {
            auto world = RiggingDetail::WorldMatrices(skeleton, pose);
            auto root = Math::TransformPoint(world[limb.Bones[0]], {});
            auto middle = Math::TransformPoint(world[limb.Bones[1]], {});
            auto end = Math::TransformPoint(world[limb.Bones[2]], {});
            TwoBoneIkRequest request;
            request.Root = limb.Bones[0];
            request.Middle = limb.Bones[1];
            request.End = limb.Bones[2];
            request.Target = target.Position;
            request.Pole = limb.Definition.PreferredBendDirection
                               ? RiggingDetail::Add(root, *limb.Definition.PreferredBendDirection)
                               : target.Pole;
            request.Weight = target.Weight;
            float minimum = 0.0F;
            float maximum = 0.0F;
            const auto project = [&](const Vector3 position)
            {
                const auto delta = RiggingDetail::Subtract(position, root);
                const auto radius = RiggingDetail::Length(delta);
                const auto desired = std::clamp(radius, minimum, maximum);
                limited = limited || std::abs(desired - radius) > RiggingDetail::Epsilon;
                const auto fallback =
                    RiggingDetail::Normalize(RiggingDetail::Subtract(end, root),
                                             RiggingDetail::Normalize(RiggingDetail::Subtract(middle, root)));
                return RiggingDetail::Add(root,
                                          RiggingDetail::Multiply(RiggingDetail::Normalize(delta, fallback), desired));
            };
            if (limb.Definition.BendLimits)
            {
                if (!BendReach(Distance(root, middle), Distance(middle, end), *limb.Definition.BendLimits, minimum,
                               maximum))
                    return false;
                request.Target = project(request.Target);
            }
            if (!SolveTwoBoneIk(skeleton, pose, request))
                return false;
            if (!limb.Definition.BendLimits)
                return true;
            const auto limits = *limb.Definition.BendLimits;
            world = RiggingDetail::WorldMatrices(skeleton, pose);
            root = Math::TransformPoint(world[request.Root], {});
            middle = Math::TransformPoint(world[request.Middle], {});
            end = Math::TransformPoint(world[request.End], {});
            auto angle = BendAngle(root, middle, end);
            if (angle < limits.MinimumDegrees || angle > limits.MaximumDegrees)
            {
                if (!BendReach(Distance(root, middle), Distance(middle, end), limits, minimum, maximum))
                    return false;
                request.Target = project(end);
                request.Weight = 1.0F;
                // Preserve the blended bend plane when projecting to a limit.
                request.Pole = middle;
                if (!SolveTwoBoneIk(skeleton, pose, request))
                    return false;
                limited = true;
                world = RiggingDetail::WorldMatrices(skeleton, pose);
                angle = BendAngle(Math::TransformPoint(world[request.Root], {}),
                                  Math::TransformPoint(world[request.Middle], {}),
                                  Math::TransformPoint(world[request.End], {}));
            }
            constexpr float angularTolerance = 0.05F;
            return std::isfinite(angle) && angle >= limits.MinimumDegrees - angularTolerance &&
                   angle <= limits.MaximumDegrees + angularTolerance;
        }
    } // namespace

    BoundLimbRig::BoundLimbRig(Ref<const SkeletonAsset> skeleton, const std::span<const LimbDefinition> definitions)
        : m_Skeleton(std::move(skeleton))
    {
        if (!m_Skeleton || definitions.empty())
            throw std::invalid_argument("A limb rig requires a skeleton and at least one limb.");
        std::vector<BoneTransform> restPose;
        for (const auto& bone : m_Skeleton->Bones())
            restPose.push_back(bone.BindPose);
        if (!ValidPose(restPose))
            throw std::invalid_argument("Limb rig bind pose contains invalid transforms.");
        const auto world = RiggingDetail::WorldMatrices(*m_Skeleton, restPose);
        std::set<std::uint32_t> ids;
        std::set<std::string> names;
        for (const auto& definition : definitions)
        {
            if (definition.Id.Value == 0 || !ids.insert(definition.Id.Value).second || definition.Name.empty() ||
                !names.insert(definition.Name).second)
                throw std::invalid_argument(
                    "Limb IDs must be nonzero and unique; limb names must be nonempty and unique.");
            if ((definition.Solver != LimbSolver::TwoBone && definition.Solver != LimbSolver::Fabrik) ||
                definition.Bones.size() < 2 ||
                (definition.Solver == LimbSolver::TwoBone && definition.Bones.size() != 3))
                throw std::invalid_argument("Two-bone limbs need exactly three bones; FABRIK needs at least two.");
            if (!std::isfinite(definition.ContactRadius) || definition.ContactRadius < 0.0F ||
                !std::isfinite(definition.Tolerance) || definition.Tolerance <= 0.0F ||
                definition.MaximumIterations == 0 || definition.MaximumIterations > 1024)
                throw std::invalid_argument("Invalid limb contact radius, tolerance, or iteration count (1..1024).");
            if ((definition.BendLimits || definition.PreferredBendDirection) &&
                definition.Solver != LimbSolver::TwoBone)
                throw std::invalid_argument("Bend limits and preferred directions currently require a two-bone limb.");
            if (definition.BendLimits &&
                (!std::isfinite(definition.BendLimits->MinimumDegrees) ||
                 !std::isfinite(definition.BendLimits->MaximumDegrees) ||
                 definition.BendLimits->MinimumDegrees < 0.0F || definition.BendLimits->MaximumDegrees > 180.0F ||
                 definition.BendLimits->MinimumDegrees > definition.BendLimits->MaximumDegrees))
                throw std::invalid_argument("Limb bend limits must be ordered finite angles within 0..180 degrees.");
            if (definition.PreferredBendDirection &&
                (!Math::IsFinite(*definition.PreferredBendDirection) ||
                 !std::isfinite(RiggingDetail::Length(*definition.PreferredBendDirection)) ||
                 RiggingDetail::Length(*definition.PreferredBendDirection) <= RiggingDetail::Epsilon))
                throw std::invalid_argument("Preferred bend direction must be finite and nonzero.");
            ResolvedLimb limb;
            limb.Definition = definition;
            for (const auto& name : definition.Bones)
            {
                const auto bones = m_Skeleton->Bones();
                const auto found = std::ranges::find(bones, name, &SkeletonBone::Name);
                if (found == bones.end())
                    throw std::invalid_argument("Limb bone not found: " + name);
                const auto index = static_cast<std::uint32_t>(found - bones.begin());
                if (!limb.Bones.empty())
                {
                    if (found->Parent != static_cast<std::int32_t>(limb.Bones.back()))
                        throw std::invalid_argument("Limb bones must form a contiguous root-to-tip chain: " + name);
                    const auto length = Distance(Math::TransformPoint(world[limb.Bones.back()], {}),
                                                 Math::TransformPoint(world[index], {}));
                    if (!std::isfinite(length) || length <= RiggingDetail::Epsilon)
                        throw std::invalid_argument("Limb contains a zero-length or nonfinite rest segment: " + name);
                    limb.RestLengths.push_back(length);
                    limb.MaximumReach += length;
                }
                limb.Bones.push_back(index);
            }
            if (!std::isfinite(limb.MaximumReach))
                throw std::invalid_argument("Limb rest reach overflows.");
            if (definition.BendLimits)
            {
                float minimum = 0.0F;
                float maximum = 0.0F;
                if (!BendReach(limb.RestLengths[0], limb.RestLengths[1], *definition.BendLimits, minimum, maximum))
                    throw std::invalid_argument(
                        "Limb bend interval cannot be represented without a solver singularity.");
            }
            for (const auto& existing : m_Limbs)
                if (Related(*m_Skeleton, existing.Bones.front(), limb.Bones.front()))
                    throw std::invalid_argument("Limb roots must be in independent skeleton branches.");
            m_Limbs.push_back(std::move(limb));
        }
        m_WorkingPose.resize(restPose.size());
    }

    bool BoundLimbRig::Solve(const std::span<BoneTransform> pose, const std::span<const LimbTarget> targets,
                             std::vector<LimbSolveResult>& results)
    {
        results.clear();
        for (const auto& limb : m_Limbs)
            results.push_back({limb.Definition.Id, LimbSolveStatus::InvalidInput});
        const auto fail = [&results]()
        {
            for (auto& result : results)
                result = {result.Id, LimbSolveStatus::InvalidInput};
            return false;
        };
        if (pose.size() != m_WorkingPose.size() || !ValidPose(pose))
            return fail();
        std::vector<const LimbTarget*> requests(m_Limbs.size(), nullptr);
        for (const auto& target : targets)
        {
            const auto found = std::ranges::find_if(m_Limbs, [&target](const ResolvedLimb& limb)
                                                    { return limb.Definition.Id == target.Id; });
            if (found == m_Limbs.end() || !Math::IsFinite(target.Position) || !Math::IsFinite(target.Pole) ||
                !std::isfinite(target.Weight) || target.Weight < 0.0F || target.Weight > 1.0F)
                return fail();
            const auto index = static_cast<std::size_t>(found - m_Limbs.begin());
            if (requests[index])
                return fail();
            requests[index] = &target;
        }
        std::ranges::copy(pose, m_WorkingPose.begin());
        for (std::size_t index = 0; index < m_Limbs.size(); ++index)
        {
            const auto& limb = m_Limbs[index];
            const auto* target = requests[index];
            auto& result = results[index];
            auto world = RiggingDetail::WorldMatrices(*m_Skeleton, m_WorkingPose);
            result.EndPosition = Math::TransformPoint(world[limb.Bones.back()], {});
            if (!Math::IsFinite(result.EndPosition))
                return fail();
            if (!target || !target->Enabled || target->Weight == 0.0F)
            {
                result.Status = LimbSolveStatus::Disabled;
                continue;
            }
            float reach = 0.0F;
            float longest = 0.0F;
            for (std::size_t segment = 1; segment < limb.Bones.size(); ++segment)
            {
                const auto length = Distance(Math::TransformPoint(world[limb.Bones[segment - 1]], {}),
                                             Math::TransformPoint(world[limb.Bones[segment]], {}));
                if (!std::isfinite(length) || length <= RiggingDetail::Epsilon)
                    return fail();
                reach += length;
                longest = std::max(longest, length);
            }
            const auto distance = Distance(Math::TransformPoint(world[limb.Bones.front()], {}), target->Position);
            if (!std::isfinite(reach) || !std::isfinite(distance))
                return fail();
            result.ReachError = std::max({0.0F, distance - reach, (2.0F * longest - reach) - distance});
            bool solved = false;
            if (limb.Definition.Solver == LimbSolver::TwoBone)
            {
                solved = SolveLimbTwoBone(*m_Skeleton, limb, *target, m_WorkingPose, result.JointLimited);
            }
            else
            {
                FabrikIkRequest request;
                request.Chain = limb.Bones;
                request.Target = target->Position;
                request.Weight = target->Weight;
                request.MaximumIterations = limb.Definition.MaximumIterations;
                request.Tolerance = limb.Definition.Tolerance;
                solved = SolveFabrikIk(*m_Skeleton, m_WorkingPose, request);
            }
            if (!solved)
                return fail();
            world = RiggingDetail::WorldMatrices(*m_Skeleton, m_WorkingPose);
            result.EndPosition = Math::TransformPoint(world[limb.Bones.back()], {});
            result.PositionError = Distance(result.EndPosition, target->Position);
            if (!Math::IsFinite(result.EndPosition) || !std::isfinite(result.PositionError))
                return fail();
            if (result.ReachError > limb.Definition.Tolerance)
                result.Status = LimbSolveStatus::Unreachable;
            else if (result.JointLimited)
                result.Status = LimbSolveStatus::JointLimited;
            else if (target->Weight < 1.0F)
                result.Status = LimbSolveStatus::Blended;
            else if (result.PositionError > limb.Definition.Tolerance)
                result.Status = LimbSolveStatus::NotConverged;
            else
                result.Status = LimbSolveStatus::Solved;
        }
        if (!ValidPose(m_WorkingPose))
            return fail();
        std::ranges::copy(m_WorkingPose, pose.begin());
        return true;
    }
} // namespace Keire
