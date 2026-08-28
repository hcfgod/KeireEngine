#include "KeireInternal/Rendering/FrameGraphInternal.h"

#include <algorithm>
#include <deque>
#include <optional>
#include <stdexcept>

namespace Keire::RenderBackend
{
    FrameGraphResource FrameGraph::AddResource(FrameGraphResourceDescription description)
    {
        if (description.Name.empty())
            throw std::invalid_argument("Frame-graph resources require a diagnostic name.");
        if (std::ranges::any_of(m_Resources, [&](const auto& resource) { return resource.Name == description.Name; }))
            throw std::invalid_argument("Frame-graph resource names must be unique.");
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
                    break;
                }
            }
            if (!selected)
            {
                selected = static_cast<std::uint32_t>(result.TransientAllocations.size());
                result.TransientAllocations.push_back({resource.Kind, resource.CompatibilityKey, resource.SizeBytes});
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

    StaticSceneFrameGraph BuildStaticSceneFrameGraph()
    {
        StaticSceneFrameGraph result;
        const auto uploads = result.Graph.AddResource({"Uploaded resources", FrameGraphResourceKind::Buffer, true});
        const auto shadows = result.Graph.AddResource({"Directional shadows", FrameGraphResourceKind::Texture, true});
        result.ForwardPlusLightTiles =
            result.Graph.AddResource({"Forward+ tile lists", FrameGraphResourceKind::Buffer, true});
        result.GpuOcclusionDepth = result.Graph.AddResource({"Occlusion depth", FrameGraphResourceKind::Texture, true});
        result.GpuOcclusionPyramid =
            result.Graph.AddResource({"Occlusion depth pyramid", FrameGraphResourceKind::Texture, true});
        result.GpuOcclusionIndirectArguments =
            result.Graph.AddResource({"Occlusion indirect arguments", FrameGraphResourceKind::Buffer, true});
        result.GpuVisibilityMasks =
            result.Graph.AddResource({"Frame-owned visibility masks", FrameGraphResourceKind::Buffer, true});
        result.HdrScene = result.Graph.AddResource({"HDR scene color", FrameGraphResourceKind::Texture, false, 4});
        result.SampledDepth = result.Graph.AddResource({"Sampled scene depth", FrameGraphResourceKind::Texture, true});
        const auto skyComplete = result.Graph.AddResource({"Sky complete", FrameGraphResourceKind::Buffer, true});
        const auto transparencyComplete =
            result.Graph.AddResource({"Transparency complete", FrameGraphResourceKind::Buffer, true});
        const auto vfxPrepared =
            result.Graph.AddResource({"VFX expansion complete", FrameGraphResourceKind::Buffer, true});
        const auto toneMapped = result.Graph.AddResource({"Tone-mapped scene", FrameGraphResourceKind::Texture, true});
        const auto overlays = result.Graph.AddResource({"Scene overlays", FrameGraphResourceKind::Texture, true});
        const auto readback = result.Graph.AddResource({"Readback", FrameGraphResourceKind::Buffer, true});
        const auto presentation = result.Graph.AddResource({"Presentation", FrameGraphResourceKind::Texture, true});

        result.ResourceUploads = result.Graph.AddPass({"Resource uploads", {}, {uploads}, FrameGraphPassKind::Upload});
        result.DirectionalShadows =
            result.Graph.AddPass({"Directional shadow maps", {uploads}, {shadows}, FrameGraphPassKind::Graphics});
        result.GpuOcclusionDepthPass = result.Graph.AddPass(
            {"Occlusion depth", {uploads}, {result.GpuOcclusionDepth}, FrameGraphPassKind::Graphics});
        result.GpuOcclusionPyramidPass = result.Graph.AddPass({"Occlusion depth pyramid",
                                                               {result.GpuOcclusionDepth},
                                                               {result.GpuOcclusionPyramid},
                                                               FrameGraphPassKind::Compute});
        result.GpuOcclusionCullingPass =
            result.Graph.AddPass({"GPU occlusion culling",
                                  {uploads, result.GpuOcclusionPyramid},
                                  {result.GpuOcclusionIndirectArguments, result.GpuVisibilityMasks},
                                  FrameGraphPassKind::Compute});
        result.ForwardPlusCulling = result.Graph.AddPass({"Forward+ light culling",
                                                          {uploads, result.GpuVisibilityMasks},
                                                          {result.ForwardPlusLightTiles},
                                                          FrameGraphPassKind::Compute});
        result.VfxPreparation =
            result.Graph.AddPass({"VFX expansion",
                                  {uploads, result.GpuVisibilityMasks, result.ForwardPlusLightTiles},
                                  {vfxPrepared},
                                  FrameGraphPassKind::Compute});
        result.Opaque = result.Graph.AddPass(
            {"Opaque and mask",
             {uploads, shadows, result.ForwardPlusLightTiles, result.GpuOcclusionIndirectArguments},
             {result.HdrScene}});
        result.ResolveDepth =
            result.Graph.AddPass({"Sampled scene depth", {uploads, result.HdrScene}, {result.SampledDepth}});
        result.Sky = result.Graph.AddPass({"Sky", {result.HdrScene}, {skyComplete}});
        result.Transparency = result.Graph.AddPass(
            {"Transparency",
             {result.HdrScene, result.SampledDepth, skyComplete, result.ForwardPlusLightTiles, vfxPrepared},
             {transparencyComplete}});
        result.ToneMap = result.Graph.AddPass({"ACES tone map", {result.HdrScene, transparencyComplete}, {toneMapped}});
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
