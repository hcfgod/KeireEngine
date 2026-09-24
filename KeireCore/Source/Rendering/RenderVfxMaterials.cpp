#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace Keire::RenderBackend
{
    void RenderSharedState::BindVfxMaterial(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                                            RenderSurfaceState& surface, const SceneRenderPacket& packet,
                                            const ShadowFrameData& shadows, const ResolvedAssetMaterial* composed,
                                            const AssetId bakedLighting)
    {
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
        const auto vfxBakedLighting = ResolveLightingSet(bakedLighting);
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
        AssetLocalLightUniforms localLights{};
        const auto localLightCount = std::min(packet.LocalLights.size(), MaximumShaderLocalLights);
        localLights.Counts.X = static_cast<float>(packet.LocalLights.size());
        localLights.Counts.Y = static_cast<float>(surface.ActiveWorkset().ForwardPlus.Columns);
        for (std::size_t lightIndex = 0; lightIndex < localLightCount; ++lightIndex)
        {
            const auto& light = packet.LocalLights[lightIndex];
            auto& uniform = localLights.Lights[lightIndex];
            uniform.PositionRange = {light.Position.X, light.Position.Y, light.Position.Z, light.Range};
            uniform.DirectionOuter = {light.Direction.X, light.Direction.Y, light.Direction.Z, light.OuterConeCosine};
            uniform.ColorIntensity = {light.ColorAndIntensity.Red, light.ColorAndIntensity.Green,
                                      light.ColorAndIntensity.Blue, light.ColorAndIntensity.Alpha};
            uniform.Parameters = {light.InnerConeCosine, light.Type == SceneLocalLightType::Spot ? 1.0F : 0.0F, 0.0F,
                                  light.ContactShadows ? 16.0F : 0.0F};
        }
        AssetShadowUniforms shadowUniforms{shadows.Directional, shadows.Local};
        for (std::size_t lightIndex = 0; lightIndex < localLightCount; ++lightIndex)
        {
            const auto& light = packet.LocalLights[lightIndex];
            shadowUniforms.Local.Parameters[lightIndex] = {shadows.LocalLayers[lightIndex], light.ShadowStrength,
                                                           light.Shadows == ShadowQuality::Soft ? 1.0F : 0.0F,
                                                           std::max(light.ShadowBias * 0.01F, 0.0001F)};
        }
        const std::array forwardPlusBuffers{surface.ActiveWorkset().ForwardPlus.Lights,
                                            surface.ActiveWorkset().ForwardPlus.Tiles,
                                            surface.ActiveWorkset().ForwardPlus.LightIndices};
        const auto deviceGeneration = DeviceGeneration.load(std::memory_order_acquire);
        if (composed->SpatialLightingAbiVersion == 3U)
        {
            if (!SpatialSelectionFallbackBuffer || SpatialSelectionFallbackDeviceGeneration != deviceGeneration)
            {
                throw std::logic_error("The mandatory device-generation spatial-selection fallback buffer is "
                                       "unavailable for ABI-v3 mesh VFX.");
            }
            const std::array storageBuffers{forwardPlusBuffers[0], forwardPlusBuffers[1], forwardPlusBuffers[2],
                                            SpatialSelectionFallbackBuffer};
            SDL_BindGPUFragmentStorageBuffers(pass, 0, storageBuffers.data(),
                                              static_cast<std::uint32_t>(storageBuffers.size()));
        }
        else if (composed->UsesForwardPlus)
        {
            SDL_BindGPUFragmentStorageBuffers(pass, 0, forwardPlusBuffers.data(),
                                              static_cast<std::uint32_t>(forwardPlusBuffers.size()));
        }
        const AssetObjectUniforms object{{}, packet.Camera.View, packet.Camera.Projection, {}};
        AssetSceneUniforms scene{};
        scene.AmbientColorIntensity = {packet.Environment.AmbientColor.Red, packet.Environment.AmbientColor.Green,
                                       packet.Environment.AmbientColor.Blue, packet.Environment.AmbientIntensity};
        scene.DirectionalColorIntensity = {
            packet.Lighting.ColorAndIntensity.Red, packet.Lighting.ColorAndIntensity.Green,
            packet.Lighting.ColorAndIntensity.Blue, packet.Lighting.ColorAndIntensity.Alpha};
        scene.DirectionalDirectionExposure = {packet.Lighting.Direction.X, packet.Lighting.Direction.Y,
                                              packet.Lighting.Direction.Z, packet.Environment.Exposure};
        scene.SurfaceParameters = {composed->Surface.AlphaCutoff, static_cast<float>(composed->Surface.AlphaMode), 1.0F,
                                   0.0F};
        scene.LocalLightCounts = localLights.Counts;
        scene.LocalLights = localLights.Lights;
        scene.FrameParameters = {packet.MaterialTimeSeconds, packet.MaterialDeltaSeconds,
                                 static_cast<float>(packet.FrameIndex & 0x00ffffffULL), 0.0F};
        SDL_PushGPUVertexUniformData(commands, 0, &object, sizeof(object));
        if (composed->InstanceAddressingAbiVersion == 2U)
        {
            constexpr std::array<std::uint32_t, 4> addressing{};
            SDL_PushGPUVertexUniformData(commands, 2, addressing.data(), sizeof(addressing));
        }
        SDL_PushGPUFragmentUniformData(commands, 0, &scene, sizeof(scene));
        const Vector4 bindingSentinel{};
        const auto* numericProperties =
            composed->NumericProperties.empty() ? &bindingSentinel : composed->NumericProperties.data();
        const auto numericPropertyBytes =
            static_cast<std::uint32_t>(std::max<std::size_t>(composed->NumericProperties.size(), 1U) * sizeof(Vector4));
        if (composed->UsesVertexMaterialParameters)
            SDL_PushGPUVertexUniformData(commands, 1, numericProperties, numericPropertyBytes);
        SDL_PushGPUFragmentUniformData(commands, 1, numericProperties, numericPropertyBytes);
        if (composed->ReceivesShadows)
            SDL_PushGPUFragmentUniformData(commands, 2, &shadowUniforms, sizeof(shadowUniforms));
        else
            SDL_PushGPUFragmentUniformData(commands, 2, &localLights, sizeof(localLights));
        if (composed->UsesSpatialLighting)
        {
            vfxSpatialUniforms.SpatialSelection[0] = InvalidAssetSpatialSelectionIndex;
            const AssetEnvironmentSpatialUniforms combined{environmentUniforms, vfxSpatialUniforms};
            SDL_PushGPUFragmentUniformData(commands, 3, &combined, sizeof(combined));
        }
        else if (composed->UsesImageBasedLighting)
            SDL_PushGPUFragmentUniformData(commands, 3, &environmentUniforms, sizeof(environmentUniforms));
        if (!composed->Textures.empty() || composed->ReceivesShadows || composed->UsesImageBasedLighting ||
            composed->UsesSpatialLighting)
        {
            std::array<SDL_GPUTextureSamplerBinding, 40> bindings{};
            std::ranges::copy(composed->Textures, bindings.begin());
            auto bindingCount = composed->Textures.size();
            if (composed->ReceivesShadows)
            {
                bindings[bindingCount++] = {surface.ActiveWorkset().DirectionalShadow
                                                ? surface.ActiveWorkset().DirectionalShadow
                                                : EmptyShadowTexture,
                                            ShadowSampler};
                bindings[bindingCount++] = {surface.ActiveWorkset().LocalShadow ? surface.ActiveWorkset().LocalShadow
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
                std::ranges::copy(vfxSpatialBindings, bindings.begin() + static_cast<std::ptrdiff_t>(bindingCount));
                bindingCount += vfxSpatialBindings.size();
            }
            SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), static_cast<std::uint32_t>(bindingCount));
        }
    }
} // namespace Keire::RenderBackend
