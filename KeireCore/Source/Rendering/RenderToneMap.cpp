#include "KeireInternal/Rendering/PostProcessLens.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace Keire::RenderBackend
{
    void RenderSharedState::EnsureLensResources(RenderSurfaceState& surface, const bool secondColorTarget,
                                                const bool depthOfField)
    {
        auto& workset = surface.ActiveWorkset();
        const auto allocate = [&](SDL_GPUTexture*& texture, const SDL_GPUTextureFormat format,
                                  const SDL_GPUTextureUsageFlags usage, const std::uint32_t divisor = 1U)
        {
            if (texture)
            {
                return;
            }
            SDL_GPUTextureCreateInfo description{};
            description.type = SDL_GPU_TEXTURETYPE_2D;
            description.format = format;
            description.usage = usage | SDL_GPU_TEXTUREUSAGE_SAMPLER;
            description.width = Detail::PostProcessDofReducedExtent(surface.Width, divisor);
            description.height = Detail::PostProcessDofReducedExtent(surface.Height, divisor);
            description.layer_count_or_depth = 1;
            description.num_levels = 1;
            description.sample_count = SDL_GPU_SAMPLECOUNT_1;
            workset.TransientTextures.reserve(workset.TransientTextures.size() + 1);
            texture = SDL_CreateGPUTexture(Device, &description);
            if (!texture)
            {
                throw std::runtime_error("Lens texture allocation failed: " + LastSdlError());
            }
            workset.TransientTextures.push_back(texture);
        };
        allocate(workset.LensDepth, DepthFormat, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
        allocate(workset.LensColors[0], SceneColorFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
        if (secondColorTarget)
        {
            allocate(workset.LensColors[1], SceneColorFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
        }
        if (depthOfField)
        {
            for (std::size_t level = 0; level < workset.DofPyramid.size(); ++level)
            {
                allocate(workset.DofPyramid[level], SceneColorFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, 2U << level);
            }
            for (auto*& texture : workset.DofLayers)
            {
                allocate(texture, SceneColorFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, 2U);
            }
            for (auto*& texture : workset.DofBounds)
            {
                allocate(texture, SceneColorFormat, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, 8U);
            }
        }
    }

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
        const std::array<Vector4, 6> uniforms{Vector4{0.0F, 0.0F, 3.0F, 0.0F}, Vector4{}};
        SDL_BindGPUGraphicsPipeline(pass, DisplayOutputPipeline);
        SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), static_cast<std::uint32_t>(bindings.size()));
        SDL_PushGPUFragmentUniformData(commands, 0, uniforms.data(), sizeof(uniforms));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;
    }

    void RenderSharedState::RecordToneMap(SDL_GPUCommandBuffer* commands, const RenderSurfaceState& surface,
                                          const RenderAntiAliasingMode antiAliasing,
                                          const bool temporalHistoryContinuous, const float bloomIntensity,
                                          const SceneRenderPacket* packet)
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
        auto* const velocity = surface.ActiveWorkset().GBufferVelocity ? surface.ActiveWorkset().GBufferVelocity
                                                                       : BlackDataTexture.Texture;
        std::array bindings{SDL_GPUTextureSamplerBinding{surface.ActiveWorkset().HdrColor, ToneMapSampler},
                            SDL_GPUTextureSamplerBinding{surface.Resources.PublishedTemporalHistory(), ToneMapSampler},
                            SDL_GPUTextureSamplerBinding{velocity, ToneMapSampler},
                            SDL_GPUTextureSamplerBinding{bloomIntensity > 0.0F ? surface.ActiveWorkset().BloomResult
                                                                               : BlackDataTexture.Texture,
                                                         ToneMapSampler}};
        const bool taa = antiAliasing == RenderAntiAliasingMode::Taa;
        const bool historyValid = taa && temporalHistoryContinuous && surface.TemporalHistoryValid &&
                                  surface.TemporalHistoryPath == surface.ActiveFeatureSelection.EffectivePath;
        const auto draw = [&](const float mode, const float intensity)
        {
            SDL_GPUColorTargetInfo target{};
            target.texture = surface.Resources.WriterColor(surface.ActiveWorksetSlot);
            target.load_op = SDL_GPU_LOADOP_DONT_CARE;
            target.store_op = SDL_GPU_STOREOP_STORE;
            auto* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
            if (!pass)
            {
                throw std::runtime_error("SDL_BeginGPURenderPass(tone map) failed: " + LastSdlError());
            }
            const Vector4 parameters{1.0F / static_cast<float>(std::max(surface.Width, 1U)),
                                     1.0F / static_cast<float>(std::max(surface.Height, 1U)), mode,
                                     historyValid ? 1.0F : 0.0F};
            const std::array<Vector4, 6> uniforms{parameters, Vector4{intensity, 0.0F, 0.0F, 0.0F}};
            SDL_BindGPUGraphicsPipeline(pass, ToneMapPipeline);
            SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), static_cast<std::uint32_t>(bindings.size()));
            SDL_PushGPUFragmentUniformData(commands, 0, uniforms.data(), sizeof(uniforms));
            SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
            SDL_EndGPURenderPass(pass);
            ++Statistics.Passes;
            ++Statistics.DrawCalls;
            ++Statistics.Triangles;
        };
        if (taa)
        {
            // Resolve and retain linear HDR before lens/color operations. The existing RGBA16 output is
            // temporary here; the final display-linear result overwrites it only after history is copied.
            draw(5.0F, bloomIntensity);
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
            bindings[0].texture = surface.Resources.WriterTemporalHistory(surface.ActiveWorksetSlot);
        }
        float remainingBloom = taa ? 0.0F : bloomIntensity;
        if (packet && surface.ActiveWorkset().LensDepth)
        {
            const auto parameter = [&](const PostProcessParameter key)
            { return packet->PostProcessing.Parameters[static_cast<std::size_t>(key)]; };
            const auto weight = [&](const PostProcessEffect effect)
            { return packet->PostProcessing.EffectWeights[static_cast<std::size_t>(effect)]; };
            const double focus = parameter(PostProcessParameter::FocusDistance);
            const double focal = parameter(PostProcessParameter::FocalLength) * 0.001;
            const double aperture = parameter(PostProcessParameter::Aperture);
            const double strength =
                parameter(PostProcessParameter::DepthOfFieldStrength) * weight(PostProcessEffect::DepthOfField);
            Detail::PostProcessLensCoefficients coefficients;
            if (focus > focal && aperture > 0.0 && strength > 0.0)
            {
                Detail::PostProcessLens lens;
                lens.FocusDistanceMetres = focus;
                lens.FocalLengthMillimetres = focal * 1000.0;
                lens.ApertureFNumber = aperture;
                lens.Strength = strength;
                coefficients = Detail::MakePostProcessLensCoefficients(lens, surface.Height);
            }
            const float motion = temporalHistoryContinuous
                                     ? static_cast<float>(parameter(PostProcessParameter::MotionBlurStrength) *
                                                          weight(PostProcessEffect::MotionBlur))
                                     : 0.0F;
            const auto inverse = Math::Inverse(packet->Camera.Projection).Elements;
            std::size_t outputIndex = 0;
            const auto lensPass = [&](const float mode)
            {
                SDL_GPUColorTargetInfo target{};
                target.texture = surface.ActiveWorkset().LensColors[outputIndex++];
                target.load_op = SDL_GPU_LOADOP_DONT_CARE;
                target.store_op = SDL_GPU_STOREOP_STORE;
                auto* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
                if (!pass)
                {
                    throw std::runtime_error("Lens render pass failed: " + LastSdlError());
                }
                bindings[1].texture = surface.ActiveWorkset().LensDepth;
                const std::array<Vector4, 6> uniforms{
                    Vector4{1.0F / surface.Width, 1.0F / surface.Height, mode, remainingBloom},
                    Vector4{static_cast<float>(coefficients.ReciprocalDepthScale),
                            static_cast<float>(coefficients.Bias),
                            static_cast<float>(std::min(32.0, coefficients.MaximumRadiusPixels)), motion},
                    Vector4{inverse[0], inverse[1], inverse[2], inverse[3]},
                    Vector4{inverse[4], inverse[5], inverse[6], inverse[7]},
                    Vector4{inverse[8], inverse[9], inverse[10], inverse[11]},
                    Vector4{inverse[12], inverse[13], inverse[14], inverse[15]}};
                SDL_BindGPUGraphicsPipeline(pass, ToneMapPipeline);
                SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), static_cast<std::uint32_t>(bindings.size()));
                SDL_PushGPUFragmentUniformData(commands, 0, uniforms.data(), sizeof(uniforms));
                SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
                SDL_EndGPURenderPass(pass);
                ++Statistics.Passes;
                ++Statistics.DrawCalls;
                ++Statistics.Triangles;
                bindings[0].texture = target.texture;
                remainingBloom = 0.0F;
            };
            if (motion > 0.0F)
            {
                lensPass(6.0F);
            }
            if (coefficients.Bias > 0.0)
            {
                // The sharp and reduced inputs must contain identical HDR/bloom composition.
                // WriterColor is safe scratch here: final tone mapping has not written its result yet.
                if (remainingBloom > 0.0F)
                {
                    draw(15.0F, remainingBloom);
                    bindings[0].texture = surface.Resources.WriterColor(surface.ActiveWorksetSlot);
                    remainingBloom = 0.0F;
                }
                auto* const sharp = bindings[0].texture;
                const auto& workset = surface.ActiveWorkset();
                const auto dofPass =
                    [&](const float mode, SDL_GPUTexture* output, const std::array<SDL_GPUTexture*, 4>& inputs)
                {
                    SDL_GPUColorTargetInfo target{};
                    target.texture = output;
                    target.load_op = SDL_GPU_LOADOP_DONT_CARE;
                    target.store_op = SDL_GPU_STOREOP_STORE;
                    auto* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
                    if (!pass)
                    {
                        throw std::runtime_error("Depth-of-field render pass failed: " + LastSdlError());
                    }
                    std::array<SDL_GPUTextureSamplerBinding, 4> dofBindings{};
                    for (std::size_t slot = 0; slot < inputs.size(); ++slot)
                    {
                        dofBindings[slot] = {inputs[slot], ToneMapSampler};
                    }
                    const std::array<Vector4, 6> uniforms{
                        Vector4{1.0F / surface.Width, 1.0F / surface.Height, mode, 0.0F},
                        Vector4{static_cast<float>(coefficients.ReciprocalDepthScale),
                                static_cast<float>(coefficients.Bias),
                                static_cast<float>(std::min(32.0, coefficients.MaximumRadiusPixels)), 0.0F},
                        Vector4{inverse[0], inverse[1], inverse[2], inverse[3]},
                        Vector4{inverse[4], inverse[5], inverse[6], inverse[7]},
                        Vector4{inverse[8], inverse[9], inverse[10], inverse[11]},
                        Vector4{inverse[12], inverse[13], inverse[14], inverse[15]}};
                    SDL_BindGPUGraphicsPipeline(pass, ToneMapPipeline);
                    SDL_BindGPUFragmentSamplers(pass, 0, dofBindings.data(),
                                                static_cast<std::uint32_t>(dofBindings.size()));
                    SDL_PushGPUFragmentUniformData(commands, 0, uniforms.data(), sizeof(uniforms));
                    SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
                    SDL_EndGPURenderPass(pass);
                    ++Statistics.Passes;
                    ++Statistics.DrawCalls;
                    ++Statistics.Triangles;
                };
                const std::array sourceInputs{sharp, workset.LensDepth, sharp, sharp};
                dofPass(8.0F, workset.DofPyramid[0], sourceInputs);
                for (std::size_t level = 1; level < workset.DofPyramid.size(); ++level)
                {
                    auto* const previous = workset.DofPyramid[level - 1];
                    dofPass(9.0F, workset.DofPyramid[level], {previous, previous, previous, previous});
                }
                dofPass(10.0F, workset.DofBounds[0], sourceInputs);
                auto* const bounds = workset.DofBounds[0];
                dofPass(11.0F, workset.DofBounds[1], {bounds, bounds, bounds, bounds});
                const std::array gatherInputs{workset.DofPyramid[0], workset.DofPyramid[1], workset.DofPyramid[2],
                                              workset.DofBounds[1]};
                dofPass(12.0F, workset.DofLayers[0], gatherInputs);
                dofPass(13.0F, workset.DofLayers[1], gatherInputs);
                auto* const output = workset.LensColors[outputIndex++];
                dofPass(14.0F, output, {sharp, workset.LensDepth, workset.DofLayers[0], workset.DofLayers[1]});
                bindings[0].texture = output;
            }
        }
        // Bloom is composed exactly once, even when both HDR lens passes are active.
        draw(antiAliasing == RenderAntiAliasingMode::Fxaa ? 1.0F : 0.0F, remainingBloom);
    }
} // namespace Keire::RenderBackend
