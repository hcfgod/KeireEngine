#include "KeireInternal/Rendering/RenderBackendInternal.h"

#include <array>

#include "Keire/ECS/Components/AnimatorComponent.h"
#include "Keire/ECS/Components/MeshRendererComponent.h"
#include "Keire/Log.h"

#include "KeireInternal/RenderInternal.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <stdexcept>

namespace Keire::RenderBackend
{
    void RenderSharedState::CollectCompletedFrames()
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
            for (auto& retired : frame.RetiredForwardPlus)
                ReleaseForwardPlusResources(retired);
            for (auto* transient : frame.TransientBuffers)
                SDL_ReleaseGPUBuffer(Device, transient);
            SDL_ReleaseGPUFence(Device, frame.Fence);
        }
        if (InFlight.size() < Specification.MaximumFramesInFlight)
            return;

        SDL_GPUFence* fence = InFlight.front().Fence;
        if (!SDL_WaitForGPUFences(Device, true, &fence, 1))
            throw std::runtime_error("SDL_WaitForGPUFences failed: " + LastSdlError());
        CollectCompletedFrames();
    }

    void RenderSharedState::BeginFrame()
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
        Statistics.VisibleSubmeshes = 0;
        Statistics.CulledSubmeshes = 0;
        Statistics.InstanceBatches = 0;
        Statistics.VisibleLocalLights = 0;
        Statistics.OverflowedLightTiles = 0;
        Statistics.DirectionalShadowCascades = 0;
        Statistics.VfxSpriteParticles = 0;
        Statistics.VfxMeshParticles = 0;
        Statistics.DroppedVfxParticles = 0;
        Statistics.VfxComputeDispatches = 0;
        Statistics.VfxIndirectDraws = 0;
        Statistics.VfxGpuWorlds = 0;
        Statistics.VfxGpuBufferBytes = 0;
        Statistics.SampledResolvedDepthAvailable = false;
        Statistics.PlannedFrameGraphPasses = static_cast<std::uint32_t>(SceneFrameGraph.Compiled.Order.size());
        Statistics.ExecutedFrameGraphPasses = 0;
        Statistics.FrameGraphTransitions = 0;
        Statistics.TransientResourceAllocations =
            static_cast<std::uint32_t>(SceneFrameGraph.Compiled.TransientAllocations.size());
        Statistics.ForwardPlusBufferReallocations = 0;
        Statistics.ForwardPlusUploadBytes = 0;
        CollectCompletedFrames();
        const auto vfxRetirementAge = static_cast<std::uint64_t>(Specification.MaximumFramesInFlight) + 2U;
        for (auto iterator = GpuVfxWorlds.begin(); iterator != GpuVfxWorlds.end();)
        {
            if (iterator->second.LastPreparedFrame != 0 && Statistics.Frame > iterator->second.LastPreparedFrame &&
                Statistics.Frame - iterator->second.LastPreparedFrame > vfxRetirementAge)
            {
                ReleaseGpuVfxWorld(iterator->second);
                iterator = GpuVfxWorlds.erase(iterator);
                continue;
            }
            ++iterator;
        }
        Statistics.VfxGpuWorlds = static_cast<std::uint32_t>(GpuVfxWorlds.size());
        for (const auto& surface : LiveSurfaces())
        {
            surface->Submitted = false;
            surface->FrameClearColor = surface->Specification.ClearColor;
            EnsureSurface(*surface);
        }
    }

    void RenderSharedState::CancelFrame() noexcept
    {
        FrameActive = false;
        Requests.clear();
    }

    void RenderSharedState::Submit(SceneRenderRequest request)
    {
        const auto preparationStarted = std::chrono::steady_clock::now();
        RequireOwner("Submit");
        if (!FrameActive)
            throw std::logic_error("Scene render requests are accepted only during an active render frame.");
        if (!request.Scene || !request.View || !request.View->Surface())
            throw std::invalid_argument("SceneRenderRequest requires a scene, view, and render surface.");
        if (!request.Scene->IsOpen())
            throw std::logic_error("SceneRenderRequest cannot submit a closed scene.");

        auto& surface =
            *static_cast<RenderSurfaceState*>(RenderSystemInternalAccess::SurfaceState(*request.View->Surface()));
        const auto owner = surface.Owner.lock();
        if (owner.get() != this)
            throw std::invalid_argument("SceneRenderRequest surface belongs to another renderer.");
        if (surface.Submitted)
            throw std::logic_error("A render surface may receive only one scene request per frame.");
        if (Specification.Mode == RenderMode::Rendered && (!surface.Specification.Depth || !DepthFormat))
            throw std::logic_error("Scene rendering requires a depth-enabled render surface.");
        const auto camera = request.View->Camera();
        if (!Math::IsFinite(camera.View) || !Math::IsFinite(camera.Projection) || !ValidColor(camera.ClearColor))
            throw std::invalid_argument("SceneRenderRequest camera contains invalid values.");
        if (!ValidColor(request.Environment.AmbientColor) || !std::isfinite(request.Environment.AmbientIntensity) ||
            request.Environment.AmbientIntensity < 0.0F || request.Environment.AmbientIntensity > 16.0F ||
            !std::isfinite(request.Environment.Exposure) || request.Environment.Exposure < 0.01F ||
            request.Environment.Exposure > 16.0F)
        {
            throw std::invalid_argument("SceneRenderRequest environment contains invalid values.");
        }
        if (request.Vfx.Particles().size() > VfxRenderSnapshot::MaximumParticles)
            throw std::invalid_argument("SceneRenderRequest exceeds the VFX particle packet bound.");
        for (const auto& particle : request.Vfx.Particles())
        {
            if (!Math::IsFinite(particle.Position) || !Math::IsFinite(particle.Rotation) ||
                !Math::IsFinite(particle.Tint) || !std::isfinite(particle.Size) || particle.Size < 0.0F ||
                particle.Renderer > VfxRendererType::Mesh)
            {
                throw std::invalid_argument("SceneRenderRequest contains an invalid VFX particle.");
            }
            if (particle.Renderer == VfxRendererType::Sprite)
                ++Statistics.VfxSpriteParticles;
            else
                ++Statistics.VfxMeshParticles;
        }
        if (!request.Vfx.GpuEmitters().empty() &&
            (request.Vfx.WorldId() == 0 || request.Vfx.ParticleCapacity() == 0 ||
             request.Vfx.ParticleCapacity() > 10'000'000U || !std::isfinite(request.Vfx.DeltaSeconds()) ||
             request.Vfx.DeltaSeconds() < 0.0F))
        {
            throw std::invalid_argument("SceneRenderRequest contains an invalid GPU VFX snapshot.");
        }
        for (const auto& emitter : request.Vfx.GpuEmitters())
        {
            if (!emitter.Handle || emitter.Revision == 0 || !Math::IsFinite(emitter.Position) ||
                !Math::IsFinite(emitter.Rotation) || !Math::IsFinite(emitter.ShapeExtent) ||
                !Math::IsFinite(emitter.VelocityMinimum) || !Math::IsFinite(emitter.VelocityMaximum) ||
                !Math::IsFinite(emitter.Acceleration) || !Math::IsFinite(emitter.ColorStart) ||
                !Math::IsFinite(emitter.ColorEnd) || !std::isfinite(emitter.LifetimeMinimum) ||
                !std::isfinite(emitter.LifetimeMaximum) || emitter.LifetimeMinimum <= 0.0F ||
                emitter.LifetimeMaximum < emitter.LifetimeMinimum || !std::isfinite(emitter.SizeStart) ||
                !std::isfinite(emitter.SizeEnd) || emitter.Renderer > VfxRendererType::Mesh)
            {
                throw std::invalid_argument("SceneRenderRequest contains an invalid GPU VFX emitter.");
            }
        }
        Statistics.DroppedVfxParticles += request.Vfx.DroppedParticles();

        surface.Submitted = true;
        surface.FrameClearColor = camera.ClearColor;
        SceneRenderPacket packet;
        packet.Camera = camera;
        packet.Environment = request.Environment;
        packet.Lighting = ResolveLighting(request.Scene);
        packet.LocalLights = ResolveLocalLights(request.Scene);
        packet.DrawGrid = request.DrawGrid;
        packet.Vfx = std::move(request.Vfx);
        const auto renderEntities = request.Scene->Query<MeshRendererComponent>();
        packet.DrawItems.reserve(renderEntities.size());
        for (const auto& entity : renderEntities)
        {
            if (!entity.ActiveInHierarchy())
                continue;
            const auto renderer = entity.GetComponent<MeshRendererComponent>();
            const auto transform = entity.GetComponent<TransformComponent>();
            if (!renderer || !renderer->Enabled() || !renderer->Visible() || !transform)
                continue;
            std::vector<Matrix4> skinPalette;
            AssetId skin;
            AssetId skinSkeleton;
            if (const auto animator = entity.GetComponent<AnimatorComponent>(); animator && animator->Enabled())
            {
                skinPalette.assign(animator->SkinPalette().begin(), animator->SkinPalette().end());
                skin = animator->SkinnedMesh();
                skinSkeleton = animator->Skeleton();
            }
            packet.DrawItems.push_back({renderer->Mesh(),
                                        {renderer->Materials().begin(), renderer->Materials().end()},
                                        transform->WorldMatrix(),
                                        renderer->Tint(),
                                        entity.Id(),
                                        skin,
                                        skinSkeleton,
                                        std::move(skinPalette),
                                        renderer->CastShadows(),
                                        renderer->ReceiveShadows()});
        }
        Requests.push_back({std::move(packet), &surface});
        Statistics.CpuPreparationMilliseconds =
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - preparationStarted).count();
        PreparationSamples.push_back(Statistics.CpuPreparationMilliseconds);
        constexpr std::size_t sampleWindow = 120;
        if (PreparationSamples.size() > sampleWindow)
            PreparationSamples.pop_front();
        std::array<float, sampleWindow> orderedSamples{};
        std::ranges::copy(PreparationSamples, orderedSamples.begin());
        std::ranges::sort(orderedSamples.begin(),
                          orderedSamples.begin() + static_cast<std::ptrdiff_t>(PreparationSamples.size()));
        const auto percentileIndex = (PreparationSamples.size() * 95U + 99U) / 100U - 1U;
        Statistics.CpuPreparationP95Milliseconds = orderedSamples[percentileIndex];
    }

    const GpuMeshResources& RenderSharedState::ResolveMesh(const AssetId id)
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
                    KEIRE_CORE_ERROR("Mesh GPU rebuild failed for id={} revision={}: {}", id.ToString(), revision,
                                     error.what());
                }
            }
        }
        return entry.Resources.Empty() ? ErrorMesh : entry.Resources;
    }

    const GpuTextureResources& RenderSharedState::ResolveTexture(const AssetId id)
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
                    KEIRE_CORE_ERROR("Texture GPU rebuild failed for id={} revision={}: {}", id.ToString(), revision,
                                     error.what());
                }
            }
        }
        return entry.Resources.Empty() ? CheckerboardTexture : entry.Resources;
    }

    const GpuTextureResources& RenderSharedState::DefaultTexture(const ShaderTextureSemantic semantic) const noexcept
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
        case ShaderTextureSemantic::Metallic:
            return BlackDataTexture;
        case ShaderTextureSemantic::Roughness:
            return WhiteDataTexture;
        case ShaderTextureSemantic::Generic:
        default:
            return CheckerboardTexture;
        }
    }

    Ref<const MaterialAsset> RenderSharedState::ResolveMaterial(const AssetId id)
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

    GpuShaderEntry* RenderSharedState::ResolveShader(const AssetId id, const SDL_GPUSampleCount samples,
                                                     MaterialSurfaceState surface, const bool explicitSurface)
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
                    if (!explicitSurface && shader->Definition().Blend)
                        surface.AlphaMode = MaterialAlphaMode::Blend;
                    auto replacement = CreateAssetPipeline(shader->Definition(), samples, surface);
                    for (const auto& pipeline : entry.Pipelines)
                        Retire(pipeline.Handle);
                    entry.Pipelines = {{samples, surface.AlphaMode, surface.DoubleSided, replacement}};
                    entry.LastGood = shader;
                    entry.LoadedRevision = revision;
                }
                catch (const std::exception& error)
                {
                    KEIRE_CORE_ERROR("Shader GPU rebuild failed for id={} revision={}: {}", id.ToString(), revision,
                                     error.what());
                }
            }
        }
        if (!entry.LastGood)
            return nullptr;
        if (!explicitSurface && entry.LastGood->Definition().Blend)
            surface.AlphaMode = MaterialAlphaMode::Blend;
        auto pipeline = std::ranges::find_if(entry.Pipelines,
                                             [&](const GpuShaderEntry::Pipeline& candidate)
                                             {
                                                 return candidate.Samples == samples &&
                                                        candidate.AlphaMode == surface.AlphaMode &&
                                                        candidate.DoubleSided == surface.DoubleSided;
                                             });
        if (pipeline == entry.Pipelines.end())
        {
            try
            {
                entry.Pipelines.push_back({samples, surface.AlphaMode, surface.DoubleSided,
                                           CreateAssetPipeline(entry.LastGood->Definition(), samples, surface)});
            }
            catch (const std::exception& error)
            {
                KEIRE_CORE_ERROR("Shader pipeline creation failed for id={}: {}", id.ToString(), error.what());
                return nullptr;
            }
        }
        return &entry;
    }

    const ResolvedAssetMaterial* RenderSharedState::ResolveAssetMaterial(const AssetId id,
                                                                         const SDL_GPUSampleCount samples)
    {
        const auto material = ResolveMaterial(id);
        auto materialEntry = MaterialCache.find(id);
        if (materialEntry == MaterialCache.end())
            return nullptr;
        auto& cache = materialEntry->second;
        std::uint64_t stamp = 1469598103934665603ULL;
        stamp = HashDependencyStamp(stamp, cache.LastAttemptedRevision);
        stamp = HashDependencyStamp(stamp, cache.LoadedRevision);
        stamp = HashDependencyStamp(stamp, static_cast<std::uint64_t>(samples));
        stamp = HashDependencyStamp(stamp, static_cast<std::uint64_t>(ColorFormat));
        stamp = HashDependencyStamp(stamp, static_cast<std::uint64_t>(DepthFormat));
        stamp =
            HashDependencyStamp(stamp, static_cast<std::uint64_t>(material ? material->Definition().Surface.AlphaMode
                                                                           : MaterialAlphaMode::Opaque));
        stamp = HashDependencyStamp(
            stamp, material ? std::bit_cast<std::uint32_t>(material->Definition().Surface.AlphaCutoff) : 0U);
        stamp = HashDependencyStamp(stamp, material && material->Definition().Surface.DoubleSided ? 1U : 0U);
        bool failedDependencyRevision = cache.LoadedRevision != 0 && cache.LastAttemptedRevision > cache.LoadedRevision;
        if (!material || !material->Definition().Shader)
        {
            cache.LastAttemptedDependencyStamp = stamp;
            return cache.Binding.Pipeline ? &cache.Binding : nullptr;
        }
        stamp = HashDependencyStamp(stamp, material->Definition().Shader);
        auto* shader = ResolveShader(material->Definition().Shader, samples, material->Definition().Surface,
                                     material->Definition().SchemaVersion >= 2);
        if (!shader)
        {
            cache.LastAttemptedDependencyStamp = stamp;
            return cache.Binding.Pipeline ? &cache.Binding : nullptr;
        }
        stamp = HashDependencyStamp(stamp, shader->LastAttemptedRevision);
        stamp = HashDependencyStamp(stamp, shader->LoadedRevision);
        auto surface = material->Definition().Surface;
        if (material->Definition().SchemaVersion < 2 && shader->LastGood->Definition().Blend)
            surface.AlphaMode = MaterialAlphaMode::Blend;
        const auto pipeline = std::ranges::find_if(shader->Pipelines,
                                                   [&](const GpuShaderEntry::Pipeline& candidate)
                                                   {
                                                       return candidate.Samples == samples &&
                                                              candidate.AlphaMode == surface.AlphaMode &&
                                                              candidate.DoubleSided == surface.DoubleSided;
                                                   });
        if (pipeline == shader->Pipelines.end())
        {
            cache.LastAttemptedDependencyStamp = stamp;
            return cache.Binding.Pipeline ? &cache.Binding : nullptr;
        }

        const auto& properties = material->Definition().Properties;
        for (const auto& [name, value] : properties)
        {
            (void)value;
            if (std::ranges::find(shader->LastGood->Definition().Properties, name, &ShaderPropertyDefinition::Name) ==
                shader->LastGood->Definition().Properties.end())
            {
                cache.LastAttemptedDependencyStamp = stamp;
                return cache.Binding.Pipeline ? &cache.Binding : nullptr;
            }
        }

        failedDependencyRevision |=
            shader->LoadedRevision != 0 && shader->LastAttemptedRevision > shader->LoadedRevision;
        for (const auto& property : shader->LastGood->Definition().Properties)
        {
            if (property.Type != ShaderPropertyType::Texture2D)
                continue;
            const auto found = properties.find(property.Name);
            AssetId texture = property.DefaultTexture;
            if (found != properties.end())
            {
                const auto* selected = std::get_if<AssetId>(&found->second);
                if (!selected)
                {
                    cache.LastAttemptedDependencyStamp = stamp;
                    return cache.Binding.Pipeline ? &cache.Binding : nullptr;
                }
                texture = *selected;
            }
            stamp = HashDependencyStamp(stamp, texture);
            if (!texture)
            {
                stamp = HashDependencyStamp(stamp, static_cast<std::uint64_t>(property.TextureSemantic));
                continue;
            }
            (void)ResolveTexture(texture);
            const auto textureEntry = TextureCache.find(texture);
            if (textureEntry == TextureCache.end())
                continue;
            stamp = HashDependencyStamp(stamp, textureEntry->second.LastAttemptedRevision);
            stamp = HashDependencyStamp(stamp, textureEntry->second.LoadedRevision);
            failedDependencyRevision |=
                textureEntry->second.LoadedRevision != 0 &&
                textureEntry->second.LastAttemptedRevision > textureEntry->second.LoadedRevision;
        }
        if (stamp == cache.LastAttemptedDependencyStamp)
            return cache.Binding.Pipeline ? &cache.Binding : nullptr;
        cache.LastAttemptedDependencyStamp = stamp;
        if (failedDependencyRevision)
            return cache.Binding.Pipeline ? &cache.Binding : nullptr;

        try
        {
            ResolvedAssetMaterial result;
            result.Pipeline = pipeline->Handle;
            result.Surface = surface;
            result.ReceivesShadows = shader->LastGood->Definition().ReceivesShadows;
            result.UsesForwardPlus = shader->LastGood->Definition().UsesForwardPlus;
            result.UsesInstancing = shader->LastGood->Definition().UsesInstancing;
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
                            throw std::runtime_error("Material texture property has an invalid value type.");
                        texture = *selected;
                    }
                    const auto& resolved = texture ? ResolveTexture(texture) : DefaultTexture(property.TextureSemantic);
                    if (result.Textures.size() >= 16)
                        throw std::runtime_error("Material exceeds the fragment texture binding limit.");
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
                        throw std::runtime_error("Material numeric property has an invalid value type.");
                }
                if (property.Name == "Tint")
                    result.TintSlot = result.NumericProperties.size();
                if (result.NumericProperties.size() >= 64)
                    throw std::runtime_error("Material exceeds the numeric property binding limit.");
                result.NumericProperties.push_back(packed);
            }
            if (result.NumericProperties.empty())
                result.NumericProperties.emplace_back();
            cache.Binding = std::move(result);
            cache.LastGoodDependencyStamp = stamp;
            ++MaterialBindingBuilds;
        }
        catch (const std::exception& error)
        {
            KEIRE_CORE_ERROR("Material GPU binding rebuild failed for id={} revision={}: {}", id.ToString(),
                             cache.LoadedRevision, error.what());
        }
        return cache.Binding.Pipeline ? &cache.Binding : nullptr;
    }
} // namespace Keire::RenderBackend
