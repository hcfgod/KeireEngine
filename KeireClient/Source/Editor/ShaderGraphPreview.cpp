#include "KeireClient/Editor/ShaderGraphPreview.h"

#include "KeireClientInternal/Editor/ShaderGraphPreviewEvaluatorInternal.h"

#include "KeireClient/Editor/ShaderGraphPreviewGeometry.h"
#include "KeireClient/Editor/ShaderGraphPreviewRaster.h"
#include "KeireClient/Editor/ShaderGraphPreviewTextureSampling.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
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
        using ShaderGraphPreviewInternal::ShaderGraphPreviewEvaluator;

        struct ProjectedVertex
        {
            float X = 0.0F;
            float Y = 0.0F;
            float Depth = 0.0F;
            Keire::Vector3 Position;
            Keire::Vector3 Normal;
            Keire::Vector2 UV;
        };

        [[nodiscard]] float Clamp01(const float value) noexcept { return std::clamp(value, 0.0F, 1.0F); }

        [[nodiscard]] Keire::Vector3 Normalize(const Keire::Vector3 value,
                                               const Keire::Vector3 fallback = {0.0F, 0.0F, 1.0F}) noexcept
        {
            const float lengthSquared = value.X * value.X + value.Y * value.Y + value.Z * value.Z;
            if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-12F)
                return fallback;
            const float inverseLength = 1.0F / std::sqrt(lengthSquared);
            return {value.X * inverseLength, value.Y * inverseLength, value.Z * inverseLength};
        }

        void Rasterize(const Detail::PreviewGeometry& geometry, const ShaderGraphPreviewRequest& request,
                       ShaderGraphPreviewEvaluator& evaluator, const std::uint32_t width, const std::uint32_t height,
                       const float exposure, const float environmentIntensity, const float rotationDegrees,
                       std::vector<std::byte>& pixels)
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
            const float scale = static_cast<float>(std::min(width, height)) * 0.78F / extent;
            std::vector<ProjectedVertex> projected;
            projected.reserve(geometry.Vertices.size());
            for (const auto& vertex : geometry.Vertices)
            {
                const auto position = Detail::RotatePreviewVector(
                    {vertex.Position.X - center.X, vertex.Position.Y - center.Y, vertex.Position.Z - center.Z},
                    rotationDegrees);
                projected.push_back({static_cast<float>(width) * 0.5F + position.X * scale,
                                     static_cast<float>(height) * 0.51F - position.Y * scale, -position.Z, position,
                                     Normalize(Detail::RotatePreviewVector(vertex.Normal, rotationDegrees)),
                                     vertex.UV});
            }

            std::vector<float> depth(static_cast<std::size_t>(width) * height, std::numeric_limits<float>::infinity());
            const auto edge =
                [](const ProjectedVertex& first, const ProjectedVertex& second, const float x, const float y)
            { return (x - first.X) * (second.Y - first.Y) - (y - first.Y) * (second.X - first.X); };
            for (std::size_t index = 0; index + 2 < geometry.Indices.size(); index += 3)
            {
                Detail::CheckShaderGraphPreviewCancellation(request);
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
                {
                    Detail::CheckShaderGraphPreviewCancellation(request);
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
                        const Keire::Vector3 position{
                            first.Position.X * w0 + second.Position.X * w1 + third.Position.X * w2,
                            first.Position.Y * w0 + second.Position.Y * w1 + third.Position.Y * w2,
                            first.Position.Z * w0 + second.Position.Z * w1 + third.Position.Z * w2};
                        const Keire::Vector2 uv{first.UV.X * w0 + second.UV.X * w1 + third.UV.X * w2,
                                                first.UV.Y * w0 + second.UV.Y * w1 + third.UV.Y * w2};
                        const auto material = evaluator.Resolve(uv, normal, position);
                        const auto shaded =
                            Detail::ShadePreviewMaterial(material, normal, uv, exposure, environmentIntensity);
                        const auto background =
                            Detail::PreviewBackground(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                        const float alpha = Clamp01(shaded[3]);
                        Detail::WritePreviewPixel(pixels, width, static_cast<std::uint32_t>(x),
                                                  static_cast<std::uint32_t>(y),
                                                  {shaded[0] * alpha + background[0] * (1.0F - alpha),
                                                   shaded[1] * alpha + background[1] * (1.0F - alpha),
                                                   shaded[2] * alpha + background[2] * (1.0F - alpha), 1.0F});
                    }
                }
            }
        }
    } // namespace

    std::vector<std::byte> RenderShaderGraphPreview(const ShaderGraphPreviewRequest& request)
    {
        if (request.Width < 32 || request.Height < 32 || request.Width > 2048 || request.Height > 2048)
            throw std::invalid_argument("Shader Graph preview dimensions must be in the range 32..2048.");
        if (!std::isfinite(request.Exposure) || request.Exposure < 0.1F || request.Exposure > 8.0F ||
            !std::isfinite(request.EnvironmentIntensity) || request.EnvironmentIntensity < 0.0F ||
            request.EnvironmentIntensity > 8.0F || !std::isfinite(request.RotationDegrees) ||
            request.RotationDegrees < -180.0F || request.RotationDegrees > 180.0F)
            throw std::invalid_argument("Shader Graph preview lighting controls are outside their supported range.");
        std::vector<std::byte> pixels(static_cast<std::size_t>(request.Width) * request.Height * 4U);
        for (std::uint32_t y = 0; y < request.Height; ++y)
        {
            Detail::CheckShaderGraphPreviewCancellation(request);
            for (std::uint32_t x = 0; x < request.Width; ++x)
            {
                const auto background = Detail::PreviewBackground(x, y);
                Detail::WritePreviewPixel(pixels, request.Width, x, y,
                                          {background[0], background[1], background[2], 1.0F});
            }
        }

        Detail::PreviewGeometry geometry;
        switch (request.Mesh)
        {
        case Keire::ShaderGraphPreviewMesh::Sphere:
            geometry = Detail::SphereGeometry();
            break;
        case Keire::ShaderGraphPreviewMesh::Plane:
            geometry = Detail::PlaneGeometry();
            break;
        case Keire::ShaderGraphPreviewMesh::Cube:
            geometry = Detail::GeometryFromMesh(*Keire::MeshAsset::Cube());
            break;
        case Keire::ShaderGraphPreviewMesh::Custom:
            if (!request.CustomMesh)
                throw std::invalid_argument("Custom Shader Graph preview mesh is unavailable.");
            geometry = Detail::GeometryFromMesh(*request.CustomMesh);
            break;
        default:
            throw std::invalid_argument("Shader Graph preview mesh is invalid.");
        }
        ShaderGraphPreviewEvaluator evaluator(request);
        Rasterize(geometry, request, evaluator, request.Width, request.Height, request.Exposure,
                  request.EnvironmentIntensity, request.RotationDegrees, pixels);
        return pixels;
    }
} // namespace KeireEditor
