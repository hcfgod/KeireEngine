#pragma once

#include "Keire/Math/Math.h"

#include <cstdint>

namespace Keire::Detail
{
    enum class EditorCameraProjection : std::uint8_t
    {
        Perspective,
        Orthographic
    };

    enum class EditorCameraAxis : std::uint8_t
    {
        PositiveX,
        PositiveY,
        PositiveZ
    };

    struct EditorCameraState
    {
        Vector3 Focus{0.0F, 0.5F, 0.0F};
        float YawDegrees = -39.0F;
        float PitchDegrees = -24.0F;
        float Distance = 10.5F;
        float OrthographicSize = 10.0F;
        float MoveSpeed = 8.0F;
        EditorCameraProjection Projection = EditorCameraProjection::Perspective;
    };

    struct EditorCameraInput
    {
        Vector2 PointerDelta;
        float Wheel = 0.0F;
        float DeltaSeconds = 0.0F;
        float MoveForward = 0.0F;
        float MoveRight = 0.0F;
        float MoveUp = 0.0F;
        bool Orbit = false;
        bool Pan = false;
        bool Zoom = false;
        bool Fly = false;
        bool Fast = false;
    };

    class EditorCameraController final
    {
      public:
        EditorCameraController() = default;
        explicit EditorCameraController(EditorCameraState state);

        [[nodiscard]] const EditorCameraState& State() const noexcept { return m_State; }
        void SetState(EditorCameraState state);
        [[nodiscard]] bool Update(const EditorCameraInput& input);
        void Frame(Vector3 center, float radius, float verticalFieldOfViewDegrees = 60.0F, float aspectRatio = 1.0F);
        void SetFocus(Vector3 focus);
        void Snap(EditorCameraAxis axis);
        void ToggleProjection() noexcept;

        [[nodiscard]] Vector3 Forward() const noexcept;
        [[nodiscard]] Vector3 Right() const noexcept;
        [[nodiscard]] Vector3 Up() const noexcept;
        [[nodiscard]] Vector3 Eye() const noexcept;
        [[nodiscard]] Matrix4 ViewMatrix() const;
        [[nodiscard]] Matrix4 ProjectionMatrix(float aspectRatio, float verticalFieldOfViewDegrees = 60.0F,
                                               float nearPlane = 0.05F, float farPlane = 2000.0F) const;

      private:
        static void Validate(const EditorCameraState& state);
        EditorCameraState m_State;
    };
} // namespace Keire::Detail
