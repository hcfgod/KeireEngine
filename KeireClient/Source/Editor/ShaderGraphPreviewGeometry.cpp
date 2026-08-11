#include "KeireClient/Editor/ShaderGraphPreviewGeometry.h"

#include "Keire/Assets/RenderingAssets.h"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace KeireEditor::Detail
{
    namespace
    {
        constexpr std::size_t MaximumPreviewTriangles = 500'000;
    }

    PreviewGeometry SphereGeometry()
    {
        constexpr std::uint32_t rings = 24;
        constexpr std::uint32_t segments = 36;
        constexpr float pi = 3.14159265358979323846F;
        PreviewGeometry result;
        result.Vertices.reserve(static_cast<std::size_t>(rings + 1U) * (segments + 1U));
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
        {
            for (std::uint32_t segment = 0; segment < segments; ++segment)
            {
                const auto first = ring * (segments + 1U) + segment;
                const auto second = first + segments + 1U;
                result.Indices.insert(result.Indices.end(),
                                      {first, second, first + 1U, first + 1U, second, second + 1U});
            }
        }
        return result;
    }

    PreviewGeometry PlaneGeometry()
    {
        return {{{{-1.0F, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 1.0F}},
                 {{1.0F, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 1.0F}},
                 {{1.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {1.0F, 0.0F}},
                 {{-1.0F, 1.0F, 0.0F}, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F}}},
                {0, 1, 2, 0, 2, 3}};
    }

    PreviewGeometry GeometryFromMesh(const Keire::MeshAsset& mesh)
    {
        PreviewGeometry result;
        const auto vertices = mesh.Vertices();
        const auto indices = mesh.Indices();
        if (vertices.empty() || indices.size() < 3 || indices.size() / 3 > MaximumPreviewTriangles)
        {
            throw std::invalid_argument("Shader Graph preview mesh has unsupported geometry.");
        }
        result.Vertices.reserve(vertices.size());
        for (const auto& vertex : vertices)
        {
            result.Vertices.push_back({vertex.Position, vertex.Normal, vertex.UV0});
        }
        result.Indices.assign(indices.begin(), indices.end());
        return result;
    }
} // namespace KeireEditor::Detail
