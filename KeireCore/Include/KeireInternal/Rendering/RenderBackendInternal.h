#pragma once

#include "Keire/Animation/Skinning.h"
#include "Keire/Assets/AssetSystem.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/PointLightComponent.h"
#include "Keire/ECS/Components/SpotLightComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Rendering/RenderSystem.h"
#include "Keire/Scenes/Scene.h"
#include "KeireInternal/Rendering/FrameGraphInternal.h"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
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
        std::uint64_t ForwardPlusContentHash = 0;
        bool ForwardPlusContentValid = false;
        std::uint64_t Generation = 0;
        bool Submitted = false;
        bool HasOutput = false;
    };

    struct GpuMeshResources final
    {
        SDL_GPUBuffer* Vertices = nullptr;
        SDL_GPUBuffer* AssetVertices = nullptr;
        SDL_GPUBuffer* Indices = nullptr;
        std::uint32_t IndexCount = 0;
        std::vector<MeshSubmesh> Submeshes;
        std::vector<MeshLod> Lods;
        std::vector<AssetId> DefaultMaterials;

        [[nodiscard]] bool Empty() const noexcept { return !Vertices && !AssetVertices && !Indices; }
    };

    struct GpuTextureResources final
    {
        SDL_GPUTexture* Texture = nullptr;
        SDL_GPUSampler* Sampler = nullptr;
        bool HdrEncoded = false;
        TextureEnvironmentLayout EnvironmentLayout = TextureEnvironmentLayout::Auto;

        [[nodiscard]] bool Empty() const noexcept { return !Texture && !Sampler; }
    };

    struct InFlightFrame final
    {
        SDL_GPUFence* Fence = nullptr;
        std::vector<SurfaceResources> Retired;
        std::vector<GpuMeshResources> RetiredMeshes;
        std::vector<GpuTextureResources> RetiredTextures;
        std::vector<SDL_GPUGraphicsPipeline*> RetiredPipelines;
        std::vector<ForwardPlusGpuResources> RetiredForwardPlus;
        std::vector<SDL_GPUBuffer*> TransientBuffers;
    };

    struct GpuTextureEntry final
    {
        AssetHandle<Texture2DAsset> Handle;
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

    struct RenderVertex final
    {
        Vector3 Position;
        Vector3 Color;
        Vector3 Normal;
    };

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
    inline constexpr std::uint32_t LocalShadowResolution = 1024;
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

    static_assert(sizeof(AssetObjectUniforms) == sizeof(float) * 64);
    static_assert(sizeof(AssetSceneUniforms) == sizeof(float) * (20 + MaximumShaderLocalLights * 16));
    static_assert(sizeof(AssetLocalLightUniform) == sizeof(float) * 16);
    static_assert(sizeof(AssetLocalLightUniforms) == sizeof(float) * (4 + MaximumShaderLocalLights * 16));
    static_assert(sizeof(AssetSceneUniforms) <= 4096);
    static_assert(sizeof(AssetShadowUniforms) <= 4096);
    static_assert(sizeof(AssetLocalLightUniforms) <= 4096);

    struct SkyUniforms final
    {
        Matrix4 InverseProjection;
        Matrix4 InverseView;
        Vector4 Parameters;
    };

    struct SceneLighting final
    {
        Vector4 Direction{0.0F, -1.0F, 0.0F, 0.0F};
        Color ColorAndIntensity{1.0F, 1.0F, 1.0F, 0.0F};
        ShadowQuality Shadows = ShadowQuality::Disabled;
        float ShadowStrength = 1.0F;
        float ShadowBias = 0.005F;
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
        float ShadowStrength = 1.0F;
        float ShadowBias = 0.0025F;
    };

    struct SceneRenderPacket final
    {
        RenderCamera Camera;
        RenderEnvironmentSettings Environment;
        SceneLighting Lighting;
        std::vector<SceneDrawItem> DrawItems;
        std::vector<SceneLocalLight> LocalLights;
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
            if (!light || !transform || !light->Enabled() || !entity.ActiveInHierarchy())
                continue;
            if (selected && selected.Id().Value() < entity.Id().Value())
                continue;
            selected = entity;
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
            if (!light || !transform || !light->Enabled() || !entity.ActiveInHierarchy())
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
                              light->ShadowStrength(),
                              light->ShadowBias()});
        }
        constexpr float degreesToRadians = 0.01745329251994329577F;
        for (const auto& entity : scene->Query<SpotLightComponent>())
        {
            const auto light = entity.GetComponent<SpotLightComponent>();
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!light || !transform || !light->Enabled() || !entity.ActiveInHierarchy())
                continue;
            result.push_back({entity.Id(), SceneLocalLightType::Spot,
                              Math::TransformPoint(transform->WorldMatrix(), {}), light->Range(),
                              Normalize(Math::TransformDirection(transform->WorldMatrix(), {0.0F, 0.0F, 1.0F})),
                              std::cos(light->OuterAngleDegrees() * degreesToRadians),
                              addColor(light->LightColor(), light->Intensity()),
                              std::cos(light->InnerAngleDegrees() * degreesToRadians), light->Shadows(),
                              light->ShadowStrength(), light->ShadowBias()});
        }
        std::ranges::sort(result, {}, &SceneLocalLight::Entity);
        if (result.size() > 4096U)
            result.resize(4096U);
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
    };

    struct GpuVfxWorldResources final
    {
        SDL_GPUBuffer* Particles = nullptr;
        SDL_GPUBuffer* FreeIndices = nullptr;
        SDL_GPUBuffer* AliveIndices = nullptr;
        SDL_GPUBuffer* Counters = nullptr;
        SDL_GPUBuffer* IndirectArguments = nullptr;
        std::unordered_map<std::uint64_t, std::uint64_t> SpawnSequences;
        std::uint32_t Capacity = 0;
        std::uint64_t ResetRevision = 0;
        std::uint64_t LastPreparedFrame = 0;

        [[nodiscard]] bool Empty() const noexcept { return Particles == nullptr; }
    };

    enum class SceneDrawPhase : std::uint8_t
    {
        Opaque,
        Transparent
    };

    struct QueuedSceneRequest final
    {
        SceneRenderPacket Packet;
        RenderSurfaceState* Surface = nullptr;
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
        [[nodiscard]] SDL_GPUBuffer* UploadVertexBuffer(std::span<const RenderVertex> vertices);
        [[nodiscard]] SDL_GPUSampler* ResolveSampler(const SamplerDescription& description);
        [[nodiscard]] GpuTextureResources CreateTextureResources(const Texture2DAsset& asset);
        [[nodiscard]] GpuMeshResources CreateMeshResources(const MeshAsset& mesh);

        void CollectCompletedFrames();
        void BeginFrame();
        void CancelFrame() noexcept;
        void Submit(SceneRenderRequest request);
        [[nodiscard]] const GpuMeshResources& ResolveMesh(AssetId id);
        [[nodiscard]] const GpuTextureResources& ResolveTexture(AssetId id);
        [[nodiscard]] const GpuTextureResources& DefaultTexture(ShaderTextureSemantic semantic) const noexcept;
        [[nodiscard]] Ref<const MaterialAsset> ResolveMaterial(AssetId id);
        [[nodiscard]] GpuShaderEntry* ResolveShader(AssetId id, SDL_GPUSampleCount samples,
                                                    MaterialSurfaceState surface, bool explicitSurface);
        [[nodiscard]] const ResolvedAssetMaterial* ResolveAssetMaterial(AssetId id, SDL_GPUSampleCount samples);
        [[nodiscard]] bool EnsureSkinningPipeline();
        void PrepareSkinning(SDL_GPUCommandBuffer* commands, SceneRenderPacket& packet);
        [[nodiscard]] bool EnsureGpuVfxPipelines();
        void PrepareGpuVfx(SDL_GPUCommandBuffer* commands, const VfxRenderSnapshot& snapshot);
        void ReleaseGpuVfxWorld(GpuVfxWorldResources& resources) noexcept;

        void DrawScene(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderSurfaceState& surface,
                       const SceneRenderPacket& packet, const ShadowFrameData& shadows, SceneDrawPhase phase);
        void DrawVfx(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderSurfaceState& surface,
                     const SceneRenderPacket& packet);
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
        void ReleaseTextureResources(GpuTextureResources& resources) noexcept;
        void ReleaseForwardPlusResources(ForwardPlusGpuResources& resources) noexcept;
        void Retire(GpuTextureResources resources) noexcept;
        void Retire(SDL_GPUGraphicsPipeline* pipeline) noexcept;
        void Retire(GpuMeshResources resources) noexcept;
        void Retire(SurfaceResources resources) noexcept;
        void Retire(ForwardPlusGpuResources resources) noexcept;
        void RetireSurface(RenderSurfaceState& surface) noexcept;
        [[nodiscard]] SDL_GPUSampleCount ResolveSamples(RenderSampleCount requested) const noexcept;
        [[nodiscard]] SurfaceResources CreateResources(const RenderSurfaceState& surface, SDL_GPUSampleCount samples);
        void EnsureSurface(RenderSurfaceState& surface);
        [[nodiscard]] SDL_GPUGraphicsPipeline*
        CreatePipeline(SDL_GPUSampleCount samples, SDL_GPUPrimitiveType primitive, bool depthWrite, bool blend = false);
        [[nodiscard]] SDL_GPUGraphicsPipeline* CreateSkyPipeline(SDL_GPUSampleCount samples);
        [[nodiscard]] SDL_GPUGraphicsPipeline* CreateGpuVfxPipeline(SDL_GPUSampleCount samples);
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
        GpuTextureResources WhiteTexture;
        GpuTextureResources FlatNormalTexture;
        GpuTextureResources NeutralOrmTexture;
        GpuTextureResources BlackTexture;
        GpuTextureResources BlackDataTexture;
        GpuTextureResources WhiteDataTexture;
        std::unordered_map<AssetId, GpuMeshEntry> MeshCache;
        std::unordered_map<AssetId, GpuTextureEntry> TextureCache;
        std::unordered_map<AssetId, GpuMaterialEntry> MaterialCache;
        std::uint64_t MaterialBindingBuilds = 0;
        std::unordered_map<AssetId, GpuShaderEntry> ShaderCache;
        std::vector<std::pair<SamplerDescription, SDL_GPUSampler*>> SamplerCache;
        std::vector<RenderPipelineSet> Pipelines;
        std::vector<std::weak_ptr<RenderSurfaceState>> Surfaces;
        std::vector<QueuedSceneRequest> Requests;
        std::vector<SurfaceResources> PendingRetired;
        std::vector<GpuMeshResources> PendingRetiredMeshes;
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
        SDL_GPUComputePipeline* VfxSimulatePipeline = nullptr;
        SDL_GPUComputePipeline* VfxSpawnPipeline = nullptr;
        SDL_GPUComputePipeline* VfxFinalizePipeline = nullptr;
        bool VfxPipelinesAttempted = false;
        std::unordered_map<std::uint64_t, GpuVfxWorldResources> GpuVfxWorlds;
        std::deque<InFlightFrame> InFlight;
        StaticSceneFrameGraph SceneFrameGraph;
        RenderStatistics Statistics;
        std::uint64_t NextSurfaceId = 1;
        std::mutex RenderQueueMutex;
        std::condition_variable RenderQueueReady;
        std::condition_variable RenderQueueSpace;
        std::deque<std::function<void()>> RenderQueue;
        std::deque<float> PreparationSamples;
        std::jthread RenderThread;
        bool StopRenderQueue = false;
        std::atomic<bool> InjectDeviceLossAtNextFrame{false};
        bool WindowClaimed = false;
        bool FrameActive = false;
        bool Open = true;
    };
} // namespace Keire::RenderBackend
