#pragma once

#include "Keire/Math/Math.h"
#include "Keire/Math/Vector3d.h"
#include "KeireInternal/Spatial/FrameRotation.h"

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

    enum class EditorCameraNavigationMode : std::uint8_t
    {
        None,
        Orbit,
        Pan,
        Zoom,
        Fly
    };

    struct EditorCameraPointerButtons
    {
        bool Left = false;
        bool Middle = false;
        bool Right = false;
    };

    [[nodiscard]] EditorCameraNavigationMode ResolveEditorCameraNavigation(bool alt,
                                                                           EditorCameraPointerButtons pressed) noexcept;
    [[nodiscard]] bool EditorCameraNavigationHeld(EditorCameraNavigationMode mode,
                                                  EditorCameraPointerButtons down) noexcept;

    struct EditorCameraPointerWrap
    {
        Vector2 Position;
        bool Wrapped = false;
    };

    [[nodiscard]] EditorCameraPointerWrap ResolveEditorCameraPointerWrap(Vector2 position, Vector2 minimum,
                                                                         Vector2 maximum) noexcept;

    struct EditorCameraState
    {
        Vector3d Focus{0.0F, 0.5F, 0.0F};
        float YawDegrees = -39.0F;
        float PitchDegrees = -24.0F;
        float Distance = 10.5F;
        float OrthographicSize = 10.0F;
        float MoveSpeed = 8.0F;
        EditorCameraProjection Projection = EditorCameraProjection::Perspective;
        FrameRotation NavigationToWorld;
        bool SurfaceNavigation = false;
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
        void SetFocusPrecise(Vector3d focus);
        void FramePrecise(Vector3d center, float radius, float verticalFieldOfViewDegrees = 60.0F,
                          float aspectRatio = 1.0F);
        void Snap(EditorCameraAxis axis);
        void ToggleProjection() noexcept;
        void SetSurfaceNavigation(bool enabled) noexcept;
        void FollowSurfaceUp(Vector3 up);
        void ReexpressReferenceFrame(Vector3d focusInTarget, const FrameRotation& sourceToTarget);
        [[nodiscard]] Vector3 NavigationUp() const noexcept;

        [[nodiscard]] Vector3 Forward() const noexcept;
        [[nodiscard]] Vector3 Right() const noexcept;
        [[nodiscard]] Vector3 Up() const noexcept;
        [[nodiscard]] Vector3 Eye() const;
        [[nodiscard]] Vector3d EyePrecise() const noexcept;
        // View coordinates are relative to State().Focus.

        [[nodiscard]] Matrix4 ViewMatrix() const;
        [[nodiscard]] Matrix4 ProjectionMatrix(float aspectRatio, float verticalFieldOfViewDegrees = 60.0F,
                                               float nearPlane = 0.05F, float farPlane = 2000.0F) const;

      private:
        static void Validate(const EditorCameraState& state);
        EditorCameraState m_State;
    };
} // namespace Keire::Detail
