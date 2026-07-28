#include "Keire/ECS/Components/AnimatorComponent.h"

#include "Keire/Animation/AnimationSystem.h"
#include "Keire/ECS/Components/TransformComponent.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace Keire
{
    namespace
    {
        template <typename T>
        [[nodiscard]] T ReadAnimatorProperty(const ComponentPropertyBag& values, const std::string_view key,
                                             const T fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            if (const auto* value = std::get_if<T>(&found->second))
                return *value;
            throw std::invalid_argument("Animator property has an incompatible type.");
        }
    } // namespace

    AnimatorComponent::AnimatorComponent() : Component(StaticType()) {}

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
        QueueCommand({0, AnimatorCommandType::SetFloat, std::move(parameter), value});
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
        QueueCommand({0, AnimatorCommandType::SetLayerWeight, std::move(layer), value});
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

    std::vector<AnimatorCommand> AnimatorComponent::ConsumeRuntimeCommands()
    {
        return std::exchange(m_RuntimeCommands, {});
    }

    void AnimatorComponent::QueueCommand(AnimatorCommand command)
    {
        if (command.Name.empty() || command.Name.size() > 256)
            throw std::invalid_argument("Animator command names must contain 1..256 characters.");
        if (m_RuntimeCommands.size() >= 1024)
            throw std::length_error("Animator runtime command queue exceeded its 1024-command bound.");
        command.Sequence = m_NextRuntimeCommand++;
        if (m_NextRuntimeCommand == 0)
            m_NextRuntimeCommand = 1;
        m_RuntimeCommands.emplace_back(std::move(command));
    }

    void AnimatorComponent::SetRuntimePose(std::string state, const std::span<const Matrix4> skinPalette)
    {
        if (state.size() > 256 || skinPalette.size() > 4096)
            throw std::invalid_argument("Animator runtime pose exceeds its bounds.");
        for (const auto& matrix : skinPalette)
            if (!Math::IsFinite(matrix))
                throw std::invalid_argument("Animator runtime pose contains a non-finite matrix.");
        m_CurrentState = std::move(state);
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

    ComponentRegistration CreateAnimatorComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = AnimatorComponent::StaticType();
        result.Name = "Animator";
        result.Category = "Animation";
        result.RequiredComponents = {TransformComponent::StaticType()};
        result.Properties = {{"graph",
                              "Graph",
                              "Animation",
                              ComponentPropertyKind::Asset,
                              false,
                              {},
                              {},
                              0.1,
                              AnimationGraphAsset::StaticType()},
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
                             {"speed", "Speed", "Animation", ComponentPropertyKind::Scalar, false, -8.0, 8.0, 0.05}};
        result.Factory = [] { return Ref<Component>(CreateRef<AnimatorComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& animator = dynamic_cast<const AnimatorComponent&>(component);
            return ComponentPropertyBag{{"graph", animator.m_Graph},
                                        {"skeleton", animator.m_Skeleton},
                                        {"skinnedMesh", animator.m_SkinnedMesh},
                                        {"applyRootMotion", animator.m_ApplyRootMotion},
                                        {"speed", static_cast<double>(animator.m_Speed)}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported Animator component schema version.");
            auto& animator = dynamic_cast<AnimatorComponent&>(component);
            animator.SetGraph(ReadAnimatorProperty(values, "graph", AssetId{}));
            animator.SetSkeleton(ReadAnimatorProperty(values, "skeleton", AssetId{}));
            animator.SetSkinnedMesh(ReadAnimatorProperty(values, "skinnedMesh", AssetId{}));
            animator.SetApplyRootMotion(ReadAnimatorProperty(values, "applyRootMotion", true));
            animator.SetSpeed(static_cast<float>(ReadAnimatorProperty(values, "speed", 1.0)));
        };
        return result;
    }
} // namespace Keire
