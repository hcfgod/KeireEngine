#include "KeireInternal/Rendering/FrameGraphInternal.h"

#include "Keire/Rendering/RenderSystem.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <stdexcept>

namespace Keire::RenderBackend
{
    namespace
    {
        [[nodiscard]] constexpr bool ValidSampleCount(const std::uint8_t sampleCount) noexcept
        {
            return sampleCount == 1U || sampleCount == 2U || sampleCount == 4U || sampleCount == 8U;
        }

        [[nodiscard]] constexpr bool IsDepthFormat(const FrameGraphTextureFormat format) noexcept
        {
            return format == FrameGraphTextureFormat::D32Float;
        }

        [[nodiscard]] constexpr std::uint64_t
        TypedTextureCompatibilityKey(const FrameGraphTextureDescription& texture) noexcept
        {
            return (std::uint64_t{1} << 63U) | (static_cast<std::uint64_t>(texture.Format) << 48U) |
                   (static_cast<std::uint64_t>(texture.SampleCount) << 40U) |
                   (static_cast<std::uint64_t>(texture.WidthScaleNumerator) << 32U) |
                   (static_cast<std::uint64_t>(texture.WidthScaleDenominator) << 24U) |
                   (static_cast<std::uint64_t>(texture.HeightScaleNumerator) << 16U) |
                   (static_cast<std::uint64_t>(texture.HeightScaleDenominator) << 8U);
        }

        void ValidateResourceDescription(FrameGraphResourceDescription& description)
        {
            const auto& texture = description.Texture;
            if (description.Kind == FrameGraphResourceKind::Buffer)
            {
                if (texture.Format != FrameGraphTextureFormat::Undefined ||
                    texture.Usage != FrameGraphResourceUsage::None)
                {
                    throw std::invalid_argument("Frame-graph buffers cannot declare texture metadata.");
                }
                return;
            }

            if (!ValidSampleCount(texture.SampleCount) || texture.WidthScaleNumerator == 0U ||
                texture.WidthScaleDenominator == 0U || texture.HeightScaleNumerator == 0U ||
                texture.HeightScaleDenominator == 0U)
            {
                throw std::invalid_argument("Frame-graph texture sample and relative-size metadata is invalid.");
            }
            if (texture.Format == FrameGraphTextureFormat::Undefined)
            {
                if (texture.Usage != FrameGraphResourceUsage::None)
                    throw std::invalid_argument("Frame-graph texture usage requires an exact texture format.");
                return;
            }
            if (texture.Usage == FrameGraphResourceUsage::None)
                throw std::invalid_argument("Typed frame-graph textures require at least one declared usage.");

            const bool depthUsage =
                HasFrameGraphResourceUsage(texture.Usage, FrameGraphResourceUsage::DepthStencilAttachment);
            const bool colorUsage = HasFrameGraphResourceUsage(texture.Usage, FrameGraphResourceUsage::ColorAttachment);
            if (IsDepthFormat(texture.Format) != depthUsage || (depthUsage && colorUsage))
                throw std::invalid_argument("Frame-graph depth formats and attachment usages are incompatible.");

            const auto typedKey = TypedTextureCompatibilityKey(texture);
            if (description.CompatibilityKey != 0U && description.CompatibilityKey != typedKey)
            {
                throw std::invalid_argument(
                    "Typed frame-graph textures cannot override their derived alias compatibility key.");
            }
            description.CompatibilityKey = typedKey;
        }

        [[nodiscard]] FrameGraphResourceDescription TextureResource(std::string name,
                                                                    const FrameGraphTextureFormat format,
                                                                    const FrameGraphResourceUsage usage,
                                                                    const bool imported = false)
        {
            FrameGraphResourceDescription result;
            result.Name = std::move(name);
            result.Kind = FrameGraphResourceKind::Texture;
            result.Imported = imported;
            result.Texture.Format = format;
            result.Texture.Usage = usage;
            return result;
        }
    } // namespace

