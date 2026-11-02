#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include "Keire/ECS/Components/PostProcessVolumeComponent.h"
#include "KeireInternal/Rendering/PostProcessEnvironment.h"
#include "KeireInternal/Rendering/PostProcessLookup.h"
#include "KeireInternal/Rendering/PostProcessProfileCache.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace Keire::RenderBackend
{
    void RenderSharedState::RecordPostProcessing(SDL_GPUCommandBuffer* commands, RenderSurfaceState& surface,
                                                 const SceneRenderPacket& packet)
    {
        Detail::PrunePostProcessCache(PostProcessLookupTextures, packet.AcceptedFrameId,
                                      [this](auto& entry) { Retire(std::move(entry.Resources)); });
        const auto& settings = packet.PostProcessing;
        const auto parameter = [&](const PostProcessParameter key)
        { return static_cast<float>(settings.Parameters[static_cast<std::size_t>(key)]); };
        const auto weight = [&](const PostProcessEffect effect)
        { return static_cast<float>(settings.EffectWeights[static_cast<std::size_t>(effect)]); };
        const float gradeWeight = weight(PostProcessEffect::ColorGrading);
        const float vignette = parameter(PostProcessParameter::VignetteIntensity) * weight(PostProcessEffect::Vignette);
        const float grain = parameter(PostProcessParameter::GrainIntensity) * weight(PostProcessEffect::FilmGrain);
        const float chromatic =
            parameter(PostProcessParameter::ChromaticAberration) * weight(PostProcessEffect::ChromaticAberration);
        if (gradeWeight == 0.0F && vignette == 0.0F && grain == 0.0F && chromatic == 0.0F)
        {
            return;
        }
        // Packet-owned immutable revisions avoid asset loads or revision races on the render thread.
        SDL_GPUTextureSamplerBinding lookup{};
        if (packet.PostProcessLookup && settings.ColorLookupWeight > 0.0 && gradeWeight > 0.0F)
        {
            constexpr std::size_t maximumLookupTextures = 32;
            if (!PostProcessLookupTextures.contains(settings.ColorLookup) &&
                PostProcessLookupTextures.size() >= maximumLookupTextures)
            {
                const auto oldest = std::ranges::min_element(PostProcessLookupTextures, {}, [](const auto& pair)
                                                             { return pair.second.LastUsedFrame; });
                Retire(std::move(oldest->second.Resources));
                PostProcessLookupTextures.erase(oldest);
            }
            auto& entry = PostProcessLookupTextures[settings.ColorLookup];
            entry.LastUsedFrame = packet.AcceptedFrameId;
            const bool sourceReady = entry.Source == packet.PostProcessLookup && !entry.Resources.Empty();
            if (entry.UploadRetry.ShouldAttempt(packet.AcceptedFrameId,
                                                entry.AttemptedSource != packet.PostProcessLookup, sourceReady))
            {
                entry.UploadRetry.RecordAttempt(packet.AcceptedFrameId);
                entry.AttemptedSource = packet.PostProcessLookup;
                try
                {
                    auto replacement = CreateTextureResources(*packet.PostProcessLookup);
                    Retire(std::exchange(entry.Resources, replacement));
                    entry.Source = packet.PostProcessLookup;
                }
                catch (const std::exception& error)
                {
                    ThrowIfDeviceLost("post-process lookup upload", error.what());
                    // A failed or incompatible replacement must never sample an unrelated last-good LUT.
                }
            }
            if (entry.Source == packet.PostProcessLookup && !entry.Resources.Empty())
            {
                lookup = {entry.Resources.Texture, entry.Resources.Sampler};
            }
        }
        auto* target = surface.Resources.WriterColor(surface.ActiveWorksetSlot);
        auto* input = EnsureFullscreenInput(surface, false);
        auto* copy = SDL_BeginGPUCopyPass(commands);
        if (!copy)
        {
            throw std::runtime_error("Post-processing scene copy failed: " + LastSdlError());
        }
        const SDL_GPUTextureLocation source{target, 0, 0, 0, 0, 0};
        const SDL_GPUTextureLocation destination{input, 0, 0, 0, 0, 0};
        SDL_CopyGPUTextureToTexture(copy, &source, &destination, surface.Width, surface.Height, 1, false);
        SDL_EndGPUCopyPass(copy);
        SDL_GPUColorTargetInfo color{};
        color.texture = target;
        color.load_op = SDL_GPU_LOADOP_DONT_CARE;
        color.store_op = SDL_GPU_STOREOP_STORE;
        auto* pass = SDL_BeginGPURenderPass(commands, &color, 1, nullptr);
        if (!pass)
        {
            throw std::runtime_error("Post-processing pass failed: " + LastSdlError());
        }
        const SDL_GPUTextureSamplerBinding binding{input, ToneMapSampler};
        const std::array bindings{binding, lookup.texture ? lookup : binding, binding, binding};
        const std::array<Vector4, 6> uniforms{
            Vector4{1.0F / static_cast<float>(std::max(surface.Width, 1U)),
                    1.0F / static_cast<float>(std::max(surface.Height, 1U)), 4.0F, 0.0F},
            Detail::PostProcessLookupParameters(packet.PostProcessLookupEdge, settings.ColorLookupWeight, gradeWeight,
                                                lookup.texture != nullptr),
            Vector4{parameter(PostProcessParameter::Temperature) * 0.01F, parameter(PostProcessParameter::Tint) * 0.01F,
                    parameter(PostProcessParameter::Contrast), parameter(PostProcessParameter::Saturation)},
            Vector4{parameter(PostProcessParameter::RedGain), parameter(PostProcessParameter::GreenGain),
                    parameter(PostProcessParameter::BlueGain), gradeWeight},
            Vector4{vignette, parameter(PostProcessParameter::VignetteSmoothness),
                    parameter(PostProcessParameter::VignetteRoundness), chromatic},
            Vector4{grain, parameter(PostProcessParameter::GrainResponse),
                    static_cast<float>(packet.AcceptedFrameId & 65535U), 0.0F}};
        SDL_BindGPUGraphicsPipeline(pass, ToneMapPipeline);
        SDL_BindGPUFragmentSamplers(pass, 0, bindings.data(), static_cast<std::uint32_t>(bindings.size()));
        SDL_PushGPUFragmentUniformData(commands, 0, uniforms.data(), sizeof(uniforms));
        SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(pass);
        Statistics.Passes += 2;
        ++Statistics.DrawCalls;
        ++Statistics.Triangles;
    }

    void RenderSharedState::CapturePostProcessing(const SceneRenderRequest& request, SceneRenderPacket& packet)
    {
        PostProcessContributions.clear();
        PostProcessProfileLeases.clear();
        Detail::PrunePostProcessCache(PostProcessProfileCache, packet.AcceptedFrameId, [](auto&) {});
        Detail::PrunePostProcessCache(PostProcessLookups, packet.AcceptedFrameId, [](auto&) {});
        packet.PostProcessing = Detail::MakePostProcessBaseline(packet.Environment);
        packet.PostProcessLookup.Reset();
        packet.PostProcessLookupEdge = 0;
        if (!Assets || !request.DrawSceneContributions || !packet.Camera.PostProcessingEnabled ||
            packet.Camera.PostProcessVolumeMask == 0U)
        {
            return;
        }
        const auto cameraLocal = Math::TransformPoint(Math::Inverse(packet.Camera.View), {});
        const Vector3d cameraPosition{packet.Camera.WorldOrigin.X + cameraLocal.X,
                                      packet.Camera.WorldOrigin.Y + cameraLocal.Y,
                                      packet.Camera.WorldOrigin.Z + cameraLocal.Z};
        const auto collect = [&](const Ref<Scene>& scene)
        {
            if (!scene)
            {
                return;
            }
            for (const auto& entity : scene->Query<PostProcessVolumeComponent>())
            {
                if (!entity.ActiveInHierarchy() || (packet.Camera.PostProcessVolumeMask & (1U << entity.Layer())) == 0U)
                {
                    continue;
                }
                const auto volume = entity.GetComponent<PostProcessVolumeComponent>();
                const auto transform = entity.GetComponent<TransformComponent>();
                if (!volume || !volume->Enabled() || !transform || !volume->Settings().Profile ||
                    volume->Settings().Weight == 0.0)
                {
                    continue;
                }
                Detail::PostProcessVolumePose pose;
                pose.Center = transform->PresentationWorldPositionPrecise();
                if (!volume->Settings().Global)
                {
                    Vector3 ignoredTranslation;
                    if (!Math::DecomposeTransform(
                            transform->PresentationRelativeMatrix(pose.Center, std::numeric_limits<double>::max()),
                            ignoredTranslation, pose.Rotation, pose.Scale) ||
                        pose.Scale.X == 0.0F || pose.Scale.Y == 0.0F || pose.Scale.Z == 0.0F)
                    {
                        continue;
                    }
                }
                const double weight = Detail::PostProcessVolumeWeight(volume->Settings(), pose, cameraPosition);
                if (weight == 0.0)
                {
                    continue;
                }
                auto* entry =
                    Detail::TryGetOrLoadPostProcessAsset(PostProcessProfileCache, volume->Settings().Profile,
                                                         [&]
                                                         {
                                                             PostProcessProfileCacheEntry candidate;
                                                             candidate.Handle = Assets->Load<PostProcessProfileAsset>(
                                                                 volume->Settings().Profile, AssetPriority::High);
                                                             return candidate;
                                                         });
                if (!entry)
                {
                    continue;
                }
                entry->LastUsedFrame = packet.AcceptedFrameId;
                if (const auto loaded = entry->Handle.TryGetLoaded())
                {
                    entry->LastGood = loaded;
                }
                if (!entry->LastGood)
                {
                    continue;
                }
                PostProcessProfileLeases.push_back(entry->LastGood);
                PostProcessContributions.push_back({entity.Id().Value(), volume->Settings().Priority, weight,
                                                    &entry->LastGood->Definition(), scene->Asset()});
            }
        };
        collect(request.Scene);
        for (const auto& contribution : request.AdditionalScenes)
        {
            collect(contribution.Scene);
        }
        packet.PostProcessing = Detail::BlendPostProcessProfiles(PostProcessContributions, packet.PostProcessing);
        const auto lookup = packet.PostProcessing.ColorLookup;
        if (lookup && packet.PostProcessing.ColorLookupWeight > 0.0 &&
            packet.PostProcessing.EffectWeights[static_cast<std::size_t>(PostProcessEffect::ColorGrading)] > 0.0)
        {
            constexpr std::size_t maximumLookupHandles = 32;
            if (!PostProcessLookups.contains(lookup) && PostProcessLookups.size() >= maximumLookupHandles)
            {
                const auto oldest = std::ranges::min_element(PostProcessLookups, {}, [](const auto& pair)
                                                             { return pair.second.LastUsedFrame; });
                PostProcessLookups.erase(oldest);
            }
            auto* entry = Detail::TryGetOrLoadPostProcessAsset(PostProcessLookups, lookup,
                                                               [&]
                                                               {
                                                                   PostProcessLookupEntry candidate;
                                                                   candidate.Handle = Assets->Load<Texture2DAsset>(
                                                                       lookup, AssetPriority::High);
                                                                   return candidate;
                                                               });
            if (entry)
            {
                entry->LastUsedFrame = packet.AcceptedFrameId;
                if (const auto texture = entry->Handle.TryGetLoaded())
                {
                    try
                    {
                        packet.PostProcessLookupEdge = ValidatePostProcessColorLookup(*texture);
                        packet.PostProcessLookup = texture;
                    }
                    catch (const std::invalid_argument&)
                    {
                        // Missing and incompatible textures preserve the input color until repaired.
                    }
                }
            }
        }
        Detail::ApplyPostProcessEnvironment(packet.Environment, packet.PostProcessing);
        // Only immutable evaluated values enter the render packet. Asset revisions stay leased until evaluation ends.
        PostProcessContributions.clear();
        PostProcessProfileLeases.clear();
    }
} // namespace Keire::RenderBackend
