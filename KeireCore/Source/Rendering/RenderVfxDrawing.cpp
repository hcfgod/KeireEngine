#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/Rendering/RenderGeometryMathInternal.h"
#include "KeireInternal/Rendering/TransparencyInternal.h"

#include "Keire/Log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace
{
    using Keire::RenderBackend::GeometryDetail::Add;
    using Keire::RenderBackend::GeometryDetail::Cross;
    using Keire::RenderBackend::GeometryDetail::NormalizeOr;
    using Keire::RenderBackend::GeometryDetail::Scale;
    using Keire::RenderBackend::GeometryDetail::Subtract;
} // namespace

namespace Keire::RenderBackend
{
    void RenderSharedState::DrawVfx(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                                    RenderSurfaceState& surface, const SceneRenderPacket& packet,
                                    const ShadowFrameData& shadows)
    {
        auto& pipelines = PipelinesFor(ToSdlSampleCount(surface.ActualSamples));
        const auto& requestedEnvironment =
            packet.Environment.Environment ? ResolveTexture(packet.Environment.Environment) : DefaultSkyTexture;
        const auto& environment = requestedEnvironment.HasDiffuseIrradiance ? requestedEnvironment : DefaultSkyTexture;
        AssetEnvironmentUniforms environmentUniforms{};
        environmentUniforms.DiffuseIrradiance = environment.DiffuseIrradiance;
        environmentUniforms.Parameters = {
            packet.Environment.EnvironmentRotationDegrees, packet.Environment.EnvironmentDiffuseIntensity,
            packet.Environment.EnvironmentSpecularIntensity, static_cast<float>(environment.MipLevels - 1U)};
        environmentUniforms.Encoding = {static_cast<float>(environment.EnvironmentLayout) +
                                            (environment.HdrEncoded ? 16.0F : 0.0F),
                                        0.0F, 0.0F, 0.0F};
        const std::array environmentBindings{
            SDL_GPUTextureSamplerBinding{environment.Texture, environment.Sampler},
            SDL_GPUTextureSamplerBinding{BrdfIntegrationLut.Texture, BrdfIntegrationLut.Sampler}};
        const auto vfxBakedLighting = ResolveLightingSet(packet.BakedLighting);
        const auto* vfxLightingSet = vfxBakedLighting ? &vfxBakedLighting->Definition() : nullptr;
        const auto& vfxLightmaps =
            vfxLightingSet ? ResolveLightingTexture(vfxLightingSet->Lightmaps) : DefaultLightingArray;
        const auto& vfxDirectionality =
            vfxLightingSet ? ResolveLightingTexture(vfxLightingSet->Directionality) : DefaultLightingArray;
        const auto& vfxShadowMasks = vfxLightingSet ? ResolveLightingTexture(vfxLightingSet->ShadowMasks, false, true)
                                                    : DefaultLightingMaskArray;
        const auto& vfxReflections = vfxLightingSet ? ResolveLightingTexture(vfxLightingSet->ReflectionCubemaps, true)
                                                    : DefaultReflectionCubeArray;
        std::array<SDL_GPUTextureSamplerBinding, 5> vfxSpatialBindings{};
        vfxSpatialBindings[0] = {vfxLightmaps.Texture, vfxLightmaps.Sampler};
        vfxSpatialBindings[1] = {vfxDirectionality.Texture, vfxDirectionality.Sampler};
        vfxSpatialBindings[2] = {vfxShadowMasks.Texture, vfxShadowMasks.Sampler};
        vfxSpatialBindings[3] = {vfxReflections.Texture, vfxReflections.Sampler};
        vfxSpatialBindings[4] = {WhiteTexture.Texture, WhiteTexture.Sampler};
        AssetSpatialLightingUniforms vfxSpatialUniforms{};
        vfxSpatialUniforms.LightmapScaleOffset = {1.0F, 1.0F, 0.0F, 0.0F};
        vfxSpatialUniforms.ShadowMaskParameters.X =
            vfxLightingSet ? static_cast<float>(vfxLightingSet->Renderers.size()) : 0.0F;
        vfxSpatialUniforms.ViewProjection = Math::Multiply(packet.Camera.Projection, packet.Camera.View);
        vfxSpatialUniforms.DirectionalCookieAndContact = {0.0F, packet.Lighting.ContactShadows ? 1.0F : 0.0F, 0.35F,
                                                          0.0025F};
        if (packet.Vfx.WorldId() != 0 && pipelines.GpuVfx && pipelines.GpuVfxRibbon && pipelines.GpuVfxMesh)
        {
            const auto world = GpuVfxWorlds.find(packet.Vfx.WorldId());
            if (world != GpuVfxWorlds.end() && !world->second.Empty())
            {
                struct alignas(16) CameraUniforms final
                {
                    Matrix4 ViewProjection;
                    std::array<float, 4> Right{};
                    std::array<float, 4> Up{};
                };
                struct alignas(16) MaterialUniforms final
                {
                    std::array<float, 4> RenderParameters{};
                    std::array<float, 4> AmbientColor{};
                    std::array<float, 4> DirectionalColor{};
                    std::array<float, 4> DirectionalDirection{};
                    std::array<float, 4> MaterialTint{};
                    std::array<float, 4> SurfaceParameters{};
                };
                const auto cameraWorld = Math::Inverse(packet.Camera.View);
                const auto right = Math::TransformDirection(cameraWorld, {1.0F, 0.0F, 0.0F});
                const auto up = Math::TransformDirection(cameraWorld, {0.0F, 1.0F, 0.0F});
                CameraUniforms camera{
                    Math::Multiply(packet.Camera.Projection, packet.Camera.View),
                    {right.X, right.Y, right.Z, 0.0F},
                    {up.X, up.Y, up.Z, 0.0F},
                };
                MaterialUniforms material{
                    {},
                    {packet.Environment.AmbientColor.Red * packet.Environment.AmbientIntensity *
                         packet.Environment.Exposure,
                     packet.Environment.AmbientColor.Green * packet.Environment.AmbientIntensity *
                         packet.Environment.Exposure,
                     packet.Environment.AmbientColor.Blue * packet.Environment.AmbientIntensity *
                         packet.Environment.Exposure,
                     1.0F},
                    {packet.Lighting.ColorAndIntensity.Red, packet.Lighting.ColorAndIntensity.Green,
                     packet.Lighting.ColorAndIntensity.Blue, packet.Lighting.ColorAndIntensity.Alpha},
                    {packet.Lighting.Direction.X, packet.Lighting.Direction.Y, packet.Lighting.Direction.Z,
                     packet.Lighting.Enabled ? 1.0F : 0.0F},
                    {1.0F, 1.0F, 1.0F, 1.0F},
                    {},
                };
                const auto samples = ToSdlSampleCount(surface.ActualSamples);
                AssetLocalLightUniforms localLights{};
                const auto localLightCount = std::min(packet.LocalLights.size(), MaximumShaderLocalLights);
                localLights.Counts.X = static_cast<float>(packet.LocalLights.size());
                localLights.Counts.Y = static_cast<float>(surface.ForwardPlus.Columns);
                for (std::size_t lightIndex = 0; lightIndex < localLightCount; ++lightIndex)
                {
                    const auto& light = packet.LocalLights[lightIndex];
                    auto& uniform = localLights.Lights[lightIndex];
                    uniform.PositionRange = {light.Position.X, light.Position.Y, light.Position.Z, light.Range};
                    uniform.DirectionOuter = {light.Direction.X, light.Direction.Y, light.Direction.Z,
                                              light.OuterConeCosine};
                    uniform.ColorIntensity = {light.ColorAndIntensity.Red, light.ColorAndIntensity.Green,
                                              light.ColorAndIntensity.Blue, light.ColorAndIntensity.Alpha};
                    uniform.Parameters = {light.InnerConeCosine, light.Type == SceneLocalLightType::Spot ? 1.0F : 0.0F,
                                          0.0F, light.ContactShadows ? 16.0F : 0.0F};
                }
                AssetShadowUniforms shadowUniforms{shadows.Directional, shadows.Local};
                for (std::size_t lightIndex = 0; lightIndex < localLightCount; ++lightIndex)
                {
                    const auto& light = packet.LocalLights[lightIndex];
                    shadowUniforms.Local.Parameters[lightIndex] = {shadows.LocalLayers[lightIndex],
                                                                   light.ShadowStrength,
                                                                   light.Shadows == ShadowQuality::Soft ? 1.0F : 0.0F,
                                                                   std::max(light.ShadowBias * 0.01F, 0.0001F)};
                }
                const std::array forwardPlusBuffers{surface.ForwardPlus.Lights, surface.ForwardPlus.Tiles,
                                                    surface.ForwardPlus.LightIndices};
                for (auto& [key, emitter] : world->second.Emitters)
                {
                    (void)key;
                    if (!emitter.RenderBuffers)
                        continue;
                    const std::array storage{world->second.Particles, emitter.RenderBuffers->Indices};
                    SDL_BindGPUVertexStorageBuffers(pass, 0, storage.data(),
                                                    static_cast<std::uint32_t>(storage.size()));
                    if (emitter.Renderer == VfxRendererType::Mesh)
                    {
                        const auto& mesh = ResolveMesh(emitter.Mesh);
                        if (mesh.Empty() || mesh.IndexCount == 0)
                            continue;
                        const SDL_GPUBufferBinding vertexBinding{mesh.AssetVertices, 0};
                        const SDL_GPUBufferBinding indexBinding{mesh.Indices, 0};
                        SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                        SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
                        const auto* composed =
                            emitter.Material ? ResolveAssetMaterial(emitter.Material, samples) : nullptr;
                        if (composed && composed->UsesInstancing)
                        {
                            SDL_BindGPUGraphicsPipeline(pass, composed->Pipeline);
                            SDL_BindGPUVertexStorageBuffers(pass, 0, &emitter.RenderBuffers->Instances, 1);
                            if (composed->UsesForwardPlus)
                            {
                                SDL_BindGPUFragmentStorageBuffers(
                                    pass, 0, forwardPlusBuffers.data(),
                                    static_cast<std::uint32_t>(forwardPlusBuffers.size()));
                            }
                            const AssetObjectUniforms object{{}, packet.Camera.View, packet.Camera.Projection, {}};
                            AssetSceneUniforms scene{};
                            scene.AmbientColorIntensity = {
                                packet.Environment.AmbientColor.Red, packet.Environment.AmbientColor.Green,
                                packet.Environment.AmbientColor.Blue, packet.Environment.AmbientIntensity};
                            scene.DirectionalColorIntensity = {
                                packet.Lighting.ColorAndIntensity.Red, packet.Lighting.ColorAndIntensity.Green,
                                packet.Lighting.ColorAndIntensity.Blue, packet.Lighting.ColorAndIntensity.Alpha};
                            scene.DirectionalDirectionExposure = {
                                packet.Lighting.Direction.X, packet.Lighting.Direction.Y, packet.Lighting.Direction.Z,
                                packet.Environment.Exposure};
                            scene.SurfaceParameters = {composed->Surface.AlphaCutoff,
                                                       static_cast<float>(composed->Surface.AlphaMode), 1.0F, 0.0F};
                            scene.LocalLightCounts = localLights.Counts;
                            scene.LocalLights = localLights.Lights;
                            scene.FrameParameters = {packet.MaterialTimeSeconds, packet.MaterialDeltaSeconds,
                                                     static_cast<float>(packet.FrameIndex & 0x00ffffffULL), 0.0F};
                            SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
                            SDL_PushGPUFragmentUniformData(commands, 0, &scene, sizeof(scene));
                            const Vector4 bindingSentinel{};
                            const auto* numericProperties = composed->NumericProperties.empty()
                                                                ? &bindingSentinel
                                                                : composed->NumericProperties.data();
                            const auto numericPropertyBytes = static_cast<std::uint32_t>(
                                std::max<std::size_t>(composed->NumericProperties.size(), 1U) * sizeof(Vector4));
                            if (composed->UsesVertexMaterialParameters)
                                SDL_PushGPUVertexUniformData(commands, 1, numericProperties, numericPropertyBytes);
                            SDL_PushGPUFragmentUniformData(commands, 1, numericProperties, numericPropertyBytes);
                            if (composed->ReceivesShadows)
                                SDL_PushGPUFragmentUniformData(commands, 2, &shadowUniforms, sizeof(shadowUniforms));
                            else
                                SDL_PushGPUFragmentUniformData(commands, 2, &localLights, sizeof(localLights));
                            if (composed->UsesSpatialLighting)
                            {
                                const AssetEnvironmentSpatialUniforms combined{environmentUniforms, vfxSpatialUniforms};
                                SDL_PushGPUFragmentUniformData(commands, 3, &combined, sizeof(combined));
                            }
                            else if (composed->UsesImageBasedLighting)
                                SDL_PushGPUFragmentUniformData(commands, 3, &environmentUniforms,
                                                               sizeof(environmentUniforms));
                            if (!composed->Textures.empty() || composed->ReceivesShadows ||
                                composed->UsesImageBasedLighting || composed->UsesSpatialLighting)
                            {
                                std::array<SDL_GPUTextureSamplerBinding, 40> bindings{};
                                std::ranges::copy(composed->Textures, bindings.begin());
                                auto bindingCount = composed->Textures.size();
                                if (composed->ReceivesShadows)
                                {
                                    bindings[bindingCount++] = {surface.Resources.DirectionalShadow
                                                                    ? surface.Resources.DirectionalShadow
                                                                    : EmptyShadowTexture,
                                                                ShadowSampler};
                                    bindings[bindingCount++] = {surface.Resources.LocalShadow
                                                                    ? surface.Resources.LocalShadow
                                                                    : EmptyShadowTexture,
                                                                ShadowSampler};
                                }
                                if (composed->UsesImageBasedLighting)
                                {
                                    bindings[bindingCount++] = environmentBindings[0];
                                    bindings[bindingCount++] = environmentBindings[1];
                                }
                                if (composed->UsesSpatialLighting)
                                {
                                    std::ranges::copy(vfxSpatialBindings,
                                                      bindings.begin() + static_cast<std::ptrdiff_t>(bindingCount));
                                    bindingCount += vfxSpatialBindings.size();
                                }
                                SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(),
                                                            static_cast<std::uint32_t>(bindingCount));
                            }
                            emitter.MaterialDiagnosticReported = false;
                        }
                        else
                        {
                            if (emitter.Material && composed && !emitter.MaterialDiagnosticReported)
                            {
                                KEIRE_CORE_ERROR("GPU VFX material {} requires an instancing-capable shader; the "
                                                 "last-good built-in Mesh output remains active.",
                                                 emitter.Material.ToString());
                                emitter.MaterialDiagnosticReported = true;
                            }
                            material.RenderParameters = {};
                            material.MaterialTint = {1.0F, 1.0F, 1.0F, 1.0F};
                            material.SurfaceParameters = {};
                            if (composed && composed->TintSlot &&
                                *composed->TintSlot < composed->NumericProperties.size())
                            {
                                const auto& tint = composed->NumericProperties[*composed->TintSlot];
                                material.MaterialTint = {tint.X, tint.Y, tint.Z, tint.W};
                                material.SurfaceParameters = {composed->Surface.AlphaCutoff,
                                                              static_cast<float>(composed->Surface.AlphaMode), 1.0F,
                                                              0.0F};
                            }
                            SDL_BindGPUGraphicsPipeline(pass, pipelines.GpuVfxMesh);
                            SDL_PushGPUVertexUniformData(commands, 0, &camera, sizeof(camera));
                            SDL_PushGPUFragmentUniformData(commands, 0, &material, sizeof(material));
                        }
                        SDL_DrawGPUIndexedPrimitivesIndirect(pass, emitter.RenderBuffers->IndirectArguments, 0, 1);
                    }
                    else
                    {
                        const auto* composed =
                            emitter.Material ? ResolveAssetMaterial(emitter.Material, samples) : nullptr;
                        const auto materialTexture = composed && !composed->Textures.empty();
                        material.RenderParameters = {emitter.Sprite || materialTexture ? 1.0F : 0.0F,
                                                     emitter.Renderer == VfxRendererType::Volumetric ? 1.0F : 0.0F,
                                                     emitter.Renderer == VfxRendererType::Ribbon ? 1.0F : 0.0F, 0.0F};
                        material.MaterialTint = {1.0F, 1.0F, 1.0F, 1.0F};
                        material.SurfaceParameters = {};
                        if (composed)
                        {
                            if (composed->TintSlot && *composed->TintSlot < composed->NumericProperties.size())
                            {
                                const auto& tint = composed->NumericProperties[*composed->TintSlot];
                                material.MaterialTint = {tint.X, tint.Y, tint.Z, tint.W};
                            }
                            material.SurfaceParameters = {composed->Surface.AlphaCutoff,
                                                          static_cast<float>(composed->Surface.AlphaMode), 1.0F, 0.0F};
                        }
                        SDL_BindGPUGraphicsPipeline(pass, emitter.Renderer == VfxRendererType::Ribbon
                                                              ? pipelines.GpuVfxRibbon
                                                              : pipelines.GpuVfx);
                        SDL_PushGPUVertexUniformData(commands, 0, &camera, sizeof(camera));
                        SDL_PushGPUFragmentUniformData(commands, 0, &material, sizeof(material));
                        const auto& texture = emitter.Sprite ? ResolveTexture(emitter.Sprite) : WhiteTexture;
                        const SDL_GPUTextureSamplerBinding textureBinding =
                            materialTexture ? composed->Textures.front()
                                            : SDL_GPUTextureSamplerBinding{texture.Texture, texture.Sampler};
                        SDL_BindGPUFragmentSamplers(pass, 0, &textureBinding, 1);
                        SDL_DrawGPUPrimitivesIndirect(pass, emitter.RenderBuffers->IndirectArguments, 0, 1);
                    }
                    ++Statistics.DrawCalls;
                    ++Statistics.VfxIndirectDraws;
                }
            }
        }

