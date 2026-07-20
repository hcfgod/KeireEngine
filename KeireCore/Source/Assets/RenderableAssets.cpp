#include "Keire/Assets/RenderingAssets.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <nlohmann/json.hpp>
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::array<char, 8> MeshMagic{'K', 'E', 'I', 'R', 'E', 'M', 'S', 'H'};
        constexpr std::array<char, 8> TextureMagic{'K', 'E', 'I', 'R', 'E', 'T', 'E', 'X'};
        constexpr std::uint32_t MeshVersion = 1;
        constexpr std::uint32_t TextureVersion = 1;
        constexpr std::size_t MaximumMeshVertices = 16U * 1024U * 1024U;
        constexpr std::size_t MaximumMeshIndices = 48U * 1024U * 1024U;
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
                                            vertex.VertexColor.Alpha}))
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

        [[nodiscard]] std::pair<std::vector<MeshVertex>, std::vector<std::uint32_t>> CubeGeometry(const Color color)
        {
            constexpr std::array positions = {Vector3{-0.5F, -0.5F, -0.5F}, Vector3{0.5F, -0.5F, -0.5F},
                                              Vector3{0.5F, 0.5F, -0.5F},   Vector3{-0.5F, 0.5F, -0.5F},
                                              Vector3{-0.5F, -0.5F, 0.5F},  Vector3{0.5F, -0.5F, 0.5F},
                                              Vector3{0.5F, 0.5F, 0.5F},    Vector3{-0.5F, 0.5F, 0.5F}};
            constexpr std::array<std::uint32_t, 36> indices = {0, 2, 1, 0, 3, 2, 1, 2, 6, 1, 6, 5, 5, 6, 7, 5, 7, 4,
                                                               4, 7, 3, 4, 3, 0, 3, 7, 6, 3, 6, 2, 4, 0, 1, 4, 1, 5};
            std::vector<MeshVertex> vertices;
            vertices.reserve(positions.size());
            for (const auto position : positions)
            {
                const auto inverseLength =
                    1.0F / std::sqrt(position.X * position.X + position.Y * position.Y + position.Z * position.Z);
                vertices.push_back(
                    {position,
                     {position.X * inverseLength, position.Y * inverseLength, position.Z * inverseLength},
                     {},
                     color});
            }
            return {std::move(vertices), {indices.begin(), indices.end()}};
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

        [[nodiscard]] TextureMipLevel Downsample(const TextureMipLevel& source)
        {
            TextureMipLevel result;
            result.Width = std::max(source.Width / 2U, 1U);
            result.Height = std::max(source.Height / 2U, 1U);
            result.Pixels.resize(static_cast<std::size_t>(result.Width) * result.Height * 4U);
            for (std::uint32_t y = 0; y < result.Height; ++y)
            {
                for (std::uint32_t x = 0; x < result.Width; ++x)
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
            while (base.Width > settings.MaximumSize || base.Height > settings.MaximumSize)
                base = Downsample(base);
            std::vector<TextureMipLevel> result{std::move(base)};
            if (settings.Mips == TextureMipPolicy::Generate)
            {
                while (result.back().Width > 1 || result.back().Height > 1)
                    result.push_back(Downsample(result.back()));
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
    }

    MeshAsset::MeshAsset(std::vector<MeshVertex> vertices, std::vector<std::uint32_t> indices, const MeshBounds bounds)
        : m_Mesh(BuiltinMesh::Error), m_Vertices(std::move(vertices)), m_Indices(std::move(indices)), m_Bounds(bounds)
    {
        ValidateMesh(m_Vertices, m_Indices);
        const auto calculated = CalculateBounds(m_Vertices);
        if (calculated.Minimum != bounds.Minimum || calculated.Maximum != bounds.Maximum)
            throw std::invalid_argument("Mesh bounds do not match its vertices.");
    }

    std::size_t MeshAsset::ResidentBytes() const noexcept
    {
        return sizeof(*this) + m_Vertices.size() * sizeof(MeshVertex) + m_Indices.size() * sizeof(std::uint32_t);
    }

    Ref<MeshAsset> MeshAsset::Cube() { return CreateRef<MeshAsset>(BuiltinMesh::Cube); }
    Ref<MeshAsset> MeshAsset::Error() { return CreateRef<MeshAsset>(BuiltinMesh::Error); }

    std::vector<std::byte> MeshAsset::Encode(const std::span<const MeshVertex> vertices,
                                             const std::span<const std::uint32_t> indices)
    {
        ValidateMesh(vertices, indices);
        const auto bounds = CalculateBounds(vertices);
        std::vector<std::byte> result;
        result.reserve(48U + vertices.size() * 48U + indices.size() * sizeof(std::uint32_t));
        for (const char value : MeshMagic)
            result.push_back(std::byte(value));
        AppendUnsigned(result, MeshVersion);
        AppendUnsigned(result, static_cast<std::uint64_t>(vertices.size()));
        AppendUnsigned(result, static_cast<std::uint64_t>(indices.size()));
        for (const float value : {bounds.Minimum.X, bounds.Minimum.Y, bounds.Minimum.Z, bounds.Maximum.X,
                                  bounds.Maximum.Y, bounds.Maximum.Z})
            AppendFloat(result, value);
        for (const auto& vertex : vertices)
        {
            for (const float value :
                 {vertex.Position.X, vertex.Position.Y, vertex.Position.Z, vertex.Normal.X, vertex.Normal.Y,
                  vertex.Normal.Z, vertex.UV0.X, vertex.UV0.Y, vertex.VertexColor.Red, vertex.VertexColor.Green,
                  vertex.VertexColor.Blue, vertex.VertexColor.Alpha})
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
        if (reader.UnsignedValue<std::uint32_t>() != MeshVersion)
            throw std::invalid_argument("Mesh asset has an unsupported version.");
        const auto vertexCount = reader.UnsignedValue<std::uint64_t>();
        const auto indexCount = reader.UnsignedValue<std::uint64_t>();
        if (vertexCount == 0 || vertexCount > MaximumMeshVertices || indexCount == 0 ||
            indexCount > MaximumMeshIndices || indexCount % 3U != 0)
            throw std::invalid_argument("Mesh asset counts are invalid.");
        MeshBounds bounds{{reader.Float(), reader.Float(), reader.Float()},
                          {reader.Float(), reader.Float(), reader.Float()}};
        const auto expected = vertexCount * 48U + indexCount * sizeof(std::uint32_t);
        if (expected > reader.Remaining() || expected != reader.Remaining())
            throw std::invalid_argument("Mesh asset payload size is invalid.");
        std::vector<MeshVertex> vertices(static_cast<std::size_t>(vertexCount));
        for (auto& vertex : vertices)
        {
            vertex.Position = {reader.Float(), reader.Float(), reader.Float()};
            vertex.Normal = {reader.Float(), reader.Float(), reader.Float()};
            vertex.UV0 = {reader.Float(), reader.Float()};
            vertex.VertexColor = {reader.Float(), reader.Float(), reader.Float(), reader.Float()};
        }
        std::vector<std::uint32_t> indices(static_cast<std::size_t>(indexCount));
        for (auto& index : indices)
            index = reader.UnsignedValue<std::uint32_t>();
        return CreateRef<MeshAsset>(std::move(vertices), std::move(indices), bounds);
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
              static_cast<std::uint8_t>(settings.Sampler.AddressW), settings.Sampler.Anisotropy})
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
        if (reader.UnsignedValue<std::uint32_t>() != TextureVersion)
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
        result.Version = 1;
        result.Type = MeshAsset::StaticType();
        result.Extensions = {".obj", ".fbx", ".gltf", ".glb", ".keiremesh"};
        result.ContextualImport = [](const AssetImportContext& context,
                                     const std::span<const std::byte> bytes) -> AssetImportOutput
        {
            if (context.SourcePath.extension() == ".keiremesh")
            {
                (void)MeshAsset::Decode(bytes);
                return {{bytes.begin(), bytes.end()}};
            }
            Assimp::Importer importer;
            constexpr unsigned int flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                                           aiProcess_GenSmoothNormals | aiProcess_PreTransformVertices |
                                           aiProcess_ImproveCacheLocality | aiProcess_SortByPType |
                                           aiProcess_ValidateDataStructure;
            const auto* scene = importer.ReadFile(context.SourcePath.string(), flags);
            if (!scene)
                throw std::invalid_argument(std::string("Mesh import failed: ") + importer.GetErrorString());
            if (scene->mNumAnimations != 0)
                throw std::invalid_argument("Animated meshes are not supported by the static mesh importer.");
            std::vector<MeshVertex> vertices;
            std::vector<std::uint32_t> indices;
            for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex)
            {
                const auto* mesh = scene->mMeshes[meshIndex];
                if (!mesh || mesh->mNumBones != 0 || (mesh->mPrimitiveTypes & ~aiPrimitiveType_TRIANGLE) != 0)
                    throw std::invalid_argument("Static mesh import rejects skinning and non-triangle primitives.");
                if (vertices.size() > std::numeric_limits<std::uint32_t>::max() - mesh->mNumVertices)
                    throw std::overflow_error("Merged mesh vertex count exceeds 32-bit indices.");
                const auto base = static_cast<std::uint32_t>(vertices.size());
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
            }
            return {MeshAsset::Encode(vertices, indices)};
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
            const auto effective = ReadTextureSettings(context.MetadataPath, settings);
            return {Texture2DAsset::Encode(effective, ImportTexture(bytes, effective))};
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
