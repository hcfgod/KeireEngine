#pragma once

#include "Keire/Rendering/ShaderGraph.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace KeireEditor
{
    struct ShaderGraphPreviewRequest
    {
        Keire::ShaderGraphOutput Output = Keire::ShaderGraphOutput::Surface;
        Keire::ShaderGraphPreviewMesh Mesh = Keire::ShaderGraphPreviewMesh::Sphere;
        Keire::Ref<const Keire::MeshAsset> CustomMesh;
        const Keire::ShaderGraphDefinition* Definition = nullptr;
        std::span<const Keire::ShaderPropertyDefinition> Properties;
        std::uint32_t Width = 320;
        std::uint32_t Height = 220;
        float Exposure = 1.0F;
        float EnvironmentIntensity = 1.0F;
        float RotationDegrees = 33.0F;
        std::function<bool()> CancellationRequested;
    };

    [[nodiscard]] std::vector<std::byte> RenderShaderGraphPreview(const ShaderGraphPreviewRequest& request);
} // namespace KeireEditor
