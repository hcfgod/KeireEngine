#include "Keire/Animation/Skinning.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace Keire
{
    namespace
    {
        constexpr float Epsilon = 0.000001F;

        [[nodiscard]] Vector3 Add(const Vector3 left, const Vector3 right) noexcept
        {
            return {left.X + right.X, left.Y + right.Y, left.Z + right.Z};
        }

        [[nodiscard]] Vector3 Multiply(const Vector3 value, const float scalar) noexcept
        {
            return {value.X * scalar, value.Y * scalar, value.Z * scalar};
        }

        [[nodiscard]] float Dot(const Vector3 left, const Vector3 right) noexcept
        {
            return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
        }

        [[nodiscard]] Vector3 Normalize(const Vector3 value, const Vector3 fallback) noexcept
        {
            const auto lengthSquared = Dot(value, value);
            if (lengthSquared <= Epsilon)
                return fallback;
            return Multiply(value, 1.0F / std::sqrt(lengthSquared));
        }

        [[nodiscard]] float Dot(const Quaternion left, const Quaternion right) noexcept
        {
            return left.X * right.X + left.Y * right.Y + left.Z * right.Z + left.W * right.W;
        }

        [[nodiscard]] Quaternion Add(const Quaternion left, const Quaternion right) noexcept
        {
            return {left.X + right.X, left.Y + right.Y, left.Z + right.Z, left.W + right.W};
        }

        [[nodiscard]] Quaternion Multiply(const Quaternion value, const float scalar) noexcept
        {
            return {value.X * scalar, value.Y * scalar, value.Z * scalar, value.W * scalar};
        }

        [[nodiscard]] Quaternion Multiply(const Quaternion left, const Quaternion right) noexcept
        {
            return {left.W * right.X + left.X * right.W + left.Y * right.Z - left.Z * right.Y,
                    left.W * right.Y - left.X * right.Z + left.Y * right.W + left.Z * right.X,
                    left.W * right.Z + left.X * right.Y - left.Y * right.X + left.Z * right.W,
                    left.W * right.W - left.X * right.X - left.Y * right.Y - left.Z * right.Z};
        }

        [[nodiscard]] Quaternion Conjugate(const Quaternion value) noexcept
        {
            return {-value.X, -value.Y, -value.Z, value.W};
        }

        [[nodiscard]] Vector3 Rotate(const Quaternion rotation, const Vector3 value) noexcept
        {
            const auto rotated =
                Multiply(Multiply(rotation, Quaternion{value.X, value.Y, value.Z, 0.0F}), Conjugate(rotation));
            return {rotated.X, rotated.Y, rotated.Z};
        }

        struct DualQuaternion
        {
            Quaternion Real;
            Quaternion Dual;
        };

        [[nodiscard]] DualQuaternion ToDualQuaternion(const Matrix4& matrix)
        {
            Vector3 translation;
            Quaternion rotation;
            Vector3 scale;
            if (!Math::DecomposeTransform(matrix, translation, rotation, scale))
                return {};
            rotation = Math::Normalize(rotation);
            const auto dual =
                Multiply(Multiply(Quaternion{translation.X, translation.Y, translation.Z, 0.0F}, rotation), 0.5F);
            return {rotation, dual};
        }

        [[nodiscard]] bool ValidInfluence(const SkinVertexInfluence8& influence, const std::size_t index,
                                          const std::size_t paletteSize) noexcept
        {
            return index < influence.Count && influence.Weights[index] > 0.0F && influence.Bones[index] < paletteSize &&
                   std::isfinite(influence.Weights[index]);
        }

        void SkinLinearBlend(const MeshVertex& source, const SkinVertexInfluence8& influence,
                             const std::span<const Matrix4> palette, MeshVertex& destination)
        {
            Vector3 position;
            Vector3 normal;
            Vector3 tangent;
            float totalWeight = 0.0F;
            for (std::size_t index = 0; index < influence.Count; ++index)
            {
                if (!ValidInfluence(influence, index, palette.size()))
                    continue;
                const auto weight = influence.Weights[index];
                const auto& matrix = palette[influence.Bones[index]];
                position = Add(position, Multiply(Math::TransformPoint(matrix, source.Position), weight));
                normal = Add(normal, Multiply(Math::TransformDirection(matrix, source.Normal), weight));
                tangent = Add(tangent, Multiply(Math::TransformDirection(
                                                    matrix, {source.Tangent.X, source.Tangent.Y, source.Tangent.Z}),
                                                weight));
                totalWeight += weight;
            }
            if (totalWeight <= Epsilon)
            {
                destination = source;
                return;
            }
            const auto inverseWeight = 1.0F / totalWeight;
            destination = source;
            destination.Position = Multiply(position, inverseWeight);
            destination.Normal = Normalize(Multiply(normal, inverseWeight), source.Normal);
            const auto normalizedTangent =
                Normalize(Multiply(tangent, inverseWeight), {source.Tangent.X, source.Tangent.Y, source.Tangent.Z});
            destination.Tangent = {normalizedTangent.X, normalizedTangent.Y, normalizedTangent.Z, source.Tangent.W};
        }

        void SkinDualQuaternion(const MeshVertex& source, const SkinVertexInfluence8& influence,
                                const std::span<const Matrix4> palette, MeshVertex& destination)
        {
            Quaternion blendedReal{0.0F, 0.0F, 0.0F, 0.0F};
            Quaternion blendedDual{0.0F, 0.0F, 0.0F, 0.0F};
            Quaternion reference;
            bool hasReference = false;
            float totalWeight = 0.0F;
            for (std::size_t index = 0; index < influence.Count; ++index)
            {
                if (!ValidInfluence(influence, index, palette.size()))
                    continue;
                auto dualQuaternion = ToDualQuaternion(palette[influence.Bones[index]]);
                if (!hasReference)
                {
                    reference = dualQuaternion.Real;
                    hasReference = true;
                }
                const auto sign = Dot(reference, dualQuaternion.Real) < 0.0F ? -1.0F : 1.0F;
                const auto weight = influence.Weights[index] * sign;
                blendedReal = Add(blendedReal, Multiply(dualQuaternion.Real, weight));
                blendedDual = Add(blendedDual, Multiply(dualQuaternion.Dual, weight));
                totalWeight += influence.Weights[index];
            }
            const auto lengthSquared = Dot(blendedReal, blendedReal);
            if (!hasReference || totalWeight <= Epsilon || lengthSquared <= Epsilon)
            {
                destination = source;
                return;
            }
            const auto inverseLength = 1.0F / std::sqrt(lengthSquared);
            blendedReal = Multiply(blendedReal, inverseLength);
            blendedDual = Multiply(blendedDual, inverseLength);
            blendedDual = Add(blendedDual, Multiply(blendedReal, -Dot(blendedReal, blendedDual)));
            const auto translationQuaternion = Multiply(Multiply(blendedDual, Conjugate(blendedReal)), 2.0F);
            const Vector3 translation{translationQuaternion.X, translationQuaternion.Y, translationQuaternion.Z};

            destination = source;
            destination.Position = Add(Rotate(blendedReal, source.Position), translation);
            destination.Normal = Normalize(Rotate(blendedReal, source.Normal), source.Normal);
            const auto tangent = Normalize(Rotate(blendedReal, {source.Tangent.X, source.Tangent.Y, source.Tangent.Z}),
                                           {source.Tangent.X, source.Tangent.Y, source.Tangent.Z});
            destination.Tangent = {tangent.X, tangent.Y, tangent.Z, source.Tangent.W};
        }
    } // namespace

    std::vector<SkinVertexInfluence8> ExpandSkinInfluences(const std::span<const SkinVertexInfluence> influences)
    {
        std::vector<SkinVertexInfluence8> result;
        result.reserve(influences.size());
        for (const auto& influence : influences)
        {
            SkinVertexInfluence8 expanded;
            expanded.Count = 4;
            std::ranges::copy(influence.Bones, expanded.Bones.begin());
            std::ranges::copy(influence.Weights, expanded.Weights.begin());
            result.push_back(expanded);
        }
        return result;
    }

    void SkinMeshCpu(const std::span<const MeshVertex> source, const std::span<const SkinVertexInfluence8> influences,
                     const std::span<const Matrix4> palette, const SkinningMethod method,
                     const std::span<MeshVertex> destination)
    {
        if (source.size() != influences.size() || source.size() != destination.size())
            throw std::invalid_argument("Skinning requires one influence and destination per source vertex.");
        if (palette.empty() && !source.empty())
            throw std::invalid_argument("Skinning requires a non-empty matrix palette.");

        for (std::size_t index = 0; index < source.size(); ++index)
        {
            if (influences[index].Count > influences[index].Bones.size())
                throw std::invalid_argument("A skin influence count exceeds the supported maximum.");
            if (method == SkinningMethod::DualQuaternion)
                SkinDualQuaternion(source[index], influences[index], palette, destination[index]);
            else
                SkinLinearBlend(source[index], influences[index], palette, destination[index]);
        }
    }

    std::vector<MeshVertex> SkinMeshCpu(const std::span<const MeshVertex> source,
                                        const std::span<const SkinVertexInfluence> influences,
                                        const std::span<const Matrix4> palette, const SkinningMethod method)
    {
        auto expanded = ExpandSkinInfluences(influences);
        std::vector<MeshVertex> result(source.size());
        SkinMeshCpu(source, expanded, palette, method, result);
        return result;
    }
} // namespace Keire
