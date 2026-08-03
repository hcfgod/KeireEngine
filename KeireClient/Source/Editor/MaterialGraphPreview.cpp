#include "KeireClient/Editor/MaterialGraphPreview.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace KeireEditor
{
    namespace
    {
        constexpr std::size_t MaximumPreviewTriangles = 500'000;

        struct PreviewMaterial
        {
            Keire::Vector4 BaseColor{0.72F, 0.72F, 0.74F, 1.0F};
            Keire::Vector3 Emission;
            float Metallic = 0.0F;
            float Roughness = 0.45F;
            float Specular = 0.5F;
            float ClearCoat = 0.0F;
            float ClearCoatRoughness = 0.1F;
            Keire::Vector3 SheenColor;
            float SheenRoughness = 0.5F;
            float Opacity = 1.0F;
            bool HasBaseTexture = false;
            bool Unlit = false;
        };

        struct PreviewVertex
        {
            Keire::Vector3 Position;
            Keire::Vector3 Normal;
            Keire::Vector2 UV;
        };

        struct ProjectedVertex
        {
            float X = 0.0F;
            float Y = 0.0F;
            float Depth = 0.0F;
            Keire::Vector3 Normal;
            Keire::Vector2 UV;
        };

        struct PreviewGeometry
        {
            std::vector<PreviewVertex> Vertices;
            std::vector<std::uint32_t> Indices;
        };

        [[nodiscard]] std::string Lower(std::string_view value)
        {
            std::string result(value);
            std::ranges::transform(result, result.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return result;
        }

        [[nodiscard]] float Clamp01(const float value) noexcept { return std::clamp(value, 0.0F, 1.0F); }

        [[nodiscard]] Keire::Vector4 ValueVector(const Keire::MaterialGraphValue& value) noexcept
        {
            return std::visit(
                [](const auto& candidate) -> Keire::Vector4
                {
                    using T = std::decay_t<decltype(candidate)>;
                    if constexpr (std::same_as<T, float>)
                        return {candidate, candidate, candidate, candidate};
                    else if constexpr (std::same_as<T, Keire::Vector2>)
                        return {candidate.X, candidate.Y, 0.0F, 0.0F};
                    else if constexpr (std::same_as<T, Keire::Vector3>)
                        return {candidate.X, candidate.Y, candidate.Z, 0.0F};
                    else if constexpr (std::same_as<T, Keire::Vector4>)
                        return candidate;
                    else if constexpr (std::same_as<T, Keire::Color>)
                        return {candidate.Red, candidate.Green, candidate.Blue, candidate.Alpha};
                    else
                        return {};
                },
                value);
        }

        [[nodiscard]] const Keire::MaterialGraphNode*
        DirectMasterInputNode(const Keire::MaterialGraphDefinition& definition, const std::string_view name)
        {
            const auto master = std::ranges::find(definition.Nodes, Keire::MaterialGraphNodeKind::Master,
                                                  &Keire::MaterialGraphNode::Kind);
            if (master == definition.Nodes.end())
                return nullptr;
            const auto pin = std::ranges::find(master->Pins, name, &Keire::MaterialGraphPin::Name);
            if (pin == master->Pins.end())
                return nullptr;
            const auto connection =
                std::ranges::find_if(definition.Connections, [&](const Keire::MaterialGraphConnection& candidate)
                                     { return candidate.Input.Node == master->Id && candidate.Input.Pin == pin->Id; });
            if (connection == definition.Connections.end())
                return nullptr;
            const auto node =
                std::ranges::find(definition.Nodes, connection->Output.Node, &Keire::MaterialGraphNode::Id);
            return node == definition.Nodes.end() ? nullptr : std::addressof(*node);
        }

        [[nodiscard]] std::optional<Keire::Vector4>
        DirectMasterInputValue(const Keire::MaterialGraphDefinition& definition, const std::string_view name)
        {
            const auto master = std::ranges::find(definition.Nodes, Keire::MaterialGraphNodeKind::Master,
                                                  &Keire::MaterialGraphNode::Kind);
            if (master == definition.Nodes.end())
                return std::nullopt;
            const auto pin = std::ranges::find(master->Pins, name, &Keire::MaterialGraphPin::Name);
            if (pin == master->Pins.end())
                return std::nullopt;
            if (const auto* node = DirectMasterInputNode(definition, name))
            {
                if (node->Kind == Keire::MaterialGraphNodeKind::Parameter ||
                    node->Kind == Keire::MaterialGraphNodeKind::Constant)
                    return ValueVector(node->Value);
                return std::nullopt;
            }
            return ValueVector(pin->DefaultValue);
        }

        [[nodiscard]] Keire::Vector3 Normalize(const Keire::Vector3 value,
                                               const Keire::Vector3 fallback = {0.0F, 0.0F, 1.0F}) noexcept
        {
            const float lengthSquared = value.X * value.X + value.Y * value.Y + value.Z * value.Z;
            if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12F)
                return fallback;
            const float inverseLength = 1.0F / std::sqrt(lengthSquared);
            return {value.X * inverseLength, value.Y * inverseLength, value.Z * inverseLength};
        }

        [[nodiscard]] float Dot(const Keire::Vector3 first, const Keire::Vector3 second) noexcept
        {
            return first.X * second.X + first.Y * second.Y + first.Z * second.Z;
        }

        [[nodiscard]] PreviewMaterial ResolveMaterial(const MaterialGraphPreviewRequest& request)
        {
            PreviewMaterial result;
            result.Unlit = request.Output == Keire::MaterialGraphOutput::Unlit;
            bool foundColor = false;
            for (const auto& property : request.Properties)
            {
                const auto name = Lower(property.Name);
                if (property.Type == Keire::ShaderPropertyType::Texture2D)
                {
                    result.HasBaseTexture |= property.TextureSemantic == Keire::ShaderTextureSemantic::BaseColor;
                    continue;
                }
                if ((name == "basecolor" || name == "color" || name == "tint" ||
                     (!foundColor && property.Type == Keire::ShaderPropertyType::Color)))
                {
                    result.BaseColor = property.DefaultValue;
                    foundColor = true;
                }
                else if (name == "metallic")
                    result.Metallic = property.DefaultValue.X;
                else if (name == "roughness")
                    result.Roughness = property.DefaultValue.X;
                else if (name == "specular")
                    result.Specular = property.DefaultValue.X;
                else if (name == "clearcoat")
                    result.ClearCoat = property.DefaultValue.X;
                else if (name == "clearcoatroughness")
                    result.ClearCoatRoughness = property.DefaultValue.X;
                else if (name == "sheencolor")
                    result.SheenColor = {property.DefaultValue.X, property.DefaultValue.Y, property.DefaultValue.Z};
                else if (name == "sheenroughness")
                    result.SheenRoughness = property.DefaultValue.X;
                else if (name == "opacity")
                    result.Opacity = property.DefaultValue.X;
                else if (name == "emission" || name == "emissive" || name.find("emission") != std::string::npos ||
                         name.find("emissive") != std::string::npos)
                    result.Emission = {property.DefaultValue.X, property.DefaultValue.Y, property.DefaultValue.Z};
            }
            if (request.Definition)
            {
                const auto colorName = request.Output == Keire::MaterialGraphOutput::Unlit ? "Color" : "BaseColor";
                if (const auto value = DirectMasterInputValue(*request.Definition, colorName))
                    result.BaseColor = *value;
                if (const auto value = DirectMasterInputValue(*request.Definition, "Emission"))
                    result.Emission = {value->X, value->Y, value->Z};
                if (const auto value = DirectMasterInputValue(*request.Definition, "Metallic"))
                    result.Metallic = value->X;
                if (const auto value = DirectMasterInputValue(*request.Definition, "Roughness"))
                    result.Roughness = value->X;
                if (const auto value = DirectMasterInputValue(*request.Definition, "Specular"))
                    result.Specular = value->X;
                if (const auto value = DirectMasterInputValue(*request.Definition, "ClearCoat"))
                    result.ClearCoat = value->X;
                if (const auto value = DirectMasterInputValue(*request.Definition, "ClearCoatRoughness"))
                    result.ClearCoatRoughness = value->X;
                if (const auto value = DirectMasterInputValue(*request.Definition, "SheenColor"))
                    result.SheenColor = {value->X, value->Y, value->Z};
                if (const auto value = DirectMasterInputValue(*request.Definition, "SheenRoughness"))
                    result.SheenRoughness = value->X;
                if (const auto value = DirectMasterInputValue(*request.Definition, "Opacity"))
                    result.Opacity = value->X;
                if (const auto* node = DirectMasterInputNode(*request.Definition, colorName))
                    result.HasBaseTexture |= node->Kind == Keire::MaterialGraphNodeKind::TextureSample;
            }
            result.BaseColor.X = Clamp01(result.BaseColor.X);
            result.BaseColor.Y = Clamp01(result.BaseColor.Y);
            result.BaseColor.Z = Clamp01(result.BaseColor.Z);
            result.BaseColor.W = Clamp01(result.BaseColor.W);
            result.Metallic = Clamp01(result.Metallic);
            result.Roughness = std::clamp(result.Roughness, 0.04F, 1.0F);
            result.Specular = Clamp01(result.Specular);
            result.ClearCoat = Clamp01(result.ClearCoat);
            result.ClearCoatRoughness = std::clamp(result.ClearCoatRoughness, 0.04F, 1.0F);
            result.SheenColor.X = Clamp01(result.SheenColor.X);
            result.SheenColor.Y = Clamp01(result.SheenColor.Y);
            result.SheenColor.Z = Clamp01(result.SheenColor.Z);
            result.SheenRoughness = std::clamp(result.SheenRoughness, 0.04F, 1.0F);
            result.Opacity = Clamp01(result.Opacity);
            return result;
        }

        [[nodiscard]] PreviewGeometry SphereGeometry()
        {
            constexpr std::uint32_t rings = 24;
            constexpr std::uint32_t segments = 36;
            constexpr float pi = 3.14159265358979323846F;
            PreviewGeometry result;
            result.Vertices.reserve((rings + 1U) * (segments + 1U));
            result.Indices.reserve(static_cast<std::size_t>(rings) * segments * 6U);
            for (std::uint32_t ring = 0; ring <= rings; ++ring)
            {
                const float v = static_cast<float>(ring) / rings;
                const float latitude = (0.5F - v) * pi;
                const float radius = std::cos(latitude);
                const float y = std::sin(latitude);
                for (std::uint32_t segment = 0; segment <= segments; ++segment)
                {
                    const float u = static_cast<float>(segment) / segments;
                    const float longitude = u * pi * 2.0F;
                    const Keire::Vector3 normal{radius * std::sin(longitude), y, radius * std::cos(longitude)};
                    result.Vertices.push_back({normal, normal, {u, v}});
                }
            }
            for (std::uint32_t ring = 0; ring < rings; ++ring)
                for (std::uint32_t segment = 0; segment < segments; ++segment)
                {
                    const auto first = ring * (segments + 1U) + segment;
                    const auto second = first + segments + 1U;
                    result.Indices.insert(result.Indices.end(),
                                          {first, second, first + 1U, first + 1U, second, second + 1U});
                }
            return result;
        }

        [[nodiscard]] PreviewGeometry PlaneGeometry()
        {
            return {{{{-1.0F, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F}},
                     {{1.0F, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 1.0F}},
                     {{1.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F}},
                     {{-1.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F}}},
                    {0, 1, 2, 0, 2, 3}};
        }

        [[nodiscard]] PreviewGeometry FromMesh(const Keire::MeshAsset& mesh)
        {
            PreviewGeometry result;
            const auto vertices = mesh.Vertices();
            const auto indices = mesh.Indices();
            if (vertices.empty() || indices.size() < 3 || indices.size() / 3 > MaximumPreviewTriangles)
                throw std::invalid_argument("Material Graph preview mesh has unsupported geometry.");
            result.Vertices.reserve(vertices.size());
            for (const auto& vertex : vertices)
                result.Vertices.push_back({vertex.Position, vertex.Normal, vertex.UV0});
            result.Indices.assign(indices.begin(), indices.end());
            return result;
        }

        void PutPixel(std::vector<std::byte>& pixels, const std::uint32_t width, const std::uint32_t x,
                      const std::uint32_t y, const std::array<float, 4> color)
        {
            const auto offset = (static_cast<std::size_t>(y) * width + x) * 4U;
            for (std::size_t channel = 0; channel < 4; ++channel)
                pixels[offset + channel] =
                    static_cast<std::byte>(static_cast<std::uint8_t>(Clamp01(color[channel]) * 255.0F + 0.5F));
        }

        [[nodiscard]] std::array<float, 3> Background(const std::uint32_t x, const std::uint32_t y) noexcept
        {
            const float checker = ((x / 14U + y / 14U) & 1U) == 0 ? 0.115F : 0.145F;
            const float vignette = 1.0F - std::min(0.28F, std::abs(static_cast<float>(x % 256U) - 128.0F) / 900.0F);
            return {checker * vignette, (checker + 0.012F) * vignette, (checker + 0.025F) * vignette};
        }

        [[nodiscard]] std::array<float, 4> Shade(const PreviewMaterial& material, const Keire::Vector3 normal,
                                                 const Keire::Vector2 uv, const float exposure,
                                                 const float environmentIntensity)
        {
            const auto n = Normalize(normal);
            const auto light = Normalize(Keire::Vector3{-0.45F, 0.62F, 0.68F});
            const auto view = Keire::Vector3{0.0F, 0.0F, 1.0F};
            const auto halfVector = Normalize({light.X + view.X, light.Y + view.Y, light.Z + view.Z});
            const float noL = Clamp01(Dot(n, light));
            const float noH = Clamp01(Dot(n, halfVector));
            const float noV = Clamp01(Dot(n, view));
            const float texture =
                material.HasBaseTexture
                    ? (((static_cast<int>(std::floor(uv.X * 10.0F)) + static_cast<int>(std::floor(uv.Y * 10.0F))) &
                        1) == 0
                           ? 0.88F
                           : 0.58F)
                    : 1.0F;
            const float glossExponent = 4.0F + (1.0F - material.Roughness) * 124.0F;
            const float dielectric = 0.08F * material.Specular;
            const float specular =
                std::pow(noH, glossExponent) * (dielectric + material.Metallic * (1.0F - dielectric));
            const float clearCoatExponent = 4.0F + (1.0F - material.ClearCoatRoughness) * 252.0F;
            const float clearCoat = std::pow(noH, clearCoatExponent) * material.ClearCoat * 0.32F;
            const float sheen = std::pow(1.0F - noV, 2.0F + material.SheenRoughness * 4.0F);
            const float diffuse = material.Unlit ? 1.0F : 0.08F * environmentIntensity + noL * 0.82F;
            const float diffuseWeight = material.Unlit ? 1.0F : 1.0F - material.Metallic * 0.72F;
            const auto channel = [&](const float base, const float emission, const float sheenChannel)
            {
                const float linear =
                    base * texture * diffuse * diffuseWeight + specular + clearCoat + sheenChannel * sheen + emission;
                return 1.0F - std::exp(-std::max(linear, 0.0F) * exposure);
            };
            return {channel(material.BaseColor.X, material.Emission.X, material.SheenColor.X),
                    channel(material.BaseColor.Y, material.Emission.Y, material.SheenColor.Y),
                    channel(material.BaseColor.Z, material.Emission.Z, material.SheenColor.Z),
                    material.BaseColor.W * material.Opacity};
        }

        [[nodiscard]] Keire::Vector3 Rotate(const Keire::Vector3 value, const float rotationDegrees) noexcept
        {
            constexpr float radiansPerDegree = 0.01745329251994329577F;
            const float yaw = rotationDegrees * radiansPerDegree;
            const float cosineYaw = std::cos(yaw);
            const float sineYaw = std::sin(yaw);
            constexpr float cosinePitch = 0.97133797F;
            constexpr float sinePitch = -0.23770263F;
            const float x = cosineYaw * value.X + sineYaw * value.Z;
            const float z = -sineYaw * value.X + cosineYaw * value.Z;
            return {x, cosinePitch * value.Y - sinePitch * z, sinePitch * value.Y + cosinePitch * z};
        }

        void Rasterize(const PreviewGeometry& geometry, const PreviewMaterial& material, const std::uint32_t width,
                       const std::uint32_t height, const float exposure, const float environmentIntensity,
                       const float rotationDegrees, std::vector<std::byte>& pixels)
        {
            Keire::Vector3 minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                                   std::numeric_limits<float>::max()};
            Keire::Vector3 maximum{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                                   std::numeric_limits<float>::lowest()};
            for (const auto& vertex : geometry.Vertices)
            {
                minimum.X = std::min(minimum.X, vertex.Position.X);
                minimum.Y = std::min(minimum.Y, vertex.Position.Y);
                minimum.Z = std::min(minimum.Z, vertex.Position.Z);
                maximum.X = std::max(maximum.X, vertex.Position.X);
                maximum.Y = std::max(maximum.Y, vertex.Position.Y);
                maximum.Z = std::max(maximum.Z, vertex.Position.Z);
            }
            const Keire::Vector3 center{(minimum.X + maximum.X) * 0.5F, (minimum.Y + maximum.Y) * 0.5F,
                                        (minimum.Z + maximum.Z) * 0.5F};
            const float extent =
                std::max({maximum.X - minimum.X, maximum.Y - minimum.Y, maximum.Z - minimum.Z, 1.0e-4F});
            const float scale = std::min(width, height) * 0.78F / extent;
            std::vector<ProjectedVertex> projected;
            projected.reserve(geometry.Vertices.size());
            for (const auto& vertex : geometry.Vertices)
            {
                const auto position =
                    Rotate({vertex.Position.X - center.X, vertex.Position.Y - center.Y, vertex.Position.Z - center.Z},
                           rotationDegrees);
                projected.push_back({width * 0.5F + position.X * scale, height * 0.51F - position.Y * scale,
                                     -position.Z, Normalize(Rotate(vertex.Normal, rotationDegrees)), vertex.UV});
            }

            std::vector<float> depth(static_cast<std::size_t>(width) * height, std::numeric_limits<float>::infinity());
            const auto edge =
                [](const ProjectedVertex& first, const ProjectedVertex& second, const float x, const float y)
            { return (x - first.X) * (second.Y - first.Y) - (y - first.Y) * (second.X - first.X); };
            for (std::size_t index = 0; index + 2 < geometry.Indices.size(); index += 3)
            {
                const auto i0 = geometry.Indices[index];
                const auto i1 = geometry.Indices[index + 1];
                const auto i2 = geometry.Indices[index + 2];
                if (i0 >= projected.size() || i1 >= projected.size() || i2 >= projected.size())
                    continue;
                const auto& first = projected[i0];
                const auto& second = projected[i1];
                const auto& third = projected[i2];
                const float area = edge(first, second, third.X, third.Y);
                if (!std::isfinite(area) || std::abs(area) <= 1.0e-5F)
                    continue;
                const int minimumX = std::max(0, static_cast<int>(std::floor(std::min({first.X, second.X, third.X}))));
                const int maximumX = std::min(static_cast<int>(width) - 1,
                                              static_cast<int>(std::ceil(std::max({first.X, second.X, third.X}))));
                const int minimumY = std::max(0, static_cast<int>(std::floor(std::min({first.Y, second.Y, third.Y}))));
                const int maximumY = std::min(static_cast<int>(height) - 1,
                                              static_cast<int>(std::ceil(std::max({first.Y, second.Y, third.Y}))));
                for (int y = minimumY; y <= maximumY; ++y)
                    for (int x = minimumX; x <= maximumX; ++x)
                    {
                        const float sampleX = static_cast<float>(x) + 0.5F;
                        const float sampleY = static_cast<float>(y) + 0.5F;
                        const float w0 = edge(second, third, sampleX, sampleY) / area;
                        const float w1 = edge(third, first, sampleX, sampleY) / area;
                        const float w2 = 1.0F - w0 - w1;
                        if (w0 < -1.0e-5F || w1 < -1.0e-5F || w2 < -1.0e-5F)
                            continue;
                        const float candidateDepth = first.Depth * w0 + second.Depth * w1 + third.Depth * w2;
                        const auto pixel = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
                        if (candidateDepth >= depth[pixel])
                            continue;
                        depth[pixel] = candidateDepth;
                        const Keire::Vector3 normal{first.Normal.X * w0 + second.Normal.X * w1 + third.Normal.X * w2,
                                                    first.Normal.Y * w0 + second.Normal.Y * w1 + third.Normal.Y * w2,
                                                    first.Normal.Z * w0 + second.Normal.Z * w1 + third.Normal.Z * w2};
                        const Keire::Vector2 uv{first.UV.X * w0 + second.UV.X * w1 + third.UV.X * w2,
                                                first.UV.Y * w0 + second.UV.Y * w1 + third.UV.Y * w2};
                        const auto shaded = Shade(material, normal, uv, exposure, environmentIntensity);
                        const auto background =
                            Background(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                        const float alpha = Clamp01(shaded[3]);
                        PutPixel(pixels, width, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                 {shaded[0] * alpha + background[0] * (1.0F - alpha),
                                  shaded[1] * alpha + background[1] * (1.0F - alpha),
                                  shaded[2] * alpha + background[2] * (1.0F - alpha), 1.0F});
                    }
            }
        }
    } // namespace

    std::vector<std::byte> RenderMaterialGraphPreview(const MaterialGraphPreviewRequest& request)
    {
        if (request.Width < 32 || request.Height < 32 || request.Width > 2048 || request.Height > 2048)
            throw std::invalid_argument("Material Graph preview dimensions must be in the range 32..2048.");
        if (!std::isfinite(request.Exposure) || request.Exposure < 0.1F || request.Exposure > 8.0F ||
            !std::isfinite(request.EnvironmentIntensity) || request.EnvironmentIntensity < 0.0F ||
            request.EnvironmentIntensity > 8.0F || !std::isfinite(request.RotationDegrees) ||
            request.RotationDegrees < -180.0F || request.RotationDegrees > 180.0F)
            throw std::invalid_argument("Material Graph preview lighting controls are outside their supported range.");
        std::vector<std::byte> pixels(static_cast<std::size_t>(request.Width) * request.Height * 4U);
        for (std::uint32_t y = 0; y < request.Height; ++y)
            for (std::uint32_t x = 0; x < request.Width; ++x)
            {
                const auto background = Background(x, y);
                PutPixel(pixels, request.Width, x, y, {background[0], background[1], background[2], 1.0F});
            }

        PreviewGeometry geometry;
        switch (request.Mesh)
        {
        case Keire::MaterialGraphPreviewMesh::Sphere:
            geometry = SphereGeometry();
            break;
        case Keire::MaterialGraphPreviewMesh::Plane:
            geometry = PlaneGeometry();
            break;
        case Keire::MaterialGraphPreviewMesh::Cube:
            geometry = FromMesh(*Keire::MeshAsset::Cube());
            break;
        case Keire::MaterialGraphPreviewMesh::Custom:
            if (!request.CustomMesh)
                throw std::invalid_argument("Custom Material Graph preview mesh is unavailable.");
            geometry = FromMesh(*request.CustomMesh);
            break;
        default:
            throw std::invalid_argument("Material Graph preview mesh is invalid.");
        }
        Rasterize(geometry, ResolveMaterial(request), request.Width, request.Height, request.Exposure,
                  request.EnvironmentIntensity, request.RotationDegrees, pixels);
        return pixels;
    }
} // namespace KeireEditor