        const auto particles = packet.Vfx.Particles();
        if (particles.empty())
            return;
        if (!pipelines.Vfx)
            return;

        struct PreparedParticle final
        {
            const VfxRenderParticle* Particle = nullptr;
            Vector3 RibbonStart;
            float Depth = 0.0F;
            std::uint32_t SpriteFirstVertex = 0;
        };
        std::vector<Vector3> ribbonStarts(particles.size());
        std::vector<std::size_t> ribbonOrder;
        ribbonOrder.reserve(particles.size());
        for (std::size_t index = 0; index < particles.size(); ++index)
        {
            ribbonStarts[index] = particles[index].Position;
            if (particles[index].Renderer == VfxRendererType::Ribbon)
                ribbonOrder.push_back(index);
        }
        std::ranges::sort(ribbonOrder,
                          [&particles](const auto left, const auto right)
                          {
                              const auto& leftParticle = particles[left];
                              const auto& rightParticle = particles[right];
                              return std::tie(leftParticle.Effect, leftParticle.System, leftParticle.StripId,
                                              leftParticle.ParticleIndexInStrip) <
                                     std::tie(rightParticle.Effect, rightParticle.System, rightParticle.StripId,
                                              rightParticle.ParticleIndexInStrip);
                          });
        for (std::size_t index = 1; index < ribbonOrder.size(); ++index)
        {
            const auto previousIndex = ribbonOrder[index - 1U];
            const auto currentIndex = ribbonOrder[index];
            const auto& previous = particles[previousIndex];
            const auto& current = particles[currentIndex];
            if (previous.Effect == current.Effect && previous.System == current.System &&
                previous.StripId == current.StripId &&
                current.ParticleIndexInStrip == previous.ParticleIndexInStrip + 1U)
            {
                ribbonStarts[currentIndex] = previous.Position;
            }
        }
        std::vector<PreparedParticle> prepared;
        prepared.reserve(particles.size());
        for (std::size_t index = 0; index < particles.size(); ++index)
        {
            const auto& particle = particles[index];
            if (particle.Renderer != VfxRendererType::Mesh)
            {
                prepared.push_back({std::addressof(particle), ribbonStarts[index],
                                    Math::TransformPoint(packet.Camera.View, particle.Position).Z});
            }
        }
        if (prepared.empty())
            return;
        std::ranges::stable_sort(prepared, [](const auto& left, const auto& right)
                                 { return Detail::TransparentBackToFront(left.Depth, right.Depth); });

