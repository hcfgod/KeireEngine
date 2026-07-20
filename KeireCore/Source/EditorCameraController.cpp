#include "KeireInternal/EditorCameraController.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Keire::Detail
{
    namespace
    {
        constexpr float DegreesToRadians = 0.01745329251994329577F;

        [[nodiscard]] Vector3 Add(const Vector3 left, const Vector3 right) noexcept
        {
            return {left.X + right.X, left.Y + right.Y, left.Z + right.Z};
        }

        [[nodiscard]] Vector3 Scale(const Vector3 value, const float scalar) noexcept
        {
            return {value.X * scalar, value.Y * scalar, value.Z * scalar};
        }

        [[nodiscard]] float Length(const Vector3 value) noexcept
        {
            return std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
        }

        [[nodiscard]] Vector3 Normalize(const Vector3 value) noexcept
        {
            const float length = Length(value);
            return length > 0.000001F ? Scale(value, 1.0F / length) : Vector3{};
        }

        [[nodiscard]] Vector3 Cross(const Vector3 left, const Vector3 right) noexcept
        {
            return {left.Y * right.Z - left.Z * right.Y, left.Z * right.X - left.X * right.Z,
                    left.X * right.Y - left.Y * right.X};
        }
    } // namespace

    EditorCameraController::EditorCameraController(EditorCameraState state) { SetState(state); }

    void EditorCameraController::Validate(const EditorCameraState& state)
    {
        if (!Math::IsFinite(state.Focus) || !std::isfinite(state.YawDegrees) || !std::isfinite(state.PitchDegrees) ||
            !std::isfinite(state.Distance) || !std::isfinite(state.OrthographicSize) ||
            !std::isfinite(state.MoveSpeed) || state.Distance < 0.05F || state.Distance > 5000.0F ||
            state.OrthographicSize < 0.01F || state.OrthographicSize > 10000.0F || state.MoveSpeed < 0.01F ||
            state.MoveSpeed > 10000.0F)
            throw std::invalid_argument("Editor camera state is invalid or outside its supported bounds.");
    }

    void EditorCameraController::SetState(EditorCameraState state)
    {
        Validate(state);
        state.PitchDegrees = std::clamp(state.PitchDegrees, -89.9F, 89.9F);
        m_State = state;
    }

    bool EditorCameraController::Update(const EditorCameraInput& input)
    {
        if (!Math::IsFinite(input.PointerDelta) || !std::isfinite(input.Wheel) || !std::isfinite(input.DeltaSeconds) ||
            input.DeltaSeconds < 0.0F || input.DeltaSeconds > 1.0F)
            throw std::invalid_argument("Editor camera input is invalid.");
        bool changed = false;
        const float acceleration = input.Fast ? 4.0F : 1.0F;

        if (input.Orbit || input.Fly)
        {
            m_State.YawDegrees += input.PointerDelta.X * 0.20F * acceleration;
            m_State.PitchDegrees =
                std::clamp(m_State.PitchDegrees - input.PointerDelta.Y * 0.20F * acceleration, -89.9F, 89.9F);
            changed = changed || input.PointerDelta.X != 0.0F || input.PointerDelta.Y != 0.0F;
        }

        if (input.Fly && input.Wheel != 0.0F)
        {
            m_State.MoveSpeed = std::clamp(m_State.MoveSpeed * std::exp(input.Wheel * 0.18F), 0.01F, 10000.0F);
            changed = true;
        }
        else if (input.Wheel != 0.0F || input.Zoom)
        {
            const float drag = input.Zoom ? input.PointerDelta.Y * 0.012F : 0.0F;
            const float zoom = -input.Wheel * 0.12F * acceleration + drag * acceleration;
            if (m_State.Projection == EditorCameraProjection::Perspective)
                m_State.Distance = std::clamp(m_State.Distance * std::exp(zoom), 0.05F, 5000.0F);
            else
                m_State.OrthographicSize = std::clamp(m_State.OrthographicSize * std::exp(zoom), 0.01F, 10000.0F);
            changed = true;
        }

        if (input.Pan)
        {
            const float scale = (m_State.Projection == EditorCameraProjection::Perspective ? m_State.Distance
                                                                                           : m_State.OrthographicSize) *
                                0.002F * acceleration;
            m_State.Focus = Add(m_State.Focus, Scale(Right(), -input.PointerDelta.X * scale));
            m_State.Focus = Add(m_State.Focus, Scale(Up(), input.PointerDelta.Y * scale));
            changed = changed || input.PointerDelta.X != 0.0F || input.PointerDelta.Y != 0.0F;
        }

        if (input.Fly || input.MoveForward != 0.0F || input.MoveRight != 0.0F || input.MoveUp != 0.0F)
        {
            const float speed = m_State.MoveSpeed * input.DeltaSeconds * acceleration;
            Vector3 movement =
                Add(Scale(Forward(), input.MoveForward * speed), Scale(Right(), input.MoveRight * speed));
            movement = Add(movement, Scale(Vector3{0.0F, 1.0F, 0.0F}, input.MoveUp * speed));
            m_State.Focus = Add(m_State.Focus, movement);
            changed = changed || Length(movement) > 0.0F;
        }
        return changed;
    }

    void EditorCameraController::Frame(const Vector3 center, const float radius, const float verticalFieldOfViewDegrees)
    {
        if (!Math::IsFinite(center) || !std::isfinite(radius) || radius <= 0.0F || verticalFieldOfViewDegrees <= 1.0F ||
            verticalFieldOfViewDegrees >= 179.0F)
            throw std::invalid_argument("Editor camera framing bounds are invalid.");
        m_State.Focus = center;
        m_State.Distance =
            std::clamp(radius / std::sin(verticalFieldOfViewDegrees * DegreesToRadians * 0.5F) * 1.1F, 0.05F, 5000.0F);
        m_State.OrthographicSize = std::clamp(radius * 2.2F, 0.01F, 10000.0F);
    }

    void EditorCameraController::SetFocus(const Vector3 focus)
    {
        if (!Math::IsFinite(focus))
            throw std::invalid_argument("Editor camera focus must be finite.");
        m_State.Focus = focus;
    }

    void EditorCameraController::Snap(const EditorCameraAxis axis)
    {
        m_State.PitchDegrees = 0.0F;
        switch (axis)
        {
        case EditorCameraAxis::PositiveX:
            m_State.YawDegrees = -90.0F;
            break;
        case EditorCameraAxis::PositiveY:
            m_State.YawDegrees = 0.0F;
            m_State.PitchDegrees = -89.9F;
            break;
        case EditorCameraAxis::PositiveZ:
            m_State.YawDegrees = 180.0F;
            break;
        }
    }

    void EditorCameraController::ToggleProjection() noexcept
    {
        m_State.Projection = m_State.Projection == EditorCameraProjection::Perspective
                                 ? EditorCameraProjection::Orthographic
                                 : EditorCameraProjection::Perspective;
    }

    Vector3 EditorCameraController::Forward() const noexcept
    {
        const float yaw = m_State.YawDegrees * DegreesToRadians;
        const float pitch = m_State.PitchDegrees * DegreesToRadians;
        const float horizontal = std::cos(pitch);
        return Normalize({horizontal * std::sin(yaw), std::sin(pitch), horizontal * std::cos(yaw)});
    }

    Vector3 EditorCameraController::Right() const noexcept
    {
        return Normalize(Cross(Vector3{0.0F, 1.0F, 0.0F}, Forward()));
    }

    Vector3 EditorCameraController::Up() const noexcept { return Normalize(Cross(Forward(), Right())); }

    Vector3 EditorCameraController::Eye() const noexcept
    {
        return Add(m_State.Focus, Scale(Forward(), -m_State.Distance));
    }

    Matrix4 EditorCameraController::ViewMatrix() const
    {
        const auto forward = Forward();
        const auto up = std::abs(forward.Y) > 0.999F ? Vector3{0.0F, 0.0F, 1.0F} : Vector3{0.0F, 1.0F, 0.0F};
        return Math::LookAt(Eye(), m_State.Focus, up);
    }

    Matrix4 EditorCameraController::ProjectionMatrix(const float aspectRatio, const float verticalFieldOfViewDegrees,
                                                     const float nearPlane, const float farPlane) const
    {
        return m_State.Projection == EditorCameraProjection::Perspective
                   ? Math::Perspective(verticalFieldOfViewDegrees, aspectRatio, nearPlane, farPlane)
                   : Math::Orthographic(m_State.OrthographicSize, aspectRatio, nearPlane, farPlane);
    }
} // namespace Keire::Detail
