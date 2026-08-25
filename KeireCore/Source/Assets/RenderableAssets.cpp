#include "Keire/Animation/AnimationSystem.h"
#include "Keire/Animation/RiggingSystem.h"
#include "Keire/Assets/RenderingAssets.h"

#include "KeireInternal/Assets/AssimpProjectIO.h"
#include "KeireInternal/Assets/BuiltinMeshes.h"
#include "KeireInternal/Assets/ImportedMaterialShader.h"
#include "KeireInternal/Assets/TextureImportBackend.h"
#include "KeireInternal/Assets/TextureImportSettingsInternal.h"

#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/matrix3x3.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Keire
{
    namespace
    {
        constexpr std::array<char, 8> MeshMagic{'K', 'E', 'I', 'R', 'E', 'M', 'S', 'H'};
        constexpr std::array<char, 8> TextureMagic{'K', 'E', 'I', 'R', 'E', 'T', 'E', 'X'};
        constexpr std::uint32_t MeshVersion = 5;
        constexpr std::uint32_t TextureVersion = 3;
        constexpr std::size_t MaximumMeshVertices = std::size_t{16} * 1024U * 1024U;
        constexpr std::size_t MaximumMeshIndices = std::size_t{48} * 1024U * 1024U;
        constexpr std::size_t MaximumMeshSubmeshes = std::size_t{1024} * 1024U;
        constexpr std::size_t MaximumMeshMaterialSlots = std::size_t{16} * 1024U;
        constexpr std::size_t MaximumMeshLods = 16U;
        constexpr std::size_t MaximumTextureBytes = std::size_t{1024} * 1024U * 1024U;

        enum class ImportedAnimationMotion : std::uint8_t
        {
            RootMotion,
            Authored,
            InPlaceHorizontal,
            InPlace
        };

        [[nodiscard]] ImportedAnimationMotion ParseImportedAnimationMotion(const std::string_view value)
        {
            if (value == "rootMotion")
                return ImportedAnimationMotion::RootMotion;
            if (value == "authored")
                return ImportedAnimationMotion::Authored;
            if (value == "inPlaceHorizontal")
                return ImportedAnimationMotion::InPlaceHorizontal;
            if (value == "inPlace")
                return ImportedAnimationMotion::InPlace;
            throw std::invalid_argument(
                "Animation motion must be rootMotion, authored, inPlaceHorizontal, or inPlace.");
        }

        [[nodiscard]] bool BakeImportedAnimationInPlace(std::vector<AnimationTrack>& tracks, const RigDefinition& rig,
                                                        const bool lockVertical)
        {
            auto motionBone = std::ranges::find(rig.Bones, RigBoneSemantic::Pelvis, &RigBoneDefinition::Semantic);
            if (motionBone == rig.Bones.end())
                motionBone = std::ranges::find(rig.Bones, RigBoneSemantic::Root, &RigBoneDefinition::Semantic);
            if (motionBone == rig.Bones.end())
                return false;
            const auto motionBoneIndex = static_cast<std::uint32_t>(std::distance(rig.Bones.begin(), motionBone));
            const auto motionTrack = std::ranges::find(tracks, motionBoneIndex, &AnimationTrack::Bone);
            if (motionTrack == tracks.end() || motionTrack->Keys.empty())
                return false;

            const auto reference = motionTrack->Keys.front().Value.Translation;
            for (auto& key : motionTrack->Keys)
            {
                key.Value.Translation.X = reference.X;
                key.Value.Translation.Z = reference.Z;
                if (lockVertical)
                    key.Value.Translation.Y = reference.Y;
            }
            return true;
        }

        [[nodiscard]] std::string Lowercase(std::string value)
        {
            std::ranges::transform(value, value.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return value;
        }

        [[nodiscard]] Matrix4 ConvertMatrix(const aiMatrix4x4& value) noexcept
        {
            return {{value.a1, value.b1, value.c1, value.d1, value.a2, value.b2, value.c2, value.d2, value.a3, value.b3,
                     value.c3, value.d3, value.a4, value.b4, value.c4, value.d4}};
        }

        [[nodiscard]] BoneTransform ConvertTransform(const aiMatrix4x4& value)
        {
            aiVector3D scale;
            aiVector3D translation;
            aiQuaternion rotation;
            value.Decompose(scale, rotation, translation);
            return {{translation.x, translation.y, translation.z},
                    Math::Normalize({rotation.x, rotation.y, rotation.z, rotation.w}),
                    {scale.x, scale.y, scale.z}};
        }

        [[nodiscard]] bool ApproximatelyEqual(const aiMatrix4x4& left, const aiMatrix4x4& right) noexcept
        {
            const auto leftMatrix = ConvertMatrix(left);
            const auto rightMatrix = ConvertMatrix(right);
            for (std::size_t index = 0; index < leftMatrix.Elements.size(); ++index)
                if (std::abs(leftMatrix.Elements[index] - rightMatrix.Elements[index]) > 1.0e-4F)
                    return false;
            return true;
        }

        template <typename Unsigned> void AppendUnsigned(std::vector<std::byte>& bytes, Unsigned value)
        {
            static_assert(std::is_unsigned_v<Unsigned>);
            std::uintmax_t remaining = value;
            for (std::size_t index = 0; index < sizeof(Unsigned); ++index)
            {
                bytes.push_back(std::byte(remaining & 0xffU));
                remaining >>= 8U;
            }
        }

        void AppendFloat(std::vector<std::byte>& bytes, const float value)
        {
            AppendUnsigned(bytes, std::bit_cast<std::uint32_t>(value));
        }

        void AppendString(std::vector<std::byte>& bytes, const std::string_view value)
        {
            if (value.size() > 1024U)
                throw std::invalid_argument("Mesh material slot name exceeds its 1 KiB limit.");
            AppendUnsigned(bytes, static_cast<std::uint32_t>(value.size()));
            const auto characters = std::as_bytes(std::span(value.data(), value.size()));
            bytes.insert(bytes.end(), characters.begin(), characters.end());
        }

        class BinaryReader final
        {
          public:
            explicit BinaryReader(const std::span<const std::byte> bytes) : m_Bytes(bytes) {}

            void Expect(const std::span<const char> magic)
            {
                if (Remaining() < magic.size() ||
                    std::memcmp(m_Bytes.data() + m_Offset, magic.data(), magic.size()) != 0)
                    throw std::invalid_argument("Asset binary magic is invalid.");
                m_Offset += magic.size();
            }

            template <typename Unsigned> [[nodiscard]] Unsigned UnsignedValue()
            {
                static_assert(std::is_unsigned_v<Unsigned>);
                if (Remaining() < sizeof(Unsigned))
                    throw std::invalid_argument("Asset binary is truncated.");
                std::uintmax_t result = 0;
                for (std::size_t index = 0; index < sizeof(Unsigned); ++index)
                    result |= std::to_integer<std::uintmax_t>(m_Bytes[m_Offset++]) << (index * 8U);
                return static_cast<Unsigned>(result);
            }

            [[nodiscard]] float Float() { return std::bit_cast<float>(UnsignedValue<std::uint32_t>()); }

            [[nodiscard]] std::string String(const std::size_t maximum)
            {
                const auto length = UnsignedValue<std::uint32_t>();
                if (length > maximum || length > Remaining())
                    throw std::invalid_argument("Asset string exceeds its limit or is truncated.");
                std::string result(length, '\0');
                if (length != 0)
                    std::memcpy(result.data(), m_Bytes.data() + m_Offset, length);
                m_Offset += length;
                return result;
            }

            [[nodiscard]] std::vector<std::byte> Bytes(const std::size_t count)
            {
                if (Remaining() < count)
                    throw std::invalid_argument("Asset binary is truncated.");
                std::vector<std::byte> result(m_Bytes.begin() + static_cast<std::ptrdiff_t>(m_Offset),
                                              m_Bytes.begin() + static_cast<std::ptrdiff_t>(m_Offset + count));
                m_Offset += count;
                return result;
            }

            [[nodiscard]] std::size_t Remaining() const noexcept { return m_Bytes.size() - m_Offset; }

          private:
            std::span<const std::byte> m_Bytes;
            std::size_t m_Offset = 0;
        };

        [[nodiscard]] MeshBounds CalculateBounds(const std::span<const MeshVertex> vertices)
        {
            if (vertices.empty())
                throw std::invalid_argument("A mesh requires at least one vertex.");
            MeshBounds result{vertices.front().Position, vertices.front().Position};
            for (const auto& vertex : vertices)
            {
                if (!Math::IsFinite(vertex.Position) || !Math::IsFinite(vertex.Normal) ||
                    !std::isfinite(vertex.UV0.X) || !std::isfinite(vertex.UV0.Y) || !std::isfinite(vertex.UV1.X) ||
                    !std::isfinite(vertex.UV1.Y) ||
                    !Math::IsFinite(Vector4{vertex.VertexColor.Red, vertex.VertexColor.Green, vertex.VertexColor.Blue,
                                            vertex.VertexColor.Alpha}) ||
                    !Math::IsFinite(vertex.Tangent))
                    throw std::invalid_argument("Mesh vertices must contain only finite values.");
                result.Minimum.X = std::min(result.Minimum.X, vertex.Position.X);
                result.Minimum.Y = std::min(result.Minimum.Y, vertex.Position.Y);
                result.Minimum.Z = std::min(result.Minimum.Z, vertex.Position.Z);
                result.Maximum.X = std::max(result.Maximum.X, vertex.Position.X);
                result.Maximum.Y = std::max(result.Maximum.Y, vertex.Position.Y);
                result.Maximum.Z = std::max(result.Maximum.Z, vertex.Position.Z);
            }
            return result;
        }

        void ValidateMesh(const std::span<const MeshVertex> vertices, const std::span<const std::uint32_t> indices)
        {
            if (vertices.empty() || vertices.size() > MaximumMeshVertices || indices.empty() ||
                indices.size() > MaximumMeshIndices)
                throw std::invalid_argument("Mesh vertex/index counts are empty or excessive.");
            (void)CalculateBounds(vertices);
            if (std::ranges::any_of(indices,
                                    [vertices](const std::uint32_t index) { return index >= vertices.size(); }))
                throw std::invalid_argument("Mesh contains an out-of-range index.");
        }

        void ValidateMeshStructure(const std::span<const std::uint32_t> indices,
                                   const std::span<const MeshSubmesh> submeshes,
                                   const std::span<const MeshMaterialSlot> materialSlots,
                                   const std::span<const MeshLod> lods)
        {
            if (submeshes.empty() || submeshes.size() > MaximumMeshSubmeshes || materialSlots.empty() ||
                materialSlots.size() > MaximumMeshMaterialSlots || lods.empty() || lods.size() > MaximumMeshLods)
                throw std::invalid_argument("Mesh submesh, material-slot, or LOD counts are invalid.");
            for (const auto& slot : materialSlots)
                if (slot.Name.size() > 1024U)
                    throw std::invalid_argument("Mesh material slot name exceeds its 1 KiB limit.");
            for (const auto& submesh : submeshes)
            {
                const auto end = static_cast<std::uint64_t>(submesh.FirstIndex) + submesh.IndexCount;
                const bool validPrimitiveCount =
                    (submesh.Topology == ShaderPrimitiveTopology::TriangleList && submesh.IndexCount % 3U == 0) ||
                    (submesh.Topology == ShaderPrimitiveTopology::LineList && submesh.IndexCount % 2U == 0) ||
                    submesh.Topology == ShaderPrimitiveTopology::PointList;
                if (submesh.IndexCount == 0 || !validPrimitiveCount || end > indices.size() ||
                    submesh.MaterialSlot >= materialSlots.size() || !Math::IsFinite(submesh.Bounds.Minimum) ||
                    !Math::IsFinite(submesh.Bounds.Maximum))
                    throw std::invalid_argument("Mesh submesh topology, range, material slot, or bounds are invalid.");
            }
            float previousThreshold = 1.0F;
            for (const auto& lod : lods)
            {
                const auto end = static_cast<std::uint64_t>(lod.FirstSubmesh) + lod.SubmeshCount;
                if (!std::isfinite(lod.MinimumScreenHeight) || lod.MinimumScreenHeight < 0.0F ||
                    lod.MinimumScreenHeight > previousThreshold || lod.SubmeshCount == 0 || end > submeshes.size() ||
                    !Math::IsFinite(lod.Bounds.Minimum) || !Math::IsFinite(lod.Bounds.Maximum))
                    throw std::invalid_argument("Mesh LOD range, threshold, or bounds are invalid.");
                previousThreshold = lod.MinimumScreenHeight;
            }
        }

        [[nodiscard]] std::tuple<std::vector<MeshSubmesh>, std::vector<MeshMaterialSlot>, std::vector<MeshLod>>
        DefaultMeshStructure(const std::span<const std::uint32_t> indices, const MeshBounds bounds)
        {
            std::vector<MeshSubmesh> submeshes{{0, static_cast<std::uint32_t>(indices.size()), 0, bounds}};
            std::vector<MeshMaterialSlot> materials{{"Default", {}}};
            std::vector<MeshLod> lods{{0.0F, 0, 1, bounds}};
            return {std::move(submeshes), std::move(materials), std::move(lods)};
        }

        [[nodiscard]] std::uint32_t MeshLodIndex(const std::string_view name)
        {
            std::string normalized(name);
            std::ranges::transform(normalized, normalized.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::toupper(value)); });
            const auto marker = normalized.rfind("_LOD");
            if (marker == std::string::npos || marker + 4U == normalized.size())
                return 0;
            std::uint32_t result = 0;
            for (std::size_t index = marker + 4U; index < normalized.size(); ++index)
            {
                const auto value = normalized[index];
                if (value < '0' || value > '9' || result > 100U)
                    return 0;
                result = result * 10U + static_cast<std::uint32_t>(value - '0');
            }
            return std::min(result, static_cast<std::uint32_t>(MaximumMeshLods - 1U));
        }

        [[nodiscard]] MeshBounds CombineBounds(const MeshBounds first, const MeshBounds second) noexcept
        {
            return {{std::min(first.Minimum.X, second.Minimum.X), std::min(first.Minimum.Y, second.Minimum.Y),
                     std::min(first.Minimum.Z, second.Minimum.Z)},
                    {std::max(first.Maximum.X, second.Maximum.X), std::max(first.Maximum.Y, second.Maximum.Y),
                     std::max(first.Maximum.Z, second.Maximum.Z)}};
        }

        [[nodiscard]] Vector3 Cross(const Vector3 left, const Vector3 right) noexcept
        {
            return {left.Y * right.Z - left.Z * right.Y, left.Z * right.X - left.X * right.Z,
                    left.X * right.Y - left.Y * right.X};
        }

        [[nodiscard]] float Dot(const Vector3 left, const Vector3 right) noexcept
        {
            return left.X * right.X + left.Y * right.Y + left.Z * right.Z;
        }

        [[nodiscard]] Vector3 Normalize(const Vector3 value, const Vector3 fallback) noexcept
        {
            const auto lengthSquared = Dot(value, value);
            if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12F)
                return fallback;
            const auto inverseLength = 1.0F / std::sqrt(lengthSquared);
            return {value.X * inverseLength, value.Y * inverseLength, value.Z * inverseLength};
        }

        void GenerateTangents(std::vector<MeshVertex>& vertices, const std::span<const std::uint32_t> indices,
                              const std::span<const MeshSubmesh> submeshes)
        {
            std::vector<Vector3> tangents(vertices.size());
            std::vector<Vector3> bitangents(vertices.size());
            for (const auto& submesh : submeshes)
            {
                if (submesh.Topology != ShaderPrimitiveTopology::TriangleList)
                    continue;
                for (std::size_t triangle = submesh.FirstIndex;
                     triangle < static_cast<std::size_t>(submesh.FirstIndex) + submesh.IndexCount; triangle += 3)
                {
                    const auto first = indices[triangle];
                    const auto second = indices[triangle + 1];
                    const auto third = indices[triangle + 2];
                    const auto& a = vertices[first];
                    const auto& b = vertices[second];
                    const auto& c = vertices[third];
                    const Vector3 edgeOne{b.Position.X - a.Position.X, b.Position.Y - a.Position.Y,
                                          b.Position.Z - a.Position.Z};
                    const Vector3 edgeTwo{c.Position.X - a.Position.X, c.Position.Y - a.Position.Y,
                                          c.Position.Z - a.Position.Z};
                    const Vector2 uvOne{b.UV0.X - a.UV0.X, b.UV0.Y - a.UV0.Y};
                    const Vector2 uvTwo{c.UV0.X - a.UV0.X, c.UV0.Y - a.UV0.Y};
                    const auto determinant = uvOne.X * uvTwo.Y - uvOne.Y * uvTwo.X;
                    if (std::abs(determinant) <= 1.0e-12F)
                        continue;
                    const auto inverse = 1.0F / determinant;
                    const Vector3 tangent{(edgeOne.X * uvTwo.Y - edgeTwo.X * uvOne.Y) * inverse,
                                          (edgeOne.Y * uvTwo.Y - edgeTwo.Y * uvOne.Y) * inverse,
                                          (edgeOne.Z * uvTwo.Y - edgeTwo.Z * uvOne.Y) * inverse};
                    const Vector3 bitangent{(edgeTwo.X * uvOne.X - edgeOne.X * uvTwo.X) * inverse,
                                            (edgeTwo.Y * uvOne.X - edgeOne.Y * uvTwo.X) * inverse,
                                            (edgeTwo.Z * uvOne.X - edgeOne.Z * uvTwo.X) * inverse};
                    for (const auto index : {first, second, third})
                    {
                        tangents[index] = {tangents[index].X + tangent.X, tangents[index].Y + tangent.Y,
                                           tangents[index].Z + tangent.Z};
                        bitangents[index] = {bitangents[index].X + bitangent.X, bitangents[index].Y + bitangent.Y,
                                             bitangents[index].Z + bitangent.Z};
                    }
                }
            }
            for (std::size_t index = 0; index < vertices.size(); ++index)
            {
                const auto normal = Normalize(vertices[index].Normal, {0.0F, 1.0F, 0.0F});
                auto tangent = tangents[index];
                const auto projection = Dot(normal, tangent);
                tangent = Normalize(
                    {tangent.X - normal.X * projection, tangent.Y - normal.Y * projection,
                     tangent.Z - normal.Z * projection},
                    Normalize(Cross(std::abs(normal.Y) < 0.999F ? Vector3{0.0F, 1.0F, 0.0F} : Vector3{1.0F, 0.0F, 0.0F},
                                    normal),
                              {1.0F, 0.0F, 0.0F}));
                const auto handedness = Dot(Cross(normal, tangent), bitangents[index]) < 0.0F ? -1.0F : 1.0F;
                vertices[index].Tangent = {tangent.X, tangent.Y, tangent.Z, handedness};
            }
        }

        void ValidateMip(const TextureMipLevel& mip)
        {
            if (mip.Width == 0 || mip.Height == 0 || mip.Width > Detail::MaximumTextureDimension ||
                mip.Height > Detail::MaximumTextureDimension ||
                static_cast<std::uint64_t>(mip.Width) * mip.Height * 4U != mip.Pixels.size())
                throw std::invalid_argument("Texture mip dimensions do not match its RGBA8 payload.");
        }

        [[nodiscard]] TextureMipLevel Downsample(const TextureMipLevel& source, const bool normalMap)
        {
            TextureMipLevel result;
            result.Width = std::max(source.Width / 2U, 1U);
            result.Height = std::max(source.Height / 2U, 1U);
            result.Pixels.resize(static_cast<std::size_t>(result.Width) * result.Height * 4U);
            for (std::uint32_t y = 0; y < result.Height; ++y)
            {
                for (std::uint32_t x = 0; x < result.Width; ++x)
                {
                    if (normalMap)
                    {
                        Vector3 normal;
                        for (std::uint32_t oy = 0; oy < 2; ++oy)
                        {
                            const auto sourceY = std::min(y * 2U + oy, source.Height - 1U);
                            for (std::uint32_t ox = 0; ox < 2; ++ox)
                            {
                                const auto sourceX = std::min(x * 2U + ox, source.Width - 1U);
                                const auto sourceIndex =
                                    (static_cast<std::size_t>(sourceY) * source.Width + sourceX) * 4U;
                                normal.X +=
                                    static_cast<float>(std::to_integer<std::uint8_t>(source.Pixels[sourceIndex])) /
                                        127.5F -
                                    1.0F;
                                normal.Y +=
                                    static_cast<float>(std::to_integer<std::uint8_t>(source.Pixels[sourceIndex + 1])) /
                                        127.5F -
                                    1.0F;
                                normal.Z +=
                                    static_cast<float>(std::to_integer<std::uint8_t>(source.Pixels[sourceIndex + 2])) /
                                        127.5F -
                                    1.0F;
                            }
                        }
                        normal = Normalize(normal, {0.0F, 0.0F, 1.0F});
                        const auto targetIndex = (static_cast<std::size_t>(y) * result.Width + x) * 4U;
                        result.Pixels[targetIndex] = std::byte(static_cast<std::uint8_t>((normal.X + 1.0F) * 127.5F));
                        result.Pixels[targetIndex + 1] =
                            std::byte(static_cast<std::uint8_t>((normal.Y + 1.0F) * 127.5F));
                        result.Pixels[targetIndex + 2] =
                            std::byte(static_cast<std::uint8_t>((normal.Z + 1.0F) * 127.5F));
                        result.Pixels[targetIndex + 3] = std::byte{255};
                    }
                    else
                    {
                        for (std::uint32_t channel = 0; channel < 4; ++channel)
                        {
                            std::uint32_t total = 0;
                            std::uint32_t samples = 0;
                            for (std::uint32_t oy = 0; oy < 2; ++oy)
                            {
                                const auto sourceY = std::min(y * 2U + oy, source.Height - 1U);
                                for (std::uint32_t ox = 0; ox < 2; ++ox)
                                {
                                    const auto sourceX = std::min(x * 2U + ox, source.Width - 1U);
                                    const auto sourceIndex =
                                        (static_cast<std::size_t>(sourceY) * source.Width + sourceX) * 4U + channel;
                                    total += std::to_integer<std::uint8_t>(source.Pixels[sourceIndex]);
                                    ++samples;
                                }
                            }
                            const auto targetIndex = (static_cast<std::size_t>(y) * result.Width + x) * 4U + channel;
                            result.Pixels[targetIndex] = std::byte((total + samples / 2U) / samples);
                        }
                    }
                }
            }
            return result;
        }

        [[nodiscard]] TextureMipLevel DownsampleRgbe(const TextureMipLevel& source)
        {
            TextureMipLevel result;
            result.Width = std::max(source.Width / 2U, 1U);
            result.Height = std::max(source.Height / 2U, 1U);
            result.Pixels.resize(static_cast<std::size_t>(result.Width) * result.Height * 4U);
            const auto decode = [&source](const std::uint32_t x, const std::uint32_t y)
            {
                const auto index = (static_cast<std::size_t>(y) * source.Width + x) * 4U;
                const auto exponent = std::to_integer<std::uint8_t>(source.Pixels[index + 3U]);
                if (exponent == 0)
                    return Vector3{};
                const float scale = std::ldexp(1.0F, static_cast<int>(exponent) - 136);
                return Vector3{static_cast<float>(std::to_integer<std::uint8_t>(source.Pixels[index])) * scale,
                               static_cast<float>(std::to_integer<std::uint8_t>(source.Pixels[index + 1U])) * scale,
                               static_cast<float>(std::to_integer<std::uint8_t>(source.Pixels[index + 2U])) * scale};
            };
            for (std::uint32_t y = 0; y < result.Height; ++y)
                for (std::uint32_t x = 0; x < result.Width; ++x)
                {
                    Vector3 radiance;
                    for (std::uint32_t offsetY = 0; offsetY < 2; ++offsetY)
                        for (std::uint32_t offsetX = 0; offsetX < 2; ++offsetX)
                        {
                            const auto sample = decode(std::min(x * 2U + offsetX, source.Width - 1U),
                                                       std::min(y * 2U + offsetY, source.Height - 1U));
                            radiance.X += sample.X * 0.25F;
                            radiance.Y += sample.Y * 0.25F;
                            radiance.Z += sample.Z * 0.25F;
                        }
                    const float maximum = std::max({radiance.X, radiance.Y, radiance.Z});
                    if (maximum < 1.0e-32F)
                        continue;
                    int exponent = 0;
                    const float scale = std::frexp(maximum, &exponent) * 256.0F / maximum;
                    const auto index = (static_cast<std::size_t>(y) * result.Width + x) * 4U;
                    result.Pixels[index] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(radiance.X * scale, 0.0F, 255.0F)));
                    result.Pixels[index + 1U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(radiance.Y * scale, 0.0F, 255.0F)));
                    result.Pixels[index + 2U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(radiance.Z * scale, 0.0F, 255.0F)));
                    result.Pixels[index + 3U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(exponent + 128, 0, 255)));
                }
            return result;
        }

        [[nodiscard]] bool IsOpenExr(const std::span<const std::byte> bytes) noexcept
        {
            constexpr std::array magic{std::byte{0x76}, std::byte{0x2f}, std::byte{0x31}, std::byte{0x01}};
            return bytes.size() >= magic.size() && std::ranges::equal(magic, bytes.first(magic.size()));
        }

        [[nodiscard]] std::vector<TextureMipLevel> ImportFloatTexture(Detail::DecodedFloatTexture decoded,
                                                                      const TextureImportSettings& settings)
        {
            if (decoded.Width == 0 || decoded.Height == 0 || decoded.Width > Detail::MaximumTextureDimension ||
                decoded.Height > Detail::MaximumTextureDimension ||
                decoded.Pixels.size() != static_cast<std::size_t>(decoded.Width) * decoded.Height * 4U)
                throw std::invalid_argument("OpenEXR decoder returned invalid RGBA dimensions or storage.");

            TextureMipLevel base;
            base.Width = decoded.Width;
            base.Height = decoded.Height;
            base.Pixels.resize(decoded.Pixels.size());
            if (settings.Semantic == TextureSemantic::Environment)
            {
                for (std::size_t pixel = 0; pixel < decoded.Pixels.size() / 4U; ++pixel)
                {
                    const float red = std::max(decoded.Pixels[pixel * 4U], 0.0F);
                    const float green = std::max(decoded.Pixels[pixel * 4U + 1U], 0.0F);
                    const float blue = std::max(decoded.Pixels[pixel * 4U + 2U], 0.0F);
                    const float maximum = std::max({red, green, blue});
                    if (!std::isfinite(maximum))
                        throw std::invalid_argument("OpenEXR texture contains a non-finite radiance value.");
                    if (maximum < 1.0e-32F)
                        continue;
                    int exponent = 0;
                    const float scale = std::frexp(maximum, &exponent) * 256.0F / maximum;
                    base.Pixels[pixel * 4U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(red * scale, 0.0F, 255.0F)));
                    base.Pixels[pixel * 4U + 1U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(green * scale, 0.0F, 255.0F)));
                    base.Pixels[pixel * 4U + 2U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(blue * scale, 0.0F, 255.0F)));
                    base.Pixels[pixel * 4U + 3U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(exponent + 128, 0, 255)));
                }
                while (base.Width > settings.MaximumSize || base.Height > settings.MaximumSize)
                    base = DownsampleRgbe(base);
                std::vector<TextureMipLevel> result{std::move(base)};
                if (settings.Mips == TextureMipPolicy::Generate)
                    while (result.back().Width > 1 || result.back().Height > 1)
                        result.push_back(DownsampleRgbe(result.back()));
                return result;
            }

            for (std::size_t index = 0; index < decoded.Pixels.size(); ++index)
            {
                const auto value = decoded.Pixels[index];
                if (!std::isfinite(value))
                    throw std::invalid_argument("OpenEXR texture contains a non-finite channel value.");
                base.Pixels[index] =
                    std::byte(static_cast<std::uint8_t>(std::clamp(value, 0.0F, 1.0F) * 255.0F + 0.5F));
            }
            if (settings.Semantic == TextureSemantic::Normal && settings.FlipGreen)
                for (std::size_t index = 1; index < base.Pixels.size(); index += 4)
                    base.Pixels[index] = std::byte(255U - std::to_integer<std::uint8_t>(base.Pixels[index]));
            while (base.Width > settings.MaximumSize || base.Height > settings.MaximumSize)
                base = Downsample(base, settings.Semantic == TextureSemantic::Normal);
            std::vector<TextureMipLevel> result{std::move(base)};
            if (settings.Mips == TextureMipPolicy::Generate)
                while (result.back().Width > 1 || result.back().Height > 1)
                    result.push_back(Downsample(result.back(), settings.Semantic == TextureSemantic::Normal));
            return result;
        }

        [[nodiscard]] std::vector<TextureMipLevel>
        ImportTexture(const std::span<const std::byte> bytes, const TextureImportSettings& settings,
                      const Detail::TextureDecodeBackend& backend,
                      std::optional<Detail::DecodedFloatTexture> decoded = std::nullopt)
        {
            if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                throw std::invalid_argument("Texture source is empty or exceeds the decoder limit.");
            if (IsOpenExr(bytes))
            {
                if (!decoded)
                {
                    if (!backend)
                        throw std::invalid_argument("OpenEXR decoding is unavailable in this asset-import process.");
                    decoded = backend(bytes);
                }
                return ImportFloatTexture(std::move(*decoded), settings);
            }
            int width = 0;
            int height = 0;
            int channels = 0;
            if (settings.Semantic == TextureSemantic::Environment &&
                stbi_is_hdr_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size())))
            {
                std::unique_ptr<float, decltype(&stbi_image_free)> pixels(
                    stbi_loadf_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                           static_cast<int>(bytes.size()), &width, &height, &channels, 4),
                    stbi_image_free);
                if (!pixels || width <= 0 || height <= 0 || width > static_cast<int>(Detail::MaximumTextureDimension) ||
                    height > static_cast<int>(Detail::MaximumTextureDimension))
                    throw std::invalid_argument(std::string("HDR texture decode failed: ") + stbi_failure_reason());
                TextureMipLevel base;
                base.Width = static_cast<std::uint32_t>(width);
                base.Height = static_cast<std::uint32_t>(height);
                base.Pixels.resize(static_cast<std::size_t>(base.Width) * base.Height * 4U);
                for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(base.Width) * base.Height; ++pixel)
                {
                    const float red = std::max(pixels.get()[pixel * 4U], 0.0F);
                    const float green = std::max(pixels.get()[pixel * 4U + 1U], 0.0F);
                    const float blue = std::max(pixels.get()[pixel * 4U + 2U], 0.0F);
                    const float maximum = std::max({red, green, blue});
                    if (!std::isfinite(maximum))
                        throw std::invalid_argument("HDR texture contains a non-finite radiance value.");
                    if (maximum < 1.0e-32F)
                        continue;
                    int exponent = 0;
                    const float scale = std::frexp(maximum, &exponent) * 256.0F / maximum;
                    base.Pixels[pixel * 4U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(red * scale, 0.0F, 255.0F)));
                    base.Pixels[pixel * 4U + 1U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(green * scale, 0.0F, 255.0F)));
                    base.Pixels[pixel * 4U + 2U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(blue * scale, 0.0F, 255.0F)));
                    base.Pixels[pixel * 4U + 3U] =
                        std::byte(static_cast<std::uint8_t>(std::clamp(exponent + 128, 0, 255)));
                }
                while (base.Width > settings.MaximumSize || base.Height > settings.MaximumSize)
                    base = DownsampleRgbe(base);
                std::vector<TextureMipLevel> result{std::move(base)};
                if (settings.Mips == TextureMipPolicy::Generate)
                {
                    while (result.back().Width > 1 || result.back().Height > 1)
                        result.push_back(DownsampleRgbe(result.back()));
                }
                return result;
            }
            std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
                stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size()),
                                      &width, &height, &channels, 4),
                stbi_image_free);
            if (!pixels || width <= 0 || height <= 0 || width > static_cast<int>(Detail::MaximumTextureDimension) ||
                height > static_cast<int>(Detail::MaximumTextureDimension))
                throw std::invalid_argument(std::string("Texture decode failed: ") + stbi_failure_reason());
            TextureMipLevel base;
            base.Width = static_cast<std::uint32_t>(width);
            base.Height = static_cast<std::uint32_t>(height);
            base.Pixels.resize(static_cast<std::size_t>(base.Width) * base.Height * 4U);
            std::memcpy(base.Pixels.data(), pixels.get(), base.Pixels.size());
            if (settings.Semantic == TextureSemantic::Normal && settings.FlipGreen)
            {
                for (std::size_t index = 1; index < base.Pixels.size(); index += 4)
                    base.Pixels[index] = std::byte(255U - std::to_integer<std::uint8_t>(base.Pixels[index]));
            }
            while (base.Width > settings.MaximumSize || base.Height > settings.MaximumSize)
                base = Downsample(base, settings.Semantic == TextureSemantic::Normal);
            std::vector<TextureMipLevel> result{std::move(base)};
            if (settings.Mips == TextureMipPolicy::Generate)
            {
                while (result.back().Width > 1 || result.back().Height > 1)
                    result.push_back(Downsample(result.back(), settings.Semantic == TextureSemantic::Normal));
            }
            return result;
        }

    } // namespace

    MeshAsset::MeshAsset(const BuiltinMesh mesh) : m_Mesh(mesh)
    {
        auto [vertices, indices] = Detail::CreateBuiltinMeshGeometry(mesh);
        m_Vertices = std::move(vertices);
        m_Indices = std::move(indices);
        m_Bounds = CalculateBounds(m_Vertices);
        std::tie(m_Submeshes, m_MaterialSlots, m_Lods) = DefaultMeshStructure(m_Indices, m_Bounds);
    }
    MeshAsset::MeshAsset(std::vector<MeshVertex> vertices, std::vector<std::uint32_t> indices, const MeshBounds bounds)
        : m_Mesh(BuiltinMesh::Error), m_Vertices(std::move(vertices)), m_Indices(std::move(indices)), m_Bounds(bounds)
    {
        ValidateMesh(m_Vertices, m_Indices);
        const auto calculated = CalculateBounds(m_Vertices);
        if (calculated.Minimum != bounds.Minimum || calculated.Maximum != bounds.Maximum)
            throw std::invalid_argument("Mesh bounds do not match its vertices.");
        std::tie(m_Submeshes, m_MaterialSlots, m_Lods) = DefaultMeshStructure(m_Indices, m_Bounds);
    }
    MeshAsset::MeshAsset(std::vector<MeshVertex> vertices, std::vector<std::uint32_t> indices,
                         std::vector<MeshSubmesh> submeshes, std::vector<MeshMaterialSlot> materialSlots,
                         std::vector<MeshLod> lods, const MeshBounds bounds)
        : m_Mesh(BuiltinMesh::Error), m_Vertices(std::move(vertices)), m_Indices(std::move(indices)),
          m_Submeshes(std::move(submeshes)), m_MaterialSlots(std::move(materialSlots)), m_Lods(std::move(lods)),
          m_Bounds(bounds)
    {
        ValidateMesh(m_Vertices, m_Indices);
        ValidateMeshStructure(m_Indices, m_Submeshes, m_MaterialSlots, m_Lods);
        if (CalculateBounds(m_Vertices) != bounds)
            throw std::invalid_argument("Mesh bounds do not match its vertices.");
    }

    std::vector<std::byte> MeshAsset::Encode(const std::span<const MeshVertex> vertices,
                                             const std::span<const std::uint32_t> indices)
    {
        const auto bounds = CalculateBounds(vertices);
        auto [submeshes, materialSlots, lods] = DefaultMeshStructure(indices, bounds);
        return Encode(vertices, indices, submeshes, materialSlots, lods);
    }

    std::vector<std::byte> MeshAsset::Encode(const std::span<const MeshVertex> vertices,
                                             const std::span<const std::uint32_t> indices,
                                             const std::span<const MeshSubmesh> submeshes,
                                             const std::span<const MeshMaterialSlot> materialSlots,
                                             const std::span<const MeshLod> lods)
    {
        ValidateMesh(vertices, indices);
        ValidateMeshStructure(indices, submeshes, materialSlots, lods);
        const auto bounds = CalculateBounds(vertices);
        std::vector<std::byte> result;
        result.reserve(48U + vertices.size() * 72U + indices.size() * sizeof(std::uint32_t));
        for (const char value : MeshMagic)
            result.push_back(std::byte(value));
        AppendUnsigned(result, MeshVersion);
        AppendUnsigned(result, static_cast<std::uint64_t>(vertices.size()));
        AppendUnsigned(result, static_cast<std::uint64_t>(indices.size()));
        for (const float value : {bounds.Minimum.X, bounds.Minimum.Y, bounds.Minimum.Z, bounds.Maximum.X,
                                  bounds.Maximum.Y, bounds.Maximum.Z})
            AppendFloat(result, value);
        AppendUnsigned(result, static_cast<std::uint32_t>(submeshes.size()));
        AppendUnsigned(result, static_cast<std::uint32_t>(materialSlots.size()));
        AppendUnsigned(result, static_cast<std::uint32_t>(lods.size()));
        for (const auto& submesh : submeshes)
        {
            AppendUnsigned(result, submesh.FirstIndex);
            AppendUnsigned(result, submesh.IndexCount);
            AppendUnsigned(result, submesh.MaterialSlot);
            for (const float value : {submesh.Bounds.Minimum.X, submesh.Bounds.Minimum.Y, submesh.Bounds.Minimum.Z,
                                      submesh.Bounds.Maximum.X, submesh.Bounds.Maximum.Y, submesh.Bounds.Maximum.Z})
                AppendFloat(result, value);
            AppendUnsigned(result, static_cast<std::uint8_t>(submesh.Topology));
        }
        for (const auto& slot : materialSlots)
        {
            AppendString(result, slot.Name);
            AppendUnsigned(result, slot.DefaultMaterial.High());
            AppendUnsigned(result, slot.DefaultMaterial.Low());
        }
        for (const auto& lod : lods)
        {
            AppendFloat(result, lod.MinimumScreenHeight);
            AppendUnsigned(result, lod.FirstSubmesh);
            AppendUnsigned(result, lod.SubmeshCount);
            for (const float value : {lod.Bounds.Minimum.X, lod.Bounds.Minimum.Y, lod.Bounds.Minimum.Z,
                                      lod.Bounds.Maximum.X, lod.Bounds.Maximum.Y, lod.Bounds.Maximum.Z})
                AppendFloat(result, value);
        }
        for (const auto& vertex : vertices)
        {
            for (const float value :
                 {vertex.Position.X, vertex.Position.Y, vertex.Position.Z, vertex.Normal.X, vertex.Normal.Y,
                  vertex.Normal.Z, vertex.UV0.X, vertex.UV0.Y, vertex.VertexColor.Red, vertex.VertexColor.Green,
                  vertex.VertexColor.Blue, vertex.VertexColor.Alpha, vertex.Tangent.X, vertex.Tangent.Y,
                  vertex.Tangent.Z, vertex.Tangent.W, vertex.UV1.X, vertex.UV1.Y})
                AppendFloat(result, value);
        }
        for (const auto index : indices)
            AppendUnsigned(result, index);
        return result;
    }

    Ref<MeshAsset> MeshAsset::Decode(const std::span<const std::byte> bytes)
    {
        BinaryReader reader(bytes);
        reader.Expect(MeshMagic);
        const auto version = reader.UnsignedValue<std::uint32_t>();
        if (version < 1 || version > MeshVersion)
            throw std::invalid_argument("Mesh asset has an unsupported version.");
        const auto vertexCount = reader.UnsignedValue<std::uint64_t>();
        const auto indexCount = reader.UnsignedValue<std::uint64_t>();
        if (vertexCount == 0 || vertexCount > MaximumMeshVertices || indexCount == 0 ||
            indexCount > MaximumMeshIndices || (version < 5 && indexCount % 3U != 0))
            throw std::invalid_argument("Mesh asset counts are invalid.");
        MeshBounds bounds{{reader.Float(), reader.Float(), reader.Float()},
                          {reader.Float(), reader.Float(), reader.Float()}};
        std::vector<MeshSubmesh> submeshes;
        std::vector<MeshMaterialSlot> materialSlots;
        std::vector<MeshLod> lods;
        if (version >= 3)
        {
            const auto submeshCount = reader.UnsignedValue<std::uint32_t>();
            const auto materialSlotCount = reader.UnsignedValue<std::uint32_t>();
            const auto lodCount = reader.UnsignedValue<std::uint32_t>();
            if (submeshCount == 0 || submeshCount > MaximumMeshSubmeshes || materialSlotCount == 0 ||
                materialSlotCount > MaximumMeshMaterialSlots || lodCount == 0 || lodCount > MaximumMeshLods)
                throw std::invalid_argument("Mesh asset structure counts are invalid.");
            submeshes.reserve(submeshCount);
            for (std::uint32_t index = 0; index < submeshCount; ++index)
            {
                MeshSubmesh submesh;
                submesh.FirstIndex = reader.UnsignedValue<std::uint32_t>();
                submesh.IndexCount = reader.UnsignedValue<std::uint32_t>();
                submesh.MaterialSlot = reader.UnsignedValue<std::uint32_t>();
                submesh.Bounds = {{reader.Float(), reader.Float(), reader.Float()},
                                  {reader.Float(), reader.Float(), reader.Float()}};
                if (version >= 5)
                {
                    submesh.Topology = static_cast<ShaderPrimitiveTopology>(reader.UnsignedValue<std::uint8_t>());
                }
                submeshes.push_back(submesh);
            }
            materialSlots.reserve(materialSlotCount);
            for (std::uint32_t index = 0; index < materialSlotCount; ++index)
            {
                MeshMaterialSlot slot;
                slot.Name = reader.String(1024U);
                slot.DefaultMaterial = {reader.UnsignedValue<std::uint64_t>(), reader.UnsignedValue<std::uint64_t>()};
                materialSlots.push_back(std::move(slot));
            }
            lods.reserve(lodCount);
            for (std::uint32_t index = 0; index < lodCount; ++index)
            {
                MeshLod lod;
                lod.MinimumScreenHeight = reader.Float();
                lod.FirstSubmesh = reader.UnsignedValue<std::uint32_t>();
                lod.SubmeshCount = reader.UnsignedValue<std::uint32_t>();
                lod.Bounds = {{reader.Float(), reader.Float(), reader.Float()},
                              {reader.Float(), reader.Float(), reader.Float()}};
                lods.push_back(lod);
            }
        }
        const auto vertexSize = version == 1 ? 48U : version < 4 ? 64U : 72U;
        const auto expected = vertexCount * vertexSize + indexCount * sizeof(std::uint32_t);
        if (expected != reader.Remaining())
            throw std::invalid_argument("Mesh asset payload size is invalid.");
        std::vector<MeshVertex> vertices(static_cast<std::size_t>(vertexCount));
        for (auto& vertex : vertices)
        {
            vertex.Position = {reader.Float(), reader.Float(), reader.Float()};
            vertex.Normal = {reader.Float(), reader.Float(), reader.Float()};
            vertex.UV0 = {reader.Float(), reader.Float()};
            vertex.VertexColor = {reader.Float(), reader.Float(), reader.Float(), reader.Float()};
            if (version >= 2)
                vertex.Tangent = {reader.Float(), reader.Float(), reader.Float(), reader.Float()};
            if (version >= 4)
                vertex.UV1 = {reader.Float(), reader.Float()};
        }
        std::vector<std::uint32_t> indices(static_cast<std::size_t>(indexCount));
        for (auto& index : indices)
            index = reader.UnsignedValue<std::uint32_t>();
        if (version < 3)
            std::tie(submeshes, materialSlots, lods) = DefaultMeshStructure(indices, bounds);
        if (version == 1)
            GenerateTangents(vertices, indices, submeshes);
        return CreateRef<MeshAsset>(std::move(vertices), std::move(indices), std::move(submeshes),
                                    std::move(materialSlots), std::move(lods), bounds);
    }

    Texture2DAsset::Texture2DAsset(TextureImportSettings settings, std::vector<TextureMipLevel> mips)
        : m_Settings(Detail::NormalizeTextureSettings(settings)), m_Mips(std::move(mips))
    {
        if (m_Mips.empty())
            throw std::invalid_argument("A texture requires at least one mip.");
        std::uint32_t expectedWidth = m_Mips.front().Width;
        std::uint32_t expectedHeight = m_Mips.front().Height;
        for (const auto& mip : m_Mips)
        {
            ValidateMip(mip);
            if (mip.Width != expectedWidth || mip.Height != expectedHeight)
                throw std::invalid_argument("Texture mip dimensions are not a complete deterministic chain.");
            expectedWidth = std::max(expectedWidth / 2U, 1U);
            expectedHeight = std::max(expectedHeight / 2U, 1U);
        }
        if (m_Settings.Mips == TextureMipPolicy::None && m_Mips.size() != 1)
            throw std::invalid_argument("A texture with disabled mips must contain only its base level.");
        if (m_Settings.Mips == TextureMipPolicy::Generate && (m_Mips.back().Width != 1 || m_Mips.back().Height != 1))
            throw std::invalid_argument("A texture with generated mips must contain the complete chain through 1x1.");
    }

    std::vector<std::byte> Texture2DAsset::Encode(const TextureImportSettings& requested,
                                                  const std::span<const TextureMipLevel> mips)
    {
        const auto settings = Detail::NormalizeTextureSettings(requested);
        Texture2DAsset validation(settings, {mips.begin(), mips.end()});
        std::vector<std::byte> result;
        result.reserve(TextureMagic.size());
        for (const char value : TextureMagic)
            result.push_back(std::byte(value));
        AppendUnsigned(result, TextureVersion);
        for (const auto value :
             {static_cast<std::uint8_t>(settings.Semantic), static_cast<std::uint8_t>(settings.ColorSpace),
              static_cast<std::uint8_t>(settings.Mips), static_cast<std::uint8_t>(settings.Sampler.Minimum),
              static_cast<std::uint8_t>(settings.Sampler.Magnification),
              static_cast<std::uint8_t>(settings.Sampler.Mip), static_cast<std::uint8_t>(settings.Sampler.AddressU),
              static_cast<std::uint8_t>(settings.Sampler.AddressV),
              static_cast<std::uint8_t>(settings.Sampler.AddressW), settings.Sampler.Anisotropy,
              static_cast<std::uint8_t>(settings.FlipGreen), static_cast<std::uint8_t>(settings.EnvironmentLayout),
              static_cast<std::uint8_t>(settings.HighDynamicRange)})
            result.push_back(std::byte(value));
        AppendUnsigned(result, settings.MaximumSize);
        AppendUnsigned(result, static_cast<std::uint32_t>(mips.size()));
        for (const auto& mip : mips)
        {
            AppendUnsigned(result, mip.Width);
            AppendUnsigned(result, mip.Height);
            AppendUnsigned(result, static_cast<std::uint64_t>(mip.Pixels.size()));
            result.insert(result.end(), mip.Pixels.begin(), mip.Pixels.end());
        }
        return result;
    }

    Ref<Texture2DAsset> Texture2DAsset::Decode(const std::span<const std::byte> bytes)
    {
        BinaryReader reader(bytes);
        reader.Expect(TextureMagic);
        const auto version = reader.UnsignedValue<std::uint32_t>();
        if (version < 1 || version > TextureVersion)
            throw std::invalid_argument("Texture asset has an unsupported version.");
        TextureImportSettings settings;
        settings.Semantic = static_cast<TextureSemantic>(reader.UnsignedValue<std::uint8_t>());
        settings.ColorSpace = static_cast<TextureColorSpace>(reader.UnsignedValue<std::uint8_t>());
        settings.Mips = static_cast<TextureMipPolicy>(reader.UnsignedValue<std::uint8_t>());
        settings.Sampler.Minimum = static_cast<TextureFilter>(reader.UnsignedValue<std::uint8_t>());
        settings.Sampler.Magnification = static_cast<TextureFilter>(reader.UnsignedValue<std::uint8_t>());
        settings.Sampler.Mip = static_cast<TextureFilter>(reader.UnsignedValue<std::uint8_t>());
        settings.Sampler.AddressU = static_cast<TextureAddressMode>(reader.UnsignedValue<std::uint8_t>());
        settings.Sampler.AddressV = static_cast<TextureAddressMode>(reader.UnsignedValue<std::uint8_t>());
        settings.Sampler.AddressW = static_cast<TextureAddressMode>(reader.UnsignedValue<std::uint8_t>());
        settings.Sampler.Anisotropy = reader.UnsignedValue<std::uint8_t>();
        if (version >= 2)
            settings.FlipGreen = reader.UnsignedValue<std::uint8_t>() != 0;
        if (version >= 3)
        {
            settings.EnvironmentLayout = static_cast<TextureEnvironmentLayout>(reader.UnsignedValue<std::uint8_t>());
            settings.HighDynamicRange = reader.UnsignedValue<std::uint8_t>() != 0;
        }
        settings.MaximumSize = reader.UnsignedValue<std::uint32_t>();
        const auto mipCount = reader.UnsignedValue<std::uint32_t>();
        if (mipCount == 0 || mipCount > 15)
            throw std::invalid_argument("Texture asset mip count is invalid.");
        std::vector<TextureMipLevel> mips;
        mips.reserve(mipCount);
        std::size_t totalBytes = 0;
        for (std::uint32_t index = 0; index < mipCount; ++index)
        {
            TextureMipLevel mip;
            mip.Width = reader.UnsignedValue<std::uint32_t>();
            mip.Height = reader.UnsignedValue<std::uint32_t>();
            const auto byteCount = reader.UnsignedValue<std::uint64_t>();
            if (byteCount > MaximumTextureBytes - totalBytes || byteCount > reader.Remaining())
                throw std::invalid_argument("Texture asset payload exceeds its limit.");
            totalBytes += static_cast<std::size_t>(byteCount);
            mip.Pixels = reader.Bytes(static_cast<std::size_t>(byteCount));
            mips.push_back(std::move(mip));
        }
        if (reader.Remaining() != 0)
            throw std::invalid_argument("Texture asset contains trailing data.");
        return CreateRef<Texture2DAsset>(settings, std::move(mips));
    }

    AssetImporterRegistration CreateMeshAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.Mesh";
        result.Version = 17;
        result.Type = MeshAsset::StaticType();
        result.CompatibleTypes = {AnimationSourceAsset::StaticType()};
        result.Extensions = {".obj", ".fbx", ".gltf", ".glb", ".keiremesh"};
        result.ContextualImport = [](const AssetImportContext& context,
                                     const std::span<const std::byte> bytes) -> AssetImportOutput
        {
            const auto stringSetting = [&context](const std::string_view key, const std::string_view fallback)
            {
                const auto setting = context.ImportSettings.find(key);
                if (setting == context.ImportSettings.end())
                    return std::string(fallback);
                const auto* value = std::get_if<std::string>(&setting->second);
                return value ? *value : std::string(fallback);
            };
            const auto contentType = stringSetting("contentType", "model");
            const auto rigSource = stringSetting("rigSource", "embedded");
            const auto requestedRigProfile = stringSetting("rigProfile", "humanoid");
            const auto requestedSkinning = stringSetting("skinningMethod", "linearBlend");
            const auto requestedInfluences = stringSetting("maximumInfluences", "4") == "8" ? 8 : 4;
            const auto requestedCompression = stringSetting("animationCompression", "balanced");
            const auto requestedAnimationMotion =
                ParseImportedAnimationMotion(stringSetting("animationMotion", "rootMotion"));
            if (context.SourcePath.extension() == ".keiremesh")
            {
                const auto mesh = MeshAsset::Decode(bytes);
                AssetImportOutput output;
                output.Bytes.assign(bytes.begin(), bytes.end());
                const auto& bounds = mesh->Bounds();
                output.Metadata.LocalBounds = AssetBounds{{bounds.Minimum.X, bounds.Minimum.Y, bounds.Minimum.Z},
                                                          {bounds.Maximum.X, bounds.Maximum.Y, bounds.Maximum.Z}};
                return output;
            }
            Assimp::Importer importer;
            AssetImportOutput output;
            Detail::AssimpProjectIO* projectIO = nullptr;
            if (context.ReadProjectFile)
            {
                auto handler = std::make_unique<Detail::AssimpProjectIO>(context);
                projectIO = handler.get();
                importer.SetIOHandler(handler.release());
            }
            const auto throwIfSidecarViolation = [projectIO]
            {
                if (projectIO && !projectIO->Violation().empty())
                    throw std::invalid_argument("Mesh import rejected a model sidecar: " +
                                                std::string(projectIO->Violation()));
            };
            const auto collectSourceDependencies = [&output, projectIO, &throwIfSidecarViolation]
            {
                throwIfSidecarViolation();
                if (projectIO)
                {
                    output.SourceDependencies = projectIO->SourceDependencies();
                    std::ranges::sort(output.SourceDependencies, {}, &AssetSourceDependency::RelativePath);
                }
            };
            importer.SetPropertyBool(AI_CONFIG_PP_PTV_KEEP_HIERARCHY, true);
            constexpr unsigned int flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                                           aiProcess_CalcTangentSpace | aiProcess_SortByPType |
                                           aiProcess_ValidateDataStructure | aiProcess_MakeLeftHanded |
                                           aiProcess_FlipUVs | aiProcess_FlipWindingOrder;
            auto extension = context.SourcePath.extension().string();
            if (!extension.empty() && extension.front() == '.')
                extension.erase(extension.begin());
            extension = Lowercase(std::move(extension));
            if (projectIO && extension == "gltf")
            {
                const auto source = std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                const auto document = nlohmann::json::parse(source, nullptr, false);
                if (!document.is_discarded())
                {
                    for (const std::string_view collection : {"buffers", "images"})
                    {
                        const auto entries = document.find(collection);
                        if (entries == document.end() || !entries->is_array())
                            continue;
                        for (const auto& entry : *entries)
                        {
                            const auto uri = entry.find("uri");
                            if (uri == entry.end() || !uri->is_string())
                                continue;
                            const auto& reference = uri->get_ref<const std::string&>();
                            if (Lowercase(reference.substr(0, std::min<std::size_t>(reference.size(), 5U))) == "data:")
                                continue;
                            (void)projectIO->ValidateReference(reference);
                        }
                    }
                    throwIfSidecarViolation();
                }
            }
            const auto* scene = importer.ReadFileFromMemory(bytes.data(), bytes.size(), flags, extension.c_str());
            throwIfSidecarViolation();
            if (!scene)
            {
                auto diagnostic = std::string("Mesh import failed: ") + importer.GetErrorString();
                if (projectIO && !projectIO->LastReadFailure().empty())
                    diagnostic += " " + std::string(projectIO->LastReadFailure());
                throw std::invalid_argument(std::move(diagnostic));
            }
            const bool animationSource = contentType == "animation";
            if (animationSource && scene->mNumAnimations == 0)
                throw std::invalid_argument("Animation Source import found no animation clips in the selected file.");
            bool hasSkinning = false;
            for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
                hasSkinning = hasSkinning || (scene->mMeshes[meshIndex] && scene->mMeshes[meshIndex]->mNumBones != 0);
            const bool useEmbeddedSkinning = hasSkinning && (rigSource == "embedded" || animationSource);
            const bool useEmbeddedHierarchy = useEmbeddedSkinning || animationSource;
            const bool generateRig = !animationSource && rigSource == "generate";
            const bool animated = useEmbeddedSkinning || generateRig;
            if (!animated && !animationSource)
            {
                scene = importer.ApplyPostProcessing(aiProcess_PreTransformVertices | aiProcess_JoinIdenticalVertices |
                                                     aiProcess_ImproveCacheLocality);
                if (!scene)
                    throw std::invalid_argument(std::string("Static mesh transform baking failed: ") +
                                                importer.GetErrorString());
            }
            if (const std::string diagnostic = importer.GetErrorString(); !diagnostic.empty())
                output.Diagnostics.push_back(
                    {AssetDiagnosticSeverity::Warning, context.RelativePath, 0, 0, "Assimp: " + diagnostic});
            std::vector<MeshVertex> vertices;
            std::vector<std::uint32_t> indices;
            std::vector<MeshSubmesh> submeshes;
            std::vector<MeshMaterialSlot> materialSlots;
            materialSlots.reserve(std::max(scene->mNumMaterials, 1U));
            std::vector<std::string> materialNames;
            materialNames.reserve(scene->mNumMaterials);
            for (unsigned int materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex)
            {
                aiString materialName;
                if (scene->mMaterials[materialIndex]->Get(AI_MATKEY_NAME, materialName) != aiReturn_SUCCESS ||
                    materialName.length == 0)
                {
                    materialName = aiString("Material " + std::to_string(materialIndex + 1U));
                    if (extension == "fbx" || extension == "obj")
                        output.Diagnostics.push_back(
                            {AssetDiagnosticSeverity::Warning, context.RelativePath, 0, 0,
                             "Imported material " + std::to_string(materialIndex + 1U) +
                                 " has no stable name; generated-subasset identity uses its fallback name."});
                }
                materialNames.emplace_back(materialName.C_Str());
                materialSlots.push_back({materialName.C_Str(), {}});
            }
            if (materialSlots.empty())
                materialSlots.push_back({"Default", {}});
            if (extension == "obj")
            {
                const std::string source(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                if (source.find("mtllib") != std::string::npos && scene->mNumMaterials <= 1)
                    output.Diagnostics.push_back(
                        {AssetDiagnosticSeverity::Warning, context.RelativePath, 0, 0,
                         "OBJ declares a material library that was not resolved. Import the OBJ with its MTL and "
                         "texture files together, or extract and repair the generated material."});
            }
            if (extension == "fbx" || extension == "obj")
            {
                constexpr std::array legacyTextureTypes{aiTextureType_SPECULAR, aiTextureType_SHININESS,
                                                        aiTextureType_OPACITY, aiTextureType_REFLECTION,
                                                        aiTextureType_UNKNOWN};
                for (unsigned int materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex)
                    for (const auto type : legacyTextureTypes)
                        if (scene->mMaterials[materialIndex]->GetTextureCount(type) != 0)
                        {
                            output.Diagnostics.push_back(
                                {AssetDiagnosticSeverity::Warning, context.RelativePath, 0, 0,
                                 "Material '" + materialNames[materialIndex] + "' uses unsupported " +
                                     aiTextureTypeToString(type) +
                                     " texture channels; review the extracted metallic/roughness material."});
                            break;
                        }
            }

            const bool importMaterials = stringSetting("materialImport", "embedded") != "none";
            if (importMaterials)
            {
                const auto shader = Detail::FindImportedMaterialShader(context);
                if (!shader)
                {
                    output.Diagnostics.push_back(
                        {AssetDiagnosticSeverity::Warning, context.RelativePath, 0, 0,
                         "Model materials were not published because no project material shader was found."});
                }
                else if (!context.ResolveSubAssetId)
                {
                    throw std::logic_error("Mesh importing requires a generated-subasset identity resolver.");
                }
                else
                {
                    const auto declared = [&shader](const std::string_view property)
                    { return shader->Properties.contains(std::string(property)); };
                    std::unordered_map<std::string, AssetId> publishedTextures;
                    const auto publishTexture = [&](const unsigned int materialIndex, const aiTextureType type,
                                                    const std::string_view property, const TextureSemantic semantic,
                                                    const TextureColorSpace colorSpace) -> AssetId
                    {
                        const auto* material = scene->mMaterials[materialIndex];
                        const auto count = material->GetTextureCount(type);
                        if (count == 0)
                            return {};
                        if (count > 1)
                            output.Diagnostics.push_back(
                                {AssetDiagnosticSeverity::Warning, context.RelativePath, 0, 0,
                                 "Material '" + materialNames[materialIndex] + "' has " + std::to_string(count) +
                                     " textures for " + std::string(property) + "; only the first is supported."});
                        aiString path;
                        if (material->GetTexture(type, 0, &path) != aiReturn_SUCCESS)
                            return {};
                        const auto* embedded = scene->GetEmbeddedTexture(path.C_Str());
                        std::optional<Detail::AssimpProjectFile> external;
                        if (!embedded)
                        {
                            if (projectIO)
                                external = projectIO->ReadReferencedFile(path.C_Str());
                            if (!external)
                            {
                                throwIfSidecarViolation();
                                auto diagnostic = "Material '" + materialNames[materialIndex] +
                                                  "' could not read external texture '" + path.C_Str() + "'.";
                                if (projectIO && !projectIO->LastReadFailure().empty())
                                    diagnostic += " " + std::string(projectIO->LastReadFailure());
                                else if (!projectIO)
                                    diagnostic += " Project-file access is unavailable.";
                                output.Diagnostics.push_back({AssetDiagnosticSeverity::Warning, context.RelativePath, 0,
                                                              0, std::move(diagnostic)});
                                return {};
                            }
                        }
                        std::string key;
                        if (embedded)
                        {
                            const auto textureIterator =
                                std::find(scene->mTextures, scene->mTextures + scene->mNumTextures, embedded);
                            if (textureIterator == scene->mTextures + scene->mNumTextures)
                                throw std::logic_error(
                                    "Assimp returned an embedded texture outside the imported scene.");
                            const auto textureIndex = static_cast<std::size_t>(textureIterator - scene->mTextures);
                            key = "texture/" + std::to_string(textureIndex) + "/" +
                                  std::to_string(static_cast<unsigned int>(semantic));
                        }
                        else
                        {
                            key = "texture/external/" + external->RelativePath.generic_string() + "/" +
                                  std::to_string(static_cast<unsigned int>(semantic));
                        }
                        if (const auto existing = publishedTextures.find(key); existing != publishedTextures.end())
                            return existing->second;

                        TextureImportSettings settings;
                        settings.Semantic = semantic;
                        settings.ColorSpace = colorSpace;
                        std::vector<TextureMipLevel> mips;
                        if (!embedded)
                        {
                            mips = ImportTexture(external->Bytes, settings, {});
                        }
                        else if (embedded->mHeight == 0)
                        {
                            const auto textureBytes = std::span(reinterpret_cast<const std::byte*>(embedded->pcData),
                                                                static_cast<std::size_t>(embedded->mWidth));
                            mips = ImportTexture(textureBytes, settings, {});
                        }
                        else
                        {
                            if (embedded->mWidth == 0 || embedded->mHeight == 0 ||
                                embedded->mWidth > Detail::MaximumTextureDimension ||
                                embedded->mHeight > Detail::MaximumTextureDimension)
                                throw std::invalid_argument("Embedded model texture dimensions are invalid.");
                            TextureMipLevel base;
                            base.Width = embedded->mWidth;
                            base.Height = embedded->mHeight;
                            base.Pixels.resize(static_cast<std::size_t>(base.Width) * base.Height * 4U);
                            for (std::size_t pixel = 0; pixel < static_cast<std::size_t>(base.Width) * base.Height;
                                 ++pixel)
                            {
                                base.Pixels[pixel * 4U] = std::byte(embedded->pcData[pixel].r);
                                base.Pixels[pixel * 4U + 1U] = std::byte(embedded->pcData[pixel].g);
                                base.Pixels[pixel * 4U + 2U] = std::byte(embedded->pcData[pixel].b);
                                base.Pixels[pixel * 4U + 3U] = std::byte(embedded->pcData[pixel].a);
                            }
                            mips.push_back(std::move(base));
                            while (mips.back().Width > 1 || mips.back().Height > 1)
                                mips.push_back(Downsample(mips.back(), semantic == TextureSemantic::Normal));
                        }
                        const auto id = context.ResolveSubAssetId(key);
                        output.SubAssets.push_back({id, Texture2DAsset::StaticType(), key,
                                                    materialNames[materialIndex] + " " + std::string(property),
                                                    Texture2DAsset::Encode(settings, mips)});
                        publishedTextures.emplace(key, id);
                        return id;
                    };

                    std::unordered_map<std::string, std::size_t> materialNameOccurrences;
                    std::vector<bool> materialUsed(scene->mNumMaterials);
                    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
                        if (scene->mMeshes[meshIndex] &&
                            scene->mMeshes[meshIndex]->mMaterialIndex < materialUsed.size())
                            materialUsed[scene->mMeshes[meshIndex]->mMaterialIndex] = true;
                    for (unsigned int materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex)
                    {
                        if (!materialUsed[materialIndex])
                            continue;
                        const auto* sourceMaterial = scene->mMaterials[materialIndex];
                        const auto occurrence = materialNameOccurrences[materialNames[materialIndex]]++;
                        const auto key = "material/" + materialNames[materialIndex] + "/" + std::to_string(occurrence);
                        MaterialAssetDefinition definition;
                        definition.SchemaVersion = 2;
                        definition.Shader = shader->Id;

                        aiColor4D baseColor{1.0F, 1.0F, 1.0F, 1.0F};
                        if (sourceMaterial->Get(AI_MATKEY_BASE_COLOR, baseColor) != aiReturn_SUCCESS)
                            (void)sourceMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor);
                        if (declared("Tint"))
                            definition.Properties.emplace("Tint",
                                                          Color{baseColor.r, baseColor.g, baseColor.b, baseColor.a});
                        float scalar = 1.0F;
                        if (declared("MetallicFactor") &&
                            sourceMaterial->Get(AI_MATKEY_METALLIC_FACTOR, scalar) == aiReturn_SUCCESS)
                            definition.Properties.emplace("MetallicFactor", std::clamp(scalar, 0.0F, 1.0F));
                        scalar = 1.0F;
                        if (declared("RoughnessFactor") &&
                            sourceMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR, scalar) == aiReturn_SUCCESS)
                            definition.Properties.emplace("RoughnessFactor", std::clamp(scalar, 0.0F, 1.0F));
                        scalar = 1.0F;
                        if (declared("NormalScale") &&
                            sourceMaterial->Get(AI_MATKEY_GLTF_TEXTURE_SCALE(aiTextureType_NORMALS, 0), scalar) ==
                                aiReturn_SUCCESS)
                            definition.Properties.emplace("NormalScale", std::max(scalar, 0.0F));
                        scalar = 1.0F;
                        if (declared("OcclusionStrength") &&
                            sourceMaterial->Get(AI_MATKEY_GLTF_TEXTURE_STRENGTH(aiTextureType_AMBIENT_OCCLUSION, 0),
                                                scalar) == aiReturn_SUCCESS)
                            definition.Properties.emplace("OcclusionStrength", std::clamp(scalar, 0.0F, 1.0F));
                        aiColor3D emissive{};
                        if (declared("EmissiveFactor") &&
                            sourceMaterial->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == aiReturn_SUCCESS)
                            definition.Properties.emplace("EmissiveFactor",
                                                          Color{emissive.r, emissive.g, emissive.b, 1.0F});

                        const auto addTexture = [&](const aiTextureType type, const std::string_view property,
                                                    const TextureSemantic semantic,
                                                    const TextureColorSpace colorSpace) -> AssetId
                        {
                            if (const auto texture =
                                    publishTexture(materialIndex, type, property, semantic, colorSpace))
                            {
                                if (declared(property))
                                    definition.Properties.insert_or_assign(std::string(property), texture);
                                else
                                    output.Diagnostics.push_back(
                                        {AssetDiagnosticSeverity::Warning, context.RelativePath, 0, 0,
                                         "Material '" + materialNames[materialIndex] + "' has " +
                                             std::string(property) +
                                             ", but the project material shader does not declare that property."});
                                return texture;
                            }
                            return {};
                        };
                        const auto baseColorTexture = addTexture(aiTextureType_BASE_COLOR, "MainTexture",
                                                                 TextureSemantic::Color, TextureColorSpace::Srgb);
                        if (!baseColorTexture)
                            addTexture(aiTextureType_DIFFUSE, "MainTexture", TextureSemantic::Color,
                                       TextureColorSpace::Srgb);
                        addTexture(aiTextureType_NORMALS, "NormalTexture", TextureSemantic::Normal,
                                   TextureColorSpace::Linear);
                        addTexture(aiTextureType_GLTF_METALLIC_ROUGHNESS, "MetallicRoughnessTexture",
                                   TextureSemantic::Data, TextureColorSpace::Linear);
                        addTexture(aiTextureType_AMBIENT_OCCLUSION, "OcclusionTexture", TextureSemantic::Data,
                                   TextureColorSpace::Linear);
                        addTexture(aiTextureType_EMISSIVE, "EmissiveTexture", TextureSemantic::Color,
                                   TextureColorSpace::Srgb);
                        addTexture(aiTextureType_METALNESS, "MetallicTexture", TextureSemantic::Data,
                                   TextureColorSpace::Linear);
                        addTexture(aiTextureType_DIFFUSE_ROUGHNESS, "RoughnessTexture", TextureSemantic::Data,
                                   TextureColorSpace::Linear);

                        aiString alphaMode;
                        const bool explicitAlphaMode =
                            sourceMaterial->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == aiReturn_SUCCESS;
                        if (explicitAlphaMode)
                        {
                            const auto mode = Lowercase(alphaMode.C_Str());
                            if (mode == "mask")
                                definition.Surface.AlphaMode = MaterialAlphaMode::Mask;
                            else if (mode == "blend")
                                definition.Surface.AlphaMode = MaterialAlphaMode::Blend;
                        }
                        scalar = 0.5F;
                        if (sourceMaterial->Get(AI_MATKEY_GLTF_ALPHACUTOFF, scalar) == aiReturn_SUCCESS)
                            definition.Surface.AlphaCutoff = std::clamp(scalar, 0.0F, 1.0F);
                        int twoSided = 0;
                        if (sourceMaterial->Get(AI_MATKEY_TWOSIDED, twoSided) == aiReturn_SUCCESS)
                            definition.Surface.DoubleSided = twoSided != 0;
                        scalar = 1.0F;
                        if (!explicitAlphaMode && sourceMaterial->Get(AI_MATKEY_OPACITY, scalar) == aiReturn_SUCCESS &&
                            scalar < 1.0F)
                        {
                            definition.Surface.AlphaMode = MaterialAlphaMode::Blend;
                            if (auto tint = definition.Properties.find("Tint"); tint != definition.Properties.end())
                                if (auto* color = std::get_if<Color>(&tint->second))
                                    color->Alpha = std::min(color->Alpha, std::clamp(scalar, 0.0F, 1.0F));
                        }

                        std::vector<AssetId> dependencies{shader->Id};
                        for (const auto& [name, value] : definition.Properties)
                        {
                            (void)name;
                            if (const auto* texture = std::get_if<AssetId>(&value); texture && *texture)
                                dependencies.push_back(*texture);
                        }
                        std::ranges::sort(dependencies);
                        dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
                        const auto materialId = context.ResolveSubAssetId(key);
                        output.SubAssets.push_back({materialId, MaterialAsset::StaticType(), key,
                                                    materialNames[materialIndex], MaterialAsset::Encode(definition),
                                                    dependencies});
                        output.AssetDependencies.push_back(materialId);
                        materialSlots[materialIndex].DefaultMaterial = materialId;
                    }
                }
            }
            AssetId skeletonId;
            AssetId skinnedMeshId;
            AssetId rigId;
            std::vector<AnimationTakeDescriptor> animationTakes;
            std::vector<SkeletonBone> skeletonBones;
            std::unordered_map<std::string, std::uint16_t> boneIndices;
            std::vector<SkinVertexInfluence8> skinInfluences8;
            SkinningMethod skinningMethod =
                requestedSkinning == "dualQuaternion" ? SkinningMethod::DualQuaternion : SkinningMethod::LinearBlend;
            std::vector<aiMatrix4x4> meshTransforms(scene->mNumMeshes);
            std::vector<bool> meshTransformAssigned(scene->mNumMeshes);
            std::unordered_map<const aiNode*, aiMatrix4x4> nodeGlobalTransforms;
            if (animated || animationSource)
            {
                const auto collectTransforms = [&](const auto& self, const aiNode& node,
                                                   const aiMatrix4x4& parentTransform) -> void
                {
                    const auto globalTransform = parentTransform * node.mTransformation;
                    nodeGlobalTransforms.insert_or_assign(&node, globalTransform);
                    for (unsigned int index = 0; index < node.mNumMeshes; ++index)
                    {
                        const auto meshIndex = node.mMeshes[index];
                        if (meshIndex >= meshTransforms.size())
                            throw std::invalid_argument("Animated model hierarchy references an unavailable mesh.");
                        if (meshTransformAssigned[meshIndex] &&
                            !ApproximatelyEqual(meshTransforms[meshIndex], globalTransform))
                            throw std::invalid_argument(
                                "Animated model instantiates one mesh under incompatible hierarchy transforms.");
                        meshTransforms[meshIndex] = globalTransform;
                        meshTransformAssigned[meshIndex] = true;
                    }
                    for (unsigned int child = 0; child < node.mNumChildren; ++child)
                        if (node.mChildren[child])
                            self(self, *node.mChildren[child], globalTransform);
                };
                collectTransforms(collectTransforms, *scene->mRootNode, aiMatrix4x4{});
                if (std::ranges::any_of(meshTransformAssigned, [](const bool assigned) { return !assigned; }))
                    throw std::invalid_argument("Animated model contains a mesh outside its scene hierarchy.");
            }
            if (useEmbeddedHierarchy)
            {
                if (!context.ResolveSubAssetId)
                    throw std::logic_error("Animated mesh importing requires generated-subasset identities.");
                std::unordered_map<std::string, aiMatrix4x4> inverseBindPoses;
                std::unordered_set<std::string> requiredNodes;
                for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
                {
                    const auto* mesh = scene->mMeshes[meshIndex];
                    if (!mesh)
                        continue;
                    for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
                    {
                        const auto* bone = mesh->mBones[boneIndex];
                        if (!bone || bone->mName.length == 0)
                            throw std::invalid_argument("Animated mesh contains an unnamed bone.");
                        const std::string name = bone->mName.C_Str();
                        auto inverseMeshTransform = meshTransforms[meshIndex];
                        if (std::abs(inverseMeshTransform.Determinant()) <= 1.0e-8F)
                            throw std::invalid_argument("Animated mesh hierarchy contains a singular transform.");
                        inverseMeshTransform.Inverse();
                        const auto normalizedInverseBind = bone->mOffsetMatrix * inverseMeshTransform;
                        if (const auto existing = inverseBindPoses.find(name);
                            existing != inverseBindPoses.end() &&
                            !ApproximatelyEqual(existing->second, normalizedInverseBind))
                            throw std::invalid_argument("Animated meshes disagree on a bone inverse bind pose.");
                        inverseBindPoses.insert_or_assign(name, normalizedInverseBind);
                        const auto* node = scene->mRootNode->FindNode(bone->mName);
                        if (!node)
                            throw std::invalid_argument("Animated mesh bone is absent from the scene hierarchy.");
                        while (node)
                        {
                            requiredNodes.insert(node->mName.C_Str());
                            node = node->mParent;
                        }
                    }
                }
                if (animationSource)
                {
                    for (unsigned int animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex)
                    {
                        const auto* animation = scene->mAnimations[animationIndex];
                        if (!animation)
                            continue;
                        for (unsigned int channelIndex = 0; channelIndex < animation->mNumChannels; ++channelIndex)
                        {
                            const auto* channel = animation->mChannels[channelIndex];
                            if (!channel || channel->mNodeName.length == 0)
                                continue;
                            const auto* node = scene->mRootNode->FindNode(channel->mNodeName);
                            if (!node)
                                throw std::invalid_argument(
                                    "Animation channel references a node absent from the scene hierarchy.");
                            while (node)
                            {
                                requiredNodes.insert(node->mName.C_Str());
                                node = node->mParent;
                            }
                        }
                    }
                }
                const auto appendBone = [&](const auto& self, const aiNode& node, const std::int32_t parent) -> void
                {
                    const std::string name = node.mName.C_Str();
                    std::int32_t nextParent = parent;
                    if (requiredNodes.contains(name))
                    {
                        if (skeletonBones.size() >= std::numeric_limits<std::uint16_t>::max())
                            throw std::overflow_error("Skeleton exceeds 16-bit skinning bone indices.");
                        const auto index = static_cast<std::uint16_t>(skeletonBones.size());
                        boneIndices.emplace(name, index);
                        SkeletonBone bone;
                        bone.Name = name;
                        bone.Parent = parent;
                        bone.BindPose = ConvertTransform(node.mTransformation);
                        skeletonBones.push_back(std::move(bone));
                        nextParent = index;
                    }
                    for (unsigned int child = 0; child < node.mNumChildren; ++child)
                        if (node.mChildren[child])
                            self(self, *node.mChildren[child], nextParent);
                };
                appendBone(appendBone, *scene->mRootNode, -1);
                std::vector<Matrix4> bindWorld(skeletonBones.size());
                for (std::size_t index = 0; index < skeletonBones.size(); ++index)
                {
                    auto& bone = skeletonBones[index];
                    const auto local =
                        Math::ComposeTransform(bone.BindPose.Translation, bone.BindPose.Rotation, bone.BindPose.Scale);
                    bindWorld[index] = bone.Parent < 0
                                           ? local
                                           : Math::Multiply(bindWorld[static_cast<std::size_t>(bone.Parent)], local);
                    const auto importedInverseBind = inverseBindPoses.find(bone.Name);
                    bone.InverseBindPose = importedInverseBind == inverseBindPoses.end()
                                               ? Math::Inverse(bindWorld[index])
                                               : ConvertMatrix(importedInverseBind->second);
                    if (!Math::IsFinite(bone.InverseBindPose))
                        throw std::invalid_argument("Animated skeleton produced a non-finite inverse bind pose.");
                }
                ValidateSkeleton(skeletonBones);
                skeletonId = context.ResolveSubAssetId("skeleton/default");
                output.SubAssets.push_back({skeletonId, SkeletonAsset::StaticType(), "skeleton/default", "Skeleton",
                                            SkeletonAsset::Encode(skeletonBones)});
                const SkeletonAsset embeddedSkeleton(skeletonBones);
                const auto profile = requestedRigProfile == "quadruped" ? RigProfileType::Quadruped
                                     : requestedRigProfile == "biped"   ? RigProfileType::Biped
                                                                        : RigProfileType::Humanoid;
                const auto embeddedRig = InferRigDefinition(embeddedSkeleton, profile, skinningMethod,
                                                            static_cast<std::uint8_t>(requestedInfluences));
                rigId = context.ResolveSubAssetId("rig/default");
                output.SubAssets.push_back({rigId, RigDefinitionAsset::StaticType(), "rig/default", "Rig",
                                            RigDefinitionAsset::Encode(embeddedRig)});

                for (unsigned int animationIndex = 0; animationIndex < scene->mNumAnimations; ++animationIndex)
                {
                    const auto* animation = scene->mAnimations[animationIndex];
                    if (!animation || animation->mDuration <= 0.0)
                        continue;
                    const auto ticksPerSecond = animation->mTicksPerSecond > 0.0 ? animation->mTicksPerSecond : 30.0;
                    const auto duration = static_cast<float>(animation->mDuration / ticksPerSecond);
                    std::vector<AnimationTrack> tracks;
                    for (unsigned int channelIndex = 0; channelIndex < animation->mNumChannels; ++channelIndex)
                    {
                        const auto* channel = animation->mChannels[channelIndex];
                        if (!channel)
                            continue;
                        const auto bone = boneIndices.find(channel->mNodeName.C_Str());
                        if (bone == boneIndices.end())
                            continue;
                        std::set<double> times;
                        for (unsigned int key = 0; key < channel->mNumPositionKeys; ++key)
                            times.insert(channel->mPositionKeys[key].mTime);
                        for (unsigned int key = 0; key < channel->mNumRotationKeys; ++key)
                            times.insert(channel->mRotationKeys[key].mTime);
                        for (unsigned int key = 0; key < channel->mNumScalingKeys; ++key)
                            times.insert(channel->mScalingKeys[key].mTime);
                        if (times.empty())
                            continue;
                        const auto sampleVector = [](const aiVectorKey* keys, const unsigned int count,
                                                     const double time, const aiVector3D fallback)
                        {
                            if (count == 0)
                                return fallback;
                            if (count == 1 || time <= keys[0].mTime)
                                return keys[0].mValue;
                            unsigned int upper = 1;
                            while (upper < count && keys[upper].mTime < time)
                                ++upper;
                            if (upper == count)
                                return keys[count - 1].mValue;
                            const auto alpha = static_cast<float>((time - keys[upper - 1].mTime) /
                                                                  (keys[upper].mTime - keys[upper - 1].mTime));
                            return keys[upper - 1].mValue + (keys[upper].mValue - keys[upper - 1].mValue) * alpha;
                        };
                        const auto sampleRotation = [](const aiQuatKey* keys, const unsigned int count,
                                                       const double time, const aiQuaternion fallback)
                        {
                            if (count == 0)
                                return fallback;
                            if (count == 1 || time <= keys[0].mTime)
                                return keys[0].mValue;
                            unsigned int upper = 1;
                            while (upper < count && keys[upper].mTime < time)
                                ++upper;
                            if (upper == count)
                                return keys[count - 1].mValue;
                            const auto alpha = static_cast<float>((time - keys[upper - 1].mTime) /
                                                                  (keys[upper].mTime - keys[upper - 1].mTime));
                            aiQuaternion interpolated;
                            aiQuaternion::Interpolate(interpolated, keys[upper - 1].mValue, keys[upper].mValue, alpha);
                            return interpolated.Normalize();
                        };
                        const auto& bind = skeletonBones[bone->second].BindPose;
                        AnimationTrack track;
                        track.Bone = bone->second;
                        for (const auto time : times)
                        {
                            const auto position =
                                sampleVector(channel->mPositionKeys, channel->mNumPositionKeys, time,
                                             {bind.Translation.X, bind.Translation.Y, bind.Translation.Z});
                            const auto scale = sampleVector(channel->mScalingKeys, channel->mNumScalingKeys, time,
                                                            {bind.Scale.X, bind.Scale.Y, bind.Scale.Z});
                            const auto rotation =
                                sampleRotation(channel->mRotationKeys, channel->mNumRotationKeys, time,
                                               {bind.Rotation.W, bind.Rotation.X, bind.Rotation.Y, bind.Rotation.Z});
                            track.Keys.push_back({static_cast<float>(time / ticksPerSecond),
                                                  {{position.x, position.y, position.z},
                                                   Math::Normalize({rotation.x, rotation.y, rotation.z, rotation.w}),
                                                   {scale.x, scale.y, scale.z}}});
                        }
                        tracks.push_back(std::move(track));
                    }
                    if (tracks.empty())
                        continue;
                    auto name = std::string(animation->mName.C_Str());
                    if (name.empty())
                        name = "Animation " + std::to_string(animationIndex + 1U);
                    if (requestedAnimationMotion == ImportedAnimationMotion::InPlaceHorizontal ||
                        requestedAnimationMotion == ImportedAnimationMotion::InPlace)
                    {
                        const auto baked = BakeImportedAnimationInPlace(
                            tracks, embeddedRig, requestedAnimationMotion == ImportedAnimationMotion::InPlace);
                        output.Diagnostics.push_back(
                            {baked ? AssetDiagnosticSeverity::Information : AssetDiagnosticSeverity::Warning,
                             context.RelativePath, 0, 0,
                             baked
                                 ? "Baked animation '" + name + "' in place using its semantic motion bone."
                                 : "Animation '" + name +
                                       "' requested in-place motion but has no animated semantic pelvis/root track."});
                    }
                    const auto compressionPreset = requestedCompression == "none" ? AnimationCompressionPreset::Disabled
                                                   : requestedCompression == "light" ? AnimationCompressionPreset::Light
                                                   : requestedCompression == "aggressive"
                                                       ? AnimationCompressionPreset::Aggressive
                                                       : AnimationCompressionPreset::Balanced;
                    auto compressed =
                        CompressAnimationTracks(tracks, AnimationCompressionSettingsForPreset(compressionPreset));
                    if (compressed.Statistics.CompressedKeyCount < compressed.Statistics.SourceKeyCount)
                    {
                        output.Diagnostics.push_back(
                            {AssetDiagnosticSeverity::Information, context.RelativePath, 0, 0,
                             "Compressed animation '" + name + "' from " +
                                 std::to_string(compressed.Statistics.SourceKeyCount) + " to " +
                                 std::to_string(compressed.Statistics.CompressedKeyCount) +
                                 " keys (maximum translation/rotation/scale error " +
                                 std::to_string(compressed.Statistics.MaximumTranslationError) + " / " +
                                 std::to_string(compressed.Statistics.MaximumRotationErrorDegrees) + " degrees / " +
                                 std::to_string(compressed.Statistics.MaximumScaleError) + ")."});
                    }
                    tracks = std::move(compressed.Tracks);
                    const auto key = "animation/" + name + "/" + std::to_string(animationIndex);
                    const auto clipId = context.ResolveSubAssetId(key);
                    output.SubAssets.push_back(
                        {clipId,
                         AnimationClipAsset::StaticType(),
                         key,
                         name,
                         AnimationClipAsset::Encode(skeletonId, duration, tracks, {},
                                                    requestedAnimationMotion == ImportedAnimationMotion::RootMotion),
                         {skeletonId}});
                    animationTakes.push_back({clipId, name, duration});
                }
                if (animationSource)
                {
                    if (animationTakes.empty())
                        throw std::invalid_argument(
                            "Animation Source import could not map any animation channels to the embedded skeleton.");
                    output.PrimaryType = AnimationSourceAsset::StaticType();
                    output.Bytes = AnimationSourceAsset::Encode({1, skeletonId, rigId, std::move(animationTakes)});
                    output.AssetDependencies.push_back(skeletonId);
                    output.AssetDependencies.push_back(rigId);
                    output.Diagnostics.push_back(
                        {AssetDiagnosticSeverity::Information, context.RelativePath, 0, 0,
                         "Published animation source with stable skeleton, rig, and clip subassets."});
                    collectSourceDependencies();
                    return output;
                }
                skinnedMeshId = context.ResolveSubAssetId("skinned-mesh/default");
            }

            struct OrderedMesh final
            {
                unsigned int Index = 0;
                std::uint32_t Lod = 0;
                ShaderPrimitiveTopology Topology = ShaderPrimitiveTopology::TriangleList;
            };
            std::vector<OrderedMesh> orderedMeshes;
            orderedMeshes.reserve(scene->mNumMeshes);
            for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
            {
                const auto* mesh = scene->mMeshes[meshIndex];
                if (!mesh)
                    continue;
                std::array<bool, 3> topologies{};
                bool triangulatedPolygon = false;
                for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
                {
                    const auto count = mesh->mFaces[faceIndex].mNumIndices;
                    if (count == 1)
                        topologies[2] = true;
                    else if (count == 2)
                        topologies[1] = true;
                    else if (count >= 3)
                    {
                        topologies[0] = true;
                        triangulatedPolygon |= count > 3;
                    }
                }
                const auto lod = MeshLodIndex(mesh->mName.C_Str());
                if (topologies[0])
                    orderedMeshes.push_back({meshIndex, lod, ShaderPrimitiveTopology::TriangleList});
                if (topologies[1])
                    orderedMeshes.push_back({meshIndex, lod, ShaderPrimitiveTopology::LineList});
                if (topologies[2])
                    orderedMeshes.push_back({meshIndex, lod, ShaderPrimitiveTopology::PointList});
                if (topologies[1])
                    output.Diagnostics.push_back({AssetDiagnosticSeverity::Information, context.RelativePath, 0, 0,
                                                  "Imported line primitives in mesh '" +
                                                      std::string(mesh->mName.C_Str()) + "' as a LineList submesh."});
                if (topologies[2])
                    output.Diagnostics.push_back({AssetDiagnosticSeverity::Information, context.RelativePath, 0, 0,
                                                  "Imported point primitives in mesh '" +
                                                      std::string(mesh->mName.C_Str()) + "' as a PointList submesh."});
                if (std::ranges::count(topologies, true) > 1U)
                    output.Diagnostics.push_back({AssetDiagnosticSeverity::Information, context.RelativePath, 0, 0,
                                                  "Partitioned mixed topology mesh '" +
                                                      std::string(mesh->mName.C_Str()) +
                                                      "' into independent triangle, line, and point submeshes."});
                if (triangulatedPolygon)
                    output.Diagnostics.push_back({AssetDiagnosticSeverity::Information, context.RelativePath, 0, 0,
                                                  "Triangulated polygon faces in mesh '" +
                                                      std::string(mesh->mName.C_Str()) + "' during import."});
            }
            if (orderedMeshes.empty())
                throw std::invalid_argument(
                    "Mesh source contains no supported triangle-list, line-list, or point-list primitives.");
            if (generateRig)
            {
                const auto removed = std::erase_if(orderedMeshes, [](const OrderedMesh& mesh)
                                                   { return mesh.Topology != ShaderPrimitiveTopology::TriangleList; });
                if (removed > 0)
                    output.Diagnostics.push_back(
                        {AssetDiagnosticSeverity::Warning, context.RelativePath, 0, 0,
                         "Auto-rigging ignored line and point submeshes because skin deformation requires triangles."});
                if (orderedMeshes.empty())
                    throw std::invalid_argument("Automatic rig generation requires at least one triangle surface.");
            }
            std::ranges::stable_sort(orderedMeshes, {}, &OrderedMesh::Lod);
            std::vector<std::uint32_t> lodValues;
            for (const auto ordered : orderedMeshes)
                if (lodValues.empty() || lodValues.back() != ordered.Lod)
                    lodValues.push_back(ordered.Lod);

            std::vector<MeshLod> lods;
            lods.reserve(lodValues.size());
            std::size_t orderedOffset = 0;
            for (std::size_t lodPosition = 0; lodPosition < lodValues.size(); ++lodPosition)
            {
                const auto firstSubmesh = static_cast<std::uint32_t>(submeshes.size());
                std::optional<MeshBounds> lodBounds;
                while (orderedOffset < orderedMeshes.size() &&
                       orderedMeshes[orderedOffset].Lod == lodValues[lodPosition])
                {
                    const auto orderedMesh = orderedMeshes[orderedOffset++];
                    const auto meshIndex = orderedMesh.Index;
                    const auto* mesh = scene->mMeshes[meshIndex];
                    if (vertices.size() > std::numeric_limits<std::uint32_t>::max() - mesh->mNumVertices)
                        throw std::overflow_error("Merged mesh vertex count exceeds 32-bit indices.");
                    const auto base = static_cast<std::uint32_t>(vertices.size());
                    const auto firstIndex = static_cast<std::uint32_t>(indices.size());
                    const auto firstVertex = vertices.size();
                    for (unsigned int vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex)
                    {
                        auto position = mesh->mVertices[vertexIndex];
                        auto normal = mesh->mNormals ? mesh->mNormals[vertexIndex] : aiVector3D{0.0F, 1.0F, 0.0F};
                        if (animated)
                        {
                            position = meshTransforms[meshIndex] * position;
                            aiMatrix3x3 normalTransform(meshTransforms[meshIndex]);
                            if (std::abs(normalTransform.Determinant()) <= 1.0e-8F)
                                throw std::invalid_argument("Animated mesh hierarchy contains a singular transform.");
                            normalTransform.Inverse().Transpose();
                            normal = normalTransform * normal;
                            if (normal.SquareLength() > 1.0e-12F)
                                normal.Normalize();
                        }
                        const auto uv = mesh->mTextureCoords[0] ? mesh->mTextureCoords[0][vertexIndex] : aiVector3D{};
                        const auto color = mesh->mColors[0] ? mesh->mColors[0][vertexIndex] : aiColor4D{};
                        vertices.push_back({{position.x, position.y, position.z},
                                            {normal.x, normal.y, normal.z},
                                            {uv.x, uv.y},
                                            mesh->mColors[0] ? Color{color.r, color.g, color.b, color.a} : Color{}});
                    }
                    if (useEmbeddedSkinning)
                    {
                        std::vector<std::vector<std::pair<std::uint16_t, float>>> weights(mesh->mNumVertices);
                        for (unsigned int meshBoneIndex = 0; meshBoneIndex < mesh->mNumBones; ++meshBoneIndex)
                        {
                            const auto* sourceBone = mesh->mBones[meshBoneIndex];
                            const auto bone = boneIndices.find(sourceBone->mName.C_Str());
                            if (bone == boneIndices.end())
                                throw std::invalid_argument("Skinned mesh references an unavailable skeleton bone.");
                            for (unsigned int weightIndex = 0; weightIndex < sourceBone->mNumWeights; ++weightIndex)
                            {
                                const auto& weight = sourceBone->mWeights[weightIndex];
                                if (weight.mVertexId >= mesh->mNumVertices || !std::isfinite(weight.mWeight) ||
                                    weight.mWeight < 0.0F)
                                    throw std::invalid_argument("Skinned mesh contains an invalid vertex weight.");
                                if (weight.mWeight > 0.0F)
                                    weights[weight.mVertexId].emplace_back(bone->second, weight.mWeight);
                            }
                        }
                        for (auto& vertexWeights : weights)
                        {
                            std::ranges::sort(vertexWeights,
                                              [](const auto& first, const auto& second)
                                              {
                                                  if (first.second != second.second)
                                                      return first.second > second.second;
                                                  return first.first < second.first;
                                              });
                            SkinVertexInfluence8 influence;
                            const auto count = std::min<std::size_t>(static_cast<std::size_t>(requestedInfluences),
                                                                     vertexWeights.size());
                            influence.Count = static_cast<std::uint8_t>(count);
                            float sum = 0.0F;
                            for (std::size_t influenceIndex = 0; influenceIndex < count; ++influenceIndex)
                            {
                                influence.Bones[influenceIndex] = vertexWeights[influenceIndex].first;
                                influence.Weights[influenceIndex] = vertexWeights[influenceIndex].second;
                                sum += influence.Weights[influenceIndex];
                            }
                            if (sum <= 1.0e-6F)
                            {
                                influence.Bones[0] = 0;
                                influence.Weights[0] = 1.0F;
                                influence.Count = 1;
                            }
                            else
                            {
                                for (std::size_t influenceIndex = 0; influenceIndex < count; ++influenceIndex)
                                    influence.Weights[influenceIndex] /= sum;
                            }
                            skinInfluences8.push_back(influence);
                        }
                    }
                    for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
                    {
                        const auto& face = mesh->mFaces[faceIndex];
                        const auto expectedIndices = orderedMesh.Topology == ShaderPrimitiveTopology::TriangleList ? 3U
                                                     : orderedMesh.Topology == ShaderPrimitiveTopology::LineList   ? 2U
                                                                                                                   : 1U;
                        if ((expectedIndices < 3U && face.mNumIndices != expectedIndices) ||
                            (expectedIndices == 3U && face.mNumIndices < 3U))
                            continue;
                        for (unsigned int corner = 0; corner < face.mNumIndices; ++corner)
                            if (face.mIndices[corner] >= mesh->mNumVertices)
                                throw std::invalid_argument("Mesh face references a vertex outside the mesh.");
                        if (expectedIndices == 3U)
                        {
                            for (unsigned int corner = 1; corner + 1 < face.mNumIndices; ++corner)
                            {
                                indices.push_back(base + face.mIndices[0]);
                                indices.push_back(base + face.mIndices[corner]);
                                indices.push_back(base + face.mIndices[corner + 1]);
                            }
                        }
                        else
                            for (unsigned int corner = 0; corner < expectedIndices; ++corner)
                                indices.push_back(base + face.mIndices[corner]);
                    }
                    const auto meshBounds = CalculateBounds(
                        std::span(vertices).subspan(firstVertex, static_cast<std::size_t>(mesh->mNumVertices)));
                    submeshes.push_back(
                        {firstIndex, static_cast<std::uint32_t>(indices.size()) - firstIndex,
                         std::min(mesh->mMaterialIndex, static_cast<unsigned int>(materialSlots.size() - 1U)),
                         meshBounds, orderedMesh.Topology});
                    lodBounds = lodBounds ? CombineBounds(*lodBounds, meshBounds) : meshBounds;
                }
                const auto threshold =
                    lodPosition + 1U == lodValues.size() ? 0.0F : std::pow(0.5F, static_cast<float>(lodPosition + 1U));
                lods.push_back(
                    {threshold, firstSubmesh, static_cast<std::uint32_t>(submeshes.size()) - firstSubmesh, *lodBounds});
            }
            GenerateTangents(vertices, indices, submeshes);
            const auto bounds = CalculateBounds(vertices);
            if (generateRig)
            {
                if (!context.ResolveSubAssetId)
                    throw std::logic_error("Auto-rig importing requires generated-subasset identities.");
                AutoRigRequest request;
                if (requestedRigProfile == "quadruped")
                    request.Profile = RigProfileType::Quadruped;
                else if (requestedRigProfile == "biped")
                    request.Profile = RigProfileType::Biped;
                else
                    request.Profile = RigProfileType::Humanoid;
                request.Skinning = requestedSkinning == "dualQuaternion" ? SkinningMethod::DualQuaternion
                                                                         : SkinningMethod::LinearBlend;
                request.MaximumInfluences = requestedInfluences == 8 ? 8 : 4;
                MeshAsset rigMesh(vertices, indices, submeshes, materialSlots, lods, bounds);
                auto generated = GenerateRig(rigMesh, request);
                skeletonId = context.ResolveSubAssetId("skeleton/default");
                skinnedMeshId = context.ResolveSubAssetId("skinned-mesh/default");
                rigId = context.ResolveSubAssetId("rig/default");
                skeletonBones = std::move(generated.Skeleton);
                skinInfluences8 = std::move(generated.Influences);
                skinningMethod = generated.Rig.Skinning;
                output.SubAssets.push_back({skeletonId, SkeletonAsset::StaticType(), "skeleton/default", "Skeleton",
                                            SkeletonAsset::Encode(skeletonBones)});
                output.SubAssets.push_back({rigId, RigDefinitionAsset::StaticType(), "rig/default", "Rig",
                                            RigDefinitionAsset::Encode(generated.Rig)});
                for (const auto& diagnostic : generated.Diagnostics)
                {
                    output.Diagnostics.push_back(
                        {diagnostic.Severity == RigDiagnosticSeverity::Error     ? AssetDiagnosticSeverity::Error
                         : diagnostic.Severity == RigDiagnosticSeverity::Warning ? AssetDiagnosticSeverity::Warning
                                                                                 : AssetDiagnosticSeverity::Information,
                         context.RelativePath, 0, 0, diagnostic.Code + ": " + diagnostic.Message});
                }
            }
            output.Bytes = MeshAsset::Encode(vertices, indices, submeshes, materialSlots, lods);
            if (animated)
            {
                if (skinInfluences8.size() != vertices.size())
                    throw std::logic_error("Skinned mesh import did not produce one influence set per vertex.");
                output.SubAssets.push_back(
                    {skinnedMeshId,
                     SkinnedMeshAsset::StaticType(),
                     "skinned-mesh/default",
                     "Skinned Mesh",
                     SkinnedMeshAsset::Encode(context.Asset, skeletonId, skinInfluences8, skinningMethod),
                     {context.Asset, skeletonId}});
            }
            output.Metadata.LocalBounds = AssetBounds{{bounds.Minimum.X, bounds.Minimum.Y, bounds.Minimum.Z},
                                                      {bounds.Maximum.X, bounds.Maximum.Y, bounds.Maximum.Z}};
            collectSourceDependencies();
            return output;
        };
        result.ImportOptions = {{"contentType",
                                 "Content",
                                 "General",
                                 AssetImportOptionKind::Choice,
                                 std::string("model"),
                                 {},
                                 {},
                                 1.0,
                                 {"model", "animation"}},
                                {"materialImport",
                                 "Materials",
                                 "Materials",
                                 AssetImportOptionKind::Choice,
                                 std::string("embedded"),
                                 {},
                                 {},
                                 1.0,
                                 {"embedded", "none"}},
                                {"rigSource",
                                 "Rig Source",
                                 "Rig",
                                 AssetImportOptionKind::Choice,
                                 std::string("embedded"),
                                 {},
                                 {},
                                 1.0,
                                 {"embedded", "generate", "none"}},
                                {"rigProfile",
                                 "Avatar Profile",
                                 "Rig",
                                 AssetImportOptionKind::Choice,
                                 std::string("humanoid"),
                                 {},
                                 {},
                                 1.0,
                                 {"humanoid", "biped", "quadruped"}},
                                {"maximumInfluences",
                                 "Maximum Influences",
                                 "Rig",
                                 AssetImportOptionKind::Choice,
                                 std::string("4"),
                                 {},
                                 {},
                                 1.0,
                                 {"4", "8"}},
                                {"skinningMethod",
                                 "Skinning Method",
                                 "Rig",
                                 AssetImportOptionKind::Choice,
                                 std::string("linearBlend"),
                                 {},
                                 {},
                                 1.0,
                                 {"linearBlend", "dualQuaternion"}},
                                {"animationCompression",
                                 "Animation Compression",
                                 "Animation",
                                 AssetImportOptionKind::Choice,
                                 std::string("balanced"),
                                 {},
                                 {},
                                 1.0,
                                 {"none", "light", "balanced", "aggressive"}},
                                {"animationMotion",
                                 "Animation Motion",
                                 "Animation",
                                 AssetImportOptionKind::Choice,
                                 std::string("rootMotion"),
                                 {},
                                 {},
                                 1.0,
                                 {"rootMotion", "authored", "inPlaceHorizontal", "inPlace"}}};
        return result;
    }

    AssetImporterRegistration Detail::CreateTexture2DAssetImporter(TextureImportSettings settings,
                                                                   TextureDecodeBackend backend)
    {
        settings = Detail::NormalizeTextureSettings(settings);
        AssetImporterRegistration result;
        result.Name = "Keire.Texture2D";
        result.Version = 5;
        result.Type = Texture2DAsset::StaticType();
        result.Extensions = {".png", ".jpg", ".jpeg", ".tga", ".bmp", ".hdr", ".exr"};
        result.Import = [settings, backend](const std::span<const std::byte> bytes)
        {
            auto effective = settings;
            if (stbi_is_hdr_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                        static_cast<int>(bytes.size())) != 0)
            {
                effective.Semantic = TextureSemantic::Environment;
                effective.ColorSpace = TextureColorSpace::Linear;
                effective.Mips = TextureMipPolicy::Generate;
                effective.EnvironmentLayout = TextureEnvironmentLayout::Equirectangular;
                effective.HighDynamicRange = true;
                effective.Sampler.AddressU = TextureAddressMode::Repeat;
                effective.Sampler.AddressV = TextureAddressMode::Clamp;
            }
            return Texture2DAsset::Encode(effective, ImportTexture(bytes, effective, backend));
        };
        result.ContextualImport = [settings, backend](const AssetImportContext& context,
                                                      const std::span<const std::byte> bytes) -> AssetImportOutput
        {
            auto effective = Detail::ReadTextureSettings(context.MetadataPath, settings);
            if (!context.ImportSettings.empty())
                effective = Detail::ApplyTextureImportSettings(effective, context.ImportSettings);
            const auto extension = Lowercase(context.SourcePath.extension().string());
            std::optional<DecodedFloatTexture> decoded;
            if (extension == ".exr")
            {
                if (!backend)
                    throw std::invalid_argument("OpenEXR decoding is unavailable in this asset-import process.");
                decoded = backend(bytes);
                effective.HighDynamicRange = effective.Semantic == TextureSemantic::Environment;
            }
            if (extension == ".hdr")
            {
                effective.Semantic = TextureSemantic::Environment;
                effective.ColorSpace = TextureColorSpace::Linear;
                effective.Mips = TextureMipPolicy::Generate;
                effective.HighDynamicRange = true;
                effective.Sampler.AddressU = TextureAddressMode::Repeat;
                effective.Sampler.AddressV = TextureAddressMode::Clamp;
                if (effective.EnvironmentLayout == TextureEnvironmentLayout::Auto)
                    effective.EnvironmentLayout = TextureEnvironmentLayout::Equirectangular;
            }
            else if (effective.Semantic == TextureSemantic::Environment &&
                     effective.EnvironmentLayout == TextureEnvironmentLayout::Auto)
            {
                int width = 0;
                int height = 0;
                int channels = 0;
                if (decoded)
                {
                    width = static_cast<int>(decoded->Width);
                    height = static_cast<int>(decoded->Height);
                }
                else if (!stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                                                static_cast<int>(bytes.size()), &width, &height, &channels))
                {
                    throw std::invalid_argument(std::string("Environment texture probe failed: ") +
                                                stbi_failure_reason());
                }
                if (width == height * 2)
                    effective.EnvironmentLayout = TextureEnvironmentLayout::Equirectangular;
                else if (width * 3 == height * 4)
                    effective.EnvironmentLayout = TextureEnvironmentLayout::HorizontalCross;
                else if (width * 4 == height * 3)
                    effective.EnvironmentLayout = TextureEnvironmentLayout::VerticalCross;
                else if (width == height * 6)
                    effective.EnvironmentLayout = TextureEnvironmentLayout::HorizontalStrip;
                else if (height == width * 6)
                    effective.EnvironmentLayout = TextureEnvironmentLayout::VerticalStrip;
                else
                    throw std::invalid_argument(
                        "Environment texture must be 2:1 equirectangular, a 4x3/3x4 cross, or a 6x1/1x6 strip.");
            }
            return {Texture2DAsset::Encode(effective, ImportTexture(bytes, effective, backend, std::move(decoded)))};
        };
        const auto choice = [](std::string key, std::string name, std::string group, std::string value,
                               std::vector<std::string> choices)
        {
            return AssetImportOptionDescriptor{std::move(key),
                                               std::move(name),
                                               std::move(group),
                                               AssetImportOptionKind::Choice,
                                               std::move(value),
                                               {},
                                               {},
                                               1.0,
                                               std::move(choices)};
        };
        result.ImportOptions = {
            choice("semantic", "Semantic", "Texture", "color", {"color", "data", "normal", "environment"}),
            choice("colorSpace", "Color Space", "Texture", "srgb", {"srgb", "linear"}),
            choice("mips", "Mip Maps", "Texture", "generate", {"generate", "none"}),
            choice("environmentLayout", "Environment Layout", "Environment", "auto",
                   {"auto", "equirectangular", "horizontalCross", "verticalCross", "horizontalStrip", "verticalStrip"}),
            {"maximumSize", "Maximum Size", "Texture", AssetImportOptionKind::Integer,
             std::int64_t{Detail::MaximumTextureDimension}, 1.0, static_cast<double>(Detail::MaximumTextureDimension),
             1.0},
            {"flipGreen", "Flip Green Channel", "Texture", AssetImportOptionKind::Boolean, false},
            choice("minFilter", "Min Filter", "Sampler", "linear", {"linear", "nearest"}),
            choice("magFilter", "Mag Filter", "Sampler", "linear", {"linear", "nearest"}),
            choice("mipFilter", "Mip Filter", "Sampler", "linear", {"linear", "nearest"}),
            choice("addressU", "Address U", "Sampler", "repeat", {"repeat", "clamp", "mirror"}),
            choice("addressV", "Address V", "Sampler", "repeat", {"repeat", "clamp", "mirror"}),
            choice("addressW", "Address W", "Sampler", "repeat", {"repeat", "clamp", "mirror"}),
            {"anisotropy", "Anisotropy", "Sampler", AssetImportOptionKind::Integer, std::int64_t{1}, 1.0, 16.0, 1.0}};
        result.NormalizeImportSettings = [settings](const AssetImportSettings& values)
        {
            const auto normalized = Detail::ApplyTextureImportSettings(settings, values);
            auto normalizedSettings = values;
            if (normalized.Semantic != TextureSemantic::Color)
                normalizedSettings["colorSpace"] = std::string("linear");
            return normalizedSettings;
        };
        result.SuggestImportSettings = [](const std::filesystem::path& path, const AssetImportSettings& defaults)
        {
            auto suggestedSettings = defaults;
            std::string stem = path.stem().string();
            std::ranges::transform(stem, stem.begin(),
                                   [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
            for (char& value : stem)
                if (!std::isalnum(static_cast<unsigned char>(value)))
                    value = ' ';

            const auto containsToken = [&stem](const std::string_view token)
            {
                std::size_t offset = 0;
                while (offset < stem.size())
                {
                    offset = stem.find_first_not_of(' ', offset);
                    if (offset == std::string::npos)
                        return false;
                    const auto end = stem.find(' ', offset);
                    if (stem.substr(offset, end - offset) == token)
                        return true;
                    offset = end == std::string::npos ? stem.size() : end + 1;
                }
                return false;
            };
            const bool normal =
                containsToken("normal") || containsToken("norm") || containsToken("nrm") || containsToken("nor");
            const bool data = containsToken("metallic") || containsToken("metal") || containsToken("roughness") ||
                              containsToken("rough") || containsToken("occlusion") || containsToken("ao") ||
                              containsToken("orm") || containsToken("rma") || containsToken("mra") ||
                              containsToken("mask") || containsToken("pbr");
            if (Lowercase(path.extension().string()) == ".hdr")
            {
                suggestedSettings["semantic"] = std::string("environment");
                suggestedSettings["colorSpace"] = std::string("linear");
                suggestedSettings["mips"] = std::string("none");
                suggestedSettings["addressV"] = std::string("clamp");
                suggestedSettings["environmentLayout"] = std::string("equirectangular");
            }
            else if (Lowercase(path.extension().string()) == ".exr")
            {
                suggestedSettings["colorSpace"] = std::string("linear");
            }
            else if (normal)
            {
                suggestedSettings["semantic"] = std::string("normal");
                suggestedSettings["colorSpace"] = std::string("linear");
            }
            else if (data)
            {
                suggestedSettings["semantic"] = std::string("data");
                suggestedSettings["colorSpace"] = std::string("linear");
            }
            return suggestedSettings;
        };
        result.RestoreCachedOutput = [](const std::span<const std::byte> bytes)
        {
            AssetImportOutput output;
            output.Bytes.assign(bytes.begin(), bytes.end());
            return output;
        };
        return result;
    }

    AssetImporterRegistration CreateTexture2DAssetImporter(TextureImportSettings settings)
    {
        return Detail::CreateTexture2DAssetImporter(std::move(settings), {});
    }

} // namespace Keire
