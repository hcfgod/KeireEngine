#pragma once

#include "Keire/Animation/Skinning.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Assets/LightingAssets.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/LightProbeVolumeComponent.h"
#include "Keire/ECS/Components/PointLightComponent.h"
#include "Keire/ECS/Components/ReflectionProbeComponent.h"
#include "Keire/ECS/Components/SpotLightComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Rendering/RenderSystem.h"
#include "Keire/Scenes/Scene.h"
#include "Keire/Vfx/VfxVolumeAsset.h"
#include "KeireInternal/Rendering/FrameGraphInternal.h"
#include "KeireInternal/Rendering/SpatialLightingInternal.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

struct ImDrawData;

namespace Keire::RenderBackend
{
    [[nodiscard]] inline std::string LastSdlError()
    {
        const char* error = SDL_GetError();
        return error && *error ? std::string(error) : std::string("SDL did not provide a diagnostic");
    }

    [[nodiscard]] inline bool ValidColor(const Color color) noexcept
    {
        const auto valid = [](const float value) { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; };
        return valid(color.Red) && valid(color.Green) && valid(color.Blue) && valid(color.Alpha);
    }

    [[nodiscard]] inline Vector3 Normalize(const Vector3 value) noexcept
    {
        const float length = std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
        return length > 0.000001F ? Vector3{value.X / length, value.Y / length, value.Z / length}
                                  : Vector3{0.0F, -1.0F, 0.0F};
    }

    [[nodiscard]] inline std::uint64_t HashDependencyStamp(std::uint64_t seed, const std::uint64_t value) noexcept
    {
        return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
    }

    [[nodiscard]] inline std::uint64_t HashDependencyStamp(std::uint64_t seed, const AssetId value) noexcept
    {
        seed = HashDependencyStamp(seed, value.High());
        return HashDependencyStamp(seed, value.Low());
    }

    [[nodiscard]] inline Color TemperatureColor(const float kelvin) noexcept
    {
        const float temperature = std::clamp(kelvin, 1000.0F, 20000.0F) / 100.0F;
        const float red = temperature <= 66.0F
                              ? 1.0F
                              : std::clamp(1.2929362F * std::pow(temperature - 60.0F, -0.13320476F), 0.0F, 1.0F);
        const float green = temperature <= 66.0F
                                ? std::clamp(0.39008158F * std::log(temperature) - 0.63184144F, 0.0F, 1.0F)
                                : std::clamp(1.1298909F * std::pow(temperature - 60.0F, -0.07551485F), 0.0F, 1.0F);
        const float blue = temperature >= 66.0F ? 1.0F
                           : temperature <= 19.0F
                               ? 0.0F
                               : std::clamp(0.5432068F * std::log(temperature - 10.0F) - 1.1962541F, 0.0F, 1.0F);
        return {red, green, blue, 1.0F};
    }

    [[nodiscard]] inline SDL_GPUPresentMode ToSdlPresentMode(const RenderPresentMode mode) noexcept
    {
        switch (mode)
        {
        case RenderPresentMode::Mailbox:
            return SDL_GPU_PRESENTMODE_MAILBOX;
        case RenderPresentMode::Immediate:
            return SDL_GPU_PRESENTMODE_IMMEDIATE;
        case RenderPresentMode::VSync:
        default:
            return SDL_GPU_PRESENTMODE_VSYNC;
        }
    }

    [[nodiscard]] constexpr std::uint32_t SdlAllowedFramesInFlight(const std::uint32_t requested) noexcept
    {
        return requested < 1U ? 1U : (requested > 3U ? 3U : requested);
    }

    [[nodiscard]] inline SDL_GPUSampleCount ToSdlSampleCount(const RenderSampleCount samples) noexcept
    {
        switch (samples)
        {
        case RenderSampleCount::Eight:
            return SDL_GPU_SAMPLECOUNT_8;
        case RenderSampleCount::Four:
            return SDL_GPU_SAMPLECOUNT_4;
        case RenderSampleCount::Two:
            return SDL_GPU_SAMPLECOUNT_2;
        case RenderSampleCount::One:
        default:
            return SDL_GPU_SAMPLECOUNT_1;
        }
    }

    [[nodiscard]] inline RenderSampleCount FromSdlSampleCount(const SDL_GPUSampleCount samples) noexcept
    {
        switch (samples)
        {
        case SDL_GPU_SAMPLECOUNT_8:
            return RenderSampleCount::Eight;
        case SDL_GPU_SAMPLECOUNT_4:
            return RenderSampleCount::Four;
        case SDL_GPU_SAMPLECOUNT_2:
            return RenderSampleCount::Two;
        case SDL_GPU_SAMPLECOUNT_1:
        default:
            return RenderSampleCount::One;
        }
    }

    struct SurfaceResources final
    {
        SDL_GPUTexture* SampledColor = nullptr;
        SDL_GPUTexture* ExchangeColor = nullptr;
        SDL_GPUTexture* HdrColor = nullptr;
        SDL_GPUTexture* MultisampleHdrColor = nullptr;
        SDL_GPUTexture* Depth = nullptr;
        SDL_GPUTexture* SampledDepth = nullptr;
        SDL_GPUTexture* DirectionalShadow = nullptr;
        SDL_GPUTexture* LocalShadow = nullptr;
        std::vector<SDL_GPUTexture*> TransientTextures;
        std::uint32_t DirectionalShadowResolution = 0;
        std::uint32_t DirectionalShadowLayers = 0;
        std::uint32_t LocalShadowResolution = 0;
        std::uint32_t LocalShadowLayers = 0;

        [[nodiscard]] bool Empty() const noexcept
        {
            return !SampledColor && !ExchangeColor && !HdrColor && !MultisampleHdrColor && !Depth && !SampledDepth &&
                   !DirectionalShadow && !LocalShadow;
        }
    };

    struct ForwardPlusGpuResources final
    {
        SDL_GPUBuffer* Lights = nullptr;
        SDL_GPUBuffer* Tiles = nullptr;
        SDL_GPUBuffer* LightIndices = nullptr;
        std::uint32_t Columns = 0;
        std::uint32_t Rows = 0;
        std::uint32_t LightCapacityBytes = 0;
        std::uint32_t TileCapacityBytes = 0;
        std::uint32_t LightIndexCapacityBytes = 0;

        [[nodiscard]] bool Empty() const noexcept { return !Lights && !Tiles && !LightIndices; }
    };

    struct RenderSharedState;

