#include "KeireInternal/Rendering/RenderBackendInternal.h"
#include "KeireInternal/Rendering/RuntimeUiMaterialInternal.h"

#include "Keire/Log.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace Keire::RenderBackend
{
    bool RenderSharedState::BindRuntimeUiMaterial(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                                                  const AssetId materialId, SDL_GPUGraphicsPipeline* fallback,
                                                  const SDL_GPUTextureSamplerBinding source, const bool worldSurface,
                                                  const bool depthTest, const SDL_GPUSampleCount samples,
                                                  const SDL_GPUTextureFormat format, const bool fullscreen,
                                                  const Vector2 time)
    {
        const auto bindFallback = [&]
        {
            if (!fallback || fullscreen)
                return false;
            SDL_BindGPUGraphicsPipeline(pass, fallback);
            SDL_BindGPUFragmentSamplers(pass, 0, &source, 1);
            return true;
        };
        if (!materialId)
            return bindFallback();
        try
        {
            const auto material = ResolveMaterial(materialId);
            if (!material || !material->Definition().Shader || !Assets)
                return bindFallback();
            const auto shaderId = material->Definition().Shader;
            auto& shaderCache = ScreenShaderCaches[fullscreen ? 1 : 0];
            if (!shaderCache.contains(shaderId) && shaderCache.size() >= 256U)
                throw std::length_error("Runtime UI shader cache exceeds 256 programs.");
            auto [iterator, inserted] = shaderCache.try_emplace(shaderId);
            auto& entry = iterator->second;
            if (inserted)
                entry.Asset = Assets->Load<ShaderAsset>(shaderId, AssetPriority::High);
            const auto revision = entry.Asset.Revision();
            if (revision != 0U && revision > entry.LastAttemptedRevision)
            {
                entry.LastAttemptedRevision = revision;
                if (const auto shader = entry.Asset.TryGetLoaded())
                {
                    try
                    {
                        if (shader->Definition().ProgramTarget != (fullscreen ? "Fullscreen" : "UI"))
                            throw std::invalid_argument(
                                "Material shader target does not match this screen-space consumer.");
                        Detail::ValidateRuntimeUiShader(shader->Definition());
                        auto replacements = entry.Pipelines;
                        if (replacements.empty())
                            replacements.push_back({worldSurface, depthTest, samples, format, nullptr});
                        for (auto& candidate : replacements)
                            candidate.Handle = nullptr;
                        try
                        {
                            for (auto& candidate : replacements)
                                candidate.Handle =
                                    CreateRuntimeUiPipeline(candidate.WorldSurface, candidate.DepthTest,
                                                            candidate.Samples, candidate.Format, &shader->Definition());
                        }
                        catch (...)
                        {
                            for (const auto& candidate : replacements)
                                if (candidate.Handle)
                                    SDL_ReleaseGPUGraphicsPipeline(Device, candidate.Handle);
                            throw;
                        }
                        for (const auto& old : entry.Pipelines)
                            Retire(old.Handle);
                        entry.Pipelines = std::move(replacements);
                        entry.LastGood = shader;
                    }
                    catch (const std::exception& error)
                    {
                        ThrowIfDeviceLost("runtime UI shader reload", error.what());
                        KEIRE_CORE_ERROR("Runtime UI shader reload failed for {}: {}", shaderId.ToString(),
                                         error.what());
                    }
                }
            }
            if (!entry.LastGood || entry.LastGood->Definition().ProgramTarget != (fullscreen ? "Fullscreen" : "UI"))
                return bindFallback();
            const auto values =
                Detail::BuildRuntimeUiMaterialValues(entry.LastGood->Definition(), material->Definition());
            auto pipeline = std::ranges::find_if(entry.Pipelines,
                                                 [&](const auto& candidate)
                                                 {
                                                     return candidate.WorldSurface == worldSurface &&
                                                            candidate.DepthTest == depthTest &&
                                                            candidate.Samples == samples && candidate.Format == format;
                                                 });
            if (pipeline == entry.Pipelines.end())
            {
                entry.Pipelines.push_back({worldSurface, depthTest, samples, format, nullptr});
                try
                {
                    entry.Pipelines.back().Handle = CreateRuntimeUiPipeline(worldSurface, depthTest, samples, format,
                                                                            &entry.LastGood->Definition());
                }
                catch (...)
                {
                    entry.Pipelines.pop_back();
                    throw;
                }
                pipeline = std::prev(entry.Pipelines.end());
            }
            std::array<SDL_GPUTextureSamplerBinding, 16> textures{};
            std::size_t textureIndex = 0;
            for (const auto asset : values.Textures)
            {
                const auto& texture = asset ? ResolveTexture(asset) : WhiteTexture;
                textures[textureIndex++] = {texture.Texture, texture.Sampler};
            }
            textures[textureIndex++] = source;
            AssetSceneUniforms scene{};
            scene.DirectionalDirectionExposure.W = 1.0F;
            scene.SurfaceParameters = {material->Definition().Surface.AlphaCutoff,
                                       static_cast<float>(material->Definition().Surface.AlphaMode), 0.0F, 0.0F};
            scene.FrameParameters.X = time.X;
            scene.FrameParameters.Y = time.Y;
            scene.FrameParameters.Z = static_cast<float>(Statistics.Frame);
            SDL_BindGPUGraphicsPipeline(pass, pipeline->Handle);
            SDL_PushGPUFragmentUniformData(commands, 0, &scene, sizeof(scene));
            const auto bytes = static_cast<std::uint32_t>(values.Numeric.size() * sizeof(Vector4));
            SDL_PushGPUFragmentUniformData(commands, 1, values.Numeric.data(), bytes);
            if (entry.LastGood->Definition().UsesVertexMaterialParameters)
                SDL_PushGPUVertexUniformData(commands, 1, values.Numeric.data(), bytes);
            SDL_BindGPUFragmentSamplers(pass, 0, textures.data(), static_cast<std::uint32_t>(textureIndex));
            return true;
        }
        catch (const std::exception& error)
        {
            ThrowIfDeviceLost("runtime UI material binding", error.what());
            KEIRE_CORE_ERROR("Runtime UI material {} unavailable: {}", materialId.ToString(), error.what());
            return bindFallback();
        }
    }

    void RenderSharedState::ReleaseRuntimeUiMaterialPipelines(const bool abandon) noexcept
    {
        for (auto& shaderCache : ScreenShaderCaches)
        {
            if (!abandon)
                for (const auto& [id, entry] : shaderCache)
                {
                    (void)id;
                    for (const auto& pipeline : entry.Pipelines)
                        SDL_ReleaseGPUGraphicsPipeline(Device, pipeline.Handle);
                }
            shaderCache.clear();
        }
    }
} // namespace Keire::RenderBackend
