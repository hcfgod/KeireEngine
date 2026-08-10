#include "KeireInternal/Vfx/VfxExpressionInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace Keire::Internal
{
    namespace
    {
        template <typename T>
        [[nodiscard]] const T* Input(const std::span<const VfxParameterValue* const> inputs,
                                     const std::size_t index) noexcept
        {
            if (index >= inputs.size() || !inputs[index])
                return nullptr;
            return std::get_if<T>(inputs[index]);
        }

        [[nodiscard]] Vector3 Subtract(const Vector3 left, const Vector3 right) noexcept
        {
            return {left.X - right.X, left.Y - right.Y, left.Z - right.Z};
        }

        [[nodiscard]] Vector3 Add(const Vector3 left, const Vector3 right) noexcept
        {
            return {left.X + right.X, left.Y + right.Y, left.Z + right.Z};
        }

        [[nodiscard]] Vector3 Scale(const Vector3 value, const float scale) noexcept
        {
            return {value.X * scale, value.Y * scale, value.Z * scale};
        }

        [[nodiscard]] float Dot(const Vector3 left, const Vector3 right) noexcept
        {
            return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
        }

        [[nodiscard]] Vector3 Cross(const Vector3 left, const Vector3 right) noexcept
        {
            return {left.Y * right.Z - left.Z * right.Y, left.Z * right.X - left.X * right.Z,
                    left.X * right.Y - left.Y * right.X};
        }

        [[nodiscard]] float Length(const Vector3 value) noexcept
        {
            return std::sqrt(std::max(0.0F, Dot(value, value)));
        }

        [[nodiscard]] Vector3 Normalize(const Vector3 value) noexcept
        {
            const auto length = Length(value);
            return length > 0.000001F ? Scale(value, 1.0F / length) : Vector3{};
        }

        [[nodiscard]] Matrix4 Transpose(const Matrix4& value) noexcept
        {
            Matrix4 result;
            for (std::size_t row = 0; row < 4; ++row)
                for (std::size_t column = 0; column < 4; ++column)
                    result.Elements[row * 4 + column] = value.Elements[column * 4 + row];
            return result;
        }

        [[nodiscard]] Vector4 Transform(const Matrix4& matrix, const Vector4 value) noexcept
        {
            const auto& m = matrix.Elements;
            return {m[0] * value.X + m[4] * value.Y + m[8] * value.Z + m[12] * value.W,
                    m[1] * value.X + m[5] * value.Y + m[9] * value.Z + m[13] * value.W,
                    m[2] * value.X + m[6] * value.Y + m[10] * value.Z + m[14] * value.W,
                    m[3] * value.X + m[7] * value.Y + m[11] * value.Z + m[15] * value.W};
        }

        [[nodiscard]] std::optional<VfxParameterValue> ValueLane(const Vector4 value, const VfxValueType type) noexcept
        {
            switch (type)
            {
            case VfxValueType::Scalar:
                return value.X;
            case VfxValueType::Vector2:
                return Vector2{value.X, value.Y};
            case VfxValueType::Vector3:
                return Vector3{value.X, value.Y, value.Z};
            case VfxValueType::Vector4:
                return value;
            case VfxValueType::Color:
                return Color{value.X, value.Y, value.Z, value.W};
            default:
                return std::nullopt;
            }
        }

        [[nodiscard]] std::optional<VfxResourceQueryResult>
        QueryResource(const VfxValueOpcode opcode, const std::span<const VfxParameterValue* const> inputs,
                      const VfxExpressionEvaluationContext* context) noexcept
        {
            if (!context || !context->ResourceQuery || !*context->ResourceQuery)
                return std::nullopt;
            const auto* resource = Input<AssetId>(inputs, 0);
            if (!resource || !*resource)
                return std::nullopt;

            VfxResourceQuery query;
            query.Resource = *resource;
            switch (opcode)
            {
            case VfxValueOpcode::ResourceAttributeMap:
                query.Kind = VfxResourceQueryKind::AttributeMap;
                break;
            case VfxValueOpcode::ResourceBufferCount:
                query.Kind = VfxResourceQueryKind::BufferCount;
                break;
            case VfxValueOpcode::ResourceMeshIndexCount:
                query.Kind = VfxResourceQueryKind::MeshIndexCount;
                break;
            case VfxValueOpcode::ResourceMeshTriangleCount:
                query.Kind = VfxResourceQueryKind::MeshTriangleCount;
                break;
            case VfxValueOpcode::ResourceMeshVertexCount:
                query.Kind = VfxResourceQueryKind::MeshVertexCount;
                break;
            case VfxValueOpcode::ResourceTextureDimensions:
                query.Kind = VfxResourceQueryKind::TextureDimensions;
                break;
            case VfxValueOpcode::ResourceLoadTexture2D:
                query.Kind = VfxResourceQueryKind::LoadTexture2D;
                break;
            case VfxValueOpcode::ResourceLoadTexture2DArray:
                query.Kind = VfxResourceQueryKind::LoadTexture2DArray;
                break;
            case VfxValueOpcode::ResourceLoadTexture3D:
                query.Kind = VfxResourceQueryKind::LoadTexture3D;
                break;
            case VfxValueOpcode::ResourceSampleBuffer:
                query.Kind = VfxResourceQueryKind::SampleBuffer;
                break;
            case VfxValueOpcode::ResourceSampleMesh:
                query.Kind = VfxResourceQueryKind::SampleMesh;
                break;
            case VfxValueOpcode::ResourceSampleMeshIndex:
                query.Kind = VfxResourceQueryKind::SampleMeshIndex;
                break;
            case VfxValueOpcode::ResourceSampleTexture2D:
                query.Kind = VfxResourceQueryKind::SampleTexture2D;
                break;
            case VfxValueOpcode::ResourceSampleTexture2DArray:
                query.Kind = VfxResourceQueryKind::SampleTexture2DArray;
                break;
            case VfxValueOpcode::ResourceSampleTexture3D:
                query.Kind = VfxResourceQueryKind::SampleTexture3D;
                break;
            case VfxValueOpcode::ResourceSampleTextureCube:
                query.Kind = VfxResourceQueryKind::SampleTextureCube;
                break;
            case VfxValueOpcode::ResourceSampleTextureCubeArray:
                query.Kind = VfxResourceQueryKind::SampleTextureCubeArray;
                break;
            case VfxValueOpcode::ResourceMeshLocalTransform:
                query.Kind = VfxResourceQueryKind::MeshLocalTransform;
                break;
            case VfxValueOpcode::ResourceMeshWorldTransform:
                query.Kind = VfxResourceQueryKind::MeshWorldTransform;
                break;
            case VfxValueOpcode::ResourceSampleSignedDistanceField:
                query.Kind = VfxResourceQueryKind::SampleSignedDistanceField;
                break;
            default:
                return std::nullopt;
            }

            if (const auto* coordinate = Input<Vector4>(inputs, 1))
                query.Coordinate = *coordinate;
            else if (const auto* coordinate3 = Input<Vector3>(inputs, 1))
                query.Coordinate = {coordinate3->X, coordinate3->Y, coordinate3->Z, 0.0F};
            else if (const auto* coordinate2 = Input<Vector2>(inputs, 1))
                query.Coordinate = {coordinate2->X, coordinate2->Y, 0.0F, 0.0F};
            else if (const auto* index = Input<std::uint64_t>(inputs, 1))
                query.Index = *index;
            if (const auto* level = Input<float>(inputs, 2))
                query.Level = *level;
            try
            {
                return (*context->ResourceQuery)(query);
            }
            catch (...)
            {
                return std::nullopt;
            }
        }
    } // namespace

    std::optional<VfxParameterValue>
    EvaluateVfxExtendedExpression(const VfxValueOpcode opcode, const std::span<const VfxParameterValue* const> inputs,
                                  const VfxValueType outputType, const std::uint32_t outputIndex,
                                  const VfxExpressionEvaluationContext* context) noexcept
    {
        try
        {
            if (opcode == VfxValueOpcode::Passthrough)
            {
                if (outputIndex >= inputs.size() || !inputs[outputIndex] ||
                    !VfxValueMatchesType(outputType, *inputs[outputIndex]))
                {
                    return std::nullopt;
                }
                return *inputs[outputIndex];
            }
            if (opcode >= VfxValueOpcode::AttributeAngularVelocity && opcode <= VfxValueOpcode::AttributeTextureIndex)
            {
                if (!context || !inputs.empty())
                    return std::nullopt;
                switch (opcode)
                {
                case VfxValueOpcode::AttributeAngularVelocity:
                    return Vector3{};
                case VfxValueOpcode::AttributeDirection:
                    return Normalize(context->Velocity);
                case VfxValueOpcode::AttributeMass:
                    return 1.0F;
                case VfxValueOpcode::AttributePivot:
                    return Vector3{};
                case VfxValueOpcode::AttributeScale:
                    return Vector3{context->Size, context->Size, context->Size};
                case VfxValueOpcode::AttributeTargetPosition:
                    return Add(context->Position, context->Velocity);
                case VfxValueOpcode::AttributeTextureIndex:
                    return static_cast<std::uint64_t>(context->ParticleIndexInStrip);
                default:
                    return std::nullopt;
                }
            }
            if (opcode == VfxValueOpcode::WeightedSelect)
            {
                const auto* first = Input<Vector3>(inputs, 0);
                const auto* second = Input<Vector3>(inputs, 1);
                const auto* firstWeight = Input<float>(inputs, 2);
                const auto* secondWeight = Input<float>(inputs, 3);
                const auto* selector = Input<float>(inputs, 4);
                if (!first || !second || !firstWeight || !secondWeight || !selector)
                    return std::nullopt;
                const auto total = std::max(0.0F, *firstWeight) + std::max(0.0F, *secondWeight);
                return total <= 0.0F || std::clamp(*selector, 0.0F, 1.0F) * total < std::max(0.0F, *firstWeight)
                           ? *first
                           : *second;
            }
            if (opcode == VfxValueOpcode::LookAtDirection)
            {
                const auto* origin = Input<Vector3>(inputs, 0);
                const auto* target = Input<Vector3>(inputs, 1);
                return origin && target ? std::optional<VfxParameterValue>(Normalize(Subtract(*target, *origin)))
                                        : std::nullopt;
            }
            if (opcode == VfxValueOpcode::SampleBezier)
            {
                const auto* p0 = Input<Vector3>(inputs, 0);
                const auto* p1 = Input<Vector3>(inputs, 1);
                const auto* p2 = Input<Vector3>(inputs, 2);
                const auto* p3 = Input<Vector3>(inputs, 3);
                const auto* time = Input<float>(inputs, 4);
                if (!p0 || !p1 || !p2 || !p3 || !time)
                    return std::nullopt;
                const auto t = std::clamp(*time, 0.0F, 1.0F);
                const auto inverse = 1.0F - t;
                return Add(Add(Scale(*p0, inverse * inverse * inverse), Scale(*p1, 3.0F * inverse * inverse * t)),
                           Add(Scale(*p2, 3.0F * inverse * t * t), Scale(*p3, t * t * t)));
            }
            if (opcode == VfxValueOpcode::Swizzle)
            {
                const auto* value = Input<Vector4>(inputs, 0);
                if (!value || inputs.size() != 5)
                    return std::nullopt;
                const std::array components{value->X, value->Y, value->Z, value->W};
                Vector4 result;
                std::array<float*, 4> destinations{&result.X, &result.Y, &result.Z, &result.W};
                for (std::size_t index = 0; index < destinations.size(); ++index)
                {
                    const auto* channel = Input<std::uint64_t>(inputs, index + 1);
                    if (!channel || *channel >= components.size())
                        return std::nullopt;
                    *destinations[index] = components[static_cast<std::size_t>(*channel)];
                }
                return result;
            }
            if (opcode == VfxValueOpcode::AreaCircle)
            {
                const auto* radius = Input<float>(inputs, 0);
                return radius ? std::optional<VfxParameterValue>(std::numbers::pi_v<float> * *radius * *radius)
                              : std::nullopt;
            }
            if (opcode == VfxValueOpcode::DistanceLine)
            {
                const auto* point = Input<Vector3>(inputs, 0);
                const auto* start = Input<Vector3>(inputs, 1);
                const auto* end = Input<Vector3>(inputs, 2);
                if (!point || !start || !end)
                    return std::nullopt;
                const auto segment = Subtract(*end, *start);
                const auto lengthSquared = Dot(segment, segment);
                const auto factor =
                    lengthSquared <= 0.000001F
                        ? 0.0F
                        : std::clamp(Dot(Subtract(*point, *start), segment) / lengthSquared, 0.0F, 1.0F);
                return Length(Subtract(*point, Add(*start, Scale(segment, factor))));
            }
            if (opcode == VfxValueOpcode::DistancePlane)
            {
                const auto* point = Input<Vector3>(inputs, 0);
                const auto* planePoint = Input<Vector3>(inputs, 1);
                const auto* normal = Input<Vector3>(inputs, 2);
                return point && planePoint && normal ? std::optional<VfxParameterValue>(std::abs(
                                                           Dot(Subtract(*point, *planePoint), Normalize(*normal))))
                                                     : std::nullopt;
            }
            if (opcode == VfxValueOpcode::DistanceSphere)
            {
                const auto* point = Input<Vector3>(inputs, 0);
                const auto* center = Input<Vector3>(inputs, 1);
                const auto* radius = Input<float>(inputs, 2);
                return point && center && radius
                           ? std::optional<VfxParameterValue>(std::abs(Length(Subtract(*point, *center)) - *radius))
                           : std::nullopt;
            }
            if (opcode >= VfxValueOpcode::VolumeAxisAlignedBox && opcode <= VfxValueOpcode::VolumeTorus)
            {
                if (opcode == VfxValueOpcode::VolumeAxisAlignedBox || opcode == VfxValueOpcode::VolumeOrientedBox)
                {
                    const auto* size = Input<Vector3>(inputs, 0);
                    return size ? std::optional<VfxParameterValue>(std::abs(size->X * size->Y * size->Z))
                                : std::nullopt;
                }
                const auto* radius = Input<float>(inputs, 0);
                if (!radius)
                    return std::nullopt;
                if (opcode == VfxValueOpcode::VolumeSphere)
                    return 4.0F * std::numbers::pi_v<float> * *radius * *radius * *radius / 3.0F;
                const auto* height = Input<float>(inputs, 1);
                if (!height)
                    return std::nullopt;
                if (opcode == VfxValueOpcode::VolumeCone)
                    return std::numbers::pi_v<float> * *radius * *radius * *height / 3.0F;
                if (opcode == VfxValueOpcode::VolumeCylinder)
                    return std::numbers::pi_v<float> * *radius * *radius * *height;
                return 2.0F * std::numbers::pi_v<float> * std::numbers::pi_v<float> * *radius * *radius * *height;
            }
            if (opcode == VfxValueOpcode::SpawnState)
            {
                if (!context || !inputs.empty())
                    return std::nullopt;
                if (outputIndex == 0 && outputType == VfxValueType::Boolean)
                    return true;
                if (outputIndex == 1 && outputType == VfxValueType::Scalar)
                    return context->EffectTime;
                if (outputIndex == 2 && outputType == VfxValueType::UnsignedInteger)
                    return context->SpawnIndex;
                return std::nullopt;
            }
            if (opcode == VfxValueOpcode::LocalToWorld || opcode == VfxValueOpcode::WorldToLocal)
            {
                if (!context || !inputs.empty())
                    return std::nullopt;
                const auto matrix = Math::ComposeTransform(context->EmitterPosition, context->EmitterRotation,
                                                           Vector3{1.0F, 1.0F, 1.0F});
                return opcode == VfxValueOpcode::LocalToWorld ? matrix : Math::Inverse(matrix);
            }
            if (opcode == VfxValueOpcode::ConstructMatrix)
            {
                if (inputs.size() != 4)
                    return std::nullopt;
                Matrix4 result;
                for (std::size_t row = 0; row < 4; ++row)
                {
                    const auto* value = Input<Vector4>(inputs, row);
                    if (!value)
                        return std::nullopt;
                    result.Elements[row * 4] = value->X;
                    result.Elements[row * 4 + 1] = value->Y;
                    result.Elements[row * 4 + 2] = value->Z;
                    result.Elements[row * 4 + 3] = value->W;
                }
                return result;
            }
            if (opcode == VfxValueOpcode::LookAt)
            {
                const auto* eye = Input<Vector3>(inputs, 0);
                const auto* target = Input<Vector3>(inputs, 1);
                const auto* up = Input<Vector3>(inputs, 2);
                return eye && target && up ? std::optional<VfxParameterValue>(Math::LookAt(*eye, *target, *up))
                                           : std::nullopt;
            }
            if (opcode == VfxValueOpcode::ChangeSpace)
            {
                const auto* value = Input<Vector3>(inputs, 0);
                const auto* from = Input<Matrix4>(inputs, 1);
                const auto* to = Input<Matrix4>(inputs, 2);
                return value && from && to ? std::optional<VfxParameterValue>(Math::TransformPoint(
                                                 Math::Multiply(Math::Inverse(*to), *from), *value))
                                           : std::nullopt;
            }
            if (opcode == VfxValueOpcode::InvertTrs || opcode == VfxValueOpcode::TransposeMatrix)
            {
                const auto* matrix = Input<Matrix4>(inputs, 0);
                if (!matrix)
                    return std::nullopt;
                return opcode == VfxValueOpcode::InvertTrs ? Math::Inverse(*matrix) : Transpose(*matrix);
            }
            if (opcode == VfxValueOpcode::TransformMatrix)
            {
                const auto* left = Input<Matrix4>(inputs, 0);
                const auto* right = Input<Matrix4>(inputs, 1);
                return left && right ? std::optional<VfxParameterValue>(Math::Multiply(*left, *right)) : std::nullopt;
            }
            if (opcode == VfxValueOpcode::TransformDirection || opcode == VfxValueOpcode::TransformPosition ||
                opcode == VfxValueOpcode::TransformVector)
            {
                const auto* matrix = Input<Matrix4>(inputs, 0);
                const auto* value = Input<Vector3>(inputs, 1);
                if (!matrix || !value)
                    return std::nullopt;
                return opcode == VfxValueOpcode::TransformPosition ? Math::TransformPoint(*matrix, *value)
                                                                   : Math::TransformDirection(*matrix, *value);
            }
            if (opcode == VfxValueOpcode::TransformVector4)
            {
                const auto* matrix = Input<Matrix4>(inputs, 0);
                const auto* value = Input<Vector4>(inputs, 1);
                return matrix && value ? std::optional<VfxParameterValue>(Transform(*matrix, *value)) : std::nullopt;
            }
            if (opcode == VfxValueOpcode::SampleCurve)
            {
                const auto* curve = Input<Curve1D>(inputs, 0);
                const auto* time = Input<float>(inputs, 1);
                return curve && time ? std::optional<VfxParameterValue>(curve->Evaluate(*time)) : std::nullopt;
            }
            if (opcode == VfxValueOpcode::SampleGradient)
            {
                const auto* gradient = Input<ColorGradient>(inputs, 0);
                const auto* time = Input<float>(inputs, 1);
                return gradient && time ? std::optional<VfxParameterValue>(gradient->Evaluate(*time)) : std::nullopt;
            }

            const auto resource = QueryResource(opcode, inputs, context);
            if (!resource)
                return std::nullopt;
            if (opcode >= VfxValueOpcode::ResourceBufferCount && opcode <= VfxValueOpcode::ResourceMeshVertexCount)
                return resource->Count;
            if (opcode == VfxValueOpcode::ResourceTextureDimensions)
                return resource->Dimensions;
            if (opcode == VfxValueOpcode::ResourceSampleMeshIndex)
                return resource->Count;
            if (opcode == VfxValueOpcode::ResourceMeshLocalTransform ||
                opcode == VfxValueOpcode::ResourceMeshWorldTransform)
            {
                return resource->Transform;
            }
            if (opcode == VfxValueOpcode::ResourceSampleMesh)
            {
                return outputIndex < resource->Values.size() ? ValueLane(resource->Values[outputIndex], outputType)
                                                             : std::nullopt;
            }
            return ValueLane(resource->Values[0], outputType);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }
} // namespace Keire::Internal
