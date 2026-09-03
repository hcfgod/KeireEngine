#pragma once

#include "Keire/Animation/Skinning.h"
#include "Keire/Assets/RenderingAssets.h"
#include "KeireInternal/Rendering/IrradynSceneCacheInternal.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace Keire::RenderBackend
{
    inline constexpr std::uint32_t MaximumGpuOcclusionPyramidLevels = 14;
    inline constexpr std::uint32_t GpuOcclusionScanBlockSize = 256;
    inline constexpr std::uint32_t MaximumGpuDispatchGroupsPerDimension = 65'535;
    inline constexpr std::uint32_t MaximumGpuOcclusionBatchInstances =
        GpuOcclusionScanBlockSize * GpuOcclusionScanBlockSize;

    struct RenderVertex final
    {
        Vector3 Position;
        Vector3 Color;
        Vector3 Normal;
    };

    struct alignas(16) GpuRenderVertex final
    {
        Vector4 Position;
        Vector4 Color;
        Vector4 Normal;
    };

    struct alignas(16) GpuMeshVertex final
    {
        Vector4 Position;
        Vector4 Normal;
        Vector4 UV0;
        Vector4 VertexColor;
        Vector4 Tangent;
        Vector4 UV1;
    };

    static_assert(sizeof(GpuRenderVertex) == 48);
    static_assert(alignof(GpuRenderVertex) == 16);
    static_assert(sizeof(GpuMeshVertex) == 96);
    static_assert(alignof(GpuMeshVertex) == 16);

    struct RuntimeUiVertex final
    {
        Vector3 Position;
        Color ColorValue;
        Vector2 UV;
    };

    static_assert(sizeof(RuntimeUiVertex) == sizeof(float) * 9);

    [[nodiscard]] constexpr bool SupportsComputeSkinning(const std::string_view driver,
                                                         const SkinningMethod method) noexcept
    {
        return !driver.empty() && method == SkinningMethod::LinearBlend;
    }

    struct ObjectUniforms final
    {
        Matrix4 ModelViewProjection;
        Matrix4 NormalMatrix;
        Color Tint;
        Vector4 LightDirection;
        Color LightColor;
        Vector4 AmbientAndExposure;
        Vector4 Parameters;
        Matrix4 Model;
        Matrix4 View;
        Matrix4 Projection;
    };

    static_assert(sizeof(ObjectUniforms) == sizeof(float) * 100);

    struct AssetObjectUniforms final
    {
        Matrix4 Model;
        Matrix4 View;
        Matrix4 Projection;
        Matrix4 NormalMatrix;
    };

    struct GpuInstanceUniform final
    {
        Matrix4 Model;
        Matrix4 NormalMatrix;
        Color Tint;
    };

    struct alignas(16) GpuOcclusionCandidate final
    {
        Vector4 BoundsMinimum;
        Vector4 BoundsMaximum;
        std::array<std::uint32_t, 4> Metadata{};
    };

    struct alignas(16) GpuOcclusionChunk final
    {
        std::uint32_t CandidateFirst = 0;
        std::uint32_t CandidateCount = 0;
        std::uint32_t BatchIndex = 0;
        std::uint32_t Padding = 0;
    };

    struct alignas(16) GpuOcclusionBatch final
    {
        std::uint32_t CandidateFirst = 0;
        std::uint32_t CandidateCount = 0;
        std::uint32_t OutputFirst = 0;
        std::uint32_t ChunkFirst = 0;
        std::uint32_t ChunkCount = 0;
        std::uint32_t IndirectByteOffset = 0;
        std::uint32_t TriangleCount = 0;
        std::uint32_t Padding = 0;
    };

    struct alignas(16) GpuOcclusionStatus final
    {
        std::uint32_t Visible = 0;
        std::uint32_t VisibleTriangleLow = 0;
        std::uint32_t VisibleTriangleHigh = 0;
        std::uint32_t ErrorFlags = 0;
        std::array<std::uint32_t, 4> ConsumerVisible{};
    };

    struct alignas(16) GpuOcclusionDepthUniforms final
    {
        Matrix4 ViewProjection;
        std::array<std::uint32_t, 4> InstanceParameters{};
    };

    struct alignas(16) GpuOcclusionPyramidUniforms final
    {
        std::array<std::uint32_t, 4> SourceTargetSize{};
    };

    struct alignas(16) GpuOcclusionClassifyUniforms final
    {
        Matrix4 ViewProjection;
        Vector4 ViewportBiasLevels;
        std::array<std::uint32_t, 4> DispatchCounts{};
        std::array<std::array<std::uint32_t, 4>, MaximumGpuOcclusionPyramidLevels> HierarchySizes{};
    };

    struct alignas(16) GpuOcclusionDispatchUniforms final
    {
        std::array<std::uint32_t, 4> DispatchCounts{};
    };

    static_assert(sizeof(GpuInstanceUniform) == 144);
    static_assert(sizeof(GpuOcclusionCandidate) == 48);
    static_assert(sizeof(GpuOcclusionChunk) == 16);
    static_assert(sizeof(GpuOcclusionBatch) == 32);
    static_assert(sizeof(GpuOcclusionStatus) == 32);
    static_assert(sizeof(GpuOcclusionDepthUniforms) == 80);
    static_assert(sizeof(GpuOcclusionPyramidUniforms) == 16);
    static_assert(sizeof(GpuOcclusionClassifyUniforms) == 320);
    static_assert(sizeof(GpuOcclusionDispatchUniforms) == 16);
    static_assert(sizeof(SDL_GPUIndexedIndirectDrawCommand) == 20);

    inline constexpr std::size_t MaximumShaderLocalLights = 62;

    struct AssetLocalLightUniform final
    {
        Vector4 PositionRange;
        Vector4 DirectionOuter;
        Vector4 ColorIntensity;
        Vector4 Parameters;
    };

    struct ForwardPlusTileUniform final
    {
        std::uint32_t Offset = 0;
        std::uint32_t Count = 0;
        std::uint32_t Padding0 = 0;
        std::uint32_t Padding1 = 0;
    };

    struct ForwardPlusIndexGroup final
    {
        std::array<std::uint32_t, 4> Indices{};
    };

    struct alignas(16) ForwardPlusVisibilityUniforms final
    {
        std::array<std::uint32_t, 4> DispatchCounts{};
    };

    static_assert(sizeof(ForwardPlusVisibilityUniforms) == 16);

    inline constexpr std::size_t MaximumShadowedSpotLights = 8;
    inline constexpr std::size_t MaximumShadowedPointLights = 2;
    inline constexpr std::uint32_t LocalShadowResolution = 4096;
    inline constexpr std::uint32_t LocalShadowLayerCount =
        static_cast<std::uint32_t>(MaximumShadowedSpotLights + MaximumShadowedPointLights * 6U);

    struct AssetDirectionalShadowUniforms final
    {
        Vector4 DirectionalParameters;
        Vector4 DirectionalCascadeSplits;
        std::array<Matrix4, 4> DirectionalMatrices;
    };

    struct AssetLocalShadowUniforms final
    {
        std::array<Matrix4, LocalShadowLayerCount> Matrices;
        std::array<Vector4, MaximumShaderLocalLights> Parameters;
        std::array<Vector4, LocalShadowLayerCount> SampleBounds;
    };
    static_assert(offsetof(AssetLocalShadowUniforms, Parameters) == sizeof(std::array<Matrix4, LocalShadowLayerCount>));
    static_assert(offsetof(AssetLocalShadowUniforms, SampleBounds) ==
                  sizeof(std::array<Matrix4, LocalShadowLayerCount>) +
                      sizeof(std::array<Vector4, MaximumShaderLocalLights>));

    struct AssetShadowUniforms final
    {
        AssetDirectionalShadowUniforms Directional;
        AssetLocalShadowUniforms Local;
    };

    struct ShadowFrameData final
    {
        AssetDirectionalShadowUniforms Directional;
        AssetLocalShadowUniforms Local;
        std::array<float, MaximumShaderLocalLights> LocalLayers{};
    };

    struct AssetSceneUniforms final
    {
        Vector4 AmbientColorIntensity;
        Vector4 DirectionalColorIntensity;
        Vector4 DirectionalDirectionExposure;
        Vector4 SurfaceParameters;
        Vector4 LocalLightCounts;
        std::array<AssetLocalLightUniform, MaximumShaderLocalLights> LocalLights;
        Vector4 FrameParameters;
    };

    struct DeferredLightingUniforms final
    {
        Vector4 ClearColor;
        Vector4 AmbientColorIntensity;
        Vector4 DirectionalColorIntensity;
        Vector4 DirectionalDirectionExposure;
        Matrix4 InverseViewProjection;
        Matrix4 View;
        Matrix4 ViewProjection;
        Vector4 CameraPositionAndLocalLightCount;
        Vector4 ForwardPlusGrid;
        Vector4 ContactShadowParameters;
        /// Environment diffuse, environment specular, baked lighting, and reserved.
        Vector4 GlobalIlluminationChannels;
    };

    static_assert(sizeof(DeferredLightingUniforms) == sizeof(float) * 80U);

    struct IrradynUniforms final
    {
        Matrix4 InverseViewProjection;
        Matrix4 View;
        /// Stage, strength, ray count, and visibility-ray step count.
        Vector4 StageStrengthRaysSteps;
        /// Screen radius, maximum world distance, history weight, and history validity.
        Vector4 RadiusDistanceHistory;
        /// Frame index, scene-card count, visibility thickness, and reserved.
        Vector4 FrameCardsThickness;
        /// Requested trace-resolution divisor and low-frequency receiver strength; remaining lanes are reserved.
        Vector4 TraceLayout;
    };

    static_assert(sizeof(IrradynUniforms) == sizeof(float) * 48U);

    struct IrradynSceneCacheUniforms final
    {
        std::array<IrradynSceneCard, MaximumIrradynSceneCards> Cards;
    };

    static_assert(sizeof(IrradynSceneCacheUniforms) == sizeof(float) * 12U * MaximumIrradynSceneCards);

    struct AssetLocalLightUniforms final
    {
        Vector4 Counts;
        std::array<AssetLocalLightUniform, MaximumShaderLocalLights> Lights;
    };

    struct AssetEnvironmentUniforms final
    {
        std::array<Vector4, 9> DiffuseIrradiance;
        Vector4 Parameters;
        Vector4 Encoding;
    };

    struct DeferredSpatialLightingUniforms final
    {
        AssetEnvironmentUniforms Environment;
        std::array<Vector4, 8> CookieTransforms;
        std::array<Vector4, 2> CookieRotations;
        Vector4 DirectionalCookieAndContact;
        /// One-based contribution index, one-based directional mixed-mask channel, lightmap RGBE, reflection RGBE.
        Vector4 Context;
    };

    static_assert(sizeof(DeferredSpatialLightingUniforms) == sizeof(float) * 92U);

    struct AssetReflectionProbeUniform final
    {
        Matrix4 WorldToLocal;
        Matrix4 LocalToWorld;
        Vector4 ExtentsWeight;
        Vector4 Parameters;
    };

    inline constexpr std::uint32_t InvalidAssetSpatialSelectionIndex = std::numeric_limits<std::uint32_t>::max();
    inline constexpr std::uint32_t AssetSpatialSelectionHasLightProbe = 1U << 0U;
    inline constexpr std::uint32_t AssetSpatialSelectionHasReflectionProbe0 = 1U << 1U;
    inline constexpr std::uint32_t AssetSpatialSelectionHasReflectionProbe1 = 1U << 2U;
    inline constexpr std::uint32_t AssetSpatialSelectionUsedFailVisible = 1U << 3U;

    /// Frame-owned ABI-v3 result selected after same-frame spatial visibility classification.
    struct alignas(16) AssetSpatialSelectionRecord final
    {
        std::array<Vector4, 9> ProbeIrradiance{};
        std::array<AssetReflectionProbeUniform, 2> ReflectionProbes{};
        /// Flags, reflection mask index 0, reflection mask index 1, and light-probe-volume mask index.
        std::array<std::uint32_t, 4> Metadata{0U, InvalidAssetSpatialSelectionIndex, InvalidAssetSpatialSelectionIndex,
                                              InvalidAssetSpatialSelectionIndex};
    };

    struct AssetSpatialLightingUniforms final
    {
        Vector4 LightmapScaleOffset;
        Vector4 LightmapParameters;
        /// Renderer count, lightmap RGBE flag, reflection RGBE flag, and reserved.
        Vector4 ShadowMaskParameters;
        std::array<Vector4, 9> ProbeIrradiance;
        std::array<AssetReflectionProbeUniform, 2> ReflectionProbes;
        std::array<Vector4, 8> CookieTransforms;
        std::array<Vector4, 2> CookieRotations;
        Vector4 DirectionalCookieAndContact;
        Matrix4 ViewProjection;
        /// ABI v3 selection-record index followed by reserved zeros. UINT_MAX preserves the embedded v2 values.
        std::array<std::uint32_t, 4> SpatialSelection{InvalidAssetSpatialSelectionIndex, 0U, 0U, 0U};
    };

    struct AssetEnvironmentSpatialUniforms final
    {
        AssetEnvironmentUniforms Environment;
        AssetSpatialLightingUniforms Spatial;
    };

    static_assert(sizeof(AssetObjectUniforms) == sizeof(float) * 64);
    static_assert(sizeof(AssetSceneUniforms) == sizeof(float) * (24 + MaximumShaderLocalLights * 16));
    static_assert(sizeof(AssetLocalLightUniform) == sizeof(float) * 16);
    static_assert(sizeof(AssetLocalLightUniforms) == sizeof(float) * (4 + MaximumShaderLocalLights * 16));
    static_assert(sizeof(AssetSceneUniforms) <= 4096);
    static_assert(sizeof(AssetShadowUniforms) <= 4096);
    static_assert(sizeof(AssetLocalLightUniforms) <= 4096);
    static_assert(sizeof(AssetEnvironmentUniforms) == sizeof(float) * 44);
    static_assert(sizeof(AssetSpatialSelectionRecord) == sizeof(float) * 120);
    static_assert(alignof(AssetSpatialSelectionRecord) == 16);
    static_assert(sizeof(AssetSpatialLightingUniforms) == sizeof(float) * 192);
    static_assert(sizeof(AssetEnvironmentSpatialUniforms) == sizeof(float) * 236);

    struct SkyUniforms final
    {
        Matrix4 InverseProjection;
        Matrix4 InverseView;
        Vector4 Parameters;
    };

    struct GridUniforms final
    {
        Matrix4 InverseProjection;
        Matrix4 InverseView;
        Matrix4 ViewProjection;
        Vector4 Parameters;
    };

    static_assert(sizeof(GridUniforms) == sizeof(float) * 52);
} // namespace Keire::RenderBackend
