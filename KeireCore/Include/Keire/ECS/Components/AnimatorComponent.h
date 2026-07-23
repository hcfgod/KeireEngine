#pragma once

#include "Keire/ECS/Component.h"

#include <span>
#include <string>
#include <vector>

namespace Keire
{
    class KEIRE_API AnimatorComponent final : public Component
    {
      public:
        AnimatorComponent();
        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245414e49ULL, 0x4d41544f52000001ULL));
        }

        [[nodiscard]] AssetId Graph() const noexcept { return m_Graph; }
        [[nodiscard]] AssetId Skeleton() const noexcept { return m_Skeleton; }
        [[nodiscard]] AssetId SkinnedMesh() const noexcept { return m_SkinnedMesh; }
        [[nodiscard]] bool ApplyRootMotion() const noexcept { return m_ApplyRootMotion; }
        [[nodiscard]] float Speed() const noexcept { return m_Speed; }
        [[nodiscard]] std::string_view CurrentState() const noexcept { return m_CurrentState; }
        [[nodiscard]] std::span<const Matrix4> SkinPalette() const noexcept { return m_SkinPalette; }

        void SetGraph(AssetId graph);
        void SetSkeleton(AssetId skeleton);
        void SetSkinnedMesh(AssetId mesh);
        void SetApplyRootMotion(bool enabled);
        void SetSpeed(float speed);
        void SetRuntimePose(std::string state, std::span<const Matrix4> skinPalette);

      private:
        friend ComponentRegistration CreateAnimatorComponentRegistration();
        AssetId m_Graph;
        AssetId m_Skeleton;
        AssetId m_SkinnedMesh;
        bool m_ApplyRootMotion = true;
        float m_Speed = 1.0F;
        std::string m_CurrentState;
        std::vector<Matrix4> m_SkinPalette;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateAnimatorComponentRegistration();
} // namespace Keire
