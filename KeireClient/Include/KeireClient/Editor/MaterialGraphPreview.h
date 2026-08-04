#pragma once

#include "Keire/Rendering/MaterialGraph.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace KeireEditor
{
    struct MaterialGraphPreviewRequest
    {
        Keire::MaterialGraphOutput Output = Keire::MaterialGraphOutput::Surface;
        Keire::MaterialGraphPreviewMesh Mesh = Keire::MaterialGraphPreviewMesh::Sphere;
        Keire::Ref<const Keire::MeshAsset> CustomMesh;
        const Keire::MaterialGraphDefinition* Definition = nullptr;
        std::span<const Keire::ShaderPropertyDefinition> Properties;
        std::uint32_t Width = 320;
        std::uint32_t Height = 220;
        float Exposure = 1.0F;
        float EnvironmentIntensity = 1.0F;
        float RotationDegrees = 33.0F;
        std::function<bool()> CancellationRequested;
    };

    [[nodiscard]] std::vector<std::byte> RenderMaterialGraphPreview(const MaterialGraphPreviewRequest& request);
} // namespace KeireEditor
