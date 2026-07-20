#include "Keire/Rendering/RenderSystem.h"

#include "Keire/Assets/AssetSystem.h"
#include "Keire/Assets/RenderingAssets.h"
#include "Keire/ECS/Components/DirectionalLightComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/ECS/Components/TransformComponent.h"
#include "Keire/Log.h"
#include "Keire/Scenes/Scene.h"

#include "KeireInternal/RenderInternal.h"
#include "KeireInternal/WindowInternal.h"

#include "Keire/BuiltinUnlitShaders.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdlgpu3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <deque>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Keire
{
    namespace
    {
        [[nodiscard]] std::string LastSdlError()
        {
            const char* error = SDL_GetError();
            return error && *error ? std::string(error) : std::string("SDL did not provide a diagnostic");
        }

        [[nodiscard]] bool ValidColor(const Color color) noexcept
        {
            const auto valid = [](const float value) { return std::isfinite(value) && value >= 0.0F && value <= 1.0F; };
            return valid(color.Red) && valid(color.Green) && valid(color.Blue) && valid(color.Alpha);
        }

        [[nodiscard]] Vector3 Normalize(const Vector3 value) noexcept
        {
            const float length = std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
            return length > 0.000001F ? Vector3{value.X / length, value.Y / length, value.Z / length}
                                      : Vector3{0.0F, -1.0F, 0.0F};
        }

        [[nodiscard]] Color TemperatureColor(const float kelvin) noexcept
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

        [[nodiscard]] SDL_GPUPresentMode ToSdlPresentMode(const RenderPresentMode mode) noexcept
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

        [[nodiscard]] SDL_GPUSampleCount ToSdlSampleCount(const RenderSampleCount samples) noexcept
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

        [[nodiscard]] RenderSampleCount FromSdlSampleCount(const SDL_GPUSampleCount samples) noexcept
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

        struct GpuMaterialEntry final
        {
            AssetHandle<MaterialAsset> Handle;
            Ref<const MaterialAsset> LastGood;
            std::uint64_t LoadedRevision = 0;
            std::uint64_t LastAttemptedRevision = 0;
        };

        struct ResolvedAssetMaterial final
        {
            SDL_GPUGraphicsPipeline* Pipeline = nullptr;
            std::vector<Vector4> NumericProperties;
            std::vector<SDL_GPUTextureSamplerBinding> Textures;
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

        [[nodiscard]] SceneLighting ResolveLighting(const Ref<Scene>& scene)
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
                const auto direction =
                    Normalize(Math::TransformDirection(transform->WorldMatrix(), {0.0F, 0.0F, 1.0F}));
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

        [[nodiscard]] Matrix4 Transpose(const Matrix4& value) noexcept
        {
            Matrix4 result;
            for (std::size_t row = 0; row < 4; ++row)
            {
                for (std::size_t column = 0; column < 4; ++column)
                    result.Elements[column * 4 + row] = value.Elements[row * 4 + column];
            }
            return result;
        }

        [[nodiscard]] ObjectUniforms MakeObjectUniforms(const Matrix4& modelViewProjection, const Matrix4& model,
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

        [[nodiscard]] std::vector<RenderVertex> CreateGridVertices()
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
            SceneRenderRequest Request;
            RenderSurfaceState* Surface = nullptr;
        };

        struct RenderSharedState final : public std::enable_shared_from_this<RenderSharedState>
        {
            RenderSharedState(RenderSpecification specification, Ref<WindowSystem> windows, Ref<Window> window,
                              Ref<AssetSystem> assets)
                : Specification(std::move(specification)), Windows(std::move(windows)), Window(std::move(window)),
                  Assets(std::move(assets)), OwnerThread(std::this_thread::get_id())
            {
                if (!ValidColor(Specification.SwapchainClearColor))
                    throw std::invalid_argument("Render swapchain clear color must contain finite values in 0..1.");
                if (Specification.MaximumFramesInFlight < 1 || Specification.MaximumFramesInFlight > 8)
                    throw std::invalid_argument("MaximumFramesInFlight must be in the range 1..8.");
                if (Specification.Mode == RenderMode::Automatic)
                    throw std::invalid_argument("RenderSystem requires a resolved render mode.");
                if (Specification.Mode != RenderMode::Rendered)
                    return;
                if (!Windows || !Window)
                    throw std::invalid_argument("Rendered mode requires an open window system and primary window.");

                NativeWindow = WindowSystemInternalAccess::NativeWindow(*Windows, Window->Id());
                if (!NativeWindow)
                    throw std::runtime_error("The renderer could not resolve the primary native window.");

                constexpr SDL_GPUShaderFormat formats = static_cast<SDL_GPUShaderFormat>(
                    SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXBC | SDL_GPU_SHADERFORMAT_DXIL |
                    SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB);
                Device = SDL_CreateGPUDevice(formats, Specification.EnableGpuValidation, nullptr);
                if (!Device)
                    throw std::runtime_error("SDL_CreateGPUDevice failed: " + LastSdlError());
                KEIRE_CORE_INFO("Created SDL_GPU device (driver={}, shader formats=0x{:x}).",
                                SDL_GetGPUDeviceDriver(Device),
                                static_cast<std::uint32_t>(SDL_GetGPUShaderFormats(Device)));

                try
                {
                    if (!SDL_ClaimWindowForGPUDevice(Device, NativeWindow))
                        throw std::runtime_error("SDL_ClaimWindowForGPUDevice failed: " + LastSdlError());
                    WindowClaimed = true;

                    PresentMode = ToSdlPresentMode(Specification.PresentMode);
                    if (!SDL_WindowSupportsGPUPresentMode(Device, NativeWindow, PresentMode))
                    {
                        KEIRE_CORE_WARN("Requested render present mode is unavailable; falling back to VSync.");
                        PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
                    }
                    if (!SDL_SetGPUSwapchainParameters(Device, NativeWindow, SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
                                                       PresentMode))
                    {
                        throw std::runtime_error("SDL_SetGPUSwapchainParameters failed: " + LastSdlError());
                    }

                    ColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB;
                    const SDL_GPUTextureUsageFlags colorUsage =
                        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
                    if (!SDL_GPUTextureSupportsFormat(Device, ColorFormat, SDL_GPU_TEXTURETYPE_2D, colorUsage))
                        ColorFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

                    constexpr SDL_GPUTextureFormat depthCandidates[] = {SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                                                                        SDL_GPU_TEXTUREFORMAT_D24_UNORM,
                                                                        SDL_GPU_TEXTUREFORMAT_D16_UNORM};
                    for (const auto candidate : depthCandidates)
                    {
                        if (SDL_GPUTextureSupportsFormat(Device, candidate, SDL_GPU_TEXTURETYPE_2D,
                                                         SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
                        {
                            DepthFormat = candidate;
                            break;
                        }
                    }
                    KEIRE_CORE_INFO("Selected GPU attachment formats (color={}, depth={}).",
                                    static_cast<std::uint32_t>(ColorFormat), static_cast<std::uint32_t>(DepthFormat));

                    CreateGeometryResources();
                }
                catch (...)
                {
                    Close();
                    throw;
                }
            }

            ~RenderSharedState() { Close(); }

            void RequireOwner(const char* operation) const
            {
                if (std::this_thread::get_id() != OwnerThread)
                    throw std::logic_error(std::string("RenderSystem::") + operation +
                                           " must be called on the application owner thread.");
                if (!Open)
                    throw std::logic_error(std::string("RenderSystem::") + operation + " called after shutdown.");
            }

            [[nodiscard]] std::vector<std::shared_ptr<RenderSurfaceState>> LiveSurfaces()
            {
                std::vector<std::shared_ptr<RenderSurfaceState>> result;
                result.reserve(Surfaces.size());
                std::erase_if(Surfaces,
                              [&result](const std::weak_ptr<RenderSurfaceState>& weak)
                              {
                                  if (auto surface = weak.lock())
                                  {
                                      result.push_back(std::move(surface));
                                      return false;
                                  }
                                  return true;
                              });
                std::ranges::sort(result, {}, &RenderSurfaceState::Id);
                return result;
            }

            void ReleaseResources(SurfaceResources& resources) noexcept
            {
                if (!Device)
                {
                    resources = {};
                    return;
                }
                if (resources.Depth)
                    SDL_ReleaseGPUTexture(Device, resources.Depth);
                if (resources.MultisampleColor)
                    SDL_ReleaseGPUTexture(Device, resources.MultisampleColor);
                if (resources.SampledColor)
                    SDL_ReleaseGPUTexture(Device, resources.SampledColor);
                resources = {};
            }

            [[nodiscard]] SDL_GPUShader* CreateShader(const bool vertex) const
            {
                SDL_GPUShaderCreateInfo information{};
                information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
                information.num_uniform_buffers = Detail::BuiltinShaderUniformBufferCount(vertex);

                const auto formats = SDL_GetGPUShaderFormats(Device);
                if (formats & SDL_GPU_SHADERFORMAT_DXIL)
                {
                    information.format = SDL_GPU_SHADERFORMAT_DXIL;
                    information.code = vertex ? Detail::BuiltinUnlitVertexDxil : Detail::BuiltinUnlitFragmentDxil;
                    information.code_size =
                        vertex ? Detail::BuiltinUnlitVertexDxilSize : Detail::BuiltinUnlitFragmentDxilSize;
                }
                else if (formats & SDL_GPU_SHADERFORMAT_MSL)
                {
                    information.format = SDL_GPU_SHADERFORMAT_MSL;
                    information.code = vertex ? Detail::BuiltinUnlitVertexMsl : Detail::BuiltinUnlitFragmentMsl;
                    information.code_size =
                        vertex ? Detail::BuiltinUnlitVertexMslSize : Detail::BuiltinUnlitFragmentMslSize;
                }
                else if (formats & SDL_GPU_SHADERFORMAT_SPIRV)
                {
                    information.format = SDL_GPU_SHADERFORMAT_SPIRV;
                    information.entrypoint = vertex ? "VSMain" : "PSMain";
                    information.code = vertex ? Detail::BuiltinUnlitVertexSpirV : Detail::BuiltinUnlitFragmentSpirV;
                    information.code_size =
                        vertex ? Detail::BuiltinUnlitVertexSpirVSize : Detail::BuiltinUnlitFragmentSpirVSize;
                }
                else
                    throw std::runtime_error("The active SDL_GPU backend exposes no supported built-in shader format.");

                SDL_GPUShader* shader = SDL_CreateGPUShader(Device, &information);
                if (!shader)
                    throw std::runtime_error("SDL_CreateGPUShader failed: " + LastSdlError());
                return shader;
            }

            [[nodiscard]] SDL_GPUBuffer* UploadBuffer(const std::span<const std::byte> bytes,
                                                      const SDL_GPUBufferUsageFlags usage)
            {
                if (bytes.empty() || bytes.size() > std::numeric_limits<std::uint32_t>::max())
                    throw std::invalid_argument("GPU buffer payload is empty or exceeds the 32-bit SDL limit.");
                const auto byteSize = static_cast<std::uint32_t>(bytes.size());
                SDL_GPUBufferCreateInfo bufferInformation{};
                bufferInformation.usage = usage;
                bufferInformation.size = byteSize;
                SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(Device, &bufferInformation);
                if (!buffer)
                    throw std::runtime_error("SDL_CreateGPUBuffer failed: " + LastSdlError());

                SDL_GPUTransferBuffer* transfer = nullptr;
                try
                {
                    SDL_GPUTransferBufferCreateInfo transferInformation{};
                    transferInformation.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                    transferInformation.size = byteSize;
                    transfer = SDL_CreateGPUTransferBuffer(Device, &transferInformation);
                    if (!transfer)
                        throw std::runtime_error("SDL_CreateGPUTransferBuffer failed: " + LastSdlError());

                    void* mapped = SDL_MapGPUTransferBuffer(Device, transfer, false);
                    if (!mapped)
                        throw std::runtime_error("SDL_MapGPUTransferBuffer failed: " + LastSdlError());
                    std::memcpy(mapped, bytes.data(), bytes.size());
                    SDL_UnmapGPUTransferBuffer(Device, transfer);

                    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(Device);
                    if (!commands)
                        throw std::runtime_error("SDL_AcquireGPUCommandBuffer(upload) failed: " + LastSdlError());
                    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
                    if (!copy)
                    {
                        (void)SDL_CancelGPUCommandBuffer(commands);
                        throw std::runtime_error("SDL_BeginGPUCopyPass failed: " + LastSdlError());
                    }
                    SDL_GPUTransferBufferLocation source{transfer, 0};
                    SDL_GPUBufferRegion destination{buffer, 0, byteSize};
                    SDL_UploadToGPUBuffer(copy, &source, &destination, false);
                    SDL_EndGPUCopyPass(copy);
                    if (!SDL_SubmitGPUCommandBuffer(commands))
                        throw std::runtime_error("SDL_SubmitGPUCommandBuffer(upload) failed: " + LastSdlError());
                    SDL_ReleaseGPUTransferBuffer(Device, transfer);
                    return buffer;
                }
                catch (...)
                {
                    if (transfer)
                        SDL_ReleaseGPUTransferBuffer(Device, transfer);
                    SDL_ReleaseGPUBuffer(Device, buffer);
                    throw;
                }
            }

            [[nodiscard]] SDL_GPUBuffer* UploadVertexBuffer(const std::span<const RenderVertex> vertices)
            {
                return UploadBuffer(std::as_bytes(vertices), SDL_GPU_BUFFERUSAGE_VERTEX);
            }

            [[nodiscard]] SDL_GPUSampler* ResolveSampler(const SamplerDescription& description)
            {
                const auto found =
                    std::ranges::find(SamplerCache, description, &decltype(SamplerCache)::value_type::first);
                if (found != SamplerCache.end())
                    return found->second;
                const auto filter = [](const TextureFilter value)
                { return value == TextureFilter::Nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR; };
                const auto address = [](const TextureAddressMode value)
                {
                    switch (value)
                    {
                    case TextureAddressMode::Clamp:
                        return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
                    case TextureAddressMode::Mirror:
                        return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
                    case TextureAddressMode::Repeat:
                    default:
                        return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
                    }
                };
                SDL_GPUSamplerCreateInfo sampler{};
                sampler.min_filter = filter(description.Minimum);
                sampler.mag_filter = filter(description.Magnification);
                sampler.mipmap_mode = description.Mip == TextureFilter::Nearest ? SDL_GPU_SAMPLERMIPMAPMODE_NEAREST
                                                                                : SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
                sampler.address_mode_u = address(description.AddressU);
                sampler.address_mode_v = address(description.AddressV);
                sampler.address_mode_w = address(description.AddressW);
                sampler.max_anisotropy = static_cast<float>(description.Anisotropy);
                sampler.max_lod = 32.0F;
                sampler.enable_anisotropy = description.Anisotropy > 1;
                SDL_GPUSampler* created = SDL_CreateGPUSampler(Device, &sampler);
                if (!created)
                    throw std::runtime_error("SDL_CreateGPUSampler failed: " + LastSdlError());
                SamplerCache.emplace_back(description, created);
                return created;
            }

            [[nodiscard]] GpuTextureResources CreateTextureResources(const Texture2DAsset& asset)
            {
                const auto mips = asset.Mips();
                if (mips.empty() || mips.size() > std::numeric_limits<std::uint32_t>::max())
                    throw std::invalid_argument("Texture GPU upload requires a bounded mip chain.");
                std::size_t totalBytes = 0;
                for (const auto& mip : mips)
                {
                    if (mip.Pixels.size() > std::numeric_limits<std::uint32_t>::max() - totalBytes)
                        throw std::invalid_argument("Texture GPU upload exceeds SDL's 32-bit transfer limit.");
                    totalBytes += mip.Pixels.size();
                }

                GpuTextureResources result;
                SDL_GPUTransferBuffer* transfer = nullptr;
                try
                {
                    SDL_GPUTextureCreateInfo texture{};
                    texture.type = SDL_GPU_TEXTURETYPE_2D;
                    texture.format = asset.Settings().ColorSpace == TextureColorSpace::Srgb
                                         ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB
                                         : SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
                    texture.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
                    texture.width = asset.Width();
                    texture.height = asset.Height();
                    texture.layer_count_or_depth = 1;
                    texture.num_levels = static_cast<std::uint32_t>(mips.size());
                    texture.sample_count = SDL_GPU_SAMPLECOUNT_1;
                    result.Texture = SDL_CreateGPUTexture(Device, &texture);
                    if (!result.Texture)
                        throw std::runtime_error("SDL_CreateGPUTexture(asset) failed: " + LastSdlError());

                    result.Sampler = ResolveSampler(asset.Settings().Sampler);

                    SDL_GPUTransferBufferCreateInfo transferInformation{};
                    transferInformation.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                    transferInformation.size = static_cast<std::uint32_t>(totalBytes);
                    transfer = SDL_CreateGPUTransferBuffer(Device, &transferInformation);
                    if (!transfer)
                        throw std::runtime_error("SDL_CreateGPUTransferBuffer(texture) failed: " + LastSdlError());
                    auto* mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(Device, transfer, false));
                    if (!mapped)
                        throw std::runtime_error("SDL_MapGPUTransferBuffer(texture) failed: " + LastSdlError());
                    std::size_t offset = 0;
                    for (const auto& mip : mips)
                    {
                        std::memcpy(mapped + offset, mip.Pixels.data(), mip.Pixels.size());
                        offset += mip.Pixels.size();
                    }
                    SDL_UnmapGPUTransferBuffer(Device, transfer);

                    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(Device);
                    if (!commands)
                        throw std::runtime_error("SDL_AcquireGPUCommandBuffer(texture) failed: " + LastSdlError());
                    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
                    if (!copy)
                    {
                        (void)SDL_CancelGPUCommandBuffer(commands);
                        throw std::runtime_error("SDL_BeginGPUCopyPass(texture) failed: " + LastSdlError());
                    }
                    offset = 0;
                    for (std::size_t index = 0; index < mips.size(); ++index)
                    {
                        const auto& mip = mips[index];
                        SDL_GPUTextureTransferInfo source{transfer, static_cast<std::uint32_t>(offset), mip.Width,
                                                          mip.Height};
                        SDL_GPUTextureRegion destination{
                            result.Texture, static_cast<std::uint32_t>(index), 0, 0, 0, 0, mip.Width, mip.Height, 1};
                        SDL_UploadToGPUTexture(copy, &source, &destination, false);
                        offset += mip.Pixels.size();
                    }
                    SDL_EndGPUCopyPass(copy);
                    if (!SDL_SubmitGPUCommandBuffer(commands))
                        throw std::runtime_error("SDL_SubmitGPUCommandBuffer(texture) failed: " + LastSdlError());
                    SDL_ReleaseGPUTransferBuffer(Device, transfer);
                    return result;
                }
                catch (...)
                {
                    if (transfer)
                        SDL_ReleaseGPUTransferBuffer(Device, transfer);
                    if (result.Texture)
                        SDL_ReleaseGPUTexture(Device, result.Texture);
                    throw;
                }
            }

            [[nodiscard]] GpuMeshResources CreateMeshResources(const MeshAsset& mesh)
            {
                std::vector<RenderVertex> vertices;
                vertices.reserve(mesh.Vertices().size());
                for (const auto& vertex : mesh.Vertices())
                {
                    vertices.push_back({vertex.Position,
                                        {vertex.VertexColor.Red, vertex.VertexColor.Green, vertex.VertexColor.Blue},
                                        vertex.Normal});
                }
                GpuMeshResources result;
                try
                {
                    result.Vertices = UploadVertexBuffer(vertices);
                    result.AssetVertices = UploadBuffer(std::as_bytes(mesh.Vertices()), SDL_GPU_BUFFERUSAGE_VERTEX);
                    result.Indices = UploadBuffer(std::as_bytes(mesh.Indices()), SDL_GPU_BUFFERUSAGE_INDEX);
                    result.IndexCount = static_cast<std::uint32_t>(mesh.Indices().size());
                    return result;
                }
                catch (...)
                {
                    if (result.Indices)
                        SDL_ReleaseGPUBuffer(Device, result.Indices);
                    if (result.Vertices)
                        SDL_ReleaseGPUBuffer(Device, result.Vertices);
                    if (result.AssetVertices)
                        SDL_ReleaseGPUBuffer(Device, result.AssetVertices);
                    throw;
                }
            }

            void CreateGeometryResources()
            {
                DefaultMesh = CreateMeshResources(*MeshAsset::Cube());
                ErrorMesh = CreateMeshResources(*MeshAsset::Error());
                CheckerboardTexture = CreateTextureResources(*Texture2DAsset::Checkerboard());
                const auto solidTexture = [](const std::array<std::byte, 4> pixel, const TextureSemantic semantic,
                                             const TextureColorSpace colorSpace)
                {
                    TextureImportSettings settings;
                    settings.Semantic = semantic;
                    settings.ColorSpace = colorSpace;
                    settings.Mips = TextureMipPolicy::None;
                    std::vector<std::byte> pixels(pixel.begin(), pixel.end());
                    return CreateRef<Texture2DAsset>(settings, std::vector<TextureMipLevel>{{1, 1, std::move(pixels)}});
                };
                WhiteTexture = CreateTextureResources(
                    *solidTexture({std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}},
                                  TextureSemantic::Color, TextureColorSpace::Srgb));
                FlatNormalTexture = CreateTextureResources(
                    *solidTexture({std::byte{128}, std::byte{128}, std::byte{255}, std::byte{255}},
                                  TextureSemantic::Normal, TextureColorSpace::Linear));
                NeutralOrmTexture =
                    CreateTextureResources(*solidTexture({std::byte{255}, std::byte{255}, std::byte{0}, std::byte{255}},
                                                         TextureSemantic::Data, TextureColorSpace::Linear));
                BlackTexture =
                    CreateTextureResources(*solidTexture({std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}},
                                                         TextureSemantic::Color, TextureColorSpace::Srgb));
                const auto grid = CreateGridVertices();
                GridVertexCount = static_cast<std::uint32_t>(grid.size());
                GridBuffer = UploadVertexBuffer(grid);
            }

            void ReleaseMeshResources(GpuMeshResources& resources) noexcept
            {
                if (Device && resources.Indices)
                    SDL_ReleaseGPUBuffer(Device, resources.Indices);
                if (Device && resources.Vertices)
                    SDL_ReleaseGPUBuffer(Device, resources.Vertices);
                if (Device && resources.AssetVertices)
                    SDL_ReleaseGPUBuffer(Device, resources.AssetVertices);
                resources = {};
            }

            void ReleaseTextureResources(GpuTextureResources& resources) noexcept
            {
                if (Device && resources.Texture)
                    SDL_ReleaseGPUTexture(Device, resources.Texture);
                resources = {};
            }

            void Retire(GpuTextureResources resources) noexcept
            {
                if (resources.Empty())
                    return;
                if (!Open || !Device)
                    ReleaseTextureResources(resources);
                else if (FrameActive)
                    PendingRetiredTextures.push_back(resources);
                else if (!InFlight.empty())
                    InFlight.back().RetiredTextures.push_back(resources);
                else
                    ReleaseTextureResources(resources);
            }

            void Retire(SDL_GPUGraphicsPipeline* pipeline) noexcept
            {
                if (!pipeline)
                    return;
                if (!Open || !Device)
                    SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
                else if (FrameActive)
                    PendingRetiredPipelines.push_back(pipeline);
                else if (!InFlight.empty())
                    InFlight.back().RetiredPipelines.push_back(pipeline);
                else
                    SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
            }

            void Retire(GpuMeshResources resources) noexcept
            {
                if (resources.Empty())
                    return;
                if (!Open || !Device)
                {
                    ReleaseMeshResources(resources);
                    return;
                }
                if (FrameActive)
                    PendingRetiredMeshes.push_back(resources);
                else if (!InFlight.empty())
                    InFlight.back().RetiredMeshes.push_back(resources);
                else
                    ReleaseMeshResources(resources);
            }

            void Retire(SurfaceResources resources) noexcept
            {
                if (resources.Empty())
                    return;
                if (!Open || !Device)
                {
                    ReleaseResources(resources);
                    return;
                }
                if (FrameActive)
                    PendingRetired.push_back(resources);
                else if (!InFlight.empty())
                    InFlight.back().Retired.push_back(resources);
                else
                    ReleaseResources(resources);
            }

            void RetireSurface(RenderSurfaceState& surface) noexcept
            {
                Retire(std::exchange(surface.Resources, {}));
                surface.Owner.reset();
                surface.Width = 0;
                surface.Height = 0;
            }

            [[nodiscard]] SDL_GPUSampleCount ResolveSamples(const RenderSampleCount requested) const noexcept
            {
                const auto maximum = static_cast<std::uint8_t>(requested);
                constexpr std::pair<std::uint8_t, SDL_GPUSampleCount> candidates[] = {
                    {std::uint8_t{8}, SDL_GPU_SAMPLECOUNT_8},
                    {std::uint8_t{4}, SDL_GPU_SAMPLECOUNT_4},
                    {std::uint8_t{2}, SDL_GPU_SAMPLECOUNT_2},
                    {std::uint8_t{1}, SDL_GPU_SAMPLECOUNT_1}};
                for (const auto [value, candidate] : candidates)
                {
                    if (value > maximum)
                        continue;
                    if (SDL_GPUTextureSupportsSampleCount(Device, ColorFormat, candidate) &&
                        (!DepthFormat || SDL_GPUTextureSupportsSampleCount(Device, DepthFormat, candidate)))
                        return candidate;
                }
                return SDL_GPU_SAMPLECOUNT_1;
            }

            [[nodiscard]] SurfaceResources CreateResources(const RenderSurfaceState& surface,
                                                           const SDL_GPUSampleCount samples)
            {
                SurfaceResources result;
                try
                {
                    SDL_GPUTextureCreateInfo sampled{};
                    sampled.type = SDL_GPU_TEXTURETYPE_2D;
                    sampled.format = ColorFormat;
                    sampled.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
                    sampled.width = surface.RequestedWidth;
                    sampled.height = surface.RequestedHeight;
                    sampled.layer_count_or_depth = 1;
                    sampled.num_levels = 1;
                    sampled.sample_count = SDL_GPU_SAMPLECOUNT_1;
                    result.SampledColor = SDL_CreateGPUTexture(Device, &sampled);
                    if (!result.SampledColor)
                        throw std::runtime_error("SDL_CreateGPUTexture(color) failed: " + LastSdlError());

                    if (samples != SDL_GPU_SAMPLECOUNT_1)
                    {
                        auto multisample = sampled;
                        multisample.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
                        multisample.sample_count = samples;
                        result.MultisampleColor = SDL_CreateGPUTexture(Device, &multisample);
                        if (!result.MultisampleColor)
                            throw std::runtime_error("SDL_CreateGPUTexture(MSAA color) failed: " + LastSdlError());
                    }

                    if (surface.Specification.Depth && DepthFormat)
                    {
                        SDL_GPUTextureCreateInfo depth{};
                        depth.type = SDL_GPU_TEXTURETYPE_2D;
                        depth.format = DepthFormat;
                        depth.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
                        depth.width = surface.RequestedWidth;
                        depth.height = surface.RequestedHeight;
                        depth.layer_count_or_depth = 1;
                        depth.num_levels = 1;
                        depth.sample_count = samples;
                        result.Depth = SDL_CreateGPUTexture(Device, &depth);
                        if (!result.Depth)
                            throw std::runtime_error("SDL_CreateGPUTexture(depth) failed: " + LastSdlError());
                    }
                    return result;
                }
                catch (...)
                {
                    ReleaseResources(result);
                    throw;
                }
            }

            void EnsureSurface(RenderSurfaceState& surface)
            {
                if (Specification.Mode == RenderMode::Headless)
                {
                    if (surface.Width != surface.RequestedWidth || surface.Height != surface.RequestedHeight)
                    {
                        surface.Width = surface.RequestedWidth;
                        surface.Height = surface.RequestedHeight;
                        ++surface.Generation;
                    }
                    return;
                }
                if (surface.Width == surface.RequestedWidth && surface.Height == surface.RequestedHeight &&
                    surface.Resources.SampledColor)
                    return;
                if (surface.FailedWidth == surface.RequestedWidth && surface.FailedHeight == surface.RequestedHeight)
                    return;

                const auto samples = ResolveSamples(surface.Specification.SampleCount);
                try
                {
                    auto replacement = CreateResources(surface, samples);
                    Retire(std::exchange(surface.Resources, replacement));
                    surface.Width = surface.RequestedWidth;
                    surface.Height = surface.RequestedHeight;
                    surface.ActualSamples = FromSdlSampleCount(samples);
                    surface.FailedWidth = 0;
                    surface.FailedHeight = 0;
                    ++surface.Generation;
                }
                catch (const std::exception& error)
                {
                    surface.FailedWidth = surface.RequestedWidth;
                    surface.FailedHeight = surface.RequestedHeight;
                    KEIRE_CORE_ERROR("Could not resize render surface '{}': {}", surface.Specification.Name,
                                     error.what());
                }
            }

            [[nodiscard]] SDL_GPUGraphicsPipeline* CreatePipeline(const SDL_GPUSampleCount samples,
                                                                  const SDL_GPUPrimitiveType primitive,
                                                                  const bool depthWrite)
            {
                SDL_GPUShader* vertex = CreateShader(true);
                SDL_GPUShader* fragment = nullptr;
                try
                {
                    fragment = CreateShader(false);

                    SDL_GPUColorTargetDescription color{};
                    color.format = ColorFormat;

                    SDL_GPUVertexBufferDescription vertexBuffer{};
                    vertexBuffer.slot = 0;
                    vertexBuffer.pitch = sizeof(RenderVertex);
                    vertexBuffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

                    std::array<SDL_GPUVertexAttribute, 3> attributes{};
                    attributes[0].location = 0;
                    attributes[0].buffer_slot = 0;
                    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
                    attributes[0].offset = offsetof(RenderVertex, Position);
                    attributes[1].location = 1;
                    attributes[1].buffer_slot = 0;
                    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
                    attributes[1].offset = offsetof(RenderVertex, Color);
                    attributes[2].location = 2;
                    attributes[2].buffer_slot = 0;
                    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
                    attributes[2].offset = offsetof(RenderVertex, Normal);

                    SDL_GPUGraphicsPipelineCreateInfo information{};
                    information.vertex_shader = vertex;
                    information.fragment_shader = fragment;
                    information.vertex_input_state.vertex_buffer_descriptions = &vertexBuffer;
                    information.vertex_input_state.num_vertex_buffers = 1;
                    information.vertex_input_state.vertex_attributes = attributes.data();
                    information.vertex_input_state.num_vertex_attributes =
                        static_cast<std::uint32_t>(attributes.size());
                    information.primitive_type = primitive;
                    information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
                    information.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
                    information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
                    information.rasterizer_state.enable_depth_clip = true;
                    information.multisample_state.sample_count = samples;
                    information.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
                    information.depth_stencil_state.enable_depth_test = true;
                    information.depth_stencil_state.enable_depth_write = depthWrite;
                    information.target_info.color_target_descriptions = &color;
                    information.target_info.num_color_targets = 1;
                    information.target_info.depth_stencil_format = DepthFormat;
                    information.target_info.has_depth_stencil_target = true;

                    KEIRE_CORE_INFO(
                        "Creating built-in pipeline (primitive={}, samples={}, color={}, depth={}, attributes=3).",
                        static_cast<std::uint32_t>(primitive), static_cast<std::uint32_t>(samples),
                        static_cast<std::uint32_t>(ColorFormat), static_cast<std::uint32_t>(DepthFormat));
                    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(Device, &information);
                    if (!pipeline)
                        throw std::runtime_error("SDL_CreateGPUGraphicsPipeline failed for " +
                                                 std::string(primitive == SDL_GPU_PRIMITIVETYPE_TRIANGLELIST
                                                                 ? "triangle list"
                                                                 : "line list") +
                                                 ": " + LastSdlError());
                    SDL_ReleaseGPUShader(Device, fragment);
                    SDL_ReleaseGPUShader(Device, vertex);
                    return pipeline;
                }
                catch (...)
                {
                    if (fragment)
                        SDL_ReleaseGPUShader(Device, fragment);
                    SDL_ReleaseGPUShader(Device, vertex);
                    throw;
                }
            }

            [[nodiscard]] RenderPipelineSet& PipelinesFor(const SDL_GPUSampleCount samples)
            {
                const auto found = std::ranges::find(Pipelines, samples, &RenderPipelineSet::Samples);
                if (found != Pipelines.end())
                    return *found;

                RenderPipelineSet result;
                result.Samples = samples;
                try
                {
                    result.Cube = CreatePipeline(samples, SDL_GPU_PRIMITIVETYPE_TRIANGLELIST, true);
                    result.Grid = CreatePipeline(samples, SDL_GPU_PRIMITIVETYPE_LINELIST, false);
                    return Pipelines.emplace_back(result);
                }
                catch (...)
                {
                    if (result.Grid)
                        SDL_ReleaseGPUGraphicsPipeline(Device, result.Grid);
                    if (result.Cube)
                        SDL_ReleaseGPUGraphicsPipeline(Device, result.Cube);
                    throw;
                }
            }

            [[nodiscard]] SDL_GPUShader* CreateAssetShader(const ShaderAssetDefinition& definition,
                                                           const bool vertex) const
            {
                const auto supported = SDL_GetGPUShaderFormats(Device);
                const ShaderVariant* variant = nullptr;
                SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
                const auto findVariant = [&definition](const ShaderBinaryFormat requested)
                {
                    const auto found = std::ranges::find(definition.Variants, requested, &ShaderVariant::Format);
                    return found == definition.Variants.end() ? nullptr : &*found;
                };
                if ((supported & SDL_GPU_SHADERFORMAT_DXIL) && (variant = findVariant(ShaderBinaryFormat::Dxil)))
                    format = SDL_GPU_SHADERFORMAT_DXIL;
                else if ((supported & SDL_GPU_SHADERFORMAT_MSL) && (variant = findVariant(ShaderBinaryFormat::Msl)))
                    format = SDL_GPU_SHADERFORMAT_MSL;
                else if ((supported & SDL_GPU_SHADERFORMAT_SPIRV) && (variant = findVariant(ShaderBinaryFormat::SpirV)))
                    format = SDL_GPU_SHADERFORMAT_SPIRV;
                if (!variant)
                    throw std::runtime_error("Shader asset lacks a variant for the active GPU backend.");

                const auto& code = vertex ? variant->Vertex : variant->Fragment;
                const auto textureCount = static_cast<std::uint32_t>(std::ranges::count(
                    definition.Properties, ShaderPropertyType::Texture2D, &ShaderPropertyDefinition::Type));
                SDL_GPUShaderCreateInfo information{};
                information.code_size = code.size();
                information.code = reinterpret_cast<const std::uint8_t*>(code.data());
                information.entrypoint =
                    format == SDL_GPU_SHADERFORMAT_SPIRV
                        ? (vertex ? definition.VertexEntry.c_str() : definition.FragmentEntry.c_str())
                        : nullptr;
                information.format = format;
                information.stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
                information.num_samplers = vertex ? 0 : textureCount;
                information.num_uniform_buffers = vertex ? 1 : 2;
                SDL_GPUShader* shader = SDL_CreateGPUShader(Device, &information);
                if (!shader)
                    throw std::runtime_error("SDL_CreateGPUShader(asset) failed: " + LastSdlError());
                return shader;
            }

            [[nodiscard]] SDL_GPUGraphicsPipeline* CreateAssetPipeline(const ShaderAssetDefinition& definition,
                                                                       const SDL_GPUSampleCount samples)
            {
                SDL_GPUShader* vertex = CreateAssetShader(definition, true);
                SDL_GPUShader* fragment = nullptr;
                try
                {
                    fragment = CreateAssetShader(definition, false);
                    SDL_GPUColorTargetDescription color{};
                    color.format = ColorFormat;
                    color.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
                    color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                    color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
                    color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
                    color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
                    color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
                    color.blend_state.enable_blend = definition.Blend;

                    SDL_GPUVertexBufferDescription buffer{};
                    buffer.slot = 0;
                    buffer.pitch = sizeof(MeshVertex);
                    buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
                    const std::array attributes{
                        SDL_GPUVertexAttribute{0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                                               offsetof(MeshVertex, Position)},
                        SDL_GPUVertexAttribute{1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(MeshVertex, Normal)},
                        SDL_GPUVertexAttribute{2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(MeshVertex, UV0)},
                        SDL_GPUVertexAttribute{3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
                                               offsetof(MeshVertex, VertexColor)},
                        SDL_GPUVertexAttribute{4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
                                               offsetof(MeshVertex, Tangent)}};
                    SDL_GPUGraphicsPipelineCreateInfo information{};
                    information.vertex_shader = vertex;
                    information.fragment_shader = fragment;
                    information.vertex_input_state.vertex_buffer_descriptions = &buffer;
                    information.vertex_input_state.num_vertex_buffers = 1;
                    information.vertex_input_state.vertex_attributes = attributes.data();
                    information.vertex_input_state.num_vertex_attributes =
                        definition.VertexLayoutVersion == 2 ? static_cast<std::uint32_t>(attributes.size()) : 4U;
                    information.primitive_type = definition.Topology == ShaderPrimitiveTopology::LineList
                                                     ? SDL_GPU_PRIMITIVETYPE_LINELIST
                                                     : SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
                    information.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
                    information.rasterizer_state.cull_mode =
                        definition.Culling == ShaderCullMode::Front  ? SDL_GPU_CULLMODE_FRONT
                        : definition.Culling == ShaderCullMode::Back ? SDL_GPU_CULLMODE_BACK
                                                                     : SDL_GPU_CULLMODE_NONE;
                    information.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
                    information.rasterizer_state.enable_depth_clip = true;
                    information.multisample_state.sample_count = samples;
                    information.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
                    information.depth_stencil_state.enable_depth_test = definition.DepthTest;
                    information.depth_stencil_state.enable_depth_write = definition.DepthWrite;
                    information.target_info.color_target_descriptions = &color;
                    information.target_info.num_color_targets = 1;
                    information.target_info.depth_stencil_format = DepthFormat;
                    information.target_info.has_depth_stencil_target = true;
                    KEIRE_CORE_INFO("Creating asset pipeline (layout={}, topology={}, samples={}, color={}, depth={}, "
                                    "attributes={}).",
                                    definition.VertexLayoutVersion, static_cast<std::uint32_t>(definition.Topology),
                                    static_cast<std::uint32_t>(samples), static_cast<std::uint32_t>(ColorFormat),
                                    static_cast<std::uint32_t>(DepthFormat),
                                    information.vertex_input_state.num_vertex_attributes);
                    SDL_GPUGraphicsPipeline* result = SDL_CreateGPUGraphicsPipeline(Device, &information);
                    if (!result)
                        throw std::runtime_error("SDL_CreateGPUGraphicsPipeline(asset) failed: " + LastSdlError());
                    SDL_ReleaseGPUShader(Device, fragment);
                    SDL_ReleaseGPUShader(Device, vertex);
                    return result;
                }
                catch (...)
                {
                    if (fragment)
                        SDL_ReleaseGPUShader(Device, fragment);
                    SDL_ReleaseGPUShader(Device, vertex);
                    throw;
                }
            }

            void CollectCompletedFrames()
            {
                if (!Device)
                    return;
                while (!InFlight.empty() && SDL_QueryGPUFence(Device, InFlight.front().Fence))
                {
                    auto frame = std::move(InFlight.front());
                    InFlight.pop_front();
                    for (auto& retired : frame.Retired)
                        ReleaseResources(retired);
                    for (auto& retired : frame.RetiredMeshes)
                        ReleaseMeshResources(retired);
                    for (auto& retired : frame.RetiredTextures)
                        ReleaseTextureResources(retired);
                    for (auto* retired : frame.RetiredPipelines)
                        SDL_ReleaseGPUGraphicsPipeline(Device, retired);
                    SDL_ReleaseGPUFence(Device, frame.Fence);
                }
                if (InFlight.size() < Specification.MaximumFramesInFlight)
                    return;

                SDL_GPUFence* fence = InFlight.front().Fence;
                if (!SDL_WaitForGPUFences(Device, true, &fence, 1))
                    throw std::runtime_error("SDL_WaitForGPUFences failed: " + LastSdlError());
                CollectCompletedFrames();
            }

            void BeginFrame()
            {
                RequireOwner("BeginFrame");
                if (FrameActive)
                    throw std::logic_error("A render frame is already active.");
                FrameActive = true;
                Requests.clear();
                ++Statistics.Frame;
                Statistics.Passes = 0;
                Statistics.Surfaces = 0;
                Statistics.DrawCalls = 0;
                Statistics.Triangles = 0;
                CollectCompletedFrames();
                for (const auto& surface : LiveSurfaces())
                {
                    surface->Submitted = false;
                    surface->FrameClearColor = surface->Specification.ClearColor;
                    EnsureSurface(*surface);
                }
            }

            void CancelFrame() noexcept
            {
                FrameActive = false;
                Requests.clear();
            }

            void Submit(SceneRenderRequest request)
            {
                RequireOwner("Submit");
                if (!FrameActive)
                    throw std::logic_error("Scene render requests are accepted only during an active render frame.");
                if (!request.Scene || !request.View || !request.View->Surface())
                    throw std::invalid_argument("SceneRenderRequest requires a scene, view, and render surface.");

                auto& surface = *static_cast<RenderSurfaceState*>(
                    RenderSystemInternalAccess::SurfaceState(*request.View->Surface()));
                const auto owner = surface.Owner.lock();
                if (owner.get() != this)
                    throw std::invalid_argument("SceneRenderRequest surface belongs to another renderer.");
                if (surface.Submitted)
                    throw std::logic_error("A render surface may receive only one scene request per frame.");
                if (!surface.Specification.Depth || !DepthFormat)
                    throw std::logic_error("Scene rendering requires a depth-enabled render surface.");
                const auto camera = request.View->Camera();
                if (!Math::IsFinite(camera.View) || !Math::IsFinite(camera.Projection) ||
                    !ValidColor(camera.ClearColor))
                    throw std::invalid_argument("SceneRenderRequest camera contains invalid values.");
                if (!ValidColor(request.Environment.AmbientColor) ||
                    !std::isfinite(request.Environment.AmbientIntensity) ||
                    request.Environment.AmbientIntensity < 0.0F || request.Environment.AmbientIntensity > 16.0F ||
                    !std::isfinite(request.Environment.Exposure) || request.Environment.Exposure < 0.01F ||
                    request.Environment.Exposure > 16.0F)
                {
                    throw std::invalid_argument("SceneRenderRequest environment contains invalid values.");
                }

                surface.Submitted = true;
                surface.FrameClearColor = camera.ClearColor;
                Requests.push_back({std::move(request), &surface});
            }

            [[nodiscard]] const GpuMeshResources& ResolveMesh(const AssetId id)
            {
                if (!id || id == MeshAsset::CubeId())
                    return DefaultMesh;
                if (id == MeshAsset::ErrorId() || !Assets)
                    return ErrorMesh;

                auto [iterator, inserted] = MeshCache.try_emplace(id);
                auto& entry = iterator->second;
                if (inserted)
                    entry.Handle = Assets->Load<MeshAsset>(id, AssetPriority::High);
                const auto revision = entry.Handle.Revision();
                if (revision != 0 && revision > entry.LastAttemptedRevision)
                {
                    entry.LastAttemptedRevision = revision;
                    if (const auto mesh = entry.Handle.TryGetLoaded())
                    {
                        try
                        {
                            auto replacement = CreateMeshResources(*mesh);
                            Retire(std::exchange(entry.Resources, replacement));
                            entry.LoadedRevision = revision;
                        }
                        catch (const std::exception& error)
                        {
                            KEIRE_CORE_ERROR("Mesh GPU rebuild failed for id={} revision={}: {}", id.ToString(),
                                             revision, error.what());
                        }
                    }
                }
                return entry.Resources.Empty() ? ErrorMesh : entry.Resources;
            }

            [[nodiscard]] const GpuTextureResources& ResolveTexture(const AssetId id)
            {
                if (!id || !Assets)
                    return CheckerboardTexture;
                auto [iterator, inserted] = TextureCache.try_emplace(id);
                auto& entry = iterator->second;
                if (inserted)
                    entry.Handle = Assets->Load<Texture2DAsset>(id, AssetPriority::High);
                const auto revision = entry.Handle.Revision();
                if (revision != 0 && revision > entry.LastAttemptedRevision)
                {
                    entry.LastAttemptedRevision = revision;
                    if (const auto texture = entry.Handle.TryGetLoaded())
                    {
                        try
                        {
                            auto replacement = CreateTextureResources(*texture);
                            Retire(std::exchange(entry.Resources, replacement));
                            entry.LoadedRevision = revision;
                        }
                        catch (const std::exception& error)
                        {
                            KEIRE_CORE_ERROR("Texture GPU rebuild failed for id={} revision={}: {}", id.ToString(),
                                             revision, error.what());
                        }
                    }
                }
                return entry.Resources.Empty() ? CheckerboardTexture : entry.Resources;
            }

            [[nodiscard]] const GpuTextureResources& DefaultTexture(const ShaderTextureSemantic semantic) const noexcept
            {
                switch (semantic)
                {
                case ShaderTextureSemantic::BaseColor:
                    return WhiteTexture;
                case ShaderTextureSemantic::Normal:
                    return FlatNormalTexture;
                case ShaderTextureSemantic::MetallicRoughness:
                case ShaderTextureSemantic::Occlusion:
                    return NeutralOrmTexture;
                case ShaderTextureSemantic::Emissive:
                    return BlackTexture;
                case ShaderTextureSemantic::Generic:
                default:
                    return CheckerboardTexture;
                }
            }

            [[nodiscard]] Ref<const MaterialAsset> ResolveMaterial(const AssetId id)
            {
                if (!id || !Assets)
                    return {};
                auto [iterator, inserted] = MaterialCache.try_emplace(id);
                auto& entry = iterator->second;
                if (inserted)
                    entry.Handle = Assets->Load<MaterialAsset>(id, AssetPriority::High);
                const auto revision = entry.Handle.Revision();
                if (revision != 0 && revision > entry.LastAttemptedRevision)
                {
                    entry.LastAttemptedRevision = revision;
                    if (const auto material = entry.Handle.TryGetLoaded())
                    {
                        entry.LastGood = material;
                        entry.LoadedRevision = revision;
                    }
                }
                return entry.LastGood;
            }

            [[nodiscard]] GpuShaderEntry* ResolveShader(const AssetId id, const SDL_GPUSampleCount samples)
            {
                if (!id || !Assets)
                    return nullptr;
                auto [iterator, inserted] = ShaderCache.try_emplace(id);
                auto& entry = iterator->second;
                if (inserted)
                    entry.Handle = Assets->Load<ShaderAsset>(id, AssetPriority::High);
                const auto revision = entry.Handle.Revision();
                if (revision != 0 && revision > entry.LastAttemptedRevision)
                {
                    entry.LastAttemptedRevision = revision;
                    if (const auto shader = entry.Handle.TryGetLoaded())
                    {
                        try
                        {
                            auto replacement = CreateAssetPipeline(shader->Definition(), samples);
                            for (const auto& [oldSamples, oldPipeline] : entry.Pipelines)
                            {
                                (void)oldSamples;
                                Retire(oldPipeline);
                            }
                            entry.Pipelines = {{samples, replacement}};
                            entry.LastGood = shader;
                            entry.LoadedRevision = revision;
                        }
                        catch (const std::exception& error)
                        {
                            KEIRE_CORE_ERROR("Shader GPU rebuild failed for id={} revision={}: {}", id.ToString(),
                                             revision, error.what());
                        }
                    }
                }
                if (!entry.LastGood)
                    return nullptr;
                auto pipeline =
                    std::ranges::find(entry.Pipelines, samples, &decltype(entry.Pipelines)::value_type::first);
                if (pipeline == entry.Pipelines.end())
                {
                    try
                    {
                        entry.Pipelines.emplace_back(samples,
                                                     CreateAssetPipeline(entry.LastGood->Definition(), samples));
                    }
                    catch (const std::exception& error)
                    {
                        KEIRE_CORE_ERROR("Shader pipeline creation failed for id={}: {}", id.ToString(), error.what());
                        return nullptr;
                    }
                }
                return &entry;
            }

            [[nodiscard]] std::optional<ResolvedAssetMaterial>
            ResolveAssetMaterial(const AssetId id, const Color componentTint, const SDL_GPUSampleCount samples)
            {
                const auto material = ResolveMaterial(id);
                if (!material || !material->Definition().Shader)
                    return std::nullopt;
                auto* shader = ResolveShader(material->Definition().Shader, samples);
                if (!shader)
                    return std::nullopt;
                const auto pipeline =
                    std::ranges::find(shader->Pipelines, samples, &decltype(shader->Pipelines)::value_type::first);
                if (pipeline == shader->Pipelines.end())
                    return std::nullopt;

                const auto& properties = material->Definition().Properties;
                for (const auto& [name, value] : properties)
                {
                    (void)value;
                    if (std::ranges::find(shader->LastGood->Definition().Properties, name,
                                          &ShaderPropertyDefinition::Name) ==
                        shader->LastGood->Definition().Properties.end())
                        return std::nullopt;
                }

                ResolvedAssetMaterial result;
                result.Pipeline = pipeline->second;
                for (const auto& property : shader->LastGood->Definition().Properties)
                {
                    const auto found = properties.find(property.Name);
                    if (property.Type == ShaderPropertyType::Texture2D)
                    {
                        AssetId texture = property.DefaultTexture;
                        if (found != properties.end())
                        {
                            const auto* selected = std::get_if<AssetId>(&found->second);
                            if (!selected)
                                return std::nullopt;
                            texture = *selected;
                        }
                        const auto& resolved =
                            texture ? ResolveTexture(texture) : DefaultTexture(property.TextureSemantic);
                        result.Textures.push_back({resolved.Texture, resolved.Sampler});
                        continue;
                    }

                    Vector4 packed = property.DefaultValue;
                    if (found != properties.end())
                    {
                        const auto& value = found->second;
                        if (const auto* scalar = std::get_if<float>(&value))
                            packed = {*scalar, 0.0F, 0.0F, 0.0F};
                        else if (const auto* vector2 = std::get_if<Vector2>(&value))
                            packed = {vector2->X, vector2->Y, 0.0F, 0.0F};
                        else if (const auto* vector3 = std::get_if<Vector3>(&value))
                            packed = {vector3->X, vector3->Y, vector3->Z, 0.0F};
                        else if (const auto* vector4 = std::get_if<Vector4>(&value))
                            packed = *vector4;
                        else if (const auto* color = std::get_if<Color>(&value))
                            packed = {color->Red, color->Green, color->Blue, color->Alpha};
                        else
                            return std::nullopt;
                    }
                    if (property.Name == "Tint")
                    {
                        packed.X *= componentTint.Red;
                        packed.Y *= componentTint.Green;
                        packed.Z *= componentTint.Blue;
                        packed.W *= componentTint.Alpha;
                    }
                    result.NumericProperties.push_back(packed);
                }
                if (result.NumericProperties.empty())
                    result.NumericProperties.emplace_back();
                return result;
            }

            void DrawScene(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, RenderSurfaceState& surface,
                           const SceneRenderRequest& request)
            {
                const auto samples = ToSdlSampleCount(surface.ActualSamples);
                auto& pipelines = PipelinesFor(samples);
                const auto camera = request.View->Camera();
                const auto lighting = ResolveLighting(request.Scene);

                if (request.DrawGrid && GridBuffer && GridVertexCount > 0)
                {
                    const ObjectUniforms object =
                        MakeObjectUniforms(Math::Multiply(camera.Projection, camera.View), {}, {1.0F, 1.0F, 1.0F, 1.0F},
                                           lighting, request.Environment, false);
                    SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                    const SDL_GPUBufferBinding binding{GridBuffer, 0};
                    SDL_BindGPUGraphicsPipeline(pass, pipelines.Grid);
                    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
                    SDL_DrawGPUPrimitives(pass, GridVertexCount, 1, 0, 0);
                    ++Statistics.DrawCalls;
                }

                for (const auto& entity : request.Scene->Query<MeshRendererComponent>())
                {
                    if (!entity.ActiveInHierarchy())
                        continue;
                    const auto renderer = entity.GetComponent<MeshRendererComponent>();
                    const auto transform = entity.GetComponent<TransformComponent>();
                    if (!renderer || !renderer->Enabled() || !renderer->Visible() || !transform)
                        continue;

                    const Matrix4 viewModel = Math::Multiply(camera.View, transform->WorldMatrix());
                    const auto& mesh = ResolveMesh(renderer->Mesh());
                    const SDL_GPUBufferBinding indexBinding{mesh.Indices, 0};
                    const auto material = renderer->Material()
                                              ? ResolveAssetMaterial(renderer->Material(), renderer->Tint(), samples)
                                              : std::nullopt;
                    if (material)
                    {
                        const AssetObjectUniforms object{transform->WorldMatrix(), camera.View, camera.Projection,
                                                         Transpose(Math::Inverse(transform->WorldMatrix()))};
                        const AssetSceneUniforms scene{
                            {request.Environment.AmbientColor.Red, request.Environment.AmbientColor.Green,
                             request.Environment.AmbientColor.Blue, request.Environment.AmbientIntensity},
                            {lighting.ColorAndIntensity.Red, lighting.ColorAndIntensity.Green,
                             lighting.ColorAndIntensity.Blue, lighting.ColorAndIntensity.Alpha},
                            {lighting.Direction.X, lighting.Direction.Y, lighting.Direction.Z,
                             request.Environment.Exposure}};
                        SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                        SDL_PushGPUFragmentUniformData(commands, 0, &scene, sizeof(scene));
                        SDL_PushGPUFragmentUniformData(
                            commands, 1, material->NumericProperties.data(),
                            static_cast<std::uint32_t>(material->NumericProperties.size() * sizeof(Vector4)));
                        SDL_BindGPUGraphicsPipeline(pass, material->Pipeline);
                        if (!material->Textures.empty())
                        {
                            SDL_BindGPUFragmentSamplers(pass, 0, material->Textures.data(),
                                                        static_cast<std::uint32_t>(material->Textures.size()));
                        }
                        const SDL_GPUBufferBinding vertexBinding{mesh.AssetVertices, 0};
                        SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                    }
                    else
                    {
                        const Color tint = renderer->Material() ? Color{1.0F, 0.0F, 1.0F, 1.0F} : renderer->Tint();
                        const ObjectUniforms object =
                            MakeObjectUniforms(Math::Multiply(camera.Projection, viewModel), transform->WorldMatrix(),
                                               tint, lighting, request.Environment, true);
                        SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                        SDL_BindGPUGraphicsPipeline(pass, pipelines.Cube);
                        const SDL_GPUBufferBinding vertexBinding{mesh.Vertices, 0};
                        SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                    }
                    SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                    SDL_DrawGPUIndexedPrimitives(pass, mesh.IndexCount, 1, 0, 0, 0);
                    ++Statistics.DrawCalls;
                    Statistics.Triangles += mesh.IndexCount / 3;
                }
            }

            void RecordSurface(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface)
            {
                if (!surface.Resources.SampledColor)
                    return;

                SDL_GPUColorTargetInfo color{};
                color.texture = surface.Resources.MultisampleColor ? surface.Resources.MultisampleColor
                                                                   : surface.Resources.SampledColor;
                color.clear_color = {surface.FrameClearColor.Red, surface.FrameClearColor.Green,
                                     surface.FrameClearColor.Blue, surface.FrameClearColor.Alpha};
                color.load_op = SDL_GPU_LOADOP_CLEAR;
                color.store_op = surface.Resources.MultisampleColor ? SDL_GPU_STOREOP_RESOLVE : SDL_GPU_STOREOP_STORE;
                color.resolve_texture = surface.Resources.MultisampleColor ? surface.Resources.SampledColor : nullptr;

                SDL_GPUDepthStencilTargetInfo depth{};
                SDL_GPUDepthStencilTargetInfo* depthPointer = nullptr;
                if (surface.Resources.Depth)
                {
                    depth.texture = surface.Resources.Depth;
                    depth.clear_depth = 1.0F;
                    depth.load_op = SDL_GPU_LOADOP_CLEAR;
                    depth.store_op = SDL_GPU_STOREOP_DONT_CARE;
                    depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
                    depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
                    depthPointer = &depth;
                }

                SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &color, 1, depthPointer);
                if (!pass)
                    throw std::runtime_error("SDL_BeginGPURenderPass(surface) failed: " + LastSdlError());
                const auto request = std::ranges::find(Requests, &surface, &QueuedSceneRequest::Surface);
                if (request != Requests.end())
                    DrawScene(commands, pass, surface, request->Request);
                SDL_EndGPURenderPass(pass);
                ++Statistics.Passes;
                ++Statistics.Surfaces;
            }

            void EndFrame(ImDrawData* drawData)
            {
                RequireOwner("EndFrame");
                if (!FrameActive)
                    throw std::logic_error("No render frame is active.");
                FrameActive = false;

                if (Specification.Mode == RenderMode::Headless)
                {
                    Statistics.Surfaces = static_cast<std::uint32_t>(LiveSurfaces().size());
                    Statistics.Passes = Statistics.Surfaces;
                    return;
                }

                SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(Device);
                if (!commands)
                    throw std::runtime_error("SDL_AcquireGPUCommandBuffer failed: " + LastSdlError());

                try
                {
                    for (const auto& surface : LiveSurfaces())
                        RecordSurface(commands, *surface);

                    SDL_GPUTexture* swapchain = nullptr;
                    if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, NativeWindow, &swapchain, nullptr, nullptr))
                    {
                        (void)SDL_CancelGPUCommandBuffer(commands);
                        throw std::runtime_error("SDL_WaitAndAcquireGPUSwapchainTexture failed: " + LastSdlError());
                    }

                    if (swapchain)
                    {
                        const bool renderUi =
                            drawData && drawData->DisplaySize.x > 0.0F && drawData->DisplaySize.y > 0.0F;
                        if (renderUi)
                            ImGui_ImplSDLGPU3_PrepareDrawData(drawData, commands);

                        SDL_GPUColorTargetInfo target{};
                        target.texture = swapchain;
                        target.clear_color = {
                            Specification.SwapchainClearColor.Red, Specification.SwapchainClearColor.Green,
                            Specification.SwapchainClearColor.Blue, Specification.SwapchainClearColor.Alpha};
                        target.load_op = SDL_GPU_LOADOP_CLEAR;
                        target.store_op = SDL_GPU_STOREOP_STORE;
                        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
                        if (!pass)
                            throw std::runtime_error("SDL_BeginGPURenderPass(swapchain) failed: " + LastSdlError());
                        if (renderUi)
                            ImGui_ImplSDLGPU3_RenderDrawData(drawData, commands, pass);
                        SDL_EndGPURenderPass(pass);
                        ++Statistics.Passes;
                    }

                    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
                    if (!fence)
                        throw std::runtime_error("SDL_SubmitGPUCommandBufferAndAcquireFence failed: " + LastSdlError());
                    InFlight.push_back({fence, std::move(PendingRetired), std::move(PendingRetiredMeshes),
                                        std::move(PendingRetiredTextures), std::move(PendingRetiredPipelines)});
                    PendingRetired.clear();
                    PendingRetiredMeshes.clear();
                    PendingRetiredTextures.clear();
                    PendingRetiredPipelines.clear();
                }
                catch (...)
                {
                    FrameActive = false;
                    throw;
                }
            }

            void Close() noexcept
            {
                if (!Open)
                    return;
                Open = false;
                FrameActive = false;

                if (Device)
                    (void)SDL_WaitForGPUIdle(Device);
                for (const auto& surface : LiveSurfaces())
                {
                    ReleaseResources(surface->Resources);
                    surface->Owner.reset();
                    surface->Width = 0;
                    surface->Height = 0;
                }
                for (auto& resources : PendingRetired)
                    ReleaseResources(resources);
                PendingRetired.clear();
                for (auto& resources : PendingRetiredMeshes)
                    ReleaseMeshResources(resources);
                PendingRetiredMeshes.clear();
                for (auto& resources : PendingRetiredTextures)
                    ReleaseTextureResources(resources);
                PendingRetiredTextures.clear();
                for (auto* pipeline : PendingRetiredPipelines)
                    SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
                PendingRetiredPipelines.clear();
                for (auto& frame : InFlight)
                {
                    for (auto& resources : frame.Retired)
                        ReleaseResources(resources);
                    for (auto& resources : frame.RetiredMeshes)
                        ReleaseMeshResources(resources);
                    for (auto& resources : frame.RetiredTextures)
                        ReleaseTextureResources(resources);
                    for (auto* pipeline : frame.RetiredPipelines)
                        SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
                    if (Device && frame.Fence)
                        SDL_ReleaseGPUFence(Device, frame.Fence);
                }
                InFlight.clear();
                Requests.clear();

                for (auto& pipelines : Pipelines)
                {
                    if (pipelines.Grid)
                        SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.Grid);
                    if (pipelines.Cube)
                        SDL_ReleaseGPUGraphicsPipeline(Device, pipelines.Cube);
                }
                Pipelines.clear();
                for (auto& [id, entry] : MeshCache)
                {
                    (void)id;
                    ReleaseMeshResources(entry.Resources);
                }
                MeshCache.clear();
                for (auto& [id, entry] : TextureCache)
                {
                    (void)id;
                    ReleaseTextureResources(entry.Resources);
                }
                TextureCache.clear();
                MaterialCache.clear();
                for (auto& [id, entry] : ShaderCache)
                {
                    (void)id;
                    for (const auto& [samples, pipeline] : entry.Pipelines)
                    {
                        (void)samples;
                        SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
                    }
                }
                ShaderCache.clear();
                ReleaseTextureResources(CheckerboardTexture);
                ReleaseTextureResources(WhiteTexture);
                ReleaseTextureResources(FlatNormalTexture);
                ReleaseTextureResources(NeutralOrmTexture);
                ReleaseTextureResources(BlackTexture);
                for (const auto& [description, sampler] : SamplerCache)
                {
                    (void)description;
                    SDL_ReleaseGPUSampler(Device, sampler);
                }
                SamplerCache.clear();
                ReleaseMeshResources(ErrorMesh);
                ReleaseMeshResources(DefaultMesh);
                if (GridBuffer)
                    SDL_ReleaseGPUBuffer(Device, GridBuffer);
                GridBuffer = nullptr;
                GridVertexCount = 0;

                if (WindowClaimed && Device && NativeWindow)
                    SDL_ReleaseWindowFromGPUDevice(Device, NativeWindow);
                WindowClaimed = false;
                if (Device)
                    SDL_DestroyGPUDevice(Device);
                Device = nullptr;
                NativeWindow = nullptr;
                Window.Reset();
                Windows.Reset();
                Assets.Reset();
            }

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
            std::unordered_map<AssetId, GpuMeshEntry> MeshCache;
            std::unordered_map<AssetId, GpuTextureEntry> TextureCache;
            std::unordered_map<AssetId, GpuMaterialEntry> MaterialCache;
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
    } // namespace

    class RenderSurface::Impl final
    {
      public:
        explicit Impl(std::shared_ptr<RenderSurfaceState> state) : State(std::move(state)) {}
        ~Impl()
        {
            if (auto owner = State->Owner.lock())
                owner->RetireSurface(*State);
        }
        std::shared_ptr<RenderSurfaceState> State;
    };

    RenderSurface::RenderSurface(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}
    RenderSurface::~RenderSurface() = default;
    std::string RenderSurface::Name() const { return m_Impl->State->Specification.Name; }
    std::uint32_t RenderSurface::Width() const noexcept { return m_Impl->State->Width; }
    std::uint32_t RenderSurface::Height() const noexcept { return m_Impl->State->Height; }
    RenderSampleCount RenderSurface::SampleCount() const noexcept { return m_Impl->State->ActualSamples; }
    Color RenderSurface::ClearColor() const noexcept { return m_Impl->State->Specification.ClearColor; }
    std::uint64_t RenderSurface::Generation() const noexcept { return m_Impl->State->Generation; }
    bool RenderSurface::Available() const noexcept
    {
        const auto owner = m_Impl->State->Owner.lock();
        return owner && owner->Open && m_Impl->State->Resources.SampledColor;
    }

    void RenderSurface::RequestSize(const std::uint32_t width, const std::uint32_t height)
    {
        if (width < 1 || width > 16384 || height < 1 || height > 16384)
            throw std::invalid_argument("Render surface dimensions must be in the range 1..16384.");
        if (const auto owner = m_Impl->State->Owner.lock())
        {
            owner->RequireOwner("RenderSurface::RequestSize");
            m_Impl->State->RequestedWidth = width;
            m_Impl->State->RequestedHeight = height;
            if (m_Impl->State->FailedWidth != width || m_Impl->State->FailedHeight != height)
            {
                m_Impl->State->FailedWidth = 0;
                m_Impl->State->FailedHeight = 0;
            }
        }
    }

    void RenderSurface::SetClearColor(const Color color)
    {
        if (!ValidColor(color))
            throw std::invalid_argument("Render surface clear color must contain finite values in 0..1.");
        if (const auto owner = m_Impl->State->Owner.lock())
        {
            owner->RequireOwner("RenderSurface::SetClearColor");
            m_Impl->State->Specification.ClearColor = color;
        }
    }

    class RenderView::Impl final
    {
      public:
        explicit Impl(Ref<RenderSurface> surface) : Surface(std::move(surface)) {}
        Ref<RenderSurface> Surface;
        RenderCamera Camera;
    };

    RenderView::RenderView(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}
    RenderView::~RenderView() = default;
    Ref<RenderSurface> RenderView::Surface() const noexcept { return m_Impl->Surface; }
    RenderCamera RenderView::Camera() const noexcept { return m_Impl->Camera; }
    void RenderView::SetCamera(const RenderCamera camera)
    {
        if (!Math::IsFinite(camera.View) || !Math::IsFinite(camera.Projection) || !ValidColor(camera.ClearColor))
            throw std::invalid_argument("Render camera contains invalid values.");
        m_Impl->Camera = camera;
    }

    class RenderSystem::Impl final
    {
      public:
        Impl(RenderSpecification specification, Ref<WindowSystem> windows, Ref<Window> window, Ref<AssetSystem> assets)
            : State(std::make_shared<RenderSharedState>(std::move(specification), std::move(windows), std::move(window),
                                                        std::move(assets)))
        {
        }
        std::shared_ptr<RenderSharedState> State;
    };

    RenderSystem::RenderSystem(RenderSpecification specification, Ref<WindowSystem> windows, Ref<Window> window,
                               Ref<AssetSystem> assets)
        : m_Impl(std::make_unique<Impl>(std::move(specification), std::move(windows), std::move(window),
                                        std::move(assets)))
    {
    }

    RenderSystem::~RenderSystem() = default;

    Ref<RenderSurface> RenderSystem::CreateSurface(RenderSurfaceSpecification specification)
    {
        m_Impl->State->RequireOwner("CreateSurface");
        if (specification.Name.empty() || specification.Name.size() > 128)
            throw std::invalid_argument("Render surface names must contain 1..128 UTF-8 bytes.");
        if (specification.Width < 1 || specification.Width > 16384 || specification.Height < 1 ||
            specification.Height > 16384)
            throw std::invalid_argument("Render surface dimensions must be in the range 1..16384.");
        if (!ValidColor(specification.ClearColor))
            throw std::invalid_argument("Render surface clear color must contain finite values in 0..1.");

        auto state = std::make_shared<RenderSurfaceState>();
        state->Owner = m_Impl->State;
        state->Specification = std::move(specification);
        state->RequestedWidth = state->Specification.Width;
        state->RequestedHeight = state->Specification.Height;
        state->FrameClearColor = state->Specification.ClearColor;
        state->Id = m_Impl->State->NextSurfaceId++;
        m_Impl->State->Surfaces.push_back(state);
        return CreateRef<RenderSurface>(std::make_unique<RenderSurface::Impl>(std::move(state)));
    }

    Ref<RenderView> RenderSystem::CreateView(RenderSurfaceSpecification specification)
    {
        return CreateRef<RenderView>(std::make_unique<RenderView::Impl>(CreateSurface(std::move(specification))));
    }

    void RenderSystem::Submit(SceneRenderRequest request) { m_Impl->State->Submit(std::move(request)); }
    RenderMode RenderSystem::Mode() const noexcept { return m_Impl->State->Specification.Mode; }
    RenderStatistics RenderSystem::Statistics() const noexcept { return m_Impl->State->Statistics; }
    bool RenderSystem::IsOpen() const noexcept { return m_Impl->State->Open; }
    void RenderSystem::Close() noexcept { m_Impl->State->Close(); }

    SDL_GPUDevice* RenderSystemInternalAccess::Device(RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->Device;
    }

    SDL_Window* RenderSystemInternalAccess::NativeWindow(RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->NativeWindow;
    }

    SDL_GPUPresentMode RenderSystemInternalAccess::PresentMode(RenderSystem& renderer) noexcept
    {
        return renderer.m_Impl->State->PresentMode;
    }

    SDL_GPUTexture* RenderSystemInternalAccess::Texture(const RenderSurface& surface) noexcept
    {
        return surface.m_Impl->State->Resources.SampledColor;
    }

    std::vector<std::uint8_t> RenderSystemInternalAccess::ReadbackRGBA8(RenderSystem& renderer,
                                                                        const RenderSurface& surface)
    {
        auto& renderState = *renderer.m_Impl->State;
        renderState.RequireOwner("ReadbackRGBA8");
        const auto& surfaceState = *surface.m_Impl->State;
        const auto owner = surfaceState.Owner.lock();
        if (owner.get() != &renderState)
            throw std::invalid_argument("Render surface belongs to another renderer.");
        if (!surfaceState.Resources.SampledColor || surfaceState.Width == 0 || surfaceState.Height == 0)
            throw std::logic_error("Render surface is not available for readback.");

        const std::uint64_t byteSize64 =
            static_cast<std::uint64_t>(surfaceState.Width) * static_cast<std::uint64_t>(surfaceState.Height) * 4ULL;
        if (byteSize64 > std::numeric_limits<std::uint32_t>::max())
            throw std::overflow_error("Render surface is too large for an RGBA8 readback.");
        const auto byteSize = static_cast<std::uint32_t>(byteSize64);

        SDL_GPUTransferBufferCreateInfo transferInformation{};
        transferInformation.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        transferInformation.size = byteSize;
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(renderState.Device, &transferInformation);
        if (!transfer)
            throw std::runtime_error("SDL_CreateGPUTransferBuffer(readback) failed: " + LastSdlError());

        SDL_GPUFence* fence = nullptr;
        try
        {
            SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(renderState.Device);
            if (!commands)
                throw std::runtime_error("SDL_AcquireGPUCommandBuffer(readback) failed: " + LastSdlError());
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
            if (!copy)
            {
                (void)SDL_CancelGPUCommandBuffer(commands);
                throw std::runtime_error("SDL_BeginGPUCopyPass(readback) failed: " + LastSdlError());
            }

            const SDL_GPUTextureRegion source{
                surfaceState.Resources.SampledColor, 0, 0, 0, 0, 0, surfaceState.Width, surfaceState.Height, 1};
            const SDL_GPUTextureTransferInfo destination{transfer, 0, surfaceState.Width, surfaceState.Height};
            SDL_DownloadFromGPUTexture(copy, &source, &destination);
            SDL_EndGPUCopyPass(copy);
            fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
            if (!fence)
                throw std::runtime_error("SDL_SubmitGPUCommandBufferAndAcquireFence(readback) failed: " +
                                         LastSdlError());
            if (!SDL_WaitForGPUFences(renderState.Device, true, &fence, 1))
                throw std::runtime_error("SDL_WaitForGPUFences(readback) failed: " + LastSdlError());

            const void* mapped = SDL_MapGPUTransferBuffer(renderState.Device, transfer, false);
            if (!mapped)
                throw std::runtime_error("SDL_MapGPUTransferBuffer(readback) failed: " + LastSdlError());
            std::vector<std::uint8_t> pixels(byteSize);
            std::memcpy(pixels.data(), mapped, byteSize);
            SDL_UnmapGPUTransferBuffer(renderState.Device, transfer);
            SDL_ReleaseGPUFence(renderState.Device, fence);
            SDL_ReleaseGPUTransferBuffer(renderState.Device, transfer);
            return pixels;
        }
        catch (...)
        {
            if (fence)
                SDL_ReleaseGPUFence(renderState.Device, fence);
            SDL_ReleaseGPUTransferBuffer(renderState.Device, transfer);
            throw;
        }
    }

    void* RenderSystemInternalAccess::SurfaceState(RenderSurface& surface) noexcept
    {
        return surface.m_Impl->State.get();
    }

    void RenderSystemInternalAccess::WaitIdle(RenderSystem& renderer) noexcept
    {
        if (renderer.m_Impl->State->Device)
            (void)SDL_WaitForGPUIdle(renderer.m_Impl->State->Device);
    }

    void RenderSystemInternalAccess::BeginFrame(RenderSystem& renderer) { renderer.m_Impl->State->BeginFrame(); }
    void RenderSystemInternalAccess::CancelFrame(RenderSystem& renderer) noexcept
    {
        renderer.m_Impl->State->CancelFrame();
    }
    void RenderSystemInternalAccess::EndFrame(RenderSystem& renderer, ImDrawData* drawData)
    {
        renderer.m_Impl->State->EndFrame(drawData);
    }
} // namespace Keire
