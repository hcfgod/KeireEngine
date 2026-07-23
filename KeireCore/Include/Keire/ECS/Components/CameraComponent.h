#pragma once

#include "Keire/ECS/Component.h"

#include <cstdint>

namespace Keire
{
    enum class CameraProjection : std::uint8_t
    {
        Perspective,
        Orthographic
    };

    enum class CameraClearMode : std::uint8_t
    {
        Skybox,
        SolidColor
    };

    class KEIRE_API CameraComponent final : public Component
    {
      public:
        CameraComponent();

        [[nodiscard]] static constexpr ComponentTypeId StaticType() noexcept
        {
            return ComponentTypeId(AssetId(0x4b4549524543414dULL, 0x4552410000000001ULL));
        }

        [[nodiscard]] CameraProjection Projection() const noexcept { return m_Projection; }
        [[nodiscard]] CameraClearMode ClearMode() const noexcept { return m_ClearMode; }
        [[nodiscard]] bool Primary() const noexcept { return m_Primary; }
        [[nodiscard]] std::int32_t Priority() const noexcept { return m_Priority; }
        [[nodiscard]] float VerticalFieldOfViewDegrees() const noexcept { return m_VerticalFieldOfViewDegrees; }
        [[nodiscard]] float OrthographicSize() const noexcept { return m_OrthographicSize; }
        [[nodiscard]] float NearPlane() const noexcept { return m_NearPlane; }
        [[nodiscard]] float FarPlane() const noexcept { return m_FarPlane; }
        [[nodiscard]] Color ClearColor() const noexcept { return m_ClearColor; }

        void SetProjection(CameraProjection projection);
        void SetClearMode(CameraClearMode mode);
        void SetPrimary(bool primary);
        void SetPriority(std::int32_t priority);
        void SetVerticalFieldOfViewDegrees(float degrees);
        void SetOrthographicSize(float size);
        void SetClipPlanes(float nearPlane, float farPlane);
        void SetClearColor(Color color);
        [[nodiscard]] Matrix4 ProjectionMatrix(float aspectRatio) const;
        void Reset();

      private:
        friend ComponentRegistration CreateCameraComponentRegistration();
        CameraProjection m_Projection = CameraProjection::Perspective;
        CameraClearMode m_ClearMode = CameraClearMode::Skybox;
        bool m_Primary = true;
        std::int32_t m_Priority = 0;
        float m_VerticalFieldOfViewDegrees = 60.0F;
        float m_OrthographicSize = 10.0F;
        float m_NearPlane = 0.1F;
        float m_FarPlane = 1000.0F;
        Color m_ClearColor{0.10F, 0.12F, 0.16F, 1.0F};
    };

    [[nodiscard]] KEIRE_API ComponentRegistration CreateCameraComponentRegistration();
} // namespace Keire
