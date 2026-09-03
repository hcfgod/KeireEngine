#include "KeireInternal/Rendering/DisplacementBoundsInternal.h"
#include "KeireInternal/Rendering/GlobalIlluminationPolicyInternal.h"
#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Keire::RenderBackend
{
    namespace
    {
        [[nodiscard]] Vector3 PropertyRadiance(const MaterialPropertyValue& value) noexcept
        {
            if (const auto* color = std::get_if<Color>(&value))
                return {color->Red, color->Green, color->Blue};
            if (const auto* vector = std::get_if<Vector3>(&value))
                return *vector;
            if (const auto* vector = std::get_if<Vector4>(&value))
                return {vector->X, vector->Y, vector->Z};
            if (const auto* scalar = std::get_if<float>(&value))
                return {*scalar, *scalar, *scalar};
            return {};
        }

        [[nodiscard]] Vector3
        FindEmission(const std::map<std::string, MaterialPropertyValue, std::less<>>& properties) noexcept
        {
            constexpr std::array names{"Emission", "Emissive", "EmissiveColor", "EmissionColor"};
            for (const auto name : names)
            {
                if (const auto found = properties.find(name); found != properties.end())
                    return PropertyRadiance(found->second);
            }
            return {};
        }

        [[nodiscard]] float MaximumComponent(const Vector3 value) noexcept
        {
            return std::max({value.X, value.Y, value.Z});
        }

        [[nodiscard]] std::uint64_t SurfaceCardKey(const SceneDrawItem& item) noexcept
        {
            auto result = HashDependencyStamp(0x4952524144594e31ULL, item.Scene);
            result = HashDependencyStamp(result, item.Entity.Value());
            return HashDependencyStamp(result, item.Mesh);
        }

        [[nodiscard]] IrradynSceneCard SurfaceCard(const MeshBounds& bounds, const Color tint, const Vector3 radiance,
                                                   const float density,
                                                   const IrradynParticipation participation) noexcept
        {
            const Vector3 center{(bounds.Minimum.X + bounds.Maximum.X) * 0.5F,
                                 (bounds.Minimum.Y + bounds.Maximum.Y) * 0.5F,
                                 (bounds.Minimum.Z + bounds.Maximum.Z) * 0.5F};
            const Vector3 extents{std::max((bounds.Maximum.X - bounds.Minimum.X) * 0.5F, 0.025F),
                                  std::max((bounds.Maximum.Y - bounds.Minimum.Y) * 0.5F, 0.025F),
                                  std::max((bounds.Maximum.Z - bounds.Minimum.Z) * 0.5F, 0.025F)};
            const auto radius = std::sqrt(extents.X * extents.X + extents.Y * extents.Y + extents.Z * extents.Z);
            return {{center.X, center.Y, center.Z, radius},
                    {std::max(radiance.X * tint.Red, 0.0F), std::max(radiance.Y * tint.Green, 0.0F),
                     std::max(radiance.Z * tint.Blue, 0.0F), std::clamp(density, 0.0F, 4.0F)},
                    {extents.X, extents.Y, extents.Z, static_cast<float>(static_cast<std::uint32_t>(participation))}};
        }

        struct VfxAggregate final
        {
            Vector3 Minimum{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max()};
            Vector3 Maximum{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                            -std::numeric_limits<float>::max()};
            Vector3 Radiance;
            std::uint32_t Count = 0;

            void Add(const Vector3 position, const float radius, const Color color) noexcept
            {
                const auto extent = std::max(radius, 0.025F);
                Minimum = {std::min(Minimum.X, position.X - extent), std::min(Minimum.Y, position.Y - extent),
                           std::min(Minimum.Z, position.Z - extent)};
                Maximum = {std::max(Maximum.X, position.X + extent), std::max(Maximum.Y, position.Y + extent),
                           std::max(Maximum.Z, position.Z + extent)};
                Radiance = {Radiance.X + std::max(color.Red, 0.0F), Radiance.Y + std::max(color.Green, 0.0F),
                            Radiance.Z + std::max(color.Blue, 0.0F)};
                ++Count;
            }
        };

        [[nodiscard]] std::vector<IrradynSceneCardCandidate> BuildVfxCandidates(const SceneRenderPacket& packet)
        {
            std::vector<IrradynSceneCardCandidate> result;
            for (const auto& snapshot : packet.VfxSnapshots)
            {
                std::array<VfxAggregate, 4> aggregates;
                for (const auto& particle : snapshot.Particles())
                {
                    const auto category = std::min(static_cast<std::size_t>(particle.Renderer), aggregates.size() - 1U);
                    aggregates[category].Add(particle.Position, particle.Size * 0.5F, particle.Tint);
                }
                for (std::size_t category = 0; category < aggregates.size(); ++category)
                {
                    const auto& aggregate = aggregates[category];
                    if (aggregate.Count == 0U)
                        continue;
                    const auto inverseCount = 1.0F / static_cast<float>(aggregate.Count);
                    const Vector3 average{aggregate.Radiance.X * inverseCount, aggregate.Radiance.Y * inverseCount,
                                          aggregate.Radiance.Z * inverseCount};
                    const auto renderer = static_cast<VfxRendererType>(category);
                    auto participation = IrradynParticipation::Vfx;
                    if (renderer == VfxRendererType::Volumetric)
                        participation = participation | IrradynParticipation::Volume;
                    else if (renderer == VfxRendererType::Ribbon)
                        participation = participation | IrradynParticipation::Hair;
                    auto key = HashDependencyStamp(0x4952524144564658ULL, snapshot.WorldId());
                    key = HashDependencyStamp(key, static_cast<std::uint64_t>(category));
                    result.push_back(
                        {key, SurfaceCard({aggregate.Minimum, aggregate.Maximum}, Color{}, average,
                                          std::min(1.0F, static_cast<float>(aggregate.Count) / 64.0F), participation)});
                }

                for (const auto& emitter : snapshot.GpuEmitters())
                {
                    const auto radius = std::max({std::abs(emitter.ShapeExtent.X), std::abs(emitter.ShapeExtent.Y),
                                                  std::abs(emitter.ShapeExtent.Z), emitter.ShapeRadius, 0.05F});
                    const MeshBounds bounds{
                        {emitter.Position.X - radius, emitter.Position.Y - radius, emitter.Position.Z - radius},
                        {emitter.Position.X + radius, emitter.Position.Y + radius, emitter.Position.Z + radius}};
                    const Vector3 average{(emitter.ColorStart.Red + emitter.ColorEnd.Red) * 0.5F,
                                          (emitter.ColorStart.Green + emitter.ColorEnd.Green) * 0.5F,
                                          (emitter.ColorStart.Blue + emitter.ColorEnd.Blue) * 0.5F};
                    auto participation = IrradynParticipation::Vfx;
                    if (emitter.Renderer == VfxRendererType::Volumetric)
                        participation = participation | IrradynParticipation::Volume;
                    else if (emitter.Renderer == VfxRendererType::Ribbon)
                        participation = participation | IrradynParticipation::Hair;
                    auto key = HashDependencyStamp(0x4952524144475055ULL, snapshot.WorldId());
                    key = HashDependencyStamp(key, emitter.Handle.Index());
                    key = HashDependencyStamp(key, emitter.Handle.Generation());
                    result.push_back({key, SurfaceCard(bounds, Color{}, average,
                                                       std::min(1.0F, static_cast<float>(emitter.Capacity) / 256.0F),
                                                       participation)});
                }
            }
            return result;
        }
    } // namespace

    void RenderSharedState::RecordIrradynTrace(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                               const SceneRenderPacket& packet, const bool temporalHistoryContinuous)
    {
        auto& workset = surface.ActiveWorkset();
        const auto policy = ResolveGlobalIlluminationPolicy(surface.ActiveFeatureSelection.EffectiveGlobalIllumination,
                                                            packet.Environment.RequestedIrradynQuality);
        if (!IrradynTracePipeline || !DeferredSampler || !workset.IrradynRadiance || !workset.HdrColor ||
            !workset.Depth || !workset.GBufferBaseColorMetallic || !workset.GBufferNormalRoughness ||
            !workset.GBufferMaterial || !workset.GBufferVelocity || !surface.Resources.PublishedIrradynHistory() ||
            !surface.Resources.WriterIrradynHistory(surface.ActiveWorksetSlot) || !BlackDataTexture.Texture)
        {
            throw std::logic_error("Irradyn trace resources are unavailable for an active render surface.");
        }

        const auto beginTracePass = [&]
        {
            SDL_GPUColorTargetInfo target{};
            target.texture = workset.IrradynRadiance;
            target.clear_color = {0.0F, 0.0F, 0.0F, 0.0F};
            target.load_op = SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;
            auto* result = SDL_BeginGPURenderPass(commands, &target, 1U, nullptr);
            if (!result)
                throw std::runtime_error("SDL_BeginGPURenderPass(Irradyn trace) failed: " + LastSdlError());
            return result;
        };
        SDL_GPURenderPass* pass = nullptr;

        if (policy.IrradynStrength > 0.0F)
        {
            std::vector<IrradynSceneCardCandidate> candidates;
            candidates.reserve(packet.DrawItems.size() + packet.VfxSnapshots.size() * 4U);
            const auto samples = ToSdlSampleCount(surface.ActualSamples);
            const Vector3 ambient{packet.Environment.AmbientColor.Red * packet.Environment.AmbientIntensity,
                                  packet.Environment.AmbientColor.Green * packet.Environment.AmbientIntensity,
                                  packet.Environment.AmbientColor.Blue * packet.Environment.AmbientIntensity};
            const Vector3 directional{packet.Lighting.ColorAndIntensity.Red * packet.Lighting.ColorAndIntensity.Alpha,
                                      packet.Lighting.ColorAndIntensity.Green * packet.Lighting.ColorAndIntensity.Alpha,
                                      packet.Lighting.ColorAndIntensity.Blue * packet.Lighting.ColorAndIntensity.Alpha};
            const Vector3 reflected{ambient.X + directional.X * 0.15F, ambient.Y + directional.Y * 0.15F,
                                    ambient.Z + directional.Z * 0.15F};
            for (const auto& item : packet.DrawItems)
            {
                const auto& mesh = ResolveMesh(item.Mesh);
                if (mesh.Empty() || !mesh.BoundsEncloseSubmeshes)
                    continue;
                const AssetId materialId = !item.Materials.empty()          ? item.Materials.front()
                                           : !mesh.DefaultMaterials.empty() ? mesh.DefaultMaterials.front()
                                                                            : AssetId{};
                const auto* binding = materialId ? ResolveAssetMaterial(materialId, samples) : nullptr;
                const auto material = materialId ? ResolveMaterial(materialId) : Ref<const MaterialAsset>{};
                const auto displacement = binding      ? binding->MaximumWorldPositionDisplacementRadius
                                          : materialId ? std::optional<float>{}
                                                       : std::optional<float>{0.0F};
                auto bounds = DisplacementBounds::WorldBounds(
                    mesh.Bounds, item.World,
                    DisplacementBounds::IsKnown(displacement) ? displacement : std::optional<float>{0.0F});
                if (!bounds)
                    continue;
                if (!DisplacementBounds::IsKnown(displacement))
                {
                    const Vector3 conservativePadding{
                        std::max((bounds->Maximum.X - bounds->Minimum.X) * 0.125F, 0.05F),
                        std::max((bounds->Maximum.Y - bounds->Minimum.Y) * 0.125F, 0.05F),
                        std::max((bounds->Maximum.Z - bounds->Minimum.Z) * 0.125F, 0.05F)};
                    bounds->Minimum = {bounds->Minimum.X - conservativePadding.X,
                                       bounds->Minimum.Y - conservativePadding.Y,
                                       bounds->Minimum.Z - conservativePadding.Z};
                    bounds->Maximum = {bounds->Maximum.X + conservativePadding.X,
                                       bounds->Maximum.Y + conservativePadding.Y,
                                       bounds->Maximum.Z + conservativePadding.Z};
                }
                auto participation = IrradynParticipation::Surface;
                if (binding && IsTransparentMaterial(binding->Surface.AlphaMode))
                    participation = participation | IrradynParticipation::Translucency;
                if (binding && !binding->HasDeferredGBufferPipeline())
                    participation = participation | IrradynParticipation::Hair;
                if (binding && (!displacement || *displacement > 0.0F))
                    participation = participation | IrradynParticipation::WorldPositionOffset;

                Vector3 radiance = reflected;
                if (material && material->Definition().ContributeEmissionToGI)
                {
                    const auto materialEmission = FindEmission(material->Definition().Properties);
                    const auto instanceEmission = FindEmission(item.MaterialProperties);
                    const auto emission =
                        MaximumComponent(instanceEmission) > 0.0F ? instanceEmission : materialEmission;
                    radiance = {radiance.X + emission.X * material->Definition().EmissiveGIIntensity,
                                radiance.Y + emission.Y * material->Definition().EmissiveGIIntensity,
                                radiance.Z + emission.Z * material->Definition().EmissiveGIIntensity};
                }
                const float density = HasIrradynParticipation(participation, IrradynParticipation::Translucency) ? 0.35F
                                      : HasIrradynParticipation(participation, IrradynParticipation::Hair)       ? 0.55F
                                                                                                                 : 1.0F;
                candidates.push_back(
                    {SurfaceCardKey(item), SurfaceCard(*bounds, item.Tint, radiance, density, participation)});
            }
            auto vfxCandidates = BuildVfxCandidates(packet);
            candidates.insert(candidates.end(), vfxCandidates.begin(), vfxCandidates.end());
            (void)surface.IrradynCache.Update(candidates, policy.IrradynSceneCardUpdateBudget);

            const auto cameraPosition = Math::TransformPoint(Math::Inverse(packet.Camera.View), {});
            const auto selectedCards = surface.IrradynCache.Select(cameraPosition, policy.IrradynSceneCardCount);
            IrradynSceneCacheUniforms cacheUniforms{};
            std::ranges::copy(selectedCards, cacheUniforms.Cards.begin());
            const auto viewProjection = Math::Multiply(packet.Camera.Projection, packet.Camera.View);
            const bool historyValid = temporalHistoryContinuous && surface.IrradynHistoryValid;
            const IrradynUniforms uniforms{
                Math::Inverse(viewProjection),
                packet.Camera.View,
                {1.0F, policy.IrradynStrength, static_cast<float>(policy.IrradynSampleCount),
                 static_cast<float>(policy.IrradynRayStepCount)},
                {policy.IrradynRadiusPixels, policy.IrradynMaximumDistance, policy.IrradynHistoryWeight,
                 historyValid ? 1.0F : 0.0F},
                {static_cast<float>(packet.FrameIndex), static_cast<float>(selectedCards.size()), 0.04F, 0.0F},
                {static_cast<float>(policy.IrradynResolutionDivisor), 0.35F, 0.0F, 0.0F}};
            const std::array bindings{
                SDL_GPUTextureSamplerBinding{workset.HdrColor, DeferredSampler},
                SDL_GPUTextureSamplerBinding{workset.Depth, DeferredSampler},
                SDL_GPUTextureSamplerBinding{workset.GBufferNormalRoughness, DeferredSampler},
                SDL_GPUTextureSamplerBinding{workset.GBufferMaterial, DeferredSampler},
                SDL_GPUTextureSamplerBinding{workset.GBufferVelocity, DeferredSampler},
                SDL_GPUTextureSamplerBinding{surface.Resources.PublishedIrradynHistory(), DeferredSampler},
                SDL_GPUTextureSamplerBinding{workset.GBufferBaseColorMetallic, DeferredSampler}};
            pass = beginTracePass();
            SDL_BindGPUGraphicsPipeline(pass, IrradynTracePipeline);
            SDL_BindGPUFragmentSamplers(pass, 0U, bindings.data(), static_cast<std::uint32_t>(bindings.size()));
            SDL_PushGPUFragmentUniformData(commands, 0U, &uniforms, sizeof(uniforms));
            SDL_PushGPUFragmentUniformData(commands, 1U, &cacheUniforms, sizeof(cacheUniforms));
            SDL_DrawGPUPrimitives(pass, 3U, 1U, 0U, 0U);
            ++Statistics.DrawCalls;
        }
        if (!pass)
            pass = beginTracePass();
        SDL_EndGPURenderPass(pass);
        ++Statistics.Passes;

        if (policy.IrradynStrength <= 0.0F)
        {
            surface.IrradynRecordedThisFrame = false;
            return;
        }
        auto* copy = SDL_BeginGPUCopyPass(commands);
        if (!copy)
            throw std::runtime_error("SDL_BeginGPUCopyPass(Irradyn history) failed: " + LastSdlError());
        const SDL_GPUTextureLocation source{workset.IrradynRadiance, 0U, 0U, 0U, 0U, 0U};
        const SDL_GPUTextureLocation destination{
            surface.Resources.WriterIrradynHistory(surface.ActiveWorksetSlot), 0U, 0U, 0U, 0U, 0U};
        const auto width = surface.Width / 2U + surface.Width % 2U;
        const auto height = surface.Height / 2U + surface.Height % 2U;
        SDL_CopyGPUTextureToTexture(copy, &source, &destination, width, height, 1U, false);
        SDL_EndGPUCopyPass(copy);
        ++Statistics.Passes;
        surface.IrradynRecordedThisFrame = true;
    }

    void RenderSharedState::RecordIrradynComposite(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                   const SceneRenderPacket& packet)
    {
        auto& workset = surface.ActiveWorkset();
        const auto policy = ResolveGlobalIlluminationPolicy(surface.ActiveFeatureSelection.EffectiveGlobalIllumination,
                                                            packet.Environment.RequestedIrradynQuality);
        if (policy.IrradynStrength <= 0.0F)
            return;
        if (!IrradynCompositePipeline || !DeferredSampler || !workset.IrradynRadiance || !workset.HdrColor ||
            !workset.Depth || !workset.GBufferBaseColorMetallic || !workset.GBufferNormalRoughness ||
            !workset.GBufferMaterial || !workset.GBufferVelocity || !BlackDataTexture.Texture)
        {
            throw std::logic_error("Irradyn composite resources are unavailable for an active render surface.");
        }

        SDL_GPUColorTargetInfo target{};
        target.texture = workset.HdrColor;
        target.load_op = SDL_GPU_LOADOP_LOAD;
        target.store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = SDL_BeginGPURenderPass(commands, &target, 1U, nullptr);
        if (!pass)
            throw std::runtime_error("SDL_BeginGPURenderPass(Irradyn composite) failed: " + LastSdlError());
        const auto viewProjection = Math::Multiply(packet.Camera.Projection, packet.Camera.View);
        const IrradynUniforms uniforms{
            Math::Inverse(viewProjection),
            packet.Camera.View,
            {2.0F, policy.IrradynStrength, static_cast<float>(policy.IrradynSampleCount),
             static_cast<float>(policy.IrradynRayStepCount)},
            {policy.IrradynRadiusPixels, policy.IrradynMaximumDistance, policy.IrradynHistoryWeight, 0.0F},
            {static_cast<float>(packet.FrameIndex), 0.0F, 0.04F, 0.0F},
            {static_cast<float>(policy.IrradynResolutionDivisor), 0.35F, 0.0F, 0.0F}};
        IrradynSceneCacheUniforms cacheUniforms{};
        const std::array bindings{SDL_GPUTextureSamplerBinding{BlackDataTexture.Texture, DeferredSampler},
                                  SDL_GPUTextureSamplerBinding{workset.Depth, DeferredSampler},
                                  SDL_GPUTextureSamplerBinding{workset.GBufferNormalRoughness, DeferredSampler},
                                  SDL_GPUTextureSamplerBinding{workset.GBufferMaterial, DeferredSampler},
                                  SDL_GPUTextureSamplerBinding{workset.GBufferVelocity, DeferredSampler},
                                  SDL_GPUTextureSamplerBinding{workset.IrradynRadiance, DeferredSampler},
                                  SDL_GPUTextureSamplerBinding{workset.GBufferBaseColorMetallic, DeferredSampler}};
        SDL_BindGPUGraphicsPipeline(pass, IrradynCompositePipeline);
        SDL_BindGPUFragmentSamplers(pass, 0U, bindings.data(), static_cast<std::uint32_t>(bindings.size()));
        SDL_PushGPUFragmentUniformData(commands, 0U, &uniforms, sizeof(uniforms));
        SDL_PushGPUFragmentUniformData(commands, 1U, &cacheUniforms, sizeof(cacheUniforms));
        SDL_DrawGPUPrimitives(pass, 3U, 1U, 0U, 0U);
        SDL_EndGPURenderPass(pass);
        ++Statistics.DrawCalls;
        ++Statistics.Passes;
    }
} // namespace Keire::RenderBackend