    struct RenderSurfaceState final
    {
        std::weak_ptr<RenderSharedState> Owner;
        RenderSurfaceSpecification Specification;
        std::uint64_t Id = 0;
        std::uint32_t RequestedWidth = 1;
        std::uint32_t RequestedHeight = 1;
        std::uint32_t Width = 0;
        std::uint32_t Height = 0;
        std::uint32_t FailedWidth = 0;
        std::uint32_t FailedHeight = 0;
        RenderSampleCount ActualSamples = RenderSampleCount::One;
        Color FrameClearColor;
        SurfaceResources Resources;
        ForwardPlusGpuResources ForwardPlus;
        Detail::ShadowAtlasAllocator ShadowAtlas{4096, 256};
        std::uint64_t ForwardPlusContentHash = 0;
        bool ForwardPlusContentValid = false;
        Matrix4 SampledDepthViewProjection;
        Matrix4 SampledDepthInverseViewProjection;
        bool SampledDepthValid = false;
        std::uint64_t Generation = 0;
        bool Submitted = false;
        bool HasOutput = false;
    };

    struct GpuMeshResources final
    {
        struct ShapeSample final
        {
            Vector4 A;
            Vector4 B;
            Vector4 C;
        };

        SDL_GPUBuffer* Vertices = nullptr;
        SDL_GPUBuffer* AssetVertices = nullptr;
        SDL_GPUBuffer* Indices = nullptr;
        std::uint32_t IndexCount = 0;
        std::vector<MeshSubmesh> Submeshes;
        std::vector<MeshLod> Lods;
        std::vector<AssetId> DefaultMaterials;
        std::vector<ShapeSample> ShapeSamples;
        float ShapeSampleWeight = 0.0F;
        std::uint64_t Revision = 0;

        [[nodiscard]] bool Empty() const noexcept { return !Vertices && !AssetVertices && !Indices; }
    };

    struct GpuTextureResources final
    {
        SDL_GPUTexture* Texture = nullptr;
        SDL_GPUSampler* Sampler = nullptr;
        bool HdrEncoded = false;
        TextureEnvironmentLayout EnvironmentLayout = TextureEnvironmentLayout::Auto;
        std::array<Vector4, 9> DiffuseIrradiance{};
        std::uint32_t MipLevels = 1;
        bool HasDiffuseIrradiance = false;

        [[nodiscard]] bool Empty() const noexcept { return !Texture && !Sampler; }
    };

    struct GpuSkinResources;

    struct InFlightFrame final
    {
        SDL_GPUFence* Fence = nullptr;
        std::vector<SurfaceResources> Retired;
        std::vector<GpuMeshResources> RetiredMeshes;
        std::vector<GpuSkinResources> RetiredSkins;
        std::vector<GpuTextureResources> RetiredTextures;
        std::vector<SDL_GPUGraphicsPipeline*> RetiredPipelines;
        std::vector<ForwardPlusGpuResources> RetiredForwardPlus;
        std::vector<SDL_GPUBuffer*> TransientBuffers;
        std::vector<SDL_GPUTransferBuffer*> TransientTransferBuffers;
        std::chrono::steady_clock::time_point SubmittedAt;
        bool IncludesGpuVfx = false;
    };

    struct GpuTextureEntry final
    {
        AssetHandle<Texture2DAsset> Handle;
        GpuTextureResources Resources;
        std::uint64_t LoadedRevision = 0;
        std::uint64_t LastAttemptedRevision = 0;
    };

    struct GpuLightingTextureEntry final
    {
        AssetHandle<LightingTextureArrayAsset> Handle;
        GpuTextureResources Resources;
        std::uint64_t LoadedRevision = 0;
        std::uint64_t LastAttemptedRevision = 0;
    };

    struct GpuShaderEntry final
    {
        struct Pipeline final
        {
            SDL_GPUSampleCount Samples = SDL_GPU_SAMPLECOUNT_1;
            MaterialAlphaMode AlphaMode = MaterialAlphaMode::Opaque;
            bool DoubleSided = false;
            SDL_GPUGraphicsPipeline* Handle = nullptr;
        };

        AssetHandle<ShaderAsset> Handle;
        Ref<const ShaderAsset> LastGood;
        std::vector<Pipeline> Pipelines;
        std::uint64_t LoadedRevision = 0;
        std::uint64_t LastAttemptedRevision = 0;
    };

    struct ResolvedAssetMaterial final
    {
        SDL_GPUGraphicsPipeline* Pipeline = nullptr;
        std::vector<Vector4> NumericProperties;
        std::vector<SDL_GPUTextureSamplerBinding> Textures;
        std::optional<std::size_t> TintSlot;
        MaterialSurfaceState Surface;
        bool ReceivesShadows = false;
        bool UsesForwardPlus = false;
        bool UsesInstancing = false;
        bool UsesImageBasedLighting = false;
        bool UsesSpatialLighting = false;
    };

    struct GpuMaterialEntry final
    {
        AssetHandle<MaterialAsset> Handle;
        Ref<const MaterialAsset> LastGood;
        ResolvedAssetMaterial Binding;
        std::uint64_t LoadedRevision = 0;
        std::uint64_t LastAttemptedRevision = 0;
        std::uint64_t LastAttemptedDependencyStamp = 0;
        std::uint64_t LastGoodDependencyStamp = 0;
    };

    struct GpuMeshEntry final
    {
        AssetHandle<MeshAsset> Handle;
        GpuMeshResources Resources;
        std::uint64_t LoadedRevision = 0;
        std::uint64_t LastAttemptedRevision = 0;
    };

    struct GpuSkinInstanceKey final
    {
        AssetId Scene;
        EntityId Entity;

        [[nodiscard]] bool operator==(const GpuSkinInstanceKey&) const noexcept = default;
    };

    struct GpuSkinInstanceKeyHash final
    {
        [[nodiscard]] std::size_t operator()(const GpuSkinInstanceKey& value) const noexcept
        {
            auto result = std::hash<AssetId>{}(value.Scene);
            result ^= std::hash<EntityId>{}(value.Entity) + 0x9e3779b9U + (result << 6U) + (result >> 2U);
            return result;
        }
    };

    struct GpuSkinOutputResources final
    {
        SDL_GPUBuffer* AssetVertices = nullptr;
        SDL_GPUBuffer* BuiltinVertices = nullptr;

        [[nodiscard]] bool Empty() const noexcept { return !AssetVertices && !BuiltinVertices; }
    };

    struct GpuSkinInstanceResources final
    {
        std::vector<GpuSkinOutputResources> Outputs;
        std::uint64_t LastPreparedFrame = 0;
    };

    struct GpuSkinResources final
    {
        SDL_GPUBuffer* Influences = nullptr;
        std::unordered_map<GpuSkinInstanceKey, GpuSkinInstanceResources, GpuSkinInstanceKeyHash> Instances;
        std::uint32_t VertexCount = 0;
        std::uint32_t MaximumBoneIndex = 0;
        std::uint32_t MaximumInfluences = 0;
        bool Valid = false;