    FrameGraphResource FrameGraph::AddResource(FrameGraphResourceDescription description)
    {
        if (description.Name.empty())
            throw std::invalid_argument("Frame-graph resources require a diagnostic name.");
        if (std::ranges::any_of(m_Resources, [&](const auto& resource) { return resource.Name == description.Name; }))
            throw std::invalid_argument("Frame-graph resource names must be unique.");
        ValidateResourceDescription(description);
        const auto index = static_cast<std::uint32_t>(m_Resources.size());
        m_Resources.push_back(std::move(description));
        return {index};
    }

    FrameGraphPass FrameGraph::AddPass(FrameGraphPassDescription description)
    {
        if (description.Name.empty())
            throw std::invalid_argument("Frame-graph passes require a diagnostic name.");
        if (std::ranges::any_of(m_Passes, [&](const auto& pass) { return pass.Name == description.Name; }))
            throw std::invalid_argument("Frame-graph pass names must be unique.");
        const auto validResource = [&](const FrameGraphResource resource)
        { return resource && resource.Value < m_Resources.size(); };
        if (!std::ranges::all_of(description.Reads, validResource) ||
            !std::ranges::all_of(description.Writes, validResource))
            throw std::invalid_argument("Frame-graph pass references an unknown resource.");
        std::ranges::sort(description.Reads, {}, &FrameGraphResource::Value);
        description.Reads.erase(std::unique(description.Reads.begin(), description.Reads.end()),
                                description.Reads.end());
        std::ranges::sort(description.Writes, {}, &FrameGraphResource::Value);
        description.Writes.erase(std::unique(description.Writes.begin(), description.Writes.end()),
                                 description.Writes.end());
        if (std::ranges::any_of(description.Reads, [&](const auto resource)
                                { return std::ranges::binary_search(description.Writes, resource); }))
            throw std::invalid_argument("A frame-graph pass cannot read and write the same resource implicitly.");
        const auto index = static_cast<std::uint32_t>(m_Passes.size());
        m_Passes.push_back(std::move(description));
        return {index};
    }

