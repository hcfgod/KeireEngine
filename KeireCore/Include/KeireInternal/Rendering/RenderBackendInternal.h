#pragma once

#include "Keire/Assets/AssetSystem.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Rendering/RenderSystem.h"
#include "Keire/Scenes/Scene.h"

#include <SDL3/SDL.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
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
        SDL_GPUTexture* MultisampleColor = nullptr;
        SDL_GPUTexture* Depth = nullptr;

        [[nodiscard]] bool Empty() const noexcept { return !SampledColor && !MultisampleColor && !Depth; }
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
        std::uint64_t Generation = 0;
        bool Submitted = false;
    };

    struct GpuMeshResources final
    {
        SDL_GPUBuffer* Vertices = nullptr;
        SDL_GPUBuffer* AssetVertices = nullptr;
        SDL_GPUBuffer* Indices = nullptr;
        std::uint32_t IndexCount = 0;

        [[nodiscard]] bool Empty() const noexcept { return !Vertices && !AssetVertices && !Indices; }
    };

    struct GpuTextureResources final
    {
        SDL_GPUTexture* Texture = nullptr;
        SDL_GPUSampler* Sampler = nullptr;

        [[nodiscard]] bool Empty() const noexcept { return !Texture && !Sampler; }
    };

    struct InFlightFrame final
    {
        SDL_GPUFence* Fence = nullptr;
        std::vector<SurfaceResources> Retired;
        std::vector<GpuMeshResources> RetiredMeshes;
        std::vector<GpuTextureResources> RetiredTextures;
        std::vector<SDL_GPUGraphicsPipeline*> RetiredPipelines;
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
        AssetHandle<ShaderAsset> Handle;
        Ref<const ShaderAsset> LastGood;
        std::vector<std::pair<SDL_GPUSampleCount, SDL_GPUGraphicsPipeline*>> Pipelines;
        std::uint64_t LoadedRevision = 0;
        std::uint64_t LastAttemptedRevision = 0;
    };

    struct ResolvedAssetMaterial final
    {
        SDL_GPUGraphicsPipeline* Pipeline = nullptr;
        std::vector<Vector4> NumericProperties;
        std::vector<SDL_GPUTextureSamplerBinding> Textures;
        std::optional<std::size_t> TintSlot;
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
    };

    static_assert(sizeof(ObjectUniforms) == sizeof(float) * 52);

    struct AssetObjectUniforms final
    {
        Matrix4 Model;
        Matrix4 View;
        Matrix4 Projection;
        Matrix4 NormalMatrix;
    };

    struct AssetSceneUniforms final
    {
        Vector4 AmbientColorIntensity;
        Vector4 DirectionalColorIntensity;
        Vector4 DirectionalDirectionExposure;
    };

    static_assert(sizeof(AssetObjectUniforms) == sizeof(float) * 64);
    static_assert(sizeof(AssetSceneUniforms) == sizeof(float) * 12);

    struct SceneLighting final
    {
        Vector4 Direction{0.0F, -1.0F, 0.0F, 0.0F};
        Color ColorAndIntensity{1.0F, 1.0F, 1.0F, 0.0F};
        bool Enabled = false;
    };

    struct SceneDrawItem final
    {
        AssetId Mesh;
        AssetId Material;
        Matrix4 World;
        Color Tint;
    };

    struct SceneRenderPacket final
    {
        RenderCamera Camera;
        RenderEnvironmentSettings Environment;
        SceneLighting Lighting;
        std::vector<SceneDrawItem> DrawItems;
        bool DrawGrid = false;
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
            result.Enabled = true;
        }
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
                                                           const Color tint, const SceneLighting& lighting,
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
                {receiveLighting ? 1.0F : 0.0F, 0.0F, 0.0F, 0.0F}};
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
        [[nodiscard]] GpuShaderEntry* ResolveShader(AssetId id, SDL_GPUSampleCount samples);
        [[nodiscard]] const ResolvedAssetMaterial* ResolveAssetMaterial(AssetId id, SDL_GPUSampleCount samples);

        void DrawScene(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderSurfaceState& surface,
                       const SceneRenderPacket& packet);
        void RecordSurface(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface);
        void EndFrame(ImDrawData* drawData);
        void Close() noexcept;

        void CreateGeometryResources();
        void ReleaseMeshResources(GpuMeshResources& resources) noexcept;
        void ReleaseTextureResources(GpuTextureResources& resources) noexcept;
        void Retire(GpuTextureResources resources) noexcept;
        void Retire(SDL_GPUGraphicsPipeline* pipeline) noexcept;
        void Retire(GpuMeshResources resources) noexcept;
        void Retire(SurfaceResources resources) noexcept;
        void RetireSurface(RenderSurfaceState& surface) noexcept;
        [[nodiscard]] SDL_GPUSampleCount ResolveSamples(RenderSampleCount requested) const noexcept;
        [[nodiscard]] SurfaceResources CreateResources(const RenderSurfaceState& surface, SDL_GPUSampleCount samples);
        void EnsureSurface(RenderSurfaceState& surface);
        [[nodiscard]] SDL_GPUGraphicsPipeline* CreatePipeline(SDL_GPUSampleCount samples,
                                                              SDL_GPUPrimitiveType primitive, bool depthWrite);
        [[nodiscard]] RenderPipelineSet& PipelinesFor(SDL_GPUSampleCount samples);
        [[nodiscard]] SDL_GPUShader* CreateAssetShader(const ShaderAssetDefinition& definition, bool vertex) const;
        [[nodiscard]] SDL_GPUGraphicsPipeline* CreateAssetPipeline(const ShaderAssetDefinition& definition,
                                                                   SDL_GPUSampleCount samples);

        RenderSpecification Specification;
        Ref<WindowSystem> Windows;
        Ref<Window> Window;
        Ref<AssetSystem> Assets;
        std::thread::id OwnerThread;
        SDL_Window* NativeWindow = nullptr;
        SDL_GPUDevice* Device = nullptr;
        SDL_GPUPresentMode PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
        SDL_GPUTextureFormat ColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        SDL_GPUTextureFormat DepthFormat = SDL_GPU_TEXTUREFORMAT_INVALID;
        SDL_GPUBuffer* GridBuffer = nullptr;
        std::uint32_t GridVertexCount = 0;
        GpuMeshResources DefaultMesh;
        GpuMeshResources ErrorMesh;
        GpuTextureResources CheckerboardTexture;
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
        std::deque<InFlightFrame> InFlight;
        RenderStatistics Statistics;
        std::uint64_t NextSurfaceId = 1;
        bool WindowClaimed = false;
        bool FrameActive = false;
        bool Open = true;
    };
} // namespace Keire::RenderBackend
