#pragma once

#include "Keire/Math/Math.h"
#include "Keire/Math/Vector3d.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Water/WaterWaveField.h"

#include <cstdint>
#include <vector>

namespace Keire::RenderBackend
{
    // A packet owns values only; disabled or removed components retain no renderer resources.
    struct WaterRenderPatch final
    {
        Vector4 Origin;
        Vector4 AxisX;
        Vector4 AxisY;
        Vector4 AxisZ;
        Vector4 Grid;       // Width, height, X cells, Z cells.
        Vector4 Footprint;  // Patch origin / half extents, inverse half extents (ellipse).
        Vector4 Appearance; // Roughness, crest foam, render distance, ellipse flag.
        Color SurfaceColor;
        WaterWaveShaderParameters Waves;
        float SortDepth = 0.0F;
        std::uint32_t VertexCount = 0;
    };

    struct WaterRenderCapture final
    {
        std::vector<WaterRenderPatch> Patches;
        std::uint32_t UnsupportedSphereBodies = 0;
        std::uint32_t UnsupportedMaterialBodies = 0;
        std::uint32_t UnrepresentableBodies = 0;
    };

    void SortWaterPatches(std::vector<WaterRenderPatch>& patches);

    // Capture uses the same immutable camera frame as mesh packets. Floating-origin subtraction and
    // local wave phase reduction happen in double precision before any GPU conversion.
    void CaptureWaterBodies(const Scene& scene, Vector3d worldOrigin, Vector3 observerRelative, const Matrix4& view,
                            const Matrix4& viewProjection, double timeSeconds, WaterRenderCapture& output);
} // namespace Keire::RenderBackend