    CompiledFrameGraph FrameGraph::Compile() const
    {
        CompiledFrameGraph result;
        result.Lifetimes.resize(m_Resources.size());
        const auto passCount = static_cast<std::uint32_t>(m_Passes.size());
        std::vector<std::vector<std::uint32_t>> edges(passCount);
        std::vector<std::uint32_t> incoming(passCount);
        std::vector<std::uint32_t> lastWriter(m_Resources.size(), std::numeric_limits<std::uint32_t>::max());
        std::vector<std::vector<std::uint32_t>> readers(m_Resources.size());

        const auto addEdge = [&](const std::uint32_t source, const std::uint32_t destination)
        {
            if (source == std::numeric_limits<std::uint32_t>::max() || source == destination)
                return;
            auto& destinations = edges[source];
            if (std::ranges::find(destinations, destination) == destinations.end())
            {
                destinations.push_back(destination);
                ++incoming[destination];
            }
        };

        for (std::uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            const auto& pass = m_Passes[passIndex];
            for (const auto resource : pass.Reads)
            {
                if (lastWriter[resource.Value] == std::numeric_limits<std::uint32_t>::max() &&
                    !m_Resources[resource.Value].Imported)
                    throw std::logic_error("Frame-graph pass '" + pass.Name + "' reads transient resource '" +
                                           m_Resources[resource.Value].Name + "' before it is written.");
                addEdge(lastWriter[resource.Value], passIndex);
                readers[resource.Value].push_back(passIndex);
            }
            for (const auto resource : pass.Writes)
            {
                addEdge(lastWriter[resource.Value], passIndex);
                for (const auto reader : readers[resource.Value])
                    addEdge(reader, passIndex);
                readers[resource.Value].clear();
                lastWriter[resource.Value] = passIndex;
            }
        }

        std::deque<std::uint32_t> ready;
        for (std::uint32_t index = 0; index < passCount; ++index)
        {
            std::ranges::sort(edges[index]);
            if (incoming[index] == 0)
                ready.push_back(index);
        }
        while (!ready.empty())
        {
            const auto passIndex = ready.front();
            ready.pop_front();
            result.Order.push_back({passIndex});
            for (const auto destination : edges[passIndex])
            {
                if (--incoming[destination] == 0)
                {
                    const auto position = std::ranges::upper_bound(ready, destination);
                    ready.insert(position, destination);
                }
            }
        }
        if (result.Order.size() != m_Passes.size())
            throw std::logic_error("Frame-graph dependencies contain a cycle.");

        for (std::uint32_t orderedIndex = 0; orderedIndex < result.Order.size(); ++orderedIndex)
        {
            const auto& pass = m_Passes[result.Order[orderedIndex].Value];
            const auto update = [&](const FrameGraphResource resource)
            {
                auto& lifetime = result.Lifetimes[resource.Value];
                if (!lifetime.Used)
                {
                    lifetime.FirstPass = orderedIndex;
                    lifetime.Used = true;
                }
                lifetime.LastPass = orderedIndex;
            };
            for (const auto resource : pass.Reads)
                update(resource);
            for (const auto resource : pass.Writes)
                update(resource);
            result.Diagnostics.push_back(std::to_string(orderedIndex) + ": " + pass.Name);
        }

        constexpr auto invalidPhysical = std::numeric_limits<std::uint32_t>::max();
        result.PhysicalResources.assign(m_Resources.size(), invalidPhysical);
        struct PhysicalLifetime final
        {
            std::uint32_t LastPass = 0;
        };
        std::vector<PhysicalLifetime> physicalLifetimes;
        std::vector<std::uint32_t> allocationOrder;
        allocationOrder.reserve(m_Resources.size());
        for (std::uint32_t resourceIndex = 0; resourceIndex < m_Resources.size(); ++resourceIndex)
        {
            if (!m_Resources[resourceIndex].Imported && result.Lifetimes[resourceIndex].Used)
                allocationOrder.push_back(resourceIndex);
        }
        std::ranges::stable_sort(allocationOrder, {}, [&](const std::uint32_t resourceIndex)
                                 { return result.Lifetimes[resourceIndex].FirstPass; });
        for (const auto resourceIndex : allocationOrder)
        {
            const auto& resource = m_Resources[resourceIndex];
            const auto& lifetime = result.Lifetimes[resourceIndex];
            std::optional<std::uint32_t> selected;
            for (std::uint32_t physicalIndex = 0; physicalIndex < result.TransientAllocations.size(); ++physicalIndex)
            {
                auto& allocation = result.TransientAllocations[physicalIndex];
                if (physicalLifetimes[physicalIndex].LastPass < lifetime.FirstPass &&
                    allocation.Kind == resource.Kind && allocation.CompatibilityKey == resource.CompatibilityKey)
                {
                    selected = physicalIndex;
                    allocation.SizeBytes = std::max(allocation.SizeBytes, resource.SizeBytes);
                    allocation.Texture.Usage = allocation.Texture.Usage | resource.Texture.Usage;
                    break;
                }
            }
            if (!selected)
            {
                selected = static_cast<std::uint32_t>(result.TransientAllocations.size());
                result.TransientAllocations.push_back(
                    {resource.Kind, resource.CompatibilityKey, resource.SizeBytes, resource.Texture});
                physicalLifetimes.push_back({});
            }
            result.PhysicalResources[resourceIndex] = *selected;
            physicalLifetimes[*selected].LastPass = lifetime.LastPass;
        }

        const auto requiredState =
            [&](const FrameGraphPassDescription& pass, const FrameGraphResource resource, const bool write)
        {
            if (!write)
                return pass.Kind == FrameGraphPassKind::Transfer  ? FrameGraphResourceState::CopySource
                       : pass.Kind == FrameGraphPassKind::Compute ? FrameGraphResourceState::StorageRead
                                                                  : FrameGraphResourceState::ShaderRead;
            if (pass.Kind == FrameGraphPassKind::Upload || pass.Kind == FrameGraphPassKind::Transfer)
                return FrameGraphResourceState::CopyDestination;
            if (pass.Kind == FrameGraphPassKind::Compute)
                return FrameGraphResourceState::StorageWrite;
            if (pass.Kind == FrameGraphPassKind::Present)
                return FrameGraphResourceState::Present;
            if (m_Resources[resource.Value].Kind == FrameGraphResourceKind::Texture &&
                IsDepthFormat(m_Resources[resource.Value].Texture.Format))
            {
                return FrameGraphResourceState::DepthStencilAttachment;
            }
            return m_Resources[resource.Value].Kind == FrameGraphResourceKind::Texture
                       ? FrameGraphResourceState::ColorAttachment
                       : FrameGraphResourceState::StorageWrite;
        };
        std::vector<FrameGraphResourceState> states(m_Resources.size(), FrameGraphResourceState::Undefined);
        std::vector<FrameGraphResourceState> physicalStates(result.TransientAllocations.size(),
                                                            FrameGraphResourceState::Undefined);
        for (std::uint32_t index = 0; index < m_Resources.size(); ++index)
        {
            if (m_Resources[index].Imported)
                states[index] = FrameGraphResourceState::External;
        }
        for (const auto passHandle : result.Order)
        {
            const auto& pass = m_Passes[passHandle.Value];
            CompiledFrameGraph::PassExecution execution;
            execution.Pass = passHandle;
            const auto transition = [&](const FrameGraphResource resource, const FrameGraphResourceState after)
            {
                auto& before = m_Resources[resource.Value].Imported
                                   ? states[resource.Value]
                                   : physicalStates[result.PhysicalResources[resource.Value]];
                if (before != after)
                    execution.Transitions.push_back({resource, before, after});
                before = after;
            };
            for (const auto resource : pass.Reads)
                transition(resource, requiredState(pass, resource, false));
            for (const auto resource : pass.Writes)
                transition(resource, requiredState(pass, resource, true));
            result.Execution.push_back(std::move(execution));
        }
        return result;
    }

