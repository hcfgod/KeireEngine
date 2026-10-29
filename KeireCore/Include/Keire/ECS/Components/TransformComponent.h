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

        // Legacy float accessors are approximate. Large-world consumers must use the precise position API.
        [[nodiscard]] Vector3 LocalPosition() const;
        [[nodiscard]] Vector3d LocalPositionPrecise() const noexcept { return m_LocalPosition; }
        void SetLocalPositionPrecise(Vector3d value);
        [[nodiscard]] Vector3d WorldPositionPrecise() const;
        [[nodiscard]] Vector3d InverseTransformPointPrecise(Vector3d value) const;
        void SetWorldPositionPrecise(Vector3d value);
        [[nodiscard]] Quaternion LocalRotation() const noexcept { return m_LocalRotation; }
        [[nodiscard]] Vector3 LocalEulerAngles() const;
        [[nodiscard]] Vector3 LocalScale() const noexcept { return m_LocalScale; }
        void SetLocalPosition(Vector3 value);
        void SetLocalRotation(Quaternion value);
        void SetLocalEulerAngles(Vector3 value);
        void SetLocalScale(Vector3 value);

        [[nodiscard]] Matrix4 LocalMatrix() const;
        [[nodiscard]] Matrix4 WorldMatrix() const;
        // Authoritative simulation pose, independent of presentation interpolation.
        [[nodiscard]] Matrix4 WorldRelativeMatrix(Vector3d origin, double maximumError) const;
        [[nodiscard]] Matrix4 PresentationWorldMatrix() const;
        [[nodiscard]] Vector3d PresentationWorldPositionPrecise() const;
        // Origin must be in the same coordinate frame. Narrow only after subtracting it in double precision.
        [[nodiscard]] Matrix4 PresentationRelativeMatrix(Vector3d origin, double maximumError) const;
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
        // The explicit precise position replaces the matrix translation.
        void SetRuntimePresentationWorldMatrix(Matrix4 value, Vector3d position);
        // Interpolate character translation while retaining current gameplay rotation and scale.
        void SetRuntimePresentationWorldPosition(Vector3 value);
        void SetRuntimePresentationWorldPositionPrecise(Vector3d value);
        // Runtime-only opt-in for transforms authored at fixed simulation boundaries.
        // Physics-owned character/dynamic presentation retains precedence.
        [[nodiscard]] bool FixedPresentationInterpolation() const noexcept { return m_FixedPresentationInterpolation; }
        void SetFixedPresentationInterpolation(bool enabled);
        void BeginRuntimeFixedPresentationSample();
        void EndRuntimeFixedPresentationSample();
        void ApplyRuntimeFixedPresentation(float alpha);
        void ClearRuntimeFixedPresentation() noexcept;
        void ResetPresentationInterpolation() noexcept;
        void Reset();

      protected:
        void OnDisable() override { ResetPresentationInterpolation(); }

      private:
        friend ComponentRegistration CreateTransformComponentRegistration();

        struct PrecisePresentationTransform
        {
            Matrix4 Linear;
            Vector3d Position;
        };

        [[nodiscard]] PrecisePresentationTransform BuildPresentationTransform() const;
        Vector3d m_LocalPosition;
        Quaternion m_LocalRotation;
        Vector3 m_LocalScale{1.0F, 1.0F, 1.0F};
        Matrix4 m_PresentationWorldMatrix;
        Vector3d m_PresentationWorldPosition;
        bool m_HasPresentationWorldMatrix = false;
        bool m_PresentationPositionOnly = false;
        std::uint64_t m_PresentationResetRevision = 0;
        bool m_FixedPresentationInterpolation = false;
        bool m_HasFixedSamples = false;
        bool m_HasFixedPresentation = false;
        EntityId m_FixedParent;
        Vector3d m_PreviousFixedPosition;
        Vector3d m_CurrentFixedPosition;
        Quaternion m_PreviousFixedRotation;
        Quaternion m_CurrentFixedRotation;
        Matrix4 m_FixedPresentationLocal;
        Vector3d m_FixedPresentationPosition;
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateTransformComponentRegistration();
} // namespace Keire