        [[nodiscard]] bool Empty() const noexcept
        {
            if (Influences)
                return false;
            for (const auto& [key, instance] : Instances)
            {
                (void)key;
                for (const auto& output : instance.Outputs)
                    if (!output.Empty())
                        return false;
            }
            return true;
        }
    };

    struct GpuSkinEntry final
    {
        Ref<const SkinnedMeshAsset> Skin;
        Ref<const MeshAsset> Mesh;
        GpuSkinResources Resources;
        std::uint64_t LoadedDependencyStamp = 0;
        std::uint64_t LastAttemptedDependencyStamp = 0;
        std::uint64_t LastRequestedFrame = 0;
    };

    [[nodiscard]] constexpr std::size_t SkinningOutputSlot(const std::uint64_t frame,
                                                           const std::size_t maximumFramesInFlight) noexcept
    {
        return frame == 0 || maximumFramesInFlight == 0 ? 0 : (frame - 1U) % maximumFramesInFlight;
    }

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
    };

    static_assert(sizeof(ObjectUniforms) == sizeof(float) * 84);

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
    };

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
    };

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

    struct AssetReflectionProbeUniform final
    {
        Matrix4 WorldToLocal;
        Matrix4 LocalToWorld;
        Vector4 ExtentsWeight;
        Vector4 Parameters;
    };

    struct AssetSpatialLightingUniforms final
    {
        Vector4 LightmapScaleOffset;
        Vector4 LightmapParameters;
        Vector4 ShadowMaskParameters;
        std::array<Vector4, 9> ProbeIrradiance;
        std::array<AssetReflectionProbeUniform, 2> ReflectionProbes;
        std::array<Vector4, 8> CookieTransforms;
        std::array<Vector4, 2> CookieRotations;
        Vector4 DirectionalCookieAndContact;
        Matrix4 ViewProjection;
    };

    struct AssetEnvironmentSpatialUniforms final
    {
        AssetEnvironmentUniforms Environment;
        AssetSpatialLightingUniforms Spatial;
    };

    static_assert(sizeof(AssetObjectUniforms) == sizeof(float) * 64);
    static_assert(sizeof(AssetSceneUniforms) == sizeof(float) * (20 + MaximumShaderLocalLights * 16));
    static_assert(sizeof(AssetLocalLightUniform) == sizeof(float) * 16);
    static_assert(sizeof(AssetLocalLightUniforms) == sizeof(float) * (4 + MaximumShaderLocalLights * 16));
    static_assert(sizeof(AssetSceneUniforms) <= 4096);
    static_assert(sizeof(AssetShadowUniforms) <= 4096);
    static_assert(sizeof(AssetLocalLightUniforms) <= 4096);
    static_assert(sizeof(AssetEnvironmentUniforms) == sizeof(float) * 44);
    static_assert(sizeof(AssetSpatialLightingUniforms) == sizeof(float) * 188);
    static_assert(sizeof(AssetEnvironmentSpatialUniforms) == sizeof(float) * 232);

    struct SkyUniforms final
    {
        Matrix4 InverseProjection;
        Matrix4 InverseView;
        Vector4 Parameters;
    };

    struct SceneLighting final
    {
        EntityId Entity;
        Vector4 Direction{0.0F, -1.0F, 0.0F, 0.0F};
        Color ColorAndIntensity{1.0F, 1.0F, 1.0F, 0.0F};
        ShadowQuality Shadows = ShadowQuality::Disabled;
        float ShadowStrength = 1.0F;
        float ShadowBias = 0.005F;
        AssetId Cookie;
        Vector2 CookieScale{1.0F, 1.0F};
        Vector2 CookieOffset;
        float CookieRotationDegrees = 0.0F;
        bool ContactShadows = false;
        bool Enabled = false;
    };

    struct SceneDrawItem final
    {
        AssetId Mesh;
        std::vector<AssetId> Materials;
        Matrix4 World;
        Color Tint;
        EntityId Entity;
        AssetId Skin;
        AssetId SkinSkeleton;
        std::vector<Matrix4> SkinPalette;
        bool CastShadows = true;
        bool ReceiveShadows = true;
        SkinningMethod Skinning = SkinningMethod::LinearBlend;
        SDL_GPUBuffer* SkinnedAssetVertices = nullptr;
        SDL_GPUBuffer* SkinnedBuiltinVertices = nullptr;
    };

    [[nodiscard]] inline std::optional<SceneDrawItem> VfxMeshDrawItem(const VfxRenderParticle& particle)
    {
        if (particle.Renderer != VfxRendererType::Mesh || !particle.Mesh || particle.Size <= 0.0F)
            return std::nullopt;
        return SceneDrawItem{particle.Mesh,
                             particle.Material ? std::vector<AssetId>{particle.Material} : std::vector<AssetId>{},
                             Math::ComposeTransform(particle.Position,
                                                    Math::EulerDegreesToQuaternion(particle.Rotation),
                                                    {particle.Size, particle.Size, particle.Size}),
                             particle.Tint,
                             {},
                             {},
                             {},
                             {},
                             false,
                             true};
    }

    enum class SceneLocalLightType : std::uint8_t
    {
        Point,
        Spot
    };

    struct SceneLocalLight final
    {
        EntityId Entity;
        SceneLocalLightType Type = SceneLocalLightType::Point;
        Vector3 Position;
        float Range = 1.0F;
        Vector3 Direction{0.0F, 0.0F, 1.0F};
        float OuterConeCosine = -1.0F;
        Color ColorAndIntensity;
        float InnerConeCosine = 1.0F;
        ShadowQuality Shadows = ShadowQuality::Disabled;
        ShadowResolutionHint ShadowResolution = ShadowResolutionHint::Medium;
        float ShadowStrength = 1.0F;
        float ShadowBias = 0.0025F;
        AssetId Cookie;
        Vector2 CookieScale{1.0F, 1.0F};
        Vector2 CookieOffset;
        float CookieRotationDegrees = 0.0F;
        bool ContactShadows = false;
    };

    struct SceneRenderPacket final
    {
        AssetId Scene;
        RenderCamera Camera;
        RenderEnvironmentSettings Environment;
        SceneLighting Lighting;
        std::vector<SceneDrawItem> DrawItems;
        std::vector<SceneLocalLight> LocalLights;
        AssetId BakedLighting;
        std::vector<Detail::SpatialReflectionProbe> ReflectionProbes;
        struct LightProbeVolume final
        {
            EntityId Entity;
            Matrix4 LocalToWorld;
            Matrix4 WorldToLocal;
            Vector3 BoxExtents;
            Vector3 Spacing;
            std::int32_t Priority = 0;
            float NormalBias = 0.0F;
            float ViewBias = 0.0F;
        };
        std::vector<LightProbeVolume> LightProbeVolumes;
        bool DrawGrid = false;
        VfxRenderSnapshot Vfx;
    };

    [[nodiscard]] inline SceneLighting ResolveLighting(const Ref<Scene>& scene)
    {
        SceneLighting result;
        Entity selected;
        for (const auto& entity : scene->Query<DirectionalLightComponent>())
        {
            const auto light = entity.GetComponent<DirectionalLightComponent>();
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!light || !transform || !light->Enabled() || !entity.ActiveInHierarchy() ||
                light->BakeMode() == LightBakeMode::Baked)
                continue;
            if (selected && selected.Id().Value() < entity.Id().Value())
                continue;
            selected = entity;
            result.Entity = entity.Id();
            const auto direction = Normalize(Math::TransformDirection(transform->WorldMatrix(), {0.0F, 0.0F, 1.0F}));
            const auto temperature =
                light->UseColorTemperature() ? TemperatureColor(light->ColorTemperatureKelvin()) : Color{};
            const auto color = light->LightColor();
            result.Direction = {direction.X, direction.Y, direction.Z, 1.0F};
            result.ColorAndIntensity = {color.Red * temperature.Red, color.Green * temperature.Green,
                                        color.Blue * temperature.Blue, light->Intensity()};
            result.Shadows = light->Shadows();
            result.ShadowStrength = light->ShadowStrength();
            result.ShadowBias = light->ShadowBias();
            result.Cookie = light->Cookie();
            result.CookieScale = light->CookieScale();
            result.CookieOffset = light->CookieOffset();
            result.CookieRotationDegrees = light->CookieRotationDegrees();
            result.ContactShadows = light->ContactShadows();
            result.Enabled = true;
        }
        return result;
    }

    [[nodiscard]] inline std::vector<SceneLocalLight> ResolveLocalLights(const Ref<Scene>& scene)
    {
        std::vector<SceneLocalLight> result;
        result.reserve(std::min<std::size_t>(scene->ObjectCount(), 4096U));
        const auto addColor = [](const Color color, const float intensity)
        { return Color{color.Red, color.Green, color.Blue, intensity}; };
        for (const auto& entity : scene->Query<PointLightComponent>())
        {
            const auto light = entity.GetComponent<PointLightComponent>();
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!light || !transform || !light->Enabled() || !entity.ActiveInHierarchy() ||
                light->BakeMode() == LightBakeMode::Baked)
                continue;
            result.push_back({entity.Id(),
                              SceneLocalLightType::Point,
                              Math::TransformPoint(transform->WorldMatrix(), {}),
                              light->Range(),
                              {},
                              -1.0F,
                              addColor(light->LightColor(), light->Intensity()),
                              1.0F,
                              light->Shadows(),
                              light->ShadowResolution(),
                              light->ShadowStrength(),
                              light->ShadowBias(),
                              light->Cookie(),
                              {},
                              {},
                              0.0F,
                              light->ContactShadows()});
        }
        constexpr float degreesToRadians = 0.01745329251994329577F;
        for (const auto& entity : scene->Query<SpotLightComponent>())
        {
            const auto light = entity.GetComponent<SpotLightComponent>();
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!light || !transform || !light->Enabled() || !entity.ActiveInHierarchy() ||
                light->BakeMode() == LightBakeMode::Baked)
                continue;
            result.push_back(
                {entity.Id(), SceneLocalLightType::Spot, Math::TransformPoint(transform->WorldMatrix(), {}),
                 light->Range(), Normalize(Math::TransformDirection(transform->WorldMatrix(), {0.0F, 0.0F, 1.0F})),
                 std::cos(light->OuterAngleDegrees() * degreesToRadians),
                 addColor(light->LightColor(), light->Intensity()),
                 std::cos(light->InnerAngleDegrees() * degreesToRadians), light->Shadows(), light->ShadowResolution(),
                 light->ShadowStrength(), light->ShadowBias(), light->Cookie(), light->CookieScale(),
                 light->CookieOffset(), light->CookieRotationDegrees(), light->ContactShadows()});
        }
        std::ranges::sort(result, {}, &SceneLocalLight::Entity);
        if (result.size() > 4096U)
            result.resize(4096U);
        return result;
    }

    [[nodiscard]] inline std::vector<Detail::SpatialReflectionProbe> ResolveReflectionProbes(const Ref<Scene>& scene)
    {
        std::vector<Detail::SpatialReflectionProbe> result;
        for (const auto& entity : scene->Query<ReflectionProbeComponent>())
        {
            const auto probe = entity.GetComponent<ReflectionProbeComponent>();
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!probe || !transform || !probe->Enabled() || !entity.ActiveInHierarchy())
                continue;
            const auto world = transform->WorldMatrix();
            result.push_back({entity.Id().Value(), world, Math::Inverse(world), probe->BoxExtents(),
                              probe->BlendDistance(), probe->Importance(), 0, probe->Intensity(),
                              probe->BoxProjection()});
        }
        std::ranges::sort(result, {}, &Detail::SpatialReflectionProbe::Entity);
        return result;
    }

    [[nodiscard]] inline std::vector<SceneRenderPacket::LightProbeVolume>
    ResolveLightProbeVolumes(const Ref<Scene>& scene)
    {
        std::vector<SceneRenderPacket::LightProbeVolume> result;
        for (const auto& entity : scene->Query<LightProbeVolumeComponent>())
        {
            const auto volume = entity.GetComponent<LightProbeVolumeComponent>();
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!volume || !transform || !volume->Enabled() || !entity.ActiveInHierarchy())
                continue;
            const auto world = transform->WorldMatrix();
            result.push_back({entity.Id(), world, Math::Inverse(world), volume->BoxExtents(), volume->Spacing(),
                              volume->Priority(), volume->NormalBias(), volume->ViewBias()});
        }
        std::ranges::sort(result,
                          [](const auto& left, const auto& right)
                          {
                              if (left.Priority != right.Priority)
                                  return left.Priority > right.Priority;
                              return left.Entity < right.Entity;
                          });
        return result;
    }

    [[nodiscard]] inline Matrix4 Transpose(const Matrix4& value) noexcept
    {
        Matrix4 result;
        for (std::size_t row = 0; row < 4; ++row)
        {
            for (std::size_t column = 0; column < 4; ++column)
                result.Elements[column * 4 + row] = value.Elements[row * 4 + column];
        }
        return result;
    }

    [[nodiscard]] inline ObjectUniforms MakeObjectUniforms(const Matrix4& modelViewProjection, const Matrix4& model,
                                                           const Matrix4& view, const Color tint,
                                                           const SceneLighting& lighting,
                                                           const RenderEnvironmentSettings& environment,
                                                           const bool receiveLighting)
    {
        auto direction = lighting.Direction;
        direction.W = receiveLighting && lighting.Enabled ? 1.0F : 0.0F;
        return {modelViewProjection,
                Transpose(Math::Inverse(model)),
                tint,
                direction,
                lighting.ColorAndIntensity,
                {environment.AmbientColor.Red * environment.AmbientIntensity,
                 environment.AmbientColor.Green * environment.AmbientIntensity,
                 environment.AmbientColor.Blue * environment.AmbientIntensity, environment.Exposure},
                {receiveLighting ? 1.0F : 0.0F, 0.0F, 0.0F, 0.0F},
                model,
                view};
    }

    [[nodiscard]] inline std::vector<RenderVertex> CreateGridVertices()
    {
        constexpr int extent = 20;
        std::vector<RenderVertex> vertices;
        vertices.reserve(static_cast<std::size_t>((extent * 2 + 1) * 4));
        for (int coordinate = -extent; coordinate <= extent; ++coordinate)
        {
            const float value = static_cast<float>(coordinate);
            const Vector3 color = coordinate == 0 ? Vector3{0.30F, 0.50F, 0.78F} : Vector3{0.24F, 0.27F, 0.32F};
            vertices.push_back({{-static_cast<float>(extent), 0.0F, value}, color, {}});
            vertices.push_back({{static_cast<float>(extent), 0.0F, value}, color, {}});
            vertices.push_back({{value, 0.0F, -static_cast<float>(extent)}, color, {}});
            vertices.push_back({{value, 0.0F, static_cast<float>(extent)}, color, {}});
        }
        return vertices;
    }

    struct RenderPipelineSet final
    {
        SDL_GPUSampleCount Samples = SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUGraphicsPipeline* Cube = nullptr;
        SDL_GPUGraphicsPipeline* Grid = nullptr;
        SDL_GPUGraphicsPipeline* Sky = nullptr;
        SDL_GPUGraphicsPipeline* Vfx = nullptr;
        SDL_GPUGraphicsPipeline* GpuVfx = nullptr;
        SDL_GPUGraphicsPipeline* GpuVfxRibbon = nullptr;
        SDL_GPUGraphicsPipeline* GpuVfxMesh = nullptr;
    };

    struct alignas(16) VfxGpuCustomInstructionRecord final
    {
        /// Context, target, operation/flags, and expression register. Operation occupies bits 0-7; bit 8 scales by
        /// the operation-context delta; bits 16-23 contain VfxValueType. UINT32_MAX selects Operand instead of an
        /// expression register.
        std::array<std::uint32_t, 4> Metadata{};
        std::array<float, 4> Operand{};
    };

    struct alignas(16) VfxGpuParticleOperationRecord final
    {
        /// Context, operation kind, custom-instruction index, operation-specific setting.
        std::array<std::uint32_t, 4> Metadata{};
        /// First property, property count, first lifetime sample, lifetime sample count.
        std::array<std::uint32_t, 4> Payload{};
    };

    struct alignas(16) VfxGpuModulePropertyRecord final
    {
        /// VfxModuleProperty, VfxValueType, VfxGpuModulePropertySource, and source index.
        std::array<std::uint32_t, 4> Metadata{};
        VfxGpuValue LiteralValue;
    };

    static_assert(sizeof(VfxGpuCustomInstructionRecord) == 32);
    static_assert(alignof(VfxGpuCustomInstructionRecord) == 16);
    static_assert(sizeof(VfxGpuParticleOperationRecord) == 32);
    static_assert(alignof(VfxGpuParticleOperationRecord) == 16);
    static_assert(sizeof(VfxGpuModulePropertyRecord) == 48);
    static_assert(alignof(VfxGpuModulePropertyRecord) == 16);

    struct alignas(16) VfxGpuShapeResourceRecord final
    {
        std::array<std::uint32_t, 4> Metadata{};
    };

    struct alignas(16) VfxGpuShapeSampleRecord final
    {
        Vector4 A;
        Vector4 B;
        Vector4 C;
    };

    static_assert(sizeof(VfxGpuShapeResourceRecord) == 16);
    static_assert(sizeof(VfxGpuShapeSampleRecord) == 48);

    /// Returns an exact diagnostic for malformed or shader-unsupported renderer execution payloads.
    [[nodiscard]] std::optional<std::string> ValidateGpuVfxExecutionPayload(const VfxGpuExecutionPayload& payload);

    struct GpuVfxExecutionBuffers final
    {
        SDL_GPUBuffer* Instructions = nullptr;
        SDL_GPUBuffer* Sources = nullptr;
        SDL_GPUBuffer* Values = nullptr;
        SDL_GPUBuffer* CustomInstructions = nullptr;
        SDL_GPUBuffer* ParticleOperations = nullptr;
        SDL_GPUBuffer* ModuleProperties = nullptr;
        SDL_GPUBuffer* LifetimeSamples = nullptr;
        std::uint64_t ByteSize = 0;
    };

    struct GpuVfxRenderBuffers final
    {
        SDL_GPUBuffer* Indices = nullptr;
        SDL_GPUBuffer* IndirectArguments = nullptr;
        SDL_GPUBuffer* Instances = nullptr;
        std::uint32_t Capacity = 0;
        std::uint64_t ByteSize = 0;
    };

    struct GpuVfxShapeBuffers final
    {
        SDL_GPUBuffer* Table = nullptr;
        std::uint32_t ResourceCount = 0;
        std::uint32_t FirstSample = 0;
        std::uint64_t RevisionHash = 0;
        std::uint64_t ByteSize = 0;
    };

    struct GpuVfxVolumeEntry final
    {
        AssetHandle<VfxVolumeAsset> Handle;
        Ref<const VfxVolumeAsset> LastGood;
        std::uint64_t LoadedRevision = 0;
    };

    inline constexpr std::uint32_t MinimumGpuVfxPoolCapacity = 16'384;

    [[nodiscard]] inline std::uint32_t
    GpuVfxActiveParticleBudget(const std::uint32_t logicalLimit, const std::span<const VfxGpuEmitter> emitters) noexcept
    {
        std::uint64_t required = 0;
        for (const auto& emitter : emitters)
        {
            required += std::max(emitter.Capacity, 1U);
            if (required >= logicalLimit)
                return logicalLimit;
        }
        return static_cast<std::uint32_t>(required);
    }

    /// Selects an amortized physical pool size from the capacities of all live systems. The VfxWorld logical budget
    /// remains the hard limit, while small scenes avoid paying to allocate and scan the entire production ceiling.
    [[nodiscard]] inline std::uint32_t SelectGpuVfxPoolCapacity(const std::uint32_t logicalLimit,
                                                                const std::uint32_t currentCapacity,
                                                                const std::span<const VfxGpuEmitter> emitters) noexcept
    {
        if (logicalLimit == 0 || emitters.empty())
            return std::min(currentCapacity, logicalLimit);

        std::uint64_t required = GpuVfxActiveParticleBudget(logicalLimit, emitters);
        required = std::max(required, static_cast<std::uint64_t>(std::min(logicalLimit, MinimumGpuVfxPoolCapacity)));
        const auto amortized = std::bit_ceil(required);
        const auto selected =
            std::min<std::uint64_t>(logicalLimit, std::max<std::uint64_t>(currentCapacity, amortized));
        return static_cast<std::uint32_t>(selected);
    }

    struct GpuVfxWorldResources final
    {
        struct EmitterState final
        {
            std::uint64_t SpawnSequence = 0;
            std::uint64_t SimulationRevision = 0;
            Vector3 Position;
            Quaternion Rotation;
            VfxSimulationSpace Space = VfxSimulationSpace::World;
            VfxRendererType Renderer = VfxRendererType::Sprite;
            AssetId Sprite;
            AssetId Mesh;
            AssetId Material;
            bool MaterialDiagnosticReported = false;
            bool HasCompactedParticles = false;
            std::shared_ptr<GpuVfxRenderBuffers> RenderBuffers;
            std::shared_ptr<const VfxGpuExecutionPayload> Execution;
            std::shared_ptr<GpuVfxExecutionBuffers> ExecutionBuffers;
            std::shared_ptr<GpuVfxShapeBuffers> ShapeBuffers;
        };

        SDL_GPUBuffer* Particles = nullptr;
        SDL_GPUBuffer* FreeIndices = nullptr;
        SDL_GPUBuffer* AliveIndices = nullptr;
        SDL_GPUBuffer* Counters = nullptr;
        SDL_GPUBuffer* IndirectArguments = nullptr;
        std::unordered_map<std::uint64_t, EmitterState> Emitters;
        std::unordered_map<const VfxGpuExecutionPayload*, std::weak_ptr<GpuVfxExecutionBuffers>> ExecutionCache;
        std::uint32_t Capacity = 0;
        std::uint64_t ResetRevision = 0;
        std::uint64_t LastPreparedFrame = 0;
        std::uint64_t LastAppliedSnapshotRevision = 0;
        std::uint64_t LastConsumedSimulationStepRevision = 0;
        bool HasAppliedSnapshot = false;
        bool HasConsumedSimulationStep = false;

        [[nodiscard]] bool Empty() const noexcept { return Particles == nullptr; }
        [[nodiscard]] bool ShouldApplySnapshot(const std::uint64_t revision) const noexcept
        {
            return !HasAppliedSnapshot || revision > LastAppliedSnapshotRevision;
        }
        [[nodiscard]] bool ShouldConsumeSimulationStep(const std::uint64_t revision) const noexcept
        {
            return !HasConsumedSimulationStep || revision > LastConsumedSimulationStepRevision;
        }
        void MarkSnapshotApplied(const std::uint64_t revision) noexcept
        {
            LastAppliedSnapshotRevision = revision;
            HasAppliedSnapshot = true;
        }
        void MarkSimulationStepConsumed(const std::uint64_t revision) noexcept
        {
            LastConsumedSimulationStepRevision = revision;
            HasConsumedSimulationStep = true;
        }
        void InvalidateSequencing() noexcept
        {
            Emitters.clear();
            ExecutionCache.clear();
            ResetRevision = 0;
            LastPreparedFrame = 0;
            LastAppliedSnapshotRevision = 0;
            LastConsumedSimulationStepRevision = 0;
            HasAppliedSnapshot = false;
            HasConsumedSimulationStep = false;
        }
    };

    enum class SceneDrawPhase : std::uint8_t
    {
        Opaque,
        Transparent
    };

    struct PreparedSceneDraw final
    {
        const SceneDrawItem* Item = nullptr;
        MeshSubmesh Submesh;
        AssetId Material;
        MaterialSurfaceState Surface;
        float Depth = 0.0F;
        std::uint32_t SubmeshIndex = 0;
    };

    struct PreparedSceneBatch final
    {
        std::uint32_t First = 0;
        std::uint32_t Count = 0;
        std::uint32_t GpuFirstInstance = 0;
        SDL_GPUBuffer* InstanceBuffer = nullptr;
    };

    struct PreparedSceneDrawList final
    {
        std::vector<PreparedSceneDraw> Draws;
        std::vector<PreparedSceneBatch> Batches;
    };

    struct PreparedSceneDrawLists final
    {
        PreparedSceneDrawList Opaque;
        PreparedSceneDrawList Transparent;
    };

    struct QueuedSceneRequest final
    {
        SceneRenderPacket Packet;
        RenderSurfaceState* Surface = nullptr;
    };

    enum class GpuVfxPipelineWarmupState : std::uint8_t
    {
        NotStarted,
        Compiling,
        Ready,
        Failed
    };

    struct RenderSharedState final : public std::enable_shared_from_this<RenderSharedState>
    {
        RenderSharedState(RenderSpecification specification, Ref<WindowSystem> windows, Ref<Window> window,
                          Ref<AssetSystem> assets);
        ~RenderSharedState();

        void RequireOwner(const char* operation) const;
        [[nodiscard]] std::vector<std::shared_ptr<RenderSurfaceState>> LiveSurfaces();
        void ReleaseResources(SurfaceResources& resources) noexcept;
        [[nodiscard]] SDL_GPUShader* CreateShader(bool vertex) const;
        [[nodiscard]] SDL_GPUShader* CreateSkyShader(bool vertex) const;
        [[nodiscard]] SDL_GPUShader* CreateShadowShader(bool vertex) const;
        [[nodiscard]] SDL_GPUShader* CreateToneMapShader(bool vertex) const;
        void EnsureFrameUploadContext();
        [[nodiscard]] SDL_GPUBuffer* UploadBuffer(std::span<const std::byte> bytes, SDL_GPUBufferUsageFlags usage);
        [[nodiscard]] SDL_GPUBuffer* UploadBuffer(SDL_GPUCommandBuffer* commands, std::span<const std::byte> bytes,
                                                  SDL_GPUBufferUsageFlags usage);
        [[nodiscard]] SDL_GPUBuffer* UploadVertexBuffer(std::span<const RenderVertex> vertices);
        [[nodiscard]] SDL_GPUBuffer* UploadVertexBuffer(SDL_GPUCommandBuffer* commands,
                                                        std::span<const RenderVertex> vertices);
        [[nodiscard]] SDL_GPUBuffer* UploadMeshVertexBuffer(std::span<const MeshVertex> vertices);
        [[nodiscard]] SDL_GPUBuffer* UploadMeshVertexBuffer(SDL_GPUCommandBuffer* commands,
                                                            std::span<const MeshVertex> vertices);
        [[nodiscard]] SDL_GPUSampler* ResolveSampler(const SamplerDescription& description);
        [[nodiscard]] GpuTextureResources CreateTextureResources(const Texture2DAsset& asset);
        [[nodiscard]] GpuTextureResources CreateLightingTextureResources(const LightingTextureArrayAsset& asset);
        [[nodiscard]] GpuMeshResources CreateMeshResources(const MeshAsset& mesh);

        void CollectCompletedFrames();
        void BeginFrame();
        void CancelFrame() noexcept;
        void Submit(SceneRenderRequest request);
        [[nodiscard]] const GpuMeshResources& ResolveMesh(AssetId id);
        [[nodiscard]] const GpuTextureResources& ResolveTexture(AssetId id);
        [[nodiscard]] const GpuTextureResources& ResolveLightingTexture(AssetId id, bool cubeArray = false,
                                                                        bool whiteFallback = false);
        [[nodiscard]] const GpuTextureResources& ResolveCookieAtlas(std::span<const AssetId> cookies);
        [[nodiscard]] Ref<const LightingSetAsset> ResolveLightingSet(AssetId id);
        [[nodiscard]] Ref<const LightProbeVolumeAsset> ResolveLightProbeVolume(AssetId id);
        [[nodiscard]] const GpuTextureResources& DefaultTexture(ShaderTextureSemantic semantic) const noexcept;
        [[nodiscard]] Ref<const MaterialAsset> ResolveMaterial(AssetId id);
        [[nodiscard]] GpuShaderEntry* ResolveShader(AssetId id, SDL_GPUSampleCount samples,
                                                    MaterialSurfaceState surface, bool explicitSurface);
        [[nodiscard]] const ResolvedAssetMaterial* ResolveAssetMaterial(AssetId id, SDL_GPUSampleCount samples);
        [[nodiscard]] bool EnsureSkinningPipeline();
        void StartGpuVfxPipelineWarmup();
        void CompileGpuVfxPipelines();
        void ReleaseGpuVfxPipelines() noexcept;
        void PrepareSkinning(SDL_GPUCommandBuffer* commands, SceneRenderPacket& packet);
        [[nodiscard]] bool EnsureGpuVfxPipelines(bool requireStripPipelines);
        void PrepareGpuVfx(SDL_GPUCommandBuffer* commands, const VfxRenderSnapshot& snapshot,
                           const RenderSurfaceState& surface);
        void ReleaseGpuVfxWorld(GpuVfxWorldResources& resources) noexcept;

        [[nodiscard]] PreparedSceneDrawLists PrepareSceneDrawLists(SDL_GPUCommandBuffer* commands,
                                                                   RenderSurfaceState& surface,
                                                                   const SceneRenderPacket& packet);
        void DrawScene(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderSurfaceState& surface,
                       const SceneRenderPacket& packet, const ShadowFrameData& shadows, SceneDrawPhase phase,
                       const PreparedSceneDrawList& prepared);
        void DrawVfx(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderSurfaceState& surface,
                     const SceneRenderPacket& packet, const ShadowFrameData& shadows);
        [[nodiscard]] ShadowFrameData RecordShadows(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                    const SceneRenderPacket& packet);
        void RecordSampledDepth(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                const SceneRenderPacket& packet);
        void RecordSurface(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface);
        void EndFrame(ImDrawData* drawData);
        void ExecuteFrame(ImDrawData* drawData);
        void StartRenderThread();
        void StopRenderThread() noexcept;
        void DispatchRender(std::function<void()> work);
        void Close() noexcept;

        void CreateGeometryResources();
        void ReleaseMeshResources(GpuMeshResources& resources) noexcept;
        void ReleaseGpuSkinResources(GpuSkinResources& resources) noexcept;
        void ReleaseTextureResources(GpuTextureResources& resources) noexcept;
        void ReleaseForwardPlusResources(ForwardPlusGpuResources& resources) noexcept;
        void Retire(GpuTextureResources resources) noexcept;
        void Retire(SDL_GPUGraphicsPipeline* pipeline) noexcept;
        void Retire(GpuMeshResources resources) noexcept;
        void Retire(GpuSkinResources resources) noexcept;
        void Retire(SurfaceResources resources) noexcept;
        void Retire(ForwardPlusGpuResources resources) noexcept;
        void RetireSurface(RenderSurfaceState& surface) noexcept;
        [[nodiscard]] SDL_GPUSampleCount ResolveSamples(RenderSampleCount requested) const noexcept;
        [[nodiscard]] SurfaceResources CreateResources(const RenderSurfaceState& surface, SDL_GPUSampleCount samples);
        void EnsureSurface(RenderSurfaceState& surface);
        [[nodiscard]] SDL_GPUGraphicsPipeline*
        CreatePipeline(SDL_GPUSampleCount samples, SDL_GPUPrimitiveType primitive, bool depthWrite, bool blend = false);
        [[nodiscard]] SDL_GPUGraphicsPipeline* CreateSkyPipeline(SDL_GPUSampleCount samples);
        [[nodiscard]] SDL_GPUGraphicsPipeline* CreateCpuVfxPipeline(SDL_GPUSampleCount samples);
        [[nodiscard]] SDL_GPUGraphicsPipeline* CreateGpuVfxPipeline(SDL_GPUSampleCount samples, bool ribbon = false);
        [[nodiscard]] SDL_GPUGraphicsPipeline* CreateGpuVfxMeshPipeline(SDL_GPUSampleCount samples);
        [[nodiscard]] SDL_GPUGraphicsPipeline* CreateDepthPipeline(bool depthBias);
        [[nodiscard]] SDL_GPUGraphicsPipeline* CreateToneMapPipeline();
        void RecordToneMap(SDL_GPUCommandBuffer* commands, const RenderSurfaceState& surface);
        [[nodiscard]] RenderPipelineSet& PipelinesFor(SDL_GPUSampleCount samples);
        [[nodiscard]] SDL_GPUShader* CreateAssetShader(const ShaderAssetDefinition& definition, bool vertex) const;
        [[nodiscard]] SDL_GPUGraphicsPipeline* CreateAssetPipeline(const ShaderAssetDefinition& definition,
                                                                   SDL_GPUSampleCount samples,
                                                                   MaterialSurfaceState surface);

        RenderSpecification Specification;
        Ref<WindowSystem> Windows;
        Ref<Window> Window;
        Ref<AssetSystem> Assets;
        std::thread::id OwnerThread;
        std::thread::id RenderThreadId;
        SDL_Window* NativeWindow = nullptr;
        SDL_GPUDevice* Device = nullptr;
        SDL_GPUPresentMode PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
        SDL_GPUTextureFormat ColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        SDL_GPUTextureFormat SceneColorFormat = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        SDL_GPUTextureFormat DepthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
        SDL_GPUTextureFormat ShadowDepthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
        SDL_GPUBuffer* GridBuffer = nullptr;
        SDL_GPUGraphicsPipeline* ShadowPipeline = nullptr;
        SDL_GPUGraphicsPipeline* SceneDepthPipeline = nullptr;
        SDL_GPUGraphicsPipeline* ToneMapPipeline = nullptr;
        SDL_GPUSampler* ShadowSampler = nullptr;
        SDL_GPUSampler* ToneMapSampler = nullptr;
        SDL_GPUTexture* EmptyShadowTexture = nullptr;
        std::uint32_t GridVertexCount = 0;
        GpuMeshResources DefaultMesh;
        GpuMeshResources ErrorMesh;
        GpuTextureResources CheckerboardTexture;
        GpuTextureResources DefaultSkyTexture;
        GpuTextureResources BrdfIntegrationLut;
        GpuTextureResources WhiteTexture;
        GpuTextureResources FlatNormalTexture;
        GpuTextureResources NeutralOrmTexture;
        GpuTextureResources BlackTexture;
        GpuTextureResources BlackDataTexture;
        GpuTextureResources WhiteDataTexture;
        GpuTextureResources DefaultLightingArray;
        GpuTextureResources DefaultLightingMaskArray;
        GpuTextureResources DefaultReflectionCubeArray;
        GpuTextureResources CookieAtlas;
        std::array<AssetId, 8> CookieAtlasAssets{};
        std::array<AssetHandle<Texture2DAsset>, 8> CookieAtlasHandles{};
        std::array<std::uint64_t, 8> CookieAtlasRevisions{};
        std::unordered_map<AssetId, GpuMeshEntry> MeshCache;
        std::unordered_map<AssetId, GpuSkinEntry> SkinCache;
        std::unordered_map<AssetId, GpuTextureEntry> TextureCache;
        std::unordered_map<AssetId, GpuLightingTextureEntry> LightingTextureCache;
        std::unordered_map<AssetId, AssetHandle<LightingSetAsset>> LightingSetCache;
        std::unordered_map<AssetId, AssetHandle<LightProbeVolumeAsset>> LightProbeVolumeCache;
        std::unordered_map<AssetId, GpuMaterialEntry> MaterialCache;
        std::uint64_t MaterialBindingBuilds = 0;
        std::unordered_map<AssetId, GpuShaderEntry> ShaderCache;
        std::vector<std::pair<SamplerDescription, SDL_GPUSampler*>> SamplerCache;
        std::vector<RenderPipelineSet> Pipelines;
        std::vector<std::weak_ptr<RenderSurfaceState>> Surfaces;
        std::vector<QueuedSceneRequest> Requests;
        std::vector<SurfaceResources> PendingRetired;
        std::vector<GpuMeshResources> PendingRetiredMeshes;
        std::vector<GpuSkinResources> PendingRetiredSkins;
        std::vector<GpuTextureResources> PendingRetiredTextures;
        std::vector<SDL_GPUGraphicsPipeline*> PendingRetiredPipelines;
        std::vector<ForwardPlusGpuResources> PendingRetiredForwardPlus;
        std::vector<SDL_GPUBuffer*> FrameTransientBuffers;
        std::vector<SDL_GPUTransferBuffer*> FrameUploadTransfers;
        SDL_GPUCommandBuffer* FrameUploadCommands = nullptr;
        SDL_GPUCopyPass* FrameUploadPass = nullptr;
        SDL_GPUComputePipeline* SkinningPipeline = nullptr;
        bool SkinningPipelineAttempted = false;
        SDL_GPUComputePipeline* VfxInitializePipeline = nullptr;
        SDL_GPUComputePipeline* VfxResetPipeline = nullptr;
        SDL_GPUComputePipeline* VfxKillPipeline = nullptr;
        SDL_GPUComputePipeline* VfxTransformPipeline = nullptr;
        SDL_GPUComputePipeline* VfxSimulatePipeline = nullptr;
        SDL_GPUComputePipeline* VfxSimulateOutputPipeline = nullptr;
        SDL_GPUComputePipeline* VfxSpawnPipeline = nullptr;
        SDL_GPUComputePipeline* VfxSpawnInitializePipeline = nullptr;
        SDL_GPUComputePipeline* VfxSpawnOutputPipeline = nullptr;
        SDL_GPUComputePipeline* VfxMapStripsPipeline = nullptr;
        SDL_GPUComputePipeline* VfxLinkStripsPipeline = nullptr;
        SDL_GPUComputePipeline* VfxFinalizePipeline = nullptr;
        SDL_GPUComputePipeline* VfxResetRenderPipeline = nullptr;
        SDL_GPUComputePipeline* VfxFilterRenderPipeline = nullptr;
        std::jthread VfxPipelineWarmupThread;
        std::atomic<GpuVfxPipelineWarmupState> VfxPipelineWarmupState{GpuVfxPipelineWarmupState::NotStarted};
        std::atomic<std::uint64_t> VfxPipelineWarmupMicroseconds{0};
        std::string VfxPipelineWarmupFailure;
        std::unordered_map<std::uint64_t, GpuVfxWorldResources> GpuVfxWorlds;
        std::unordered_map<AssetId, GpuVfxVolumeEntry> VfxVolumeCache;
        std::deque<InFlightFrame> InFlight;
        StaticSceneFrameGraph SceneFrameGraph;
        RenderStatistics Statistics;
        std::uint64_t NextSurfaceId = 1;
        std::mutex RenderQueueMutex;
        std::condition_variable RenderQueueReady;
        std::condition_variable RenderQueueSpace;
        std::deque<std::function<void()>> RenderQueue;
        std::deque<float> PreparationSamples;
        std::uint64_t SkinningStaticBuilds = 0;
        std::uint64_t SkinningOutputBuilds = 0;
        std::uint64_t GpuSubmissionSerial = 0;
        std::uint64_t ActiveGpuSubmissionSerial = 0;
        std::jthread RenderThread;
        bool StopRenderQueue = false;
        std::atomic<bool> InjectDeviceLossAtNextFrame{false};
        bool WindowClaimed = false;
        bool FrameActive = false;
        bool FrameExecutionActive = false;
        bool Open = true;
    };
} // namespace Keire::RenderBackend
