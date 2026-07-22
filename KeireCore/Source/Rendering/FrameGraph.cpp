#include "KeireInternal/Rendering/FrameGraphInternal.h"

#include <algorithm>
#include <deque>
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
        return result;
    }

    void FrameGraph::Clear() noexcept
    {
        m_Resources.clear();
        m_Passes.clear();
    }

    StaticSceneFrameGraph BuildStaticSceneFrameGraph()
    {
        StaticSceneFrameGraph result;
        const auto uploads = result.Graph.AddResource({"Uploaded resources", FrameGraphResourceKind::Buffer});
        const auto shadows = result.Graph.AddResource({"Directional shadows", FrameGraphResourceKind::Texture});
        const auto lightTiles = result.Graph.AddResource({"Forward+ tile lists", FrameGraphResourceKind::Buffer});
        const auto opaque = result.Graph.AddResource({"HDR opaque scene", FrameGraphResourceKind::Texture});
        const auto sky = result.Graph.AddResource({"HDR scene and sky", FrameGraphResourceKind::Texture});
        const auto transparent =
            result.Graph.AddResource({"HDR scene and transparency", FrameGraphResourceKind::Texture});
        const auto toneMapped = result.Graph.AddResource({"Tone-mapped scene", FrameGraphResourceKind::Texture});
        const auto overlays = result.Graph.AddResource({"Scene overlays", FrameGraphResourceKind::Texture});
        const auto readback = result.Graph.AddResource({"Readback", FrameGraphResourceKind::Buffer});
        const auto presentation = result.Graph.AddResource({"Presentation", FrameGraphResourceKind::Texture, true});

        (void)result.Graph.AddPass({"Resource uploads", {}, {uploads}});
        (void)result.Graph.AddPass({"Directional shadow maps", {uploads}, {shadows}});
        (void)result.Graph.AddPass({"Forward+ light culling", {uploads}, {lightTiles}});
        (void)result.Graph.AddPass({"Opaque and mask", {uploads, shadows, lightTiles}, {opaque}});
        (void)result.Graph.AddPass({"Sky", {opaque}, {sky}});
        (void)result.Graph.AddPass({"Transparency", {sky, lightTiles}, {transparent}});
        (void)result.Graph.AddPass({"ACES tone map", {transparent}, {toneMapped}});
        (void)result.Graph.AddPass({"Editor overlays", {toneMapped}, {overlays}});
        (void)result.Graph.AddPass({"Readback", {overlays}, {readback}});
        (void)result.Graph.AddPass({"Presentation", {overlays}, {presentation}});
        result.Compiled = result.Graph.Compile();
        return result;
    }
} // namespace Keire::RenderBackend
