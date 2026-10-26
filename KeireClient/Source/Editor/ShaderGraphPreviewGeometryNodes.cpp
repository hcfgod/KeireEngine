#include "KeireClientInternal/Editor/ShaderGraphPreviewEvaluatorState.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace KeireEditor::ShaderGraphPreviewInternal
{
    PreviewGraphValue ShaderGraphPreviewEvaluator::Impl::EvaluateGeometryNode(const Keire::ShaderGraphNode& node)
    {
        PreviewGraphValue result;
        switch (node.Kind)
        {
        case Keire::ShaderGraphNodeKind::UV:
            result = {.Data = {m_Uv.X, m_Uv.Y, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector2};
            break;
        case Keire::ShaderGraphNodeKind::UVTransform:
        {
            const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
            const auto tiling = Coerce(NamedInput(node, "Tiling"), Keire::ShaderGraphValueType::Vector2).Data;
            const auto offset = Coerce(NamedInput(node, "Offset"), Keire::ShaderGraphValueType::Vector2).Data;
            result = {.Data = {uv.X * tiling.X + offset.X, uv.Y * tiling.Y + offset.Y, 0.0F, 0.0F},
                      .Type = Keire::ShaderGraphValueType::Vector2};
            break;
        }
        case Keire::ShaderGraphNodeKind::NormalMap:
        {
            const auto sample = Coerce(NamedInput(node, "Sample"), Keire::ShaderGraphValueType::Color).Data;
            const float scale = Coerce(NamedInput(node, "Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
            Keire::Vector3 tangentNormal{sample.X * 2.0F - 1.0F, sample.Y * 2.0F - 1.0F, sample.Z * 2.0F - 1.0F};
            tangentNormal.X *= scale;
            tangentNormal.Y *= scale;
            tangentNormal = Normalize(tangentNormal);
            const auto tangent = Normalize(Cross({0.0F, 1.0F, 0.0F}, m_Normal), {1.0F, 0.0F, 0.0F});
            const auto bitangent = Normalize(Cross(m_Normal, tangent), {0.0F, 1.0F, 0.0F});
            const auto world =
                Normalize({tangent.X * tangentNormal.X + bitangent.X * tangentNormal.Y + m_Normal.X * tangentNormal.Z,
                           tangent.Y * tangentNormal.X + bitangent.Y * tangentNormal.Y + m_Normal.Y * tangentNormal.Z,
                           tangent.Z * tangentNormal.X + bitangent.Z * tangentNormal.Y + m_Normal.Z * tangentNormal.Z},
                          m_Normal);
            result = {.Data = {world.X, world.Y, world.Z, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector3};
            break;
        }
        case Keire::ShaderGraphNodeKind::DetailNormal:
        {
            const auto base = Coerce(NamedInput(node, "Base"), Keire::ShaderGraphValueType::Vector3).Data;
            const auto detail = Coerce(NamedInput(node, "Detail"), Keire::ShaderGraphValueType::Vector3).Data;
            const float strength = std::clamp(
                Coerce(NamedInput(node, "Strength"), Keire::ShaderGraphValueType::Scalar).Data.X, 0.0F, 1.0F);
            const auto baseNormal = Normalize({base.X, base.Y, base.Z}, m_Normal);
            const auto combined = Normalize({base.X + detail.X, base.Y + detail.Y, base.Z + detail.Z}, baseNormal);
            const auto blended =
                Normalize({std::lerp(baseNormal.X, combined.X, strength), std::lerp(baseNormal.Y, combined.Y, strength),
                           std::lerp(baseNormal.Z, combined.Z, strength)},
                          baseNormal);
            result = {.Data = {blended.X, blended.Y, blended.Z, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector3};
            break;
        }
        case Keire::ShaderGraphNodeKind::Parallax:
        {
            const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
            const float height = Coerce(NamedInput(node, "Height"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float scale = Coerce(NamedInput(node, "Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const auto tangent = Normalize(Cross({0.0F, 1.0F, 0.0F}, m_Normal), {1.0F, 0.0F, 0.0F});
            const auto bitangent = Normalize(Cross(m_Normal, tangent), {0.0F, 1.0F, 0.0F});
            constexpr Keire::Vector3 viewDirection{0.0F, 0.0F, 1.0F};
            const Keire::Vector3 tangentView{Dot(viewDirection, tangent), Dot(viewDirection, bitangent),
                                             Dot(viewDirection, m_Normal)};
            const float offset = (height - 0.5F) * scale / std::max(std::abs(tangentView.Z), 0.05F);
            result = {.Data = {uv.X + tangentView.X * offset, uv.Y + tangentView.Y * offset, 0.0F, 0.0F},
                      .Type = Keire::ShaderGraphValueType::Vector2};
            break;
        }
        case Keire::ShaderGraphNodeKind::Fresnel:
        {
            const auto normal = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
            const float power = Coerce(NamedInput(node, "Power"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float reflectance = Coerce(NamedInput(node, "F0"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float facing =
                std::clamp(Dot(Normalize({normal.X, normal.Y, normal.Z}), {0.0F, 0.0F, 1.0F}), 0.0F, 1.0F);
            const float value = std::clamp(
                reflectance + (1.0F - reflectance) * std::pow(1.0F - facing, std::max(std::abs(power), 1.0e-4F)), 0.0F,
                1.0F);
            result = {.Data = {value, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
            break;
        }
        case Keire::ShaderGraphNodeKind::VertexColor:
            result = {.Data = {1.0F, 1.0F, 1.0F, 1.0F}, .Type = Keire::ShaderGraphValueType::Color};
            break;
        case Keire::ShaderGraphNodeKind::WorldPosition:
            result = {.Data = {m_Position.X, m_Position.Y, m_Position.Z, 0.0F},
                      .Type = Keire::ShaderGraphValueType::Vector3};
            break;
        case Keire::ShaderGraphNodeKind::WorldNormal:
            result = {.Data = {m_Normal.X, m_Normal.Y, m_Normal.Z, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector3};
            break;
        case Keire::ShaderGraphNodeKind::ViewDirection:
            result = {.Data = {0.0F, 0.0F, 1.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector3};
            break;
        case Keire::ShaderGraphNodeKind::RotateUV:
        {
            const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
            const auto center = Coerce(NamedInput(node, "Center"), Keire::ShaderGraphValueType::Vector2).Data;
            const float rotation = Coerce(NamedInput(node, "Rotation"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float sine = std::sin(rotation);
            const float cosine = std::cos(rotation);
            const float localX = uv.X - center.X;
            const float localY = uv.Y - center.Y;
            result = {.Data = {center.X + localX * cosine - localY * sine, center.Y + localX * sine + localY * cosine,
                               0.0F, 0.0F},
                      .Type = Keire::ShaderGraphValueType::Vector2};
            break;
        }
        case Keire::ShaderGraphNodeKind::UV1:
        case Keire::ShaderGraphNodeKind::ScreenPosition:
            result = {.Data = {m_Uv.X, m_Uv.Y, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector2};
            break;
        case Keire::ShaderGraphNodeKind::WorldTangent:
        {
            const auto tangent = Normalize(Cross({0.0F, 1.0F, 0.0F}, m_Normal), {1.0F, 0.0F, 0.0F});
            result = {.Data = {tangent.X, tangent.Y, tangent.Z, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector3};
            break;
        }
        case Keire::ShaderGraphNodeKind::CameraPosition:
            result = {.Data = {0.0F, 0.0F, 3.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector3};
            break;
        case Keire::ShaderGraphNodeKind::ObjectPosition:
            result = {.Data = {0.0F, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector3};
            break;
        case Keire::ShaderGraphNodeKind::Time:
        case Keire::ShaderGraphNodeKind::DeltaTime:
            result = {.Type = Keire::ShaderGraphValueType::Scalar};
            break;
        case Keire::ShaderGraphNodeKind::DepthFade:
        {
            const float distance = Coerce(NamedInput(node, "Distance"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float fadeDistance = std::max(
                std::abs(Coerce(NamedInput(node, "Fade Distance"), Keire::ShaderGraphValueType::Scalar).Data.X),
                1.0e-4F);
            result = {.Data = {std::clamp(distance / fadeDistance, 0.0F, 1.0F), 0.0F, 0.0F, 0.0F},
                      .Type = Keire::ShaderGraphValueType::Scalar};
            break;
        }
        case Keire::ShaderGraphNodeKind::Panner:
        {
            const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
            const auto speed = Coerce(NamedInput(node, "Speed"), Keire::ShaderGraphValueType::Vector2).Data;
            const float time = Coerce(NamedInput(node, "Time"), Keire::ShaderGraphValueType::Scalar).Data.X;
            result = {.Data = {uv.X + speed.X * time, uv.Y + speed.Y * time, 0.0F, 0.0F},
                      .Type = Keire::ShaderGraphValueType::Vector2};
            break;
        }
        case Keire::ShaderGraphNodeKind::PolarCoordinates:
        {
            const auto uv = Coerce(NamedInput(node, "UV"), Keire::ShaderGraphValueType::Vector2).Data;
            const auto center = Coerce(NamedInput(node, "Center"), Keire::ShaderGraphValueType::Vector2).Data;
            const float radialScale =
                Coerce(NamedInput(node, "Radial Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float lengthScale =
                Coerce(NamedInput(node, "Length Scale"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float x = uv.X - center.X;
            const float y = uv.Y - center.Y;
            result = {.Data = {std::sqrt(x * x + y * y) * radialScale,
                               Fraction(std::atan2(y, x) / 6.28318530718F + 0.5F) * lengthScale, 0.0F, 0.0F},
                      .Type = Keire::ShaderGraphValueType::Vector2};
            break;
        }
        case Keire::ShaderGraphNodeKind::ReflectionVector:
        {
            const auto normalValue = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
            const auto normal = Normalize({normalValue.X, normalValue.Y, normalValue.Z}, m_Normal);
            constexpr Keire::Vector3 incident{0.0F, 0.0F, -1.0F};
            const float projection = 2.0F * Dot(incident, normal);
            result = {.Data = {incident.X - projection * normal.X, incident.Y - projection * normal.Y,
                               incident.Z - projection * normal.Z, 0.0F},
                      .Type = Keire::ShaderGraphValueType::Vector3};
            break;
        }
        case Keire::ShaderGraphNodeKind::FacingRatio:
        {
            const auto normalValue = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
            const auto normal = Normalize({normalValue.X, normalValue.Y, normalValue.Z}, m_Normal);
            const float power = std::max(
                std::abs(Coerce(NamedInput(node, "Power"), Keire::ShaderGraphValueType::Scalar).Data.X), 1.0e-4F);
            const float ratio = std::pow(std::clamp(1.0F - normal.Z, 0.0F, 1.0F), power);
            result = {.Data = {ratio, 0.0F, 0.0F, 0.0F}, .Type = Keire::ShaderGraphValueType::Scalar};
            break;
        }
        case Keire::ShaderGraphNodeKind::HeightToNormal:
        {
            const float height = Coerce(NamedInput(node, "Height"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const float strength = Coerce(NamedInput(node, "Strength"), Keire::ShaderGraphValueType::Scalar).Data.X;
            const auto normal = Normalize({-(height - 0.5F) * strength, -(height - 0.5F) * strength, 1.0F}, m_Normal);
            result = {.Data = {normal.X, normal.Y, normal.Z, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector3};
            break;
        }
        case Keire::ShaderGraphNodeKind::FlattenNormal:
        {
            const auto normalValue = Coerce(NamedInput(node, "Normal"), Keire::ShaderGraphValueType::Vector3).Data;
            const float strength = std::clamp(
                Coerce(NamedInput(node, "Strength"), Keire::ShaderGraphValueType::Scalar).Data.X, 0.0F, 1.0F);
            const auto normal = Normalize(
                {normalValue.X * strength, normalValue.Y * strength, std::lerp(1.0F, normalValue.Z, strength)},
                m_Normal);
            result = {.Data = {normal.X, normal.Y, normal.Z, 0.0F}, .Type = Keire::ShaderGraphValueType::Vector3};
            break;
        }
        default:
            throw std::logic_error("Preview node dispatched to an incompatible geometry family.");
        }
        return result;
    }
} // namespace KeireEditor::ShaderGraphPreviewInternal
