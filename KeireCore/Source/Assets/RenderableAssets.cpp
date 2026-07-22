#include "Keire/Assets/RenderingAssets.h"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
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
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::array<char, 8> MeshMagic{'K', 'E', 'I', 'R', 'E', 'M', 'S', 'H'};
        constexpr std::array<char, 8> TextureMagic{'K', 'E', 'I', 'R', 'E', 'T', 'E', 'X'};
        constexpr std::uint32_t MeshVersion = 3;
        constexpr std::uint32_t TextureVersion = 2;
        constexpr std::size_t MaximumMeshVertices = 16U * 1024U * 1024U;
        constexpr std::size_t MaximumMeshIndices = 48U * 1024U * 1024U;
        constexpr std::size_t MaximumMeshSubmeshes = 1024U * 1024U;
        constexpr std::size_t MaximumMeshMaterialSlots = 16U * 1024U;
        constexpr std::size_t MaximumMeshLods = 16U;
        constexpr std::size_t MaximumTextureDimension = 16U * 1024U;
        constexpr std::size_t MaximumTextureBytes = 1024U * 1024U * 1024U;

        template <typename Unsigned> void AppendUnsigned(std::vector<std::byte>& bytes, Unsigned value)
        {
            static_assert(std::is_unsigned_v<Unsigned>);
            for (std::size_t index = 0; index < sizeof(Unsigned); ++index)
            {
                bytes.push_back(std::byte(value & 0xffU));
                value >>= 8U;
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
                Unsigned result = 0;
                for (std::size_t index = 0; index < sizeof(Unsigned); ++index)
                    result |= static_cast<Unsigned>(std::to_integer<std::uint8_t>(m_Bytes[m_Offset++])) << (index * 8U);
                return result;
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
                    !std::isfinite(vertex.UV0.X) || !std::isfinite(vertex.UV0.Y) ||
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
                indices.size() > MaximumMeshIndices || indices.size() % 3 != 0)
                throw std::invalid_argument("Mesh vertex/index counts are empty, excessive, or not triangles.");
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
                if (submesh.IndexCount == 0 || submesh.IndexCount % 3U != 0 || end > indices.size() ||
                    submesh.MaterialSlot >= materialSlots.size() || !Math::IsFinite(submesh.Bounds.Minimum) ||
                    !Math::IsFinite(submesh.Bounds.Maximum))
                    throw std::invalid_argument("Mesh submesh range, material slot, or bounds are invalid.");
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

        [[nodiscard]] std::pair<std::vector<MeshVertex>, std::vector<std::uint32_t>> CubeGeometry(const Color color)
        {
            struct Face final
            {
                Vector3 Normal;
                Vector4 Tangent;
                std::array<Vector3, 4> Positions;
            };
            constexpr std::array faces{
                Face{{0.0F, 0.0F, 1.0F},
                     {1.0F, 0.0F, 0.0F, 1.0F},
                     {{{-0.5F, -0.5F, 0.5F}, {0.5F, -0.5F, 0.5F}, {0.5F, 0.5F, 0.5F}, {-0.5F, 0.5F, 0.5F}}}},
                Face{{0.0F, 0.0F, -1.0F},
                     {-1.0F, 0.0F, 0.0F, 1.0F},
                     {{{0.5F, -0.5F, -0.5F}, {-0.5F, -0.5F, -0.5F}, {-0.5F, 0.5F, -0.5F}, {0.5F, 0.5F, -0.5F}}}},
                Face{{1.0F, 0.0F, 0.0F},
                     {0.0F, 0.0F, -1.0F, 1.0F},
                     {{{0.5F, -0.5F, 0.5F}, {0.5F, -0.5F, -0.5F}, {0.5F, 0.5F, -0.5F}, {0.5F, 0.5F, 0.5F}}}},
                Face{{-1.0F, 0.0F, 0.0F},
                     {0.0F, 0.0F, 1.0F, 1.0F},
                     {{{-0.5F, -0.5F, -0.5F}, {-0.5F, -0.5F, 0.5F}, {-0.5F, 0.5F, 0.5F}, {-0.5F, 0.5F, -0.5F}}}},
                Face{{0.0F, 1.0F, 0.0F},
                     {1.0F, 0.0F, 0.0F, 1.0F},
                     {{{-0.5F, 0.5F, 0.5F}, {0.5F, 0.5F, 0.5F}, {0.5F, 0.5F, -0.5F}, {-0.5F, 0.5F, -0.5F}}}},
                Face{{0.0F, -1.0F, 0.0F},
                     {1.0F, 0.0F, 0.0F, -1.0F},
                     {{{-0.5F, -0.5F, -0.5F}, {0.5F, -0.5F, -0.5F}, {0.5F, -0.5F, 0.5F}, {-0.5F, -0.5F, 0.5F}}}}};
            constexpr std::array uvs{Vector2{0.0F, 1.0F}, Vector2{1.0F, 1.0F}, Vector2{1.0F, 0.0F},
                                     Vector2{0.0F, 0.0F}};
            std::vector<MeshVertex> vertices;
            std::vector<std::uint32_t> indices;
            vertices.reserve(24);
            indices.reserve(36);
            for (const auto& face : faces)
            {
                const auto base = static_cast<std::uint32_t>(vertices.size());
                for (std::size_t corner = 0; corner < face.Positions.size(); ++corner)
                    vertices.push_back({face.Positions[corner], face.Normal, uvs[corner], color, face.Tangent});
                indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            }
            return {std::move(vertices), std::move(indices)};
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

        void GenerateTangents(std::vector<MeshVertex>& vertices, const std::span<const std::uint32_t> indices)
        {
            std::vector<Vector3> tangents(vertices.size());
            std::vector<Vector3> bitangents(vertices.size());
            for (std::size_t triangle = 0; triangle < indices.size(); triangle += 3)
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

        [[nodiscard]] TextureImportSettings NormalizeTextureSettings(TextureImportSettings settings)
        {
            const auto validSemantic = settings.Semantic == TextureSemantic::Color ||
                                       settings.Semantic == TextureSemantic::Data ||
                                       settings.Semantic == TextureSemantic::Normal;
            const auto validColorSpace =
                settings.ColorSpace == TextureColorSpace::Linear || settings.ColorSpace == TextureColorSpace::Srgb;
            const auto validMipPolicy =
                settings.Mips == TextureMipPolicy::None || settings.Mips == TextureMipPolicy::Generate;
            const auto validFilter = [](const TextureFilter filter)
            { return filter == TextureFilter::Nearest || filter == TextureFilter::Linear; };
            const auto validAddress = [](const TextureAddressMode mode)
            {
                return mode == TextureAddressMode::Repeat || mode == TextureAddressMode::Clamp ||
                       mode == TextureAddressMode::Mirror;
            };
            if (!validSemantic || !validColorSpace || !validMipPolicy || !validFilter(settings.Sampler.Minimum) ||
                !validFilter(settings.Sampler.Magnification) || !validFilter(settings.Sampler.Mip) ||
                !validAddress(settings.Sampler.AddressU) || !validAddress(settings.Sampler.AddressV) ||
                !validAddress(settings.Sampler.AddressW))
                throw std::invalid_argument("Texture import settings contain an invalid enum value.");
            if (settings.MaximumSize == 0 || settings.MaximumSize > MaximumTextureDimension ||
                settings.Sampler.Anisotropy == 0 || settings.Sampler.Anisotropy > 16)
                throw std::invalid_argument("Texture import settings contain invalid size or anisotropy limits.");
            if (settings.Semantic != TextureSemantic::Color)
                settings.ColorSpace = TextureColorSpace::Linear;
            return settings;
        }

        [[nodiscard]] TextureImportSettings ApplyTextureImportSettings(TextureImportSettings settings,
                                                                       const AssetImportSettings& values)
        {
            const auto choice = [&](const std::string_view key, const std::string fallback)
            {
                const auto found = values.find(key);
                return found == values.end() ? fallback : std::get<std::string>(found->second);
            };
            const auto semantic = choice("semantic", "color");
            settings.Semantic = semantic == "normal" ? TextureSemantic::Normal
                                : semantic == "data" ? TextureSemantic::Data
                                                     : TextureSemantic::Color;
            settings.ColorSpace =
                choice("colorSpace", "srgb") == "linear" ? TextureColorSpace::Linear : TextureColorSpace::Srgb;
            settings.Mips = choice("mips", "generate") == "none" ? TextureMipPolicy::None : TextureMipPolicy::Generate;
            if (const auto found = values.find("maximumSize"); found != values.end())
                settings.MaximumSize = static_cast<std::uint32_t>(std::get<std::int64_t>(found->second));
            if (const auto found = values.find("flipGreen"); found != values.end())
                settings.FlipGreen = std::get<bool>(found->second);
            const auto filter = [&](const std::string_view key, const TextureFilter fallback)
            {
                return choice(key, fallback == TextureFilter::Nearest ? "nearest" : "linear") == "nearest"
                           ? TextureFilter::Nearest
                           : TextureFilter::Linear;
            };
            settings.Sampler.Minimum = filter("minFilter", settings.Sampler.Minimum);
            settings.Sampler.Magnification = filter("magFilter", settings.Sampler.Magnification);
            settings.Sampler.Mip = filter("mipFilter", settings.Sampler.Mip);
            const auto address = [&](const std::string_view key, const TextureAddressMode fallback)
            {
                const auto fallbackText = fallback == TextureAddressMode::Clamp    ? "clamp"
                                          : fallback == TextureAddressMode::Mirror ? "mirror"
                                                                                   : "repeat";
                const auto value = choice(key, fallbackText);
                return value == "clamp"    ? TextureAddressMode::Clamp
                       : value == "mirror" ? TextureAddressMode::Mirror
                                           : TextureAddressMode::Repeat;
            };
            settings.Sampler.AddressU = address("addressU", settings.Sampler.AddressU);
            settings.Sampler.AddressV = address("addressV", settings.Sampler.AddressV);
            settings.Sampler.AddressW = address("addressW", settings.Sampler.AddressW);
            if (const auto found = values.find("anisotropy"); found != values.end())
                settings.Sampler.Anisotropy = static_cast<std::uint8_t>(std::get<std::int64_t>(found->second));
            return NormalizeTextureSettings(settings);
        }

        [[nodiscard]] TextureImportSettings ReadTextureSettings(const std::filesystem::path& metadataPath,
                                                                TextureImportSettings settings)
        {
            if (metadataPath.empty() || !std::filesystem::is_regular_file(metadataPath))
                return NormalizeTextureSettings(settings);
            std::ifstream stream(metadataPath, std::ios::binary);
            Json metadata;
            stream >> metadata;
            if (!stream || !metadata.is_object())
                throw std::invalid_argument("Texture metadata is not valid JSON.");
            const auto found = metadata.find("textureImportSettings");
            if (found == metadata.end())
                return NormalizeTextureSettings(settings);
            if (!found->is_object())
                throw std::invalid_argument("textureImportSettings must be an object.");
            const auto& values = *found;
            if (values.contains("semantic"))
            {
                const auto semantic = values.at("semantic").get<std::string>();
                if (semantic == "color")
                    settings.Semantic = TextureSemantic::Color;
                else if (semantic == "data")
                    settings.Semantic = TextureSemantic::Data;
                else if (semantic == "normal")
                    settings.Semantic = TextureSemantic::Normal;
                else
                    throw std::invalid_argument("Texture semantic must be color, data, or normal.");
            }
            if (values.contains("colorSpace"))
            {
                const auto colorSpace = values.at("colorSpace").get<std::string>();
                if (colorSpace == "linear")
                    settings.ColorSpace = TextureColorSpace::Linear;
                else if (colorSpace == "srgb")
                    settings.ColorSpace = TextureColorSpace::Srgb;
                else
                    throw std::invalid_argument("Texture colorSpace must be linear or srgb.");
            }
            if (values.contains("mips"))
            {
                const auto mips = values.at("mips").get<std::string>();
                if (mips == "none")
                    settings.Mips = TextureMipPolicy::None;
                else if (mips == "generate")
                    settings.Mips = TextureMipPolicy::Generate;
                else
                    throw std::invalid_argument("Texture mips must be none or generate.");
            }
            settings.MaximumSize = values.value("maximumSize", settings.MaximumSize);
            settings.FlipGreen = values.value("flipGreen", settings.FlipGreen);
            if (const auto sampler = values.find("sampler"); sampler != values.end())
            {
                if (!sampler->is_object())
                    throw std::invalid_argument("Texture sampler settings must be an object.");
                const auto filter = [](const std::string& value)
                {
                    if (value == "nearest")
                        return TextureFilter::Nearest;
                    if (value == "linear")
                        return TextureFilter::Linear;
                    throw std::invalid_argument("Texture filter must be nearest or linear.");
                };
                const auto address = [](const std::string& value)
                {
                    if (value == "repeat")
                        return TextureAddressMode::Repeat;
                    if (value == "clamp")
                        return TextureAddressMode::Clamp;
                    if (value == "mirror")
                        return TextureAddressMode::Mirror;
                    throw std::invalid_argument("Texture address mode must be repeat, clamp, or mirror.");
                };
                if (sampler->contains("min"))
                    settings.Sampler.Minimum = filter(sampler->at("min").get<std::string>());
                if (sampler->contains("mag"))
                    settings.Sampler.Magnification = filter(sampler->at("mag").get<std::string>());
                if (sampler->contains("mip"))
                    settings.Sampler.Mip = filter(sampler->at("mip").get<std::string>());
                if (sampler->contains("addressU"))
                    settings.Sampler.AddressU = address(sampler->at("addressU").get<std::string>());
                if (sampler->contains("addressV"))
                    settings.Sampler.AddressV = address(sampler->at("addressV").get<std::string>());
                if (sampler->contains("addressW"))
                    settings.Sampler.AddressW = address(sampler->at("addressW").get<std::string>());
                if (sampler->contains("anisotropy"))
                {
                    const auto anisotropy = sampler->at("anisotropy").get<unsigned int>();
                    if (anisotropy > std::numeric_limits<std::uint8_t>::max())
                        throw std::invalid_argument("Texture anisotropy exceeds its encoded range.");
                    settings.Sampler.Anisotropy = static_cast<std::uint8_t>(anisotropy);
                }
            }
            return NormalizeTextureSettings(settings);
        }

        void ValidateMip(const TextureMipLevel& mip)
        {
            if (mip.Width == 0 || mip.Height == 0 || mip.Width > MaximumTextureDimension ||
                mip.Height > MaximumTextureDimension ||
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
                        std::uint32_t samples = 0;
                        for (std::uint32_t oy = 0; oy < 2; ++oy)
                        {
                            const auto sourceY = std::min(y * 2U + oy, source.Height - 1U);
                            for (std::uint32_t ox = 0; ox < 2; ++ox)
                            {
                                const auto sourceX = std::min(x * 2U + ox, source.Width - 1U);
                                const auto sourceIndex =
                                    (static_cast<std::size_t>(sourceY) * source.Width + sourceX) * 4U;
                                normal.X += std::to_integer<std::uint8_t>(source.Pixels[sourceIndex]) / 127.5F - 1.0F;
                                normal.Y +=
                                    std::to_integer<std::uint8_t>(source.Pixels[sourceIndex + 1]) / 127.5F - 1.0F;
                                normal.Z +=
                                    std::to_integer<std::uint8_t>(source.Pixels[sourceIndex + 2]) / 127.5F - 1.0F;
                                ++samples;
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

        [[nodiscard]] std::vector<TextureMipLevel> ImportTexture(const std::span<const std::byte> bytes,
                                                                 const TextureImportSettings& settings)
        {
            if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                throw std::invalid_argument("Texture source is empty or exceeds the decoder limit.");
            int width = 0;
            int height = 0;
            int channels = 0;
            std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
                stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()), static_cast<int>(bytes.size()),
                                      &width, &height, &channels, 4),
                stbi_image_free);
            if (!pixels || width <= 0 || height <= 0 || width > static_cast<int>(MaximumTextureDimension) ||
                height > static_cast<int>(MaximumTextureDimension))
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
        auto [vertices, indices] = CubeGeometry(mesh == BuiltinMesh::Error ? Color{1.0F, 0.0F, 1.0F, 1.0F} : Color{});
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

    std::size_t MeshAsset::ResidentBytes() const noexcept
    {
        auto result = sizeof(*this) + m_Vertices.size() * sizeof(MeshVertex) +
                      m_Indices.size() * sizeof(std::uint32_t) + m_Submeshes.size() * sizeof(MeshSubmesh) +
                      m_Lods.size() * sizeof(MeshLod) + m_MaterialSlots.size() * sizeof(MeshMaterialSlot);
        for (const auto& slot : m_MaterialSlots)
            result += slot.Name.size();
        return result;
    }

    Ref<MeshAsset> MeshAsset::Cube() { return CreateRef<MeshAsset>(BuiltinMesh::Cube); }
    Ref<MeshAsset> MeshAsset::Error() { return CreateRef<MeshAsset>(BuiltinMesh::Error); }

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
        result.reserve(48U + vertices.size() * 64U + indices.size() * sizeof(std::uint32_t));
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
                  vertex.Tangent.Z, vertex.Tangent.W})
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
            indexCount > MaximumMeshIndices || indexCount % 3U != 0)
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
        const auto vertexSize = version == 1 ? 48U : 64U;
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
        }
        std::vector<std::uint32_t> indices(static_cast<std::size_t>(indexCount));
        for (auto& index : indices)
            index = reader.UnsignedValue<std::uint32_t>();
        if (version == 1)
            GenerateTangents(vertices, indices);
        if (version < 3)
            std::tie(submeshes, materialSlots, lods) = DefaultMeshStructure(indices, bounds);
        return CreateRef<MeshAsset>(std::move(vertices), std::move(indices), std::move(submeshes),
                                    std::move(materialSlots), std::move(lods), bounds);
    }

    Texture2DAsset::Texture2DAsset(TextureImportSettings settings, std::vector<TextureMipLevel> mips)
        : m_Settings(NormalizeTextureSettings(settings)), m_Mips(std::move(mips))
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

    std::size_t Texture2DAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this);
        for (const auto& mip : m_Mips)
            result += sizeof(TextureMipLevel) + mip.Pixels.size();
        return result;
    }

    std::vector<std::byte> Texture2DAsset::Encode(const TextureImportSettings& requested,
                                                  const std::span<const TextureMipLevel> mips)
    {
        const auto settings = NormalizeTextureSettings(requested);
        Texture2DAsset validation(settings, {mips.begin(), mips.end()});
        std::vector<std::byte> result;
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
              static_cast<std::uint8_t>(settings.FlipGreen)})
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
        if (version != 1 && version != TextureVersion)
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

    Ref<Texture2DAsset> Texture2DAsset::Checkerboard()
    {
        TextureMipLevel mip{2,
                            2,
                            {std::byte{255}, std::byte{0}, std::byte{255}, std::byte{255}, std::byte{32}, std::byte{32},
                             std::byte{32}, std::byte{255}, std::byte{32}, std::byte{32}, std::byte{32}, std::byte{255},
                             std::byte{255}, std::byte{0}, std::byte{255}, std::byte{255}}};
        TextureImportSettings settings;
        settings.Mips = TextureMipPolicy::None;
        return CreateRef<Texture2DAsset>(settings, std::vector<TextureMipLevel>{std::move(mip)});
    }

    AssetImporterRegistration CreateMeshAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.Mesh";
        result.Version = 3;
        result.Type = MeshAsset::StaticType();
        result.Extensions = {".obj", ".fbx", ".gltf", ".glb", ".keiremesh"};
        result.ContextualImport = [](const AssetImportContext& context,
                                     const std::span<const std::byte> bytes) -> AssetImportOutput
        {
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
            importer.SetPropertyBool(AI_CONFIG_PP_PTV_KEEP_HIERARCHY, true);
            constexpr unsigned int flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                                           aiProcess_GenSmoothNormals | aiProcess_PreTransformVertices |
                                           aiProcess_CalcTangentSpace | aiProcess_ImproveCacheLocality |
                                           aiProcess_SortByPType | aiProcess_ValidateDataStructure |
                                           aiProcess_MakeLeftHanded | aiProcess_FlipUVs | aiProcess_FlipWindingOrder;
            auto extension = context.SourcePath.extension().string();
            if (!extension.empty() && extension.front() == '.')
                extension.erase(extension.begin());
            const auto* scene = importer.ReadFileFromMemory(bytes.data(), bytes.size(), flags, extension.c_str());
            if (!scene)
                throw std::invalid_argument(std::string("Mesh import failed: ") + importer.GetErrorString());
            if (scene->mNumAnimations != 0)
                throw std::invalid_argument("Animated meshes are not supported by the static mesh importer.");
            std::vector<MeshVertex> vertices;
            std::vector<std::uint32_t> indices;
            std::vector<MeshSubmesh> submeshes;
            std::vector<MeshMaterialSlot> materialSlots;
            materialSlots.reserve(std::max(scene->mNumMaterials, 1U));
            for (unsigned int materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex)
            {
                aiString materialName;
                if (scene->mMaterials[materialIndex]->Get(AI_MATKEY_NAME, materialName) != aiReturn_SUCCESS ||
                    materialName.length == 0)
                    materialName = aiString(("Material " + std::to_string(materialIndex + 1U)).c_str());
                materialSlots.push_back({materialName.C_Str(), {}});
            }
            if (materialSlots.empty())
                materialSlots.push_back({"Default", {}});
            struct OrderedMesh final
            {
                unsigned int Index = 0;
                std::uint32_t Lod = 0;
            };
            std::vector<OrderedMesh> orderedMeshes;
            orderedMeshes.reserve(scene->mNumMeshes);
            for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
            {
                const auto* mesh = scene->mMeshes[meshIndex];
                orderedMeshes.push_back({meshIndex, mesh ? MeshLodIndex(mesh->mName.C_Str()) : 0U});
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
                    const auto* mesh = scene->mMeshes[orderedMeshes[orderedOffset++].Index];
                    if (!mesh || mesh->mNumBones != 0 || (mesh->mPrimitiveTypes & ~aiPrimitiveType_TRIANGLE) != 0)
                        throw std::invalid_argument("Static mesh import rejects skinning and non-triangle primitives.");
                    if (vertices.size() > std::numeric_limits<std::uint32_t>::max() - mesh->mNumVertices)
                        throw std::overflow_error("Merged mesh vertex count exceeds 32-bit indices.");
                    const auto base = static_cast<std::uint32_t>(vertices.size());
                    const auto firstIndex = static_cast<std::uint32_t>(indices.size());
                    const auto firstVertex = vertices.size();
                    for (unsigned int vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex)
                    {
                        const auto position = mesh->mVertices[vertexIndex];
                        const auto normal = mesh->mNormals ? mesh->mNormals[vertexIndex] : aiVector3D{0.0F, 1.0F, 0.0F};
                        const auto uv = mesh->mTextureCoords[0] ? mesh->mTextureCoords[0][vertexIndex] : aiVector3D{};
                        const auto color = mesh->mColors[0] ? mesh->mColors[0][vertexIndex] : aiColor4D{};
                        vertices.push_back({{position.x, position.y, position.z},
                                            {normal.x, normal.y, normal.z},
                                            {uv.x, uv.y},
                                            mesh->mColors[0] ? Color{color.r, color.g, color.b, color.a} : Color{}});
                    }
                    for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
                    {
                        const auto& face = mesh->mFaces[faceIndex];
                        if (face.mNumIndices != 3)
                            throw std::invalid_argument("Assimp did not triangulate a mesh face.");
                        for (unsigned int corner = 0; corner < 3; ++corner)
                            indices.push_back(base + face.mIndices[corner]);
                    }
                    const auto meshBounds = CalculateBounds(
                        std::span(vertices).subspan(firstVertex, static_cast<std::size_t>(mesh->mNumVertices)));
                    submeshes.push_back(
                        {firstIndex, static_cast<std::uint32_t>(indices.size()) - firstIndex,
                         std::min(mesh->mMaterialIndex, static_cast<unsigned int>(materialSlots.size() - 1U)),
                         meshBounds});
                    lodBounds = lodBounds ? CombineBounds(*lodBounds, meshBounds) : meshBounds;
                }
                const auto threshold =
                    lodPosition + 1U == lodValues.size() ? 0.0F : std::pow(0.5F, static_cast<float>(lodPosition + 1U));
                lods.push_back(
                    {threshold, firstSubmesh, static_cast<std::uint32_t>(submeshes.size()) - firstSubmesh, *lodBounds});
            }
            GenerateTangents(vertices, indices);
            const auto bounds = CalculateBounds(vertices);
            AssetImportOutput output;
            output.Bytes = MeshAsset::Encode(vertices, indices, submeshes, materialSlots, lods);
            output.Metadata.LocalBounds = AssetBounds{{bounds.Minimum.X, bounds.Minimum.Y, bounds.Minimum.Z},
                                                      {bounds.Maximum.X, bounds.Maximum.Y, bounds.Maximum.Z}};
            return output;
        };
        result.RestoreCachedOutput = [](const std::span<const std::byte> bytes)
        {
            const auto mesh = MeshAsset::Decode(bytes);
            AssetImportOutput output;
            output.Bytes.assign(bytes.begin(), bytes.end());
            const auto& bounds = mesh->Bounds();
            output.Metadata.LocalBounds = AssetBounds{{bounds.Minimum.X, bounds.Minimum.Y, bounds.Minimum.Z},
                                                      {bounds.Maximum.X, bounds.Maximum.Y, bounds.Maximum.Z}};
            return output;
        };
        return result;
    }

    AssetImporterRegistration CreateTexture2DAssetImporter(TextureImportSettings settings)
    {
        settings = NormalizeTextureSettings(settings);
        AssetImporterRegistration result;
        result.Name = "Keire.Texture2D";
        result.Version = 2;
        result.Type = Texture2DAsset::StaticType();
        result.Extensions = {".png", ".jpg", ".jpeg", ".tga", ".bmp"};
        result.Import = [settings](const std::span<const std::byte> bytes)
        { return Texture2DAsset::Encode(settings, ImportTexture(bytes, settings)); };
        result.ContextualImport = [settings](const AssetImportContext& context,
                                             const std::span<const std::byte> bytes) -> AssetImportOutput
        {
            auto effective = ReadTextureSettings(context.MetadataPath, settings);
            if (!context.ImportSettings.empty())
                effective = ApplyTextureImportSettings(effective, context.ImportSettings);
            return {Texture2DAsset::Encode(effective, ImportTexture(bytes, effective))};
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
            choice("semantic", "Semantic", "Texture", "color", {"color", "data", "normal"}),
            choice("colorSpace", "Color Space", "Texture", "srgb", {"srgb", "linear"}),
            choice("mips", "Mip Maps", "Texture", "generate", {"generate", "none"}),
            {"maximumSize", "Maximum Size", "Texture", AssetImportOptionKind::Integer,
             std::int64_t{MaximumTextureDimension}, 1.0, static_cast<double>(MaximumTextureDimension), 1.0},
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
            const auto normalized = ApplyTextureImportSettings(settings, values);
            auto result = values;
            if (normalized.Semantic != TextureSemantic::Color)
                result["colorSpace"] = std::string("linear");
            return result;
        };
        result.SuggestImportSettings = [](const std::filesystem::path& path, const AssetImportSettings& defaults)
        {
            auto result = defaults;
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
            if (normal)
            {
                result["semantic"] = std::string("normal");
                result["colorSpace"] = std::string("linear");
            }
            else if (data)
            {
                result["semantic"] = std::string("data");
                result["colorSpace"] = std::string("linear");
            }
            return result;
        };
        result.RestoreCachedOutput = [](const std::span<const std::byte> bytes)
        {
            AssetImportOutput output;
            output.Bytes.assign(bytes.begin(), bytes.end());
            return output;
        };
        return result;
    }

    AssetDecoderRegistration CreateMeshAssetDecoder()
    {
        return {MeshAsset::StaticType(), MeshAsset::Error(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return MeshAsset::Decode(bytes); }};
    }

    AssetDecoderRegistration CreateTexture2DAssetDecoder()
    {
        return {Texture2DAsset::StaticType(), Texture2DAsset::Checkerboard(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return Texture2DAsset::Decode(bytes); }};
    }
} // namespace Keire