        const auto cameraWorld = Math::Inverse(packet.Camera.View);
        const auto cameraRight = Math::TransformDirection(cameraWorld, {1.0F, 0.0F, 0.0F});
        const auto cameraUp = Math::TransformDirection(cameraWorld, {0.0F, 1.0F, 0.0F});
        std::vector<RenderVertex> spriteVertices;
        spriteVertices.reserve(prepared.size() * 6U);
        const auto cameraForward = NormalizeOr(Cross(cameraRight, cameraUp), {0.0F, 0.0F, 1.0F});
        constexpr float degreesToRadians = 0.01745329251994329577F;
        for (auto& value : prepared)
        {
            const auto& particle = *value.Particle;
            value.SpriteFirstVertex = static_cast<std::uint32_t>(spriteVertices.size());
            constexpr Vector3 white{1.0F, 1.0F, 1.0F};
            if (particle.Renderer == VfxRendererType::Ribbon)
            {
                const auto segment = Subtract(particle.Position, value.RibbonStart);
                const auto side = Scale(NormalizeOr(Cross(segment, cameraForward), cameraRight), particle.Size * 0.5F);
                const auto startLeft = Subtract(value.RibbonStart, side);
                const auto startRight = Add(value.RibbonStart, side);
                const auto endRight = Add(particle.Position, side);
                const auto endLeft = Subtract(particle.Position, side);
                constexpr float mode = 1.0F;
                spriteVertices.push_back({startLeft, white, {0.0F, 0.0F, mode}});
                spriteVertices.push_back({startRight, white, {0.0F, 1.0F, mode}});
                spriteVertices.push_back({endRight, white, {1.0F, 1.0F, mode}});
                spriteVertices.push_back({startLeft, white, {0.0F, 0.0F, mode}});
                spriteVertices.push_back({endRight, white, {1.0F, 1.0F, mode}});
                spriteVertices.push_back({endLeft, white, {1.0F, 0.0F, mode}});
                continue;
            }
            const auto angle = particle.Rotation.Z * degreesToRadians;
            const auto cosine = std::cos(angle);
            const auto sine = std::sin(angle);
            const auto right = Scale(Add(Scale(cameraRight, cosine), Scale(cameraUp, sine)), particle.Size * 0.5F);
            const auto up = Scale(Add(Scale(cameraUp, cosine), Scale(cameraRight, -sine)), particle.Size * 0.5F);
            const auto lowerLeft = Subtract(Subtract(particle.Position, right), up);
            const auto lowerRight = Add(Subtract(particle.Position, up), right);
            const auto upperRight = Add(Add(particle.Position, right), up);
            const auto upperLeft = Add(Subtract(particle.Position, right), up);
            const auto mode = particle.Renderer == VfxRendererType::Volumetric ? 2.0F : 0.0F;
            spriteVertices.push_back({lowerLeft, white, {0.0F, 0.0F, mode}});
            spriteVertices.push_back({lowerRight, white, {1.0F, 0.0F, mode}});
            spriteVertices.push_back({upperRight, white, {1.0F, 1.0F, mode}});
            spriteVertices.push_back({lowerLeft, white, {0.0F, 0.0F, mode}});
            spriteVertices.push_back({upperRight, white, {1.0F, 1.0F, mode}});
            spriteVertices.push_back({upperLeft, white, {0.0F, 1.0F, mode}});
        }

