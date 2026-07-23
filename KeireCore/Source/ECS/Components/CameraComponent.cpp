#include "Keire/ECS/Components/CameraComponent.h"

#include "Keire/ECS/Components/TransformComponent.h"

#include <cmath>
#include <stdexcept>

namespace Keire
{
    namespace
    {
        template <typename T>
        [[nodiscard]] T ReadCameraProperty(const ComponentPropertyBag& values, const std::string_view key,
                                           const T fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
                return fallback;
            if (const auto* value = std::get_if<T>(&found->second))
                return *value;
            throw std::invalid_argument("Camera property has an incompatible type.");
        }

        [[nodiscard]] bool ValidUnitColor(const Color color) noexcept
        {
            return Math::IsFinite(color) && color.Red >= 0.0F && color.Red <= 1.0F && color.Green >= 0.0F &&
                   color.Green <= 1.0F && color.Blue >= 0.0F && color.Blue <= 1.0F && color.Alpha >= 0.0F &&
                   color.Alpha <= 1.0F;
        }
    } // namespace

    CameraComponent::CameraComponent() : Component(StaticType()) {}

    void CameraComponent::SetProjection(const CameraProjection projection)
    {
        m_Projection = projection;
        NotifyChanged();
    }

    void CameraComponent::SetClearMode(const CameraClearMode mode)
    {
        if (mode != CameraClearMode::Skybox && mode != CameraClearMode::SolidColor)
            throw std::invalid_argument("Camera clear mode is invalid.");
        m_ClearMode = mode;
        NotifyChanged();
    }

    void CameraComponent::SetPrimary(const bool primary)
    {
        m_Primary = primary;
        NotifyChanged();
    }

    void CameraComponent::SetPriority(const std::int32_t priority)
    {
        if (priority < -1'000'000 || priority > 1'000'000)
            throw std::invalid_argument("Camera priority must be in the range -1000000..1000000.");
        m_Priority = priority;
        NotifyChanged();
    }

    void CameraComponent::SetVerticalFieldOfViewDegrees(const float degrees)
    {
        if (!std::isfinite(degrees) || degrees <= 1.0F || degrees >= 179.0F)
            throw std::invalid_argument("Camera vertical field of view must be in the range 1..179 degrees.");
        m_VerticalFieldOfViewDegrees = degrees;
        NotifyChanged();
    }

    void CameraComponent::SetOrthographicSize(const float size)
    {
        if (!std::isfinite(size) || size <= 0.001F || size > 1'000'000.0F)
            throw std::invalid_argument("Camera orthographic size must be in the range 0.001..1000000.");
        m_OrthographicSize = size;
        NotifyChanged();
    }

    void CameraComponent::SetClipPlanes(const float nearPlane, const float farPlane)
    {
        if (!std::isfinite(nearPlane) || !std::isfinite(farPlane) || nearPlane <= 0.0F || farPlane <= nearPlane ||
            farPlane > 10'000'000.0F)
            throw std::invalid_argument("Camera clip planes must satisfy 0 < near < far <= 10000000.");
        m_NearPlane = nearPlane;
        m_FarPlane = farPlane;
        NotifyChanged();
    }

    void CameraComponent::SetClearColor(const Color color)
    {
        if (!ValidUnitColor(color))
            throw std::invalid_argument("Camera clear color must contain finite values in 0..1.");
        m_ClearColor = color;
        NotifyChanged();
    }

    Matrix4 CameraComponent::ProjectionMatrix(const float aspectRatio) const
    {
        return m_Projection == CameraProjection::Perspective
                   ? Math::Perspective(m_VerticalFieldOfViewDegrees, aspectRatio, m_NearPlane, m_FarPlane)
                   : Math::Orthographic(m_OrthographicSize, aspectRatio, m_NearPlane, m_FarPlane);
    }

