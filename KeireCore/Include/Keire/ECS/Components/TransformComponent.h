#pragma once

#include "Keire/ECS/Component.h"
#include "Keire/ECS/Entity.h"

#include <cstdint>

namespace Keire
{
    class KEIRE_API TransformComponent final : public Component
    {
      public:
        static constexpr float MinimumScaleMagnitude = 0.000001F;

        TransformComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b45495245545241ULL, 0x4e53464f524d0001ULL));
        }

        [[nodiscard]] static bool IsValidLocalScale(Vector3 value) noexcept;

        [[nodiscard]] Vector3 LocalPosition() const noexcept { return m_LocalPosition; }
        [[nodiscard]] Quaternion LocalRotation() const noexcept { return m_LocalRotation; }
        [[nodiscard]] Vector3 LocalEulerAngles() const;
        [[nodiscard]] Vector3 LocalScale() const noexcept { return m_LocalScale; }
        void SetLocalPosition(Vector3 value);
        void SetLocalRotation(Quaternion value);
        void SetLocalEulerAngles(Vector3 value);
        void SetLocalScale(Vector3 value);

        [[nodiscard]] Matrix4 LocalMatrix() const;
        [[nodiscard]] Matrix4 WorldMatrix() const;
        [[nodiscard]] Matrix4 PresentationWorldMatrix() const;
        [[nodiscard]] Vector3 WorldPosition() const;
        [[nodiscard]] Quaternion WorldRotation() const;
        [[nodiscard]] Vector3 PresentationWorldPosition() const;
        [[nodiscard]] Quaternion PresentationWorldRotation() const;
        [[nodiscard]] std::uint64_t PresentationResetRevision() const noexcept { return m_PresentationResetRevision; }
        void SetWorldPosition(Vector3 value);
        void SetWorldRotation(Quaternion value);
        [[nodiscard]] Entity Parent() const noexcept;
        [[nodiscard]] std::vector<Entity> Children() const;
        void SetParent(Entity parent = {}, bool preserveWorldTransform = true);
        void SetRuntimePresentationWorldMatrix(Matrix4 value);
        void ResetPresentationInterpolation() noexcept;
        void Reset();

      private:
        friend ComponentRegistration CreateTransformComponentRegistration();
        Vector3 m_LocalPosition;
        Quaternion m_LocalRotation;
        Vector3 m_LocalScale{1.0F, 1.0F, 1.0F};
        Matrix4 m_PresentationWorldMatrix;
        bool m_HasPresentationWorldMatrix = false;
        std::uint64_t m_PresentationResetRevision = 0;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateTransformComponentRegistration();
} // namespace Keire
