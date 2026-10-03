#pragma once

#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Api.h"
#include "Keire/Math/Math.h"
#include "Keire/Ref.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Keire
{
    struct LimbId
    {
        std::uint32_t Value = 0;
        [[nodiscard]] bool operator==(const LimbId&) const noexcept = default;
    };

    enum class LimbSolver : std::uint8_t
    {
        TwoBone,
        Fabrik
    };

    struct LimbBendLimits
    {
        // Angle between the upper and lower segment directions: zero is straight, 180 is folded back.
        float MinimumDegrees = 0.0F;
        float MaximumDegrees = 180.0F;
    };

    struct LimbDefinition
    {
        LimbId Id;
        std::string Name;
        // A contiguous root-to-tip chain; no humanoid semantics are required.
        std::vector<std::string> Bones;
        LimbSolver Solver = LimbSolver::TwoBone;
        // Contact-planner metadata in model units; the pose solver does not query terrain.
        float ContactRadius = 0.02F;
        float Tolerance = 0.001F;
        std::uint32_t MaximumIterations = 12;
        // Enforced within 0.05 degrees for TwoBone only; limits take precedence over partial blending.
        // FABRIK constraints are explicitly rejected rather than silently ignored.
        std::optional<LimbBendLimits> BendLimits;
        // Optional model-space direction relative to the root; overrides target Pole for TwoBone.
        std::optional<Vector3> PreferredBendDirection;
    };

    struct ResolvedLimb
    {
        LimbDefinition Definition;
        std::vector<std::uint32_t> Bones;
        std::vector<float> RestLengths;
        float MaximumReach = 0.0F;
    };

    struct LimbTarget
    {
        LimbId Id;
        // Position and pole are in skeleton model space, not world space.
        Vector3 Position;
        Vector3 Pole{0.0F, 0.0F, 1.0F};
        float Weight = 1.0F;
        bool Enabled = true;
    };

    enum class LimbSolveStatus : std::uint8_t
    {
        Disabled,
        Solved,
        Blended,
        Unreachable,
        NotConverged,
        InvalidInput,
        JointLimited
    };

    struct LimbSolveResult
    {
        LimbId Id;
        LimbSolveStatus Status = LimbSolveStatus::Disabled;
        Vector3 EndPosition;
        // Actual residual after blending, distinct from geometric reach failure.
        float PositionError = 0.0F;
        float ReachError = 0.0F;
        // Joint constraints take precedence over partial pose blending. Endpoint diagnostics retain the original goal.
        bool JointLimited = false;
    };

    // Owns the immutable skeleton and resolves names only at construction. Instances retain scratch pose storage;
    // simultaneous calls on one instance require external synchronization. Returned spans live with this instance.
    class KEIRE_API BoundLimbRig final
    {
      public:
        // Throws invalid_argument for invalid definitions, degenerate rest chains, or interacting limbs.
        // Interacting chains are rejected so solve order cannot move an already-solved limb.
        BoundLimbRig(Ref<const SkeletonAsset> skeleton, std::span<const LimbDefinition> definitions);
        [[nodiscard]] std::span<const ResolvedLimb> Limbs() const noexcept { return m_Limbs; }
        [[nodiscard]] const SkeletonAsset& Skeleton() const noexcept { return *m_Skeleton; }
        // Missing targets and zero weight disable a limb. Unknown/duplicate IDs or invalid input reject the whole
        // batch without changing pose. Results follow definition order; failure marks every result InvalidInput.
        // True means the batch was applied; inspect statuses for unreachable/nonconverged targets.
        [[nodiscard]] bool Solve(std::span<BoneTransform> pose, std::span<const LimbTarget> targets,
                                 std::vector<LimbSolveResult>& results);

      private:
        Ref<const SkeletonAsset> m_Skeleton;
        std::vector<ResolvedLimb> m_Limbs;
        std::vector<BoneTransform> m_WorkingPose;
    };
} // namespace Keire
