#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace Keire::RenderBackend
{
    void RenderSharedState::ReleaseDisplayOutputs(SurfaceResources& resources) noexcept
    {
        for (auto* texture : resources.DisplayOutputs)
        {
            if (texture)
            {
                SDL_ReleaseGPUTexture(Device, texture);
            }
        }
        resources.DisplayOutputs.clear();
    }

    void RenderSharedState::RecordDisplayOutput(SDL_GPUCommandBuffer* commands, const RenderSurfaceState& surface)
    {
        if (!DisplayOutputPipeline || !ToneMapSampler || !surface.Resources.WriterColor(surface.ActiveWorksetSlot) ||
            !surface.Resources.WriterDisplayColor(surface.ActiveWorksetSlot))
        {
            throw std::logic_error("Display output resources are unavailable for an active render surface.");
        }
        SDL_GPUColorTargetInfo target{};
        target.texture = surface.Resources.WriterDisplayColor(surface.ActiveWorksetSlot);
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
        if (!pass)
        {
            throw std::runtime_error("SDL_BeginGPURenderPass(display output) failed: " + LastSdlError());
        }
        const SDL_GPUTextureSamplerBinding binding{surface.Resources.WriterColor(surface.ActiveWorksetSlot),
                                                   ToneMapSampler};
        const std::array bindings{binding, binding, binding, binding};
        const std::array uniforms{Vector4{0.0F, 0.0F, 3.0F, 0.0F}, Vector4{}};
        SDL_BindGPUGraphicsPipeline(pass, DisplayOutputPipeline);
        SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), static_cast<std::uint32_t>(bindings.size()));
        SDL_PushGPUFragmentUniformData(commands, 0, uniforms.data(), sizeof(uniforms));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
    }

    void RenderSharedState::RecordToneMap(SDL_GPUCommandBuffer* commands, const RenderSurfaceState& surface,
                                          const RenderAntiAliasingMode antiAliasing,
                                          const bool temporalHistoryContinuous, const float bloomIntensity)
    {
        if (!std::isfinite(bloomIntensity) || bloomIntensity < 0.0F || bloomIntensity > 16.0F ||
            (bloomIntensity > 0.0F && !surface.ActiveWorkset().BloomResult))
        {
            throw std::invalid_argument("Tone mapping requires a valid bloom intensity and a prepared bloom texture.");
        }
        if (!ToneMapPipeline || !ToneMapSampler || !surface.ActiveWorkset().HdrColor ||
            !surface.Resources.WriterColor(surface.ActiveWorksetSlot) ||
            !surface.Resources.PublishedTemporalHistory() ||
            !surface.Resources.WriterTemporalHistory(surface.ActiveWorksetSlot) || !BlackDataTexture.Texture)
        {
            throw std::logic_error("Tone-map resources are unavailable for an active render surface.");
        }
        SDL_GPUColorTargetInfo target{};
        target.texture = surface.Resources.WriterColor(surface.ActiveWorksetSlot);
        target.load_op = SDL_GPU_LOADOP_DONT_CARE;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
        if (!pass)
        {
            throw std::runtime_error("SDL_BeginGPURenderPass(tone map) failed: " + LastSdlError());
        }
        auto* const velocity = surface.ActiveWorkset().GBufferVelocity ? surface.ActiveWorkset().GBufferVelocity
                                                                       : BlackDataTexture.Texture;
        const std::array bindings{
            SDL_GPUTextureSamplerBinding{surface.ActiveWorkset().HdrColor, ToneMapSampler},
            SDL_GPUTextureSamplerBinding{surface.Resources.PublishedTemporalHistory(), ToneMapSampler},
            SDL_GPUTextureSamplerBinding{velocity, ToneMapSampler},
            SDL_GPUTextureSamplerBinding{bloomIntensity > 0.0F ? surface.ActiveWorkset().BloomResult
                                                               : BlackDataTexture.Texture,
                                         ToneMapSampler}};
        const bool taa = antiAliasing == RenderAntiAliasingMode::Taa;
        const bool historyValid = taa && temporalHistoryContinuous && surface.TemporalHistoryValid &&
                                  surface.TemporalHistoryPath == surface.ActiveFeatureSelection.EffectivePath;
        const Vector4 parameters{1.0F / static_cast<float>(std::max(surface.Width, 1U)),
                                 1.0F / static_cast<float>(std::max(surface.Height, 1U)),
                                 taa                                            ? 2.0F
                                 : antiAliasing == RenderAntiAliasingMode::Fxaa ? 1.0F
                                                                                : 0.0F,
                                 historyValid ? 1.0F : 0.0F};
        SDL_BindGPUGraphicsPipeline(pass, ToneMapPipeline);
        SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), static_cast<std::uint32_t>(bindings.size()));
        const std::array toneMapUniforms{parameters, Vector4{bloomIntensity, 0.0F, 0.0F, 0.0F}};
        SDL_PushGPUFragmentUniformData(commands, 0, toneMapUniforms.data(), sizeof(toneMapUniforms));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;

        if (taa)
        {
            auto* copy = SDL_BeginGPUCopyPass(commands);
            if (!copy)
            {
                throw std::runtime_error("SDL_BeginGPUCopyPass(temporal history) failed: " + LastSdlError());
            }
            const SDL_GPUTextureLocation source{
                surface.Resources.WriterColor(surface.ActiveWorksetSlot), 0, 0, 0, 0, 0};
            const SDL_GPUTextureLocation destination{
                surface.Resources.WriterTemporalHistory(surface.ActiveWorksetSlot), 0, 0, 0, 0, 0};
            SDL_CopyGPUTextureToTexture(copy, &source, &destination, surface.Width, surface.Height, 1, false);
            SDL_EndGPUCopyPass(copy);
            ++Statistics.Passes;
        }
    }
} // namespace Keire::RenderBackend