    void CameraComponent::Reset()
    {
        m_Projection = CameraProjection::Perspective;
        m_ClearMode = CameraClearMode::Skybox;
        m_Primary = true;
        m_Priority = 0;
        m_VerticalFieldOfViewDegrees = 60.0F;
        m_OrthographicSize = 10.0F;
        m_NearPlane = 0.1F;
        m_FarPlane = 1000.0F;
        m_ClearColor = {0.10F, 0.12F, 0.16F, 1.0F};
        NotifyChanged();
    }

    ComponentRegistration CreateCameraComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = CameraComponent::StaticType();
        result.Name = "Camera";
        result.Category = "Rendering";
        result.RequiredComponents = {TransformComponent::StaticType()};
        result.Properties = {
            {"projection", "Projection", "Camera", ComponentPropertyKind::Integer},
            {"clearMode", "Background", "Environment", ComponentPropertyKind::Integer},
            {"primary", "Primary", "Camera", ComponentPropertyKind::Boolean},
            {"priority", "Priority", "Camera", ComponentPropertyKind::Integer, false, -1'000'000.0, 1'000'000.0, 1.0},
            {"fieldOfView", "Field of View", "Projection", ComponentPropertyKind::Scalar, false, 1.0, 179.0, 0.1},
            {"orthographicSize", "Size", "Projection", ComponentPropertyKind::Scalar, false, 0.001, 1'000'000.0, 0.1},
            {"nearPlane", "Near", "Clipping", ComponentPropertyKind::Scalar, false, 0.0001, 10'000'000.0, 0.01},
            {"farPlane", "Far", "Clipping", ComponentPropertyKind::Scalar, false, 0.001, 10'000'000.0, 1.0},
            {"clearColor", "Clear Color", "Environment", ComponentPropertyKind::Color}};
        result.Factory = [] { return Ref<Component>(CreateRef<CameraComponent>()); };
        result.Serialize = [](const Component& component)
        {
            const auto& camera = dynamic_cast<const CameraComponent&>(component);
            return ComponentPropertyBag{{"projection", static_cast<std::int64_t>(camera.m_Projection)},
                                        {"clearMode", static_cast<std::int64_t>(camera.m_ClearMode)},
                                        {"primary", camera.m_Primary},
                                        {"priority", static_cast<std::int64_t>(camera.m_Priority)},
                                        {"fieldOfView", static_cast<double>(camera.m_VerticalFieldOfViewDegrees)},
                                        {"orthographicSize", static_cast<double>(camera.m_OrthographicSize)},
                                        {"nearPlane", static_cast<double>(camera.m_NearPlane)},
                                        {"farPlane", static_cast<double>(camera.m_FarPlane)},
                                        {"clearColor", camera.m_ClearColor}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
                throw std::invalid_argument("Unsupported Camera component schema version.");
            auto& camera = dynamic_cast<CameraComponent&>(component);
            const auto projection = ReadCameraProperty(values, "projection", std::int64_t{0});
            if (projection < 0 || projection > 1)
                throw std::invalid_argument("Camera projection is invalid.");
            camera.SetProjection(static_cast<CameraProjection>(projection));
            const auto clearMode = ReadCameraProperty(values, "clearMode", std::int64_t{0});
            if (clearMode < 0 || clearMode > 1)
                throw std::invalid_argument("Camera clear mode is invalid.");
            camera.SetClearMode(static_cast<CameraClearMode>(clearMode));
            camera.SetPrimary(ReadCameraProperty(values, "primary", true));
            camera.SetPriority(static_cast<std::int32_t>(ReadCameraProperty(values, "priority", std::int64_t{0})));
            camera.SetVerticalFieldOfViewDegrees(static_cast<float>(ReadCameraProperty(values, "fieldOfView", 60.0)));
            camera.SetOrthographicSize(static_cast<float>(ReadCameraProperty(values, "orthographicSize", 10.0)));
            camera.SetClipPlanes(static_cast<float>(ReadCameraProperty(values, "nearPlane", 0.1)),
                                 static_cast<float>(ReadCameraProperty(values, "farPlane", 1000.0)));
            camera.SetClearColor(ReadCameraProperty(values, "clearColor", Color{0.10F, 0.12F, 0.16F, 1.0F}));
        };
        return result;
    }
} // namespace Keire