        SDL_GPUBuffer* spriteBuffer = nullptr;
        if (!spriteVertices.empty())
        {
            spriteBuffer = UploadVertexBuffer(spriteVertices);
            FrameTransientBuffers.push_back(spriteBuffer);
        }

        SDL_BindGPUGraphicsPipeline(pass, pipelines.Vfx);
        struct alignas(16) CpuVfxUniforms final
        {
            Matrix4 ViewProjection;
        };
        CpuVfxUniforms uniforms{Math::Multiply(packet.Camera.Projection, packet.Camera.View)};
        static_assert(sizeof(CpuVfxUniforms) == 64);
        struct alignas(16) CpuMaterialUniforms final
        {
            std::array<float, 4> Tint{};
            std::array<float, 4> SurfaceParameters{};
        };
        const auto samples = ToSdlSampleCount(surface.ActualSamples);
        for (const auto& value : prepared)
        {
            const auto& particle = *value.Particle;
            if (particle.Size <= 0.0F)
                continue;
            if (particle.Renderer != VfxRendererType::Mesh)
            {
                const auto* composed = particle.Material ? ResolveAssetMaterial(particle.Material, samples) : nullptr;
                CpuMaterialUniforms material{
                    {particle.Tint.Red, particle.Tint.Green, particle.Tint.Blue, particle.Tint.Alpha}, {}};
                if (composed)
                {
                    if (composed->TintSlot && *composed->TintSlot < composed->NumericProperties.size())
                    {
                        const auto& tint = composed->NumericProperties[*composed->TintSlot];
                        material.Tint[0] *= tint.X;
                        material.Tint[1] *= tint.Y;
                        material.Tint[2] *= tint.Z;
                        material.Tint[3] *= tint.W;
                    }
                    material.SurfaceParameters = {!composed->Textures.empty() ? 1.0F : 0.0F,
                                                  composed->Surface.AlphaCutoff,
                                                  static_cast<float>(composed->Surface.AlphaMode), 1.0F};
                }
                else
                {
                    material.SurfaceParameters[0] = particle.Sprite ? 1.0F : 0.0F;
                }
                const SDL_GPUBufferBinding vertexBinding{spriteBuffer, 0};
                SDL_PushGPUVertexUniformData(commands, 0, &uniforms, sizeof(uniforms));
                SDL_PushGPUFragmentUniformData(commands, 0, &material, sizeof(material));
                const auto& texture = particle.Sprite ? ResolveTexture(particle.Sprite) : WhiteTexture;
                const SDL_GPUTextureSamplerBinding textureBinding =
                    composed && !composed->Textures.empty()
                        ? composed->Textures.front()
                        : SDL_GPUTextureSamplerBinding{texture.Texture, texture.Sampler};
                SDL_BindGPUFragmentSamplers(pass, 0, &textureBinding, 1);
                SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
                SDL_DrawGPUPrimitives(pass, 6, 1, value.SpriteFirstVertex, 0);
                ++Statistics.DrawCalls;
                Statistics.Triangles += 2;
            }
        }
    }

} // namespace Keire::RenderBackend
