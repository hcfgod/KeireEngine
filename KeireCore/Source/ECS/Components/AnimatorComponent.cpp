#include "Keire/ECS/Components/AnimatorComponent.h"

#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Animation/RiggingSystem.h"
#include "Keire/ECS/Components/TransformComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace Keire
{
    namespace
    {
        constexpr std::uint32_t AnimatorSchemaVersion = 7;

        template <typename T>
        [[nodiscard]] T ReadAnimatorProperty(const ComponentPropertyBag& values, const std::string_view key, T fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            if (const auto* value = std::get_if<T>(&found->second))
                return *value;
            throw std::invalid_argument("Animator property has an incompatible type.");
        }

        void ValidateLimbIk(const AnimatorLimbIkSettings& settings)
        {
            const std::array<std::string_view, 3> bones{settings.Root, settings.Middle, settings.End};
            if ((!settings.AutomaticBoneMapping &&
                 std::ranges::any_of(bones, [](const auto bone) { return bone.empty() || bone.size() > 256; })) ||
                std::ranges::any_of(bones, [](const auto bone) { return bone.size() > 256; }) ||
                !Math::IsFinite(settings.TargetOffset) || !std::isfinite(settings.PositionWeight) ||
                settings.PositionWeight < 0.0F || settings.PositionWeight > 1.0F ||
                !std::isfinite(settings.RotationWeight) || settings.RotationWeight < 0.0F ||
                settings.RotationWeight > 1.0F)
            {
                throw std::invalid_argument("Animator limb-IK settings are invalid.");
            }
        }

        void AddLimbDefaults(ComponentPropertyBag& values, const std::string_view prefix, const std::string_view root,
                             const std::string_view middle, const std::string_view end)
        {
            values.insert_or_assign(std::string(prefix) + "Enabled", false);
            values.insert_or_assign(std::string(prefix) + "AutomaticBoneMapping", true);
            values.insert_or_assign(std::string(prefix) + "Target", EntityId{});
            values.insert_or_assign(std::string(prefix) + "Pole", EntityId{});
            values.insert_or_assign(std::string(prefix) + "Root", std::string(root));
            values.insert_or_assign(std::string(prefix) + "Middle", std::string(middle));
            values.insert_or_assign(std::string(prefix) + "End", std::string(end));
            values.insert_or_assign(std::string(prefix) + "TargetOffset", Vector3{});
            values.insert_or_assign(std::string(prefix) + "PositionWeight", 1.0);
            values.insert_or_assign(std::string(prefix) + "RotationWeight", 1.0);
        }
    } // namespace

    AnimatorComponent::AnimatorComponent() : Component(StaticType())
    {
        m_LeftArmIk.Root = "LeftArm";
        m_LeftArmIk.Middle = "LeftForeArm";
        m_LeftArmIk.End = "LeftHand";
        m_RightArmIk.Root = "RightArm";
        m_RightArmIk.Middle = "RightForeArm";
        m_RightArmIk.End = "RightHand";
    }

    void AnimatorComponent::SetGraph(const AssetId graph)
    {
        m_Graph = graph;
        NotifyChanged();
    }

    void AnimatorComponent::SetSkeleton(const AssetId skeleton)
    {
        m_Skeleton = skeleton;
        NotifyChanged();
    }

    void AnimatorComponent::SetSkinnedMesh(const AssetId mesh)
    {
        m_SkinnedMesh = mesh;
        NotifyChanged();
    }

    void AnimatorComponent::SetPoseSource(const AnimatorPoseSource source)
    {
        if (source != AnimatorPoseSource::AnimationGraph && source != AnimatorPoseSource::ProceduralHumanoid)
            throw std::invalid_argument("Animator pose source is invalid.");
        m_PoseSource = source;
        NotifyChanged();
    }

    void AnimatorComponent::SetProceduralProfile(const AssetId profile)
    {
        m_ProceduralProfile = profile;
        NotifyChanged();
    }

    void AnimatorComponent::SetRigDefinition(const AssetId rig)
    {
        m_RigDefinition = rig;
        NotifyChanged();
    }

    void AnimatorComponent::SetProceduralQuality(const ProceduralMotionQuality quality)
    {
        if (quality > ProceduralMotionQuality::Low)
            throw std::invalid_argument("Animator procedural quality is invalid.");
        m_ProceduralQuality = quality;
        NotifyChanged();
    }

    void AnimatorComponent::SetApplyRootMotion(const bool enabled)
    {
        m_ApplyRootMotion = enabled;
        NotifyChanged();
    }

    void AnimatorComponent::SetSpeed(const float speed)
    {
        if (!std::isfinite(speed) || speed < -8.0F || speed > 8.0F)
            throw std::invalid_argument("Animator speed must be finite and in the range -8..8.");
        m_Speed = speed;
        NotifyChanged();
    }

    void AnimatorComponent::SetFloat(std::string parameter, const float value)
    {
        if (!std::isfinite(value))
            throw std::invalid_argument("Animator float parameter values must be finite.");
        AnimatorCommand command;
        command.Type = AnimatorCommandType::SetFloat;
        command.Name = std::move(parameter);
        command.FloatValue = value;
        QueueCommand(std::move(command));
    }

    void AnimatorComponent::SetInteger(std::string parameter, const std::int32_t value)
    {
        AnimatorCommand command;
        command.Type = AnimatorCommandType::SetInteger;
        command.Name = std::move(parameter);
        command.IntegerValue = value;
        QueueCommand(std::move(command));
    }

    void AnimatorComponent::SetBool(std::string parameter, const bool value)
    {
        AnimatorCommand command;
        command.Type = AnimatorCommandType::SetBoolean;
        command.Name = std::move(parameter);
        command.BooleanValue = value;
        QueueCommand(std::move(command));
    }

    void AnimatorComponent::SetTrigger(std::string parameter)
    {
        AnimatorCommand command;
        command.Type = AnimatorCommandType::SetTrigger;
        command.Name = std::move(parameter);
        QueueCommand(std::move(command));
    }

    void AnimatorComponent::ResetTrigger(std::string parameter)
    {
        AnimatorCommand command;
        command.Type = AnimatorCommandType::ResetTrigger;
        command.Name = std::move(parameter);
        QueueCommand(std::move(command));
    }

    void AnimatorComponent::SetLayerWeight(std::string layer, const float value)
    {
        if (!std::isfinite(value) || value < 0.0F || value > 1.0F)
            throw std::invalid_argument("Animator layer weights must be finite and in the range 0..1.");
        AnimatorCommand command;
        command.Type = AnimatorCommandType::SetLayerWeight;
        command.Name = std::move(layer);
        command.FloatValue = value;
        QueueCommand(std::move(command));
    }

    void AnimatorComponent::SetPaused(const bool paused) noexcept { m_Paused = paused; }

    void AnimatorComponent::Play(std::string state, std::string layer, const float normalizedTime)
    {
        if (!std::isfinite(normalizedTime) || normalizedTime < 0.0F || normalizedTime > 1.0F)
            throw std::invalid_argument("Animator normalized time must be finite and in the range 0..1.");
        AnimatorCommand command;
        command.Type = AnimatorCommandType::Play;
        command.Name = std::move(state);
        command.Layer = std::move(layer);
        command.FloatValue = normalizedTime;
        QueueCommand(std::move(command));
    }

    void AnimatorComponent::CrossFade(std::string state, const float duration, std::string layer,
                                      const float normalizedTime)
    {
        if (!std::isfinite(duration) || duration < 0.0F || duration > 60.0F)
            throw std::invalid_argument("Animator cross-fade duration must be finite and in the range 0..60.");
        if (!std::isfinite(normalizedTime) || normalizedTime < 0.0F || normalizedTime > 1.0F)
            throw std::invalid_argument("Animator normalized time must be finite and in the range 0..1.");
        AnimatorCommand command;
        command.Type = AnimatorCommandType::CrossFade;
        command.Name = std::move(state);
        command.Layer = std::move(layer);
        command.FloatValue = normalizedTime;
        command.SecondaryFloatValue = duration;
        QueueCommand(std::move(command));
    }

    void AnimatorComponent::Stop()
    {
        AnimatorCommand command;
        command.Type = AnimatorCommandType::Stop;
        QueueCommand(std::move(command));
    }

    void AnimatorComponent::SetTwoBoneIk(std::string name, std::string root, std::string middle, std::string end,
                                         const Vector3 target, const Vector3 pole, const float weight,
                                         const AnimatorIkSpace space)
    {
        if (!Math::IsFinite(pole))
            throw std::invalid_argument("Animator IK pole must be finite.");
        const auto goalName = name;
        SetFabrikIk(std::move(name), {std::move(root), std::move(middle), std::move(end)}, target, weight, 1, 0.001F,
                    space);
        const auto found = std::ranges::find(m_IkGoals, goalName, &AnimatorIkGoal::Name);
        found->Solver = AnimatorIkSolver::TwoBone;
        found->Pole = pole;
    }

    void AnimatorComponent::SetFabrikIk(std::string name, std::vector<std::string> chain, const Vector3 target,
                                        const float weight, const std::uint32_t maximumIterations,
                                        const float tolerance, const AnimatorIkSpace space)
    {
        if (name.empty() || name.size() > 256)
            throw std::invalid_argument("Animator IK goal names must contain 1..256 characters.");
        if (chain.size() < 2 || chain.size() > 256)
            throw std::invalid_argument("Animator IK chains must contain 2..256 bones.");
        if (std::ranges::any_of(chain, [](const auto& bone) { return bone.empty() || bone.size() > 256; }))
            throw std::invalid_argument("Animator IK bone names must contain 1..256 characters.");
        if (!Math::IsFinite(target) || !std::isfinite(weight) || weight < 0.0F || weight > 1.0F)
            throw std::invalid_argument("Animator IK targets must be finite and weights must be in the range 0..1.");
        if (maximumIterations == 0 || maximumIterations > 1024 || !std::isfinite(tolerance) || tolerance <= 0.0F)
            throw std::invalid_argument("Animator FABRIK settings are outside their supported bounds.");

        AnimatorIkGoal replacement;
        replacement.Name = std::move(name);
        replacement.Solver = AnimatorIkSolver::Fabrik;
        replacement.Space = space;
        replacement.Bones = std::move(chain);
        replacement.Target = target;
        replacement.Weight = weight;
        replacement.MaximumIterations = maximumIterations;
        replacement.Tolerance = tolerance;

        const auto found = std::ranges::find(m_IkGoals, replacement.Name, &AnimatorIkGoal::Name);
        if (found != m_IkGoals.end())
            *found = std::move(replacement);
        else
        {
            if (m_IkGoals.size() >= 64)
                throw std::length_error("Animator IK goal count exceeded its 64-goal bound.");
            m_IkGoals.emplace_back(std::move(replacement));
        }
    }

    bool AnimatorComponent::ClearIk(const std::string_view name) noexcept
    {
        const auto originalSize = m_IkGoals.size();
        std::erase_if(m_IkGoals, [name](const auto& goal) { return goal.Name == name; });
        return m_IkGoals.size() != originalSize;
    }

    void AnimatorComponent::ClearIk() noexcept { m_IkGoals.clear(); }

    void AnimatorComponent::SetFootGrounding(AnimatorFootGroundingSettings settings)
    {
        const std::array<std::string_view, 7> bones{settings.Pelvis,   settings.LeftUpperLeg,  settings.LeftLowerLeg,
                                                    settings.LeftFoot, settings.RightUpperLeg, settings.RightLowerLeg,
                                                    settings.RightFoot};
        if (std::ranges::any_of(bones, [](const auto bone) { return bone.empty() || bone.size() > 256; }) ||
            !std::isfinite(settings.Weight) || settings.Weight < 0.0F || settings.Weight > 1.0F ||
            !std::isfinite(settings.RotationWeight) || settings.RotationWeight < 0.0F ||
            settings.RotationWeight > 1.0F || !std::isfinite(settings.RaycastHeight) || settings.RaycastHeight < 0.0F ||
            !std::isfinite(settings.RaycastDistance) || settings.RaycastDistance <= 0.0F ||
            !std::isfinite(settings.FootOffset) || settings.FootOffset < 0.0F ||
            !std::isfinite(settings.MaximumPelvisAdjustment) || settings.MaximumPelvisAdjustment < 0.0F ||
            !std::isfinite(settings.PlantDistance) || settings.PlantDistance < 0.0F ||
            !std::isfinite(settings.ReleaseDistance) || settings.ReleaseDistance < settings.PlantDistance ||
            !std::isfinite(settings.ResponseTime) || settings.ResponseTime < 0.0F ||
            !std::isfinite(settings.KneeStability) || settings.KneeStability < 0.0F || settings.KneeStability > 1.0F ||
            !std::isfinite(settings.LeanCorrectionWeight) || settings.LeanCorrectionWeight < 0.0F ||
            settings.LeanCorrectionWeight > 1.0F || !std::isfinite(settings.MaximumLeanCorrectionDegrees) ||
            settings.MaximumLeanCorrectionDegrees < 0.0F || settings.MaximumLeanCorrectionDegrees > 180.0F ||
            !std::isfinite(settings.MaximumSlopeDegrees) || settings.MaximumSlopeDegrees < 0.0F ||
            settings.MaximumSlopeDegrees > 89.0F)
            throw std::invalid_argument("Animator foot-grounding settings are invalid.");
        m_FootGrounding = std::move(settings);
        NotifyChanged();
    }

    void AnimatorComponent::SetRuntimeFootGroundingWeight(const float weight)
    {
        if (!std::isfinite(weight) || weight < 0.0F || weight > 1.0F)
            throw std::invalid_argument("Animator runtime foot-grounding weight must be finite and in the range 0..1.");
        m_RuntimeFootGroundingWeight = weight;
    }

    void AnimatorComponent::SetLeftArmIk(AnimatorLimbIkSettings settings)
    {
        ValidateLimbIk(settings);
        m_LeftArmIk = std::move(settings);
        NotifyChanged();
    }

    void AnimatorComponent::SetRightArmIk(AnimatorLimbIkSettings settings)
    {
        ValidateLimbIk(settings);
        m_RightArmIk = std::move(settings);
        NotifyChanged();
    }

    void AnimatorComponent::SetProceduralLocomotion(ProceduralLocomotionIntent intent)
    {
        ValidateProceduralLocomotionIntent(intent);
        m_ProceduralIntent = intent;
    }

    ProceduralLocomotionIntent AnimatorComponent::ConsumeProceduralLocomotionIntent() noexcept
    {
        auto result = m_ProceduralIntent;
        m_ProceduralIntent.JumpRequested = false;
        return result;
    }

    void AnimatorComponent::SetRuntimeProceduralState(const ProceduralLocomotionState state) noexcept
    {
        m_ProceduralState = state;
    }

    std::vector<AnimatorCommand> AnimatorComponent::ConsumeRuntimeCommands()
    {
        return std::exchange(m_RuntimeCommands, {});
    }

    void AnimatorComponent::QueueCommand(AnimatorCommand command)
    {
        if (command.Type != AnimatorCommandType::Stop && (command.Name.empty() || command.Name.size() > 256))
            throw std::invalid_argument("Animator command names must contain 1..256 characters.");
        if (command.Layer.size() > 256)
            throw std::invalid_argument("Animator layer names may not exceed 256 characters.");
        if (m_RuntimeCommands.size() >= 1024)
            throw std::length_error("Animator runtime command queue exceeded its 1024-command bound.");
        command.Sequence = m_NextRuntimeCommand++;
        if (m_NextRuntimeCommand == 0)
            m_NextRuntimeCommand = 1;
        m_RuntimeCommands.emplace_back(std::move(command));
    }

    void AnimatorComponent::SetRuntimePose(std::string state, const float normalizedTime, const bool playing,
                                           const std::span<const Matrix4> skinPalette)
    {
        if (state.size() > 256 || !std::isfinite(normalizedTime) || normalizedTime < 0.0F || normalizedTime > 1.0F ||
            skinPalette.size() > 4096)
            throw std::invalid_argument("Animator runtime pose exceeds its bounds.");
        for (const auto& matrix : skinPalette)
            if (!Math::IsFinite(matrix))
                throw std::invalid_argument("Animator runtime pose contains a non-finite matrix.");
        m_CurrentState = std::move(state);
        m_NormalizedTime = normalizedTime;
        m_RuntimePlaying = playing;
        m_SkinPalette.assign(skinPalette.begin(), skinPalette.end());
    }

    void AnimatorComponent::SetRuntimeDebugSnapshot(std::shared_ptr<const AnimatorDebugSnapshot> snapshot) noexcept
    {
        m_DebugSnapshot = std::move(snapshot);
    }

    void AnimatorComponent::SetRuntimeDiagnostic(std::string diagnostic) noexcept
    {
        if (diagnostic.size() > 4096)
            diagnostic.resize(4096);
        m_RuntimeDiagnostic = std::move(diagnostic);
    }

    void AnimatorComponent::ClearRuntimePose() noexcept
    {
        m_CurrentState.clear();
        m_NormalizedTime = 0.0F;
        m_RuntimePlaying = false;
        m_SkinPalette.clear();
        m_DebugSnapshot.reset();
        m_RuntimeDiagnostic.clear();
        m_RuntimeFootGroundingWeight = 1.0F;
        m_ProceduralIntent = {};
        m_ProceduralState = {};
    }

    ComponentRegistration CreateAnimatorComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = AnimatorComponent::StaticType();
        result.Name = "Animator";
        result.Category = "Animation";
        result.SchemaVersion = AnimatorSchemaVersion;
        result.RequiredComponents = {TransformComponent::StaticType()};
        result.Properties = {
            {"poseSource", "Pose Source", "Animation", ComponentPropertyKind::Integer, false, 0.0, 1.0, 1.0},
            {"graph",
             "Graph",
             "Animation",
             ComponentPropertyKind::Asset,
             false,
             {},
             {},
             0.1,
             AnimationGraphAsset::StaticType()},
            {"proceduralProfile",
             "Procedural Profile",
             "Procedural Motion",
             ComponentPropertyKind::Asset,
             false,
             {},
             {},
             0.1,
             ProceduralMotionProfileAsset::StaticType()},
            {"rigDefinition",
             "Rig Definition",
             "Procedural Motion",
             ComponentPropertyKind::Asset,
             false,
             {},
             {},
             0.1,
             RigDefinitionAsset::StaticType()},
            {"proceduralQuality", "Quality", "Procedural Motion", ComponentPropertyKind::Integer, false, 0.0, 3.0, 1.0},
            {"skeleton",
             "Skeleton",
             "Animation",
             ComponentPropertyKind::Asset,
             false,
             {},
             {},
             0.1,
             SkeletonAsset::StaticType()},
            {"skinnedMesh",
             "Skinned Mesh",
             "Animation",
             ComponentPropertyKind::Asset,
             false,
             {},
             {},
             0.1,
             SkinnedMeshAsset::StaticType()},
            {"applyRootMotion", "Apply Root Motion", "Animation", ComponentPropertyKind::Boolean},
            {"speed", "Speed", "Animation", ComponentPropertyKind::Scalar, false, -8.0, 8.0, 0.05},
            {"footGrounding", "Foot Grounding", "Ground Adaptation", ComponentPropertyKind::Boolean},
            {"footAutomaticBoneMapping", "Automatic Bone Mapping", "Ground Adaptation", ComponentPropertyKind::Boolean},
            {"footAutomaticRaycastDistance", "Automatic Ray Distance", "Ground Adaptation",
             ComponentPropertyKind::Boolean},
            {"footLockPlanted", "Lock Planted Feet", "Ground Adaptation", ComponentPropertyKind::Boolean},
            {"footPelvis", "Pelvis Fallback", "Ground Adaptation", ComponentPropertyKind::Text},
            {"leftUpperLeg", "Left Upper Leg Fallback", "Ground Adaptation", ComponentPropertyKind::Text},
            {"leftLowerLeg", "Left Lower Leg Fallback", "Ground Adaptation", ComponentPropertyKind::Text},
            {"leftFoot", "Left Foot Fallback", "Ground Adaptation", ComponentPropertyKind::Text},
            {"rightUpperLeg", "Right Upper Leg Fallback", "Ground Adaptation", ComponentPropertyKind::Text},
            {"rightLowerLeg", "Right Lower Leg Fallback", "Ground Adaptation", ComponentPropertyKind::Text},
            {"rightFoot", "Right Foot Fallback", "Ground Adaptation", ComponentPropertyKind::Text},
            {"footIkWeight", "Position Weight", "Ground Adaptation", ComponentPropertyKind::Scalar, false, 0.0, 1.0,
             0.01},
            {"footRotationWeight", "Rotation Weight", "Ground Adaptation", ComponentPropertyKind::Scalar, false, 0.0,
             1.0, 0.01},
            {"footRaycastHeight", "Raycast Height", "Ground Adaptation", ComponentPropertyKind::Scalar, false, 0.0,
             1000.0, 0.01},
            {"footRaycastDistance", "Raycast Distance", "Ground Adaptation", ComponentPropertyKind::Scalar, false,
             0.001, 1000.0, 0.01},
            {"footOffset", "Sole Offset", "Ground Adaptation", ComponentPropertyKind::Scalar, false, 0.0, 1000.0,
             0.005},
            {"maximumPelvisAdjustment", "Maximum Pelvis Adjustment", "Ground Adaptation", ComponentPropertyKind::Scalar,
             false, 0.0, 1000.0, 0.01},
            {"footPlantDistance", "Plant Distance", "Ground Adaptation", ComponentPropertyKind::Scalar, false, 0.0,
             1000.0, 0.005},
            {"footReleaseDistance", "Release Distance", "Ground Adaptation", ComponentPropertyKind::Scalar, false, 0.0,
             1000.0, 0.005},
            {"footResponseTime", "Response Time (Seconds)", "Ground Adaptation", ComponentPropertyKind::Scalar, false,
             0.0, 10.0, 0.01},
            {"footKneeStability", "Knee Stability", "Ground Adaptation", ComponentPropertyKind::Scalar, false, 0.0, 1.0,
             0.01},
            {"footLeanCorrectionWeight", "Body Lean Correction", "Ground Adaptation", ComponentPropertyKind::Scalar,
             false, 0.0, 1.0, 0.01},
            {"footMaximumLeanCorrectionDegrees", "Maximum Lean Correction (Degrees)", "Ground Adaptation",
             ComponentPropertyKind::Scalar, false, 0.0, 180.0, 1.0},
            {"footMaximumSlope", "Maximum Ground Slope", "Ground Adaptation", ComponentPropertyKind::Scalar, false, 0.0,
             89.0, 1.0},
            {"footCollisionMask", "Collision Mask", "Ground Adaptation", ComponentPropertyKind::Integer, false, 0.0,
             static_cast<double>(std::numeric_limits<std::uint32_t>::max()), 1.0}};
        const auto appendLimbProperties = [&result](const std::string_view prefix, const std::string_view group)
        {
            const auto key = [prefix](const std::string_view suffix)
            { return std::string(prefix) + std::string(suffix); };
            result.Properties.push_back(
                {key("Enabled"), "Enabled", std::string(group), ComponentPropertyKind::Boolean});
            result.Properties.push_back({key("AutomaticBoneMapping"), "Automatic Bone Mapping", std::string(group),
                                         ComponentPropertyKind::Boolean});
            result.Properties.push_back({key("Target"),
                                         "Target",
                                         std::string(group),
                                         ComponentPropertyKind::Entity,
                                         false,
                                         {},
                                         {},
                                         0.1,
                                         {},
                                         "Drag a scene entity here. Its transform drives hand position and rotation."});
            result.Properties.push_back(
                {key("Pole"),
                 "Pole Override",
                 std::string(group),
                 ComponentPropertyKind::Entity,
                 false,
                 {},
                 {},
                 0.1,
                 {},
                 "Optional elbow pole. When empty, the current animated bend plane is preserved."});
            result.Properties.push_back(
                {key("Root"), "Upper Arm Fallback", std::string(group), ComponentPropertyKind::Text});
            result.Properties.push_back(
                {key("Middle"), "Lower Arm Fallback", std::string(group), ComponentPropertyKind::Text});
            result.Properties.push_back({key("End"), "Hand Fallback", std::string(group), ComponentPropertyKind::Text});
            result.Properties.push_back(
                {key("TargetOffset"), "Target Local Offset", std::string(group), ComponentPropertyKind::Vector3});
            result.Properties.push_back({key("PositionWeight"), "Position Weight", std::string(group),
                                         ComponentPropertyKind::Scalar, false, 0.0, 1.0, 0.01});
            result.Properties.push_back({key("RotationWeight"), "Hand Rotation Weight", std::string(group),
                                         ComponentPropertyKind::Scalar, false, 0.0, 1.0, 0.01});
        };
        appendLimbProperties("leftArmIk", "Left Arm IK");
        appendLimbProperties("rightArmIk", "Right Arm IK");
        result.Factory = [] { return Ref<Component>(CreateRef<AnimatorComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& animator = dynamic_cast<const AnimatorComponent&>(component);
            const auto& foot = animator.m_FootGrounding;
            ComponentPropertyBag values{
                {"poseSource", static_cast<std::int64_t>(animator.m_PoseSource)},
                {"graph", animator.m_Graph},
                {"proceduralProfile", animator.m_ProceduralProfile},
                {"rigDefinition", animator.m_RigDefinition},
                {"proceduralQuality", static_cast<std::int64_t>(animator.m_ProceduralQuality)},
                {"skeleton", animator.m_Skeleton},
                {"skinnedMesh", animator.m_SkinnedMesh},
                {"applyRootMotion", animator.m_ApplyRootMotion},
                {"speed", static_cast<double>(animator.m_Speed)},
                {"footGrounding", foot.Enabled},
                {"footAutomaticBoneMapping", foot.AutomaticBoneMapping},
                {"footAutomaticRaycastDistance", foot.AutomaticRaycastDistance},
                {"footLockPlanted", foot.LockPlantedFeet},
                {"footPelvis", foot.Pelvis},
                {"leftUpperLeg", foot.LeftUpperLeg},
                {"leftLowerLeg", foot.LeftLowerLeg},
                {"leftFoot", foot.LeftFoot},
                {"rightUpperLeg", foot.RightUpperLeg},
                {"rightLowerLeg", foot.RightLowerLeg},
                {"rightFoot", foot.RightFoot},
                {"footIkWeight", static_cast<double>(foot.Weight)},
                {"footRotationWeight", static_cast<double>(foot.RotationWeight)},
                {"footRaycastHeight", static_cast<double>(foot.RaycastHeight)},
                {"footRaycastDistance", static_cast<double>(foot.RaycastDistance)},
                {"footOffset", static_cast<double>(foot.FootOffset)},
                {"maximumPelvisAdjustment", static_cast<double>(foot.MaximumPelvisAdjustment)},
                {"footPlantDistance", static_cast<double>(foot.PlantDistance)},
                {"footReleaseDistance", static_cast<double>(foot.ReleaseDistance)},
                {"footResponseTime", static_cast<double>(foot.ResponseTime)},
                {"footKneeStability", static_cast<double>(foot.KneeStability)},
                {"footLeanCorrectionWeight", static_cast<double>(foot.LeanCorrectionWeight)},
                {"footMaximumLeanCorrectionDegrees", static_cast<double>(foot.MaximumLeanCorrectionDegrees)},
                {"footMaximumSlope", static_cast<double>(foot.MaximumSlopeDegrees)},
                {"footCollisionMask", static_cast<std::int64_t>(foot.CollisionMask)}};
            const auto writeLimb = [&values](const std::string_view prefix, const AnimatorLimbIkSettings& limb)
            {
                const auto key = [prefix](const std::string_view suffix)
                { return std::string(prefix) + std::string(suffix); };
                values.emplace(key("Enabled"), limb.Enabled);
                values.emplace(key("AutomaticBoneMapping"), limb.AutomaticBoneMapping);
                values.emplace(key("Target"), limb.Target);
                values.emplace(key("Pole"), limb.Pole);
                values.emplace(key("Root"), limb.Root);
                values.emplace(key("Middle"), limb.Middle);
                values.emplace(key("End"), limb.End);
                values.emplace(key("TargetOffset"), limb.TargetOffset);
                values.emplace(key("PositionWeight"), static_cast<double>(limb.PositionWeight));
                values.emplace(key("RotationWeight"), static_cast<double>(limb.RotationWeight));
            };
            writeLimb("leftArmIk", animator.m_LeftArmIk);
            writeLimb("rightArmIk", animator.m_RightArmIk);
            return values;
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != AnimatorSchemaVersion)
                throw std::invalid_argument("Unsupported Animator component schema version.");
            auto& animator = dynamic_cast<AnimatorComponent&>(component);
            const auto poseSource = ReadAnimatorProperty(values, "poseSource", std::int64_t{0});
            const auto quality = ReadAnimatorProperty(values, "proceduralQuality", std::int64_t{0});
            if (poseSource < 0 || poseSource > static_cast<std::int64_t>(AnimatorPoseSource::ProceduralHumanoid) ||
                quality < 0 || quality > static_cast<std::int64_t>(ProceduralMotionQuality::Low))
            {
                throw std::invalid_argument("Animator procedural mode contains an invalid enum value.");
            }
            animator.SetPoseSource(static_cast<AnimatorPoseSource>(poseSource));
            animator.SetGraph(ReadAnimatorProperty(values, "graph", AssetId{}));
            animator.SetProceduralProfile(ReadAnimatorProperty(values, "proceduralProfile", AssetId{}));
            animator.SetRigDefinition(ReadAnimatorProperty(values, "rigDefinition", AssetId{}));
            animator.SetProceduralQuality(static_cast<ProceduralMotionQuality>(quality));
            animator.SetSkeleton(ReadAnimatorProperty(values, "skeleton", AssetId{}));
            animator.SetSkinnedMesh(ReadAnimatorProperty(values, "skinnedMesh", AssetId{}));
            animator.SetApplyRootMotion(ReadAnimatorProperty(values, "applyRootMotion", true));
            animator.SetSpeed(static_cast<float>(ReadAnimatorProperty(values, "speed", 1.0)));
            AnimatorFootGroundingSettings foot;
            foot.Enabled = ReadAnimatorProperty(values, "footGrounding", false);
            foot.AutomaticBoneMapping = ReadAnimatorProperty(values, "footAutomaticBoneMapping", true);
            foot.AutomaticRaycastDistance = ReadAnimatorProperty(values, "footAutomaticRaycastDistance", true);
            foot.LockPlantedFeet = ReadAnimatorProperty(values, "footLockPlanted", true);
            foot.Pelvis = ReadAnimatorProperty(values, "footPelvis", std::string("Hips"));
            foot.LeftUpperLeg = ReadAnimatorProperty(values, "leftUpperLeg", std::string("LeftUpLeg"));
            foot.LeftLowerLeg = ReadAnimatorProperty(values, "leftLowerLeg", std::string("LeftLeg"));
            foot.LeftFoot = ReadAnimatorProperty(values, "leftFoot", std::string("LeftFoot"));
            foot.RightUpperLeg = ReadAnimatorProperty(values, "rightUpperLeg", std::string("RightUpLeg"));
            foot.RightLowerLeg = ReadAnimatorProperty(values, "rightLowerLeg", std::string("RightLeg"));
            foot.RightFoot = ReadAnimatorProperty(values, "rightFoot", std::string("RightFoot"));
            foot.Weight = static_cast<float>(ReadAnimatorProperty(values, "footIkWeight", 1.0));
            foot.RotationWeight = static_cast<float>(ReadAnimatorProperty(values, "footRotationWeight", 1.0));
            foot.RaycastHeight = static_cast<float>(ReadAnimatorProperty(values, "footRaycastHeight", 0.35));
            foot.RaycastDistance = static_cast<float>(ReadAnimatorProperty(values, "footRaycastDistance", 0.75));
            foot.FootOffset = static_cast<float>(ReadAnimatorProperty(values, "footOffset", 0.02));
            foot.MaximumPelvisAdjustment =
                static_cast<float>(ReadAnimatorProperty(values, "maximumPelvisAdjustment", 0.5));
            foot.PlantDistance = static_cast<float>(ReadAnimatorProperty(values, "footPlantDistance", 0.08));
            foot.ReleaseDistance = static_cast<float>(ReadAnimatorProperty(values, "footReleaseDistance", 0.18));
            foot.ResponseTime = static_cast<float>(ReadAnimatorProperty(values, "footResponseTime", 0.12));
            foot.KneeStability = static_cast<float>(ReadAnimatorProperty(values, "footKneeStability", 0.9));
            foot.LeanCorrectionWeight =
                static_cast<float>(ReadAnimatorProperty(values, "footLeanCorrectionWeight", 1.0));
            foot.MaximumLeanCorrectionDegrees =
                static_cast<float>(ReadAnimatorProperty(values, "footMaximumLeanCorrectionDegrees", 35.0));
            foot.MaximumSlopeDegrees = static_cast<float>(ReadAnimatorProperty(values, "footMaximumSlope", 60.0));
            const auto collisionMask = ReadAnimatorProperty(
                values, "footCollisionMask", static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()));
            if (collisionMask < 0 ||
                collisionMask > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max()))
                throw std::invalid_argument("Animator foot-grounding collision mask is invalid.");
            foot.CollisionMask = static_cast<std::uint32_t>(collisionMask);
            animator.SetFootGrounding(std::move(foot));
            const auto readLimb = [&values](const std::string_view prefix, const std::string_view root,
                                            const std::string_view middle, const std::string_view end)
            {
                const auto key = [prefix](const std::string_view suffix)
                { return std::string(prefix) + std::string(suffix); };
                AnimatorLimbIkSettings limb;
                limb.Enabled = ReadAnimatorProperty(values, key("Enabled"), false);
                limb.AutomaticBoneMapping = ReadAnimatorProperty(values, key("AutomaticBoneMapping"), true);
                limb.Target = ReadAnimatorProperty(values, key("Target"), EntityId{});
                limb.Pole = ReadAnimatorProperty(values, key("Pole"), EntityId{});
                limb.Root = ReadAnimatorProperty(values, key("Root"), std::string(root));
                limb.Middle = ReadAnimatorProperty(values, key("Middle"), std::string(middle));
                limb.End = ReadAnimatorProperty(values, key("End"), std::string(end));
                limb.TargetOffset = ReadAnimatorProperty(values, key("TargetOffset"), Vector3{});
                limb.PositionWeight = static_cast<float>(ReadAnimatorProperty(values, key("PositionWeight"), 1.0));
                limb.RotationWeight = static_cast<float>(ReadAnimatorProperty(values, key("RotationWeight"), 1.0));
                return limb;
            };
            animator.SetLeftArmIk(readLimb("leftArmIk", "LeftArm", "LeftForeArm", "LeftHand"));
            animator.SetRightArmIk(readLimb("rightArmIk", "RightArm", "RightForeArm", "RightHand"));
        };
        result.Migrate = [](const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1 && version != 2 && version != 3 && version != 4 && version != 5 && version != 6)
                throw std::invalid_argument("Unsupported Animator component schema migration.");
            auto migrated = values;
            migrated.insert_or_assign("poseSource", std::int64_t{0});
            migrated.insert_or_assign("proceduralProfile", AssetId{});
            migrated.insert_or_assign("rigDefinition", AssetId{});
            migrated.insert_or_assign("proceduralQuality", std::int64_t{0});
            if (version == 1)
            {
                migrated.emplace("footGrounding", false);
                migrated.emplace("footPelvis", std::string("Hips"));
                migrated.emplace("leftUpperLeg", std::string("LeftUpLeg"));
                migrated.emplace("leftLowerLeg", std::string("LeftLeg"));
                migrated.emplace("leftFoot", std::string("LeftFoot"));
                migrated.emplace("rightUpperLeg", std::string("RightUpLeg"));
                migrated.emplace("rightLowerLeg", std::string("RightLeg"));
                migrated.emplace("rightFoot", std::string("RightFoot"));
                migrated.emplace("footIkWeight", 1.0);
                migrated.emplace("footRotationWeight", 1.0);
                migrated.emplace("footRaycastHeight", 0.35);
                migrated.emplace("footRaycastDistance", 0.75);
                migrated.emplace("footOffset", 0.02);
                migrated.emplace("maximumPelvisAdjustment", 0.5);
                migrated.emplace("footCollisionMask", static_cast<std::int64_t>(~0U));
            }
            if (version == 1 || version == 2)
            {
                migrated.insert_or_assign("footAutomaticBoneMapping", false);
                migrated.insert_or_assign("footAutomaticRaycastDistance", true);
                migrated.insert_or_assign("footMaximumSlope", 60.0);
            }
            if (version <= 3)
            {
                migrated.insert_or_assign("footLockPlanted", true);
                migrated.insert_or_assign("footPlantDistance", 0.08);
                migrated.insert_or_assign("footReleaseDistance", 0.18);
            }
            if (version <= 4)
            {
                migrated.insert_or_assign("footResponseTime", 0.12);
                migrated.insert_or_assign("footLeanCorrectionWeight", 1.0);
                migrated.insert_or_assign("footMaximumLeanCorrectionDegrees", 35.0);
            }
            migrated.insert_or_assign("footKneeStability", 0.9);
            if (version == 1 || version == 2)
            {
                AddLimbDefaults(migrated, "leftArmIk", "LeftArm", "LeftForeArm", "LeftHand");
                AddLimbDefaults(migrated, "rightArmIk", "RightArm", "RightForeArm", "RightHand");
            }
            return migrated;
        };
        return result;
    }
} // namespace Keire
