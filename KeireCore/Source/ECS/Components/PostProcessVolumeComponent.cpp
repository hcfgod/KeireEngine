#include "Keire/ECS/Components/PostProcessVolumeComponent.h"

#include "Keire/Assets/PostProcessProfileAsset.h"
#include "Keire/ECS/Components/TransformComponent.h"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace Keire
{
    namespace
    {
        template <typename T>
        [[nodiscard]] T ReadVolumeProperty(const ComponentPropertyBag& values, const std::string_view key,
                                           const T fallback)
        {
            const auto found = values.find(key);
            if (found == values.end())
            {
                return fallback;
            }
            if (const auto* value = std::get_if<T>(&found->second))
            {
                return *value;
            }
            throw std::invalid_argument("Post Process Volume property has an incompatible type.");
        }
    } // namespace

    PostProcessVolumeComponent::PostProcessVolumeComponent() : Component(StaticType()) {}

    void PostProcessVolumeComponent::Validate(const PostProcessVolumeSettings& settings)
    {
        if (settings.Shape != PostProcessVolumeShape::Box && settings.Shape != PostProcessVolumeShape::Sphere)
        {
            throw std::invalid_argument("Post Process Volume shape must be Box or Sphere.");
        }
        if (!Math::IsFinite(settings.HalfExtents) || settings.HalfExtents.X <= 0.0 || settings.HalfExtents.Y <= 0.0 ||
            settings.HalfExtents.Z <= 0.0 || !std::isfinite(settings.Radius) || settings.Radius <= 0.0)
        {
            throw std::invalid_argument("Post Process Volume dimensions must be finite and positive.");
        }
        if (!std::isfinite(settings.BlendDistance) || settings.BlendDistance < 0.0)
        {
            throw std::invalid_argument("Post Process Volume blend distance must be finite and nonnegative.");
        }
        if (!std::isfinite(settings.Weight) || settings.Weight < 0.0 || settings.Weight > 1.0)
        {
            throw std::invalid_argument("Post Process Volume weight must be between zero and one.");
        }
    }

    void PostProcessVolumeComponent::Configure(const PostProcessVolumeSettings& settings)
    {
        Validate(settings);
        if (settings == m_Settings)
        {
            return;
        }
        m_Settings = settings;
        NotifyChanged();
    }

    void PostProcessVolumeComponent::Reset() { Configure({}); }

    ComponentRegistration CreatePostProcessVolumeComponentRegistration()
    {
        ComponentRegistration result;
        result.Type = PostProcessVolumeComponent::StaticType();
        result.Name = "Post Process Volume";
        result.Category = "Rendering";
        result.RequiredComponents = {TransformComponent::StaticType()};
        result.Properties = {
            {"profile", "Profile", "Post Processing", ComponentPropertyKind::Asset},
            {"global", "Global", "Volume", ComponentPropertyKind::Boolean},
            {"shape", "Shape (Box / Sphere)", "Volume", ComponentPropertyKind::Integer, false, 0.0, 1.0, 1.0},
            {"halfExtents", "Box Half Extents (m)", "Volume", ComponentPropertyKind::Vector3d},
            {"radius", "Sphere Radius (m)", "Volume", ComponentPropertyKind::Scalar, false, 0.001, {}, 0.1},
            {"blendDistance", "Blend Distance (m)", "Blending", ComponentPropertyKind::Scalar, false, 0.0, {}, 0.1},
            {"weight", "Weight", "Blending", ComponentPropertyKind::Scalar, false, 0.0, 1.0, 0.01},
            {"priority", "Priority", "Blending", ComponentPropertyKind::Integer, false,
             static_cast<double>(std::numeric_limits<std::int32_t>::min()),
             static_cast<double>(std::numeric_limits<std::int32_t>::max()), 1.0}};
        result.Factory = [] { return Ref<Component>(CreateRef<PostProcessVolumeComponent>()); };
        result.Properties.front().ExpectedAssetType = PostProcessProfileAsset::StaticType();
        result.Serialize = [](const Component& component)
        {
            const auto& settings = dynamic_cast<const PostProcessVolumeComponent&>(component).Settings();
            return ComponentPropertyBag{{"profile", settings.Profile},
                                        {"global", settings.Global},
                                        {"shape", static_cast<std::int64_t>(settings.Shape)},
                                        {"halfExtents", settings.HalfExtents},
                                        {"radius", settings.Radius},
                                        {"blendDistance", settings.BlendDistance},
                                        {"weight", settings.Weight},
                                        {"priority", static_cast<std::int64_t>(settings.Priority)}};
        };
        result.Deserialize = [](Component& component, const ComponentPropertyBag& values, const std::uint32_t version)
        {
            if (version != 1)
            {
                throw std::invalid_argument("Unsupported Post Process Volume component schema version.");
            }
            PostProcessVolumeSettings settings;
            settings.Profile = ReadVolumeProperty(values, "profile", settings.Profile);
            settings.Global = ReadVolumeProperty(values, "global", settings.Global);
            const auto shape = ReadVolumeProperty(values, "shape", std::int64_t{0});
            if (shape < 0 || shape > 1)
            {
                throw std::invalid_argument("Post Process Volume shape is invalid.");
            }
            settings.Shape = static_cast<PostProcessVolumeShape>(shape);
            settings.HalfExtents = ReadVolumeProperty(values, "halfExtents", settings.HalfExtents);
            settings.Radius = ReadVolumeProperty(values, "radius", settings.Radius);
            settings.BlendDistance = ReadVolumeProperty(values, "blendDistance", settings.BlendDistance);
            settings.Weight = ReadVolumeProperty(values, "weight", settings.Weight);
            const auto priority = ReadVolumeProperty(values, "priority", std::int64_t{0});
            if (priority < std::numeric_limits<std::int32_t>::min() ||
                priority > std::numeric_limits<std::int32_t>::max())
            {
                throw std::invalid_argument("Post Process Volume priority exceeds the supported integer range.");
            }
            settings.Priority = static_cast<std::int32_t>(priority);
            dynamic_cast<PostProcessVolumeComponent&>(component).Configure(settings);
        };
        return result;
    }
} // namespace Keire
