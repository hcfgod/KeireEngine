#include "Keire/ECS/Components/AnimatorComponent.h"

#include "Keire/Animation/AnimationSystem.h"
#include "Keire/ECS/Components/TransformComponent.h"

#include <cmath>
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
