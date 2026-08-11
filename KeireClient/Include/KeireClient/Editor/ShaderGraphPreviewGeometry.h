#pragma once

#include "Keire/Math/Math.h"

#include <cstdint>
#include <vector>

namespace Keire
{
    class MeshAsset;
}

namespace KeireEditor::Detail
{
    struct PreviewVertex
    {
        Keire::Vector3 Position;
        Keire::Vector3 Normal;
        Keire::Vector2 UV;
    };

    struct PreviewGeometry
    {
        std::vector<PreviewVertex> Vertices;
        std::vector<std::uint32_t> Indices;
    };

    [[nodiscard]] PreviewGeometry SphereGeometry();
    [[nodiscard]] PreviewGeometry PlaneGeometry();
    [[nodiscard]] PreviewGeometry GeometryFromMesh(const Keire::MeshAsset& mesh);
} // namespace KeireEditor::Detail
