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

} // namespace Keire