    void FrameGraph::Execute(const CompiledFrameGraph& compiled, FrameGraphExecutionContext& context) const
    {
        if (compiled.Execution.size() != compiled.Order.size())
            throw std::invalid_argument("Compiled frame graph does not contain an executable pass schedule.");
        for (const auto& execution : compiled.Execution)
        {
            if (!execution.Pass || execution.Pass.Value >= m_Passes.size())
                throw std::invalid_argument("Compiled frame graph references an unknown pass.");
            for (const auto& transition : execution.Transitions)
                context.Transition(transition);
            context.Execute(execution.Pass, m_Passes[execution.Pass.Value]);
        }
    }

    void FrameGraph::Clear() noexcept
    {
        m_Resources.clear();
        m_Passes.clear();
    }

    StaticSceneFrameGraph BuildStaticSceneFrameGraph() { return BuildStaticSceneFrameGraph(RenderPath::ForwardPlus); }

    StaticSceneFrameGraph BuildStaticSceneFrameGraph(const RenderPath path)
    {
        if (path != RenderPath::ForwardPlus && path != RenderPath::DeferredHybrid)
            throw std::invalid_argument("A static scene frame graph requires a resolved render path.");

        StaticSceneFrameGraph result;
        result.Path = path;
        const auto uploads = result.Graph.AddResource({"Uploaded resources", FrameGraphResourceKind::Buffer, true});
        const auto shadows = result.Graph.AddResource(
            TextureResource("Directional shadows", FrameGraphTextureFormat::D32Float,
                            FrameGraphResourceUsage::DepthStencilAttachment | FrameGraphResourceUsage::Sampled, true));
        result.ForwardPlusLightTiles =
            result.Graph.AddResource({"Forward+ tile lists", FrameGraphResourceKind::Buffer, true});
        result.GpuOcclusionDepth = result.Graph.AddResource(
            TextureResource("Occlusion depth", FrameGraphTextureFormat::D32Float,
                            FrameGraphResourceUsage::DepthStencilAttachment | FrameGraphResourceUsage::Sampled, true));
        result.GpuOcclusionPyramid = result.Graph.AddResource(
            TextureResource("Occlusion depth pyramid", FrameGraphTextureFormat::R32Float,
                            FrameGraphResourceUsage::Sampled | FrameGraphResourceUsage::Storage, true));
        result.GpuOcclusionIndirectArguments =
            result.Graph.AddResource({"Occlusion indirect arguments", FrameGraphResourceKind::Buffer, true});
        result.GpuVisibilityMasks =
            result.Graph.AddResource({"Frame-owned visibility masks", FrameGraphResourceKind::Buffer, true});
        result.SpatialSelectionRecords =
            result.Graph.AddResource({"Frame-owned spatial selection records", FrameGraphResourceKind::Buffer, true});
        result.VfxDynamicCandidates =
            result.Graph.AddResource({"VFX dynamic visibility candidates", FrameGraphResourceKind::Buffer, true});
        result.HdrScene = result.Graph.AddResource(
            TextureResource("HDR scene color", FrameGraphTextureFormat::Rgba16Float,
                            FrameGraphResourceUsage::ColorAttachment | FrameGraphResourceUsage::Sampled));
        result.SceneDepth = result.Graph.AddResource(
            TextureResource("Scene depth", FrameGraphTextureFormat::D32Float,
                            FrameGraphResourceUsage::DepthStencilAttachment | FrameGraphResourceUsage::Sampled, true));
        result.SampledDepth = result.Graph.AddResource(
            TextureResource("Sampled scene depth", FrameGraphTextureFormat::D32Float,
                            FrameGraphResourceUsage::DepthStencilAttachment | FrameGraphResourceUsage::Sampled, true));
        const auto skyComplete = result.Graph.AddResource({"Sky complete", FrameGraphResourceKind::Buffer, true});
        const auto transparencyComplete =
            result.Graph.AddResource({"Transparency complete", FrameGraphResourceKind::Buffer, true});
        const auto vfxPrepared =
            result.Graph.AddResource({"VFX expansion complete", FrameGraphResourceKind::Buffer, true});
        const auto toneMapped = result.Graph.AddResource(
            TextureResource("Tone-mapped scene", FrameGraphTextureFormat::Rgba8Unorm,
                            FrameGraphResourceUsage::ColorAttachment | FrameGraphResourceUsage::Sampled, true));
        const auto overlays = result.Graph.AddResource(
            TextureResource("Scene overlays", FrameGraphTextureFormat::Rgba8Unorm,
                            FrameGraphResourceUsage::ColorAttachment | FrameGraphResourceUsage::Sampled, true));
        const auto readback = result.Graph.AddResource({"Readback", FrameGraphResourceKind::Buffer, true});
        const auto presentation = result.Graph.AddResource(
            TextureResource("Presentation", FrameGraphTextureFormat::Rgba8Unorm,
                            FrameGraphResourceUsage::ColorAttachment | FrameGraphResourceUsage::Present, true));

        constexpr auto gBufferUsage = FrameGraphResourceUsage::ColorAttachment | FrameGraphResourceUsage::Sampled;
        result.GBufferVelocity = result.Graph.AddResource(
            TextureResource("Motion vectors", FrameGraphTextureFormat::Rg16Float, gBufferUsage));

        if (path == RenderPath::DeferredHybrid)
        {
            result.GBufferBaseColorMetallic = result.Graph.AddResource(
                TextureResource("GBuffer base color and metallic", FrameGraphTextureFormat::Rgba8Srgb, gBufferUsage));
            result.GBufferNormalRoughness = result.Graph.AddResource(
                TextureResource("GBuffer normal and roughness", FrameGraphTextureFormat::Rgba16Float, gBufferUsage));
            result.GBufferMaterial = result.Graph.AddResource(
                TextureResource("GBuffer material", FrameGraphTextureFormat::Rgba8Unorm, gBufferUsage));
            result.GBufferLighting = result.Graph.AddResource(
                TextureResource("GBuffer baked and spatial lighting", FrameGraphTextureFormat::Rgba32Float,
                                FrameGraphResourceUsage::ColorAttachment | FrameGraphResourceUsage::Sampled));
            result.DBufferBaseColor = result.Graph.AddResource(
                TextureResource("DBuffer base color", FrameGraphTextureFormat::Rgba8Srgb, gBufferUsage));
            result.DBufferNormal = result.Graph.AddResource(
                TextureResource("DBuffer normal", FrameGraphTextureFormat::Rgba16Float, gBufferUsage));
            result.DBufferMaterial = result.Graph.AddResource(
                TextureResource("DBuffer material", FrameGraphTextureFormat::Rgba8Unorm, gBufferUsage));
            auto irradynRadiance =
                TextureResource("Irradyn radiance", FrameGraphTextureFormat::Rgba16Float, gBufferUsage);
            irradynRadiance.Texture.WidthScaleNumerator = 1U;
            irradynRadiance.Texture.WidthScaleDenominator = 2U;
            irradynRadiance.Texture.HeightScaleNumerator = 1U;
            irradynRadiance.Texture.HeightScaleDenominator = 2U;
            result.IrradynRadiance = result.Graph.AddResource(std::move(irradynRadiance));
        }

        result.ResourceUploads = result.Graph.AddPass({"Resource uploads", {}, {uploads}, FrameGraphPassKind::Upload});
        result.DirectionalShadows =
            result.Graph.AddPass({"Directional shadow maps", {uploads}, {shadows}, FrameGraphPassKind::Graphics});
        result.VfxSimulation = result.Graph.AddPass({"VFX simulation and dynamic bounds",
                                                     {uploads},
                                                     {result.VfxDynamicCandidates},
                                                     FrameGraphPassKind::Compute});
        result.GpuOcclusionDepthPass = result.Graph.AddPass({"Occlusion depth",
                                                             {uploads, result.VfxDynamicCandidates},
                                                             {result.GpuOcclusionDepth},
                                                             FrameGraphPassKind::Graphics});
        result.GpuOcclusionPyramidPass = result.Graph.AddPass({"Occlusion depth pyramid",
                                                               {result.GpuOcclusionDepth},
                                                               {result.GpuOcclusionPyramid},
                                                               FrameGraphPassKind::Compute});
        result.GpuOcclusionCullingPass =
            result.Graph.AddPass({"GPU occlusion culling",
                                  {uploads, result.GpuOcclusionPyramid, result.VfxDynamicCandidates},
                                  {result.GpuOcclusionIndirectArguments, result.GpuVisibilityMasks},
                                  FrameGraphPassKind::Compute});
        result.ForwardPlusCulling = result.Graph.AddPass({"Forward+ light culling",
                                                          {uploads, result.GpuVisibilityMasks},
                                                          {result.ForwardPlusLightTiles},
                                                          FrameGraphPassKind::Compute});
        result.SpatialSelection = result.Graph.AddPass({"Spatial lighting selection",
                                                        {uploads, result.GpuVisibilityMasks},
                                                        {result.SpatialSelectionRecords},
                                                        FrameGraphPassKind::Compute});
        result.VfxPreparation =
            result.Graph.AddPass({"VFX expansion",
                                  {uploads, result.GpuVisibilityMasks, result.ForwardPlusLightTiles},
                                  {vfxPrepared},
                                  FrameGraphPassKind::Compute});

        const std::vector opaqueInputs{uploads,
                                       shadows,
                                       result.ForwardPlusLightTiles,
                                       result.GpuOcclusionIndirectArguments,
                                       result.SpatialSelectionRecords,
                                       vfxPrepared};
        result.DepthVelocity = result.Graph.AddPass(
            {path == RenderPath::DeferredHybrid ? "Depth and velocity prepass" : "Forward+ motion-vector prepass",
             opaqueInputs,
             {result.SceneDepth, result.GBufferVelocity}});
        if (path == RenderPath::ForwardPlus)
        {
            result.Opaque = result.Graph.AddPass({"Opaque and mask", opaqueInputs, {result.HdrScene}});
        }
        else
        {
            result.DeferredGBufferStandard =
                result.Graph.AddPass({"Deferred GBuffer standard",
                                      opaqueInputs,
                                      {result.SceneDepth, result.GBufferBaseColorMetallic,
                                       result.GBufferNormalRoughness, result.GBufferMaterial, result.GBufferLighting}});
            result.DeferredGBufferExtended =
                result.Graph.AddPass({"Deferred GBuffer extended",
                                      opaqueInputs,
                                      {result.SceneDepth, result.GBufferBaseColorMetallic,
                                       result.GBufferNormalRoughness, result.GBufferMaterial, result.GBufferLighting}});
            result.DeferredDecals =
                result.Graph.AddPass({"Deferred decals",
                                      {result.SceneDepth},
                                      {result.DBufferBaseColor, result.DBufferNormal, result.DBufferMaterial}});
            result.DeferredLighting = result.Graph.AddPass(
                {"Deferred lighting",
                 {shadows, result.ForwardPlusLightTiles, result.SpatialSelectionRecords, result.SceneDepth,
                  result.GBufferBaseColorMetallic, result.GBufferNormalRoughness, result.GBufferMaterial,
                  result.GBufferLighting, result.GBufferVelocity, result.DBufferBaseColor, result.DBufferNormal,
                  result.DBufferMaterial},
                 {result.HdrScene}});
            result.ForwardOpaqueTail =
                result.Graph.AddPass({"Forward-only opaque tail", opaqueInputs, {result.HdrScene}});
            result.Opaque = result.ForwardOpaqueTail;
        }

        result.ResolveDepth =
            result.Graph.AddPass({"Sampled scene depth", {uploads, result.HdrScene}, {result.SampledDepth}});
        result.Sky = result.Graph.AddPass({"Sky", {result.HdrScene}, {skyComplete}});
        result.Transparency =
            result.Graph.AddPass({"Transparency",
                                  {result.HdrScene, result.SampledDepth, skyComplete, result.ForwardPlusLightTiles,
                                   result.SpatialSelectionRecords, vfxPrepared},
                                  {transparencyComplete}});
        auto toneMapDependency = transparencyComplete;
        if (path == RenderPath::DeferredHybrid)
        {
            const auto irradynComplete =
                result.Graph.AddResource({"Irradyn complete", FrameGraphResourceKind::Buffer, true});
            result.IrradynTrace = result.Graph.AddPass(
                {"Irradyn trace and temporal accumulation",
                 {result.HdrScene, result.SceneDepth, result.GBufferBaseColorMetallic, result.GBufferNormalRoughness,
                  result.GBufferMaterial, result.GBufferVelocity, transparencyComplete},
                 {result.IrradynRadiance}});
            result.IrradynComposite =
                result.Graph.AddPass({"Irradyn bilateral composite",
                                      {result.IrradynRadiance, result.SceneDepth, result.GBufferBaseColorMetallic,
                                       result.GBufferNormalRoughness, result.GBufferMaterial},
                                      {result.HdrScene, irradynComplete}});
            toneMapDependency = irradynComplete;
        }
        result.ToneMap = result.Graph.AddPass(
            {"ACES tone map", {result.HdrScene, result.GBufferVelocity, toneMapDependency}, {toneMapped}});
        result.Overlays =
            result.Graph.AddPass({"Editor overlays",
                                  {toneMapped, result.GpuOcclusionPyramid, result.GpuOcclusionIndirectArguments},
                                  {overlays}});
        result.Readback = result.Graph.AddPass({"Readback", {overlays}, {readback}, FrameGraphPassKind::Transfer});
        result.Presentation =
            result.Graph.AddPass({"Presentation", {overlays}, {presentation}, FrameGraphPassKind::Present});
        result.Compiled = result.Graph.Compile();
        return result;
    }
} // namespace Keire::RenderBackend
