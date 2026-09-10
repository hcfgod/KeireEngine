#include "KeireClient/Editor/GraphClipboard.h"

#include "KeireClient/Editor/GraphDuplication.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>

namespace KeireEditor
{
    namespace
    {
        using Json = nlohmann::ordered_json;

        constexpr std::string_view FragmentFormat = "keire.graph-fragment";
        constexpr std::uint32_t FragmentVersion = 1;

        [[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text)
        {
            std::vector<std::byte> result(text.size());
            std::memcpy(result.data(), text.data(), text.size());
            return result;
        }

        [[nodiscard]] std::vector<Keire::AssetId> DecodeSelection(const Json& document)
        {
            const auto& encoded = document.at("selection");
            if (!encoded.is_array() || encoded.empty() || encoded.size() > 4096U)
                throw std::invalid_argument("Graph fragment selection is empty or exceeds its limit.");
            std::vector<Keire::AssetId> result;
            result.reserve(encoded.size());
            for (const auto& value : encoded)
                result.push_back(Keire::AssetId::Parse(value.get<std::string>()));
            return result;
        }

        [[nodiscard]] Json DecodeEnvelope(const std::string_view fragment, const std::string_view expectedKind)
        {
            if (fragment.empty() || fragment.size() > MaximumGraphFragmentBytes)
                throw std::invalid_argument("Graph fragment is empty or exceeds the 1 MiB clipboard limit.");
            Json document;
            try
            {
                document = Json::parse(fragment);
            }
            catch (const Json::exception& error)
            {
                throw std::invalid_argument(std::string("Graph fragment JSON is malformed: ") + error.what());
            }
            if (!document.is_object() || document.value("format", std::string{}) != FragmentFormat ||
                document.value("version", 0U) != FragmentVersion)
                throw std::invalid_argument("Clipboard text is not a supported K\u00e9ire graph fragment.");
            if (document.value("kind", std::string{}) != expectedKind)
                throw std::invalid_argument("Graph fragment belongs to a different graph editor.");
            if (!document.contains("source") || !document.at("source").is_object())
                throw std::invalid_argument("Graph fragment source is missing.");
            return document;
        }

        [[nodiscard]] std::string EncodeEnvelope(const std::string_view kind, const Json& source,
                                                 const std::span<const Keire::AssetId> selection)
        {
            if (selection.empty() || selection.size() > 4096U)
                throw std::invalid_argument("Graph fragment selection is empty or exceeds its limit.");
            Json encodedSelection = Json::array();
            for (const auto id : selection)
                encodedSelection.push_back(id.ToString());
            const Json document{{"format", FragmentFormat},
                                {"version", FragmentVersion},
                                {"kind", kind},
                                {"selection", std::move(encodedSelection)},
                                {"source", source}};
            const auto result = document.dump();
            if (result.size() > MaximumGraphFragmentBytes)
                throw std::invalid_argument("Graph fragment exceeds the 1 MiB clipboard limit.");
            return result;
        }

        [[nodiscard]] std::vector<Keire::AssetId> ExistingSelection(const std::span<const Keire::AssetId> selection,
                                                                    const std::span<const Keire::ShaderGraphNode> nodes,
                                                                    const bool excludeMaster)
        {
            std::vector<Keire::AssetId> result;
            for (const auto id : selection)
            {
                const auto found = std::ranges::find(nodes, id, &Keire::ShaderGraphNode::Id);
                if (found != nodes.end() && (!excludeMaster || found->Kind != Keire::ShaderGraphNodeKind::Master) &&
                    std::ranges::find(result, id) == result.end())
                    result.push_back(id);
            }
            return result;
        }

        [[nodiscard]] Json SourceJson(const std::span<const std::byte> source)
        {
            return Json::parse(reinterpret_cast<const char*>(source.data()),
                               reinterpret_cast<const char*>(source.data() + source.size()));
        }

        void AppendFragmentAuthoring(Keire::GraphAuthoringMetadata& target, const Keire::GraphAuthoringMetadata& source,
                                     const std::set<Keire::AssetId>& selected)
        {
            for (const auto& annotation : source.NodeAnnotations)
                if (selected.contains(annotation.Node))
                    target.NodeAnnotations.push_back(annotation);

            auto included = selected;
            bool changed = true;
            while (changed)
            {
                changed = false;
                for (const auto& comment : source.Comments)
                    if (!comment.Members.empty() && !included.contains(comment.Id) &&
                        std::ranges::all_of(comment.Members,
                                            [&](const Keire::AssetId id) { return included.contains(id); }))
                        changed = included.insert(comment.Id).second || changed;
            }
            for (const auto& comment : source.Comments)
                if (included.contains(comment.Id))
                    target.Comments.push_back(comment);
        }
    } // namespace

    std::string CopyShaderGraphFragment(const Keire::ShaderGraphDefinition& definition,
                                        const std::span<const Keire::AssetId> selection)
    {
        const auto existing = ExistingSelection(selection, definition.Nodes, true);
        return EncodeEnvelope("shader", SourceJson(Keire::ShaderGraphAsset::EncodeSource(definition)), existing);
    }

    std::string CopyMaterialGraphFragment(const Keire::MaterialGraphDefinition& definition,
                                          const std::span<const Keire::AssetId> selection)
    {
        std::set<Keire::AssetId> available;
        for (const auto& node : definition.Nodes)
            available.insert(node.Id);
        for (const auto& node : definition.SurfaceGraph.Nodes)
            if (node.Kind != Keire::ShaderGraphNodeKind::Master)
                available.insert(node.Id);
        std::vector<Keire::AssetId> existing;
        for (const auto id : selection)
            if (available.contains(id) && std::ranges::find(existing, id) == existing.end())
                existing.push_back(id);
        return EncodeEnvelope("material", SourceJson(Keire::MaterialGraphAsset::EncodeSource(definition)), existing);
    }

    std::string CopyVfxGraphFragment(const Keire::VfxEffectDefinition& definition, const Keire::AssetId system,
                                     const std::span<const Keire::AssetId> selection)
    {
        const auto foundSystem = std::ranges::find(definition.Systems, system, &Keire::VfxGraphSystem::Id);
        if (foundSystem == definition.Systems.end())
            throw std::invalid_argument("VFX graph system does not exist.");
        std::vector<Keire::AssetId> existing;
        for (const auto id : selection)
        {
            const auto node = std::ranges::find(foundSystem->Nodes, id, &Keire::VfxGraphNode::Id);
            if (node != foundSystem->Nodes.end() && node->Kind != Keire::VfxGraphNodeKind::Context &&
                std::ranges::find(existing, id) == existing.end())
                existing.push_back(id);
        }
        return EncodeEnvelope("vfx", SourceJson(Keire::VfxEffectAsset::Encode(definition)), existing);
    }

    std::vector<Keire::AssetId> PasteShaderGraphFragment(Keire::ShaderGraphDefinition& definition,
                                                         const std::string_view fragment, const Keire::Vector2 offset)
    {
        const auto document = DecodeEnvelope(fragment, "shader");
        const auto selection = DecodeSelection(document);
        const auto sourceText = document.at("source").dump();
        auto source = Keire::ShaderGraphAsset::DecodeSource(Bytes(sourceText));
        const auto copied =
            DuplicateShaderGraphSelection(source, selection, offset, GraphParameterDuplication::PreserveSymbols);
        if (copied.empty())
            throw std::invalid_argument("Graph fragment contains no editable Shader Graph nodes.");
        const auto appendedSelection = copied;
        const auto sourceNodes = source.Nodes;
        const auto sourceConnections = source.Connections;
        const auto sourceAuthoring = source.Authoring;
        Keire::ShaderGraphDefinition transfer = definition;
        for (const auto id : appendedSelection)
        {
            const auto node = std::ranges::find(sourceNodes, id, &Keire::ShaderGraphNode::Id);
            if (node != sourceNodes.end())
                transfer.Nodes.push_back(*node);
        }
        const std::set selected(appendedSelection.begin(), appendedSelection.end());
        for (const auto& connection : sourceConnections)
            if (selected.contains(connection.Output.Node) && selected.contains(connection.Input.Node))
                transfer.Connections.push_back(connection);
        AppendFragmentAuthoring(transfer.Authoring, sourceAuthoring, selected);
        ResolveGraphParameterSymbols(transfer, appendedSelection);
        Keire::ValidateShaderGraph(transfer);
        definition = std::move(transfer);
        return appendedSelection;
    }

    std::vector<Keire::AssetId> PasteMaterialGraphFragment(Keire::MaterialGraphDefinition& definition,
                                                           const std::string_view fragment, const Keire::Vector2 offset)
    {
        const auto document = DecodeEnvelope(fragment, "material");
        const auto selection = DecodeSelection(document);
        const auto sourceText = document.at("source").dump();
        auto source = Keire::MaterialGraphAsset::DecodeSource(Bytes(sourceText));
        const auto copied =
            DuplicateMaterialGraphSelection(source, selection, offset, GraphParameterDuplication::PreserveSymbols);
        if (copied.empty())
            throw std::invalid_argument("Graph fragment contains no editable Material Graph nodes.");

        const auto sourceValueNodes = source.Nodes;
        const auto sourceExpressionNodes = source.SurfaceGraph.Nodes;
        const auto sourceConnections = source.Connections;
        const auto sourceExpressionConnections = source.SurfaceGraph.Connections;
        const std::set selected(copied.begin(), copied.end());
        auto transfer = definition;
        for (const auto& node : sourceValueNodes)
            if (selected.contains(node.Id))
                transfer.Nodes.push_back(node);
        for (const auto& node : sourceExpressionNodes)
            if (selected.contains(node.Id))
                transfer.SurfaceGraph.Nodes.push_back(node);
        for (const auto& connection : sourceConnections)
            if (selected.contains(connection.Output.Node) && selected.contains(connection.Input.Node))
                transfer.Connections.push_back(connection);
        for (const auto& connection : sourceExpressionConnections)
            if (selected.contains(connection.Output.Node) && selected.contains(connection.Input.Node))
                transfer.SurfaceGraph.Connections.push_back(connection);
        AppendFragmentAuthoring(transfer.Authoring, source.Authoring, selected);
        AppendFragmentAuthoring(transfer.SurfaceGraph.Authoring, source.SurfaceGraph.Authoring, selected);
        ResolveGraphParameterSymbols(transfer.SurfaceGraph, copied);
        Keire::ValidateMaterialGraph(transfer);
        definition = std::move(transfer);
        return copied;
    }

    std::vector<Keire::AssetId> PasteVfxGraphFragment(Keire::VfxEffectDefinition& definition,
                                                      const Keire::AssetId system, const std::string_view fragment,
                                                      const Keire::Vector2 offset)
    {
        const auto document = DecodeEnvelope(fragment, "vfx");
        const auto selection = DecodeSelection(document);
        const auto sourceText = document.at("source").dump();
        auto source = Keire::VfxEffectAsset::Decode(Bytes(sourceText))->Definition();
        const auto sourceSystem =
            std::ranges::find_if(source.Systems,
                                 [&](const Keire::VfxGraphSystem& candidate)
                                 {
                                     return std::ranges::any_of(
                                         selection,
                                         [&](const Keire::AssetId id)
                                         {
                                             return std::ranges::find(candidate.Nodes, id, &Keire::VfxGraphNode::Id) !=
                                                    candidate.Nodes.end();
                                         });
                                 });
        if (sourceSystem == source.Systems.end())
            throw std::invalid_argument("Graph fragment VFX system is unavailable.");
        const auto copied = DuplicateVfxGraphSelection(source, sourceSystem->Id, selection, offset);
        if (copied.empty())
            throw std::invalid_argument("Graph fragment contains no editable VFX nodes.");
        auto target = std::ranges::find(definition.Systems, system, &Keire::VfxGraphSystem::Id);
        if (target == definition.Systems.end())
            throw std::invalid_argument("VFX graph system does not exist.");
        const std::set selected(copied.begin(), copied.end());
        for (const auto& node : sourceSystem->Nodes)
            if (selected.contains(node.Id))
                target->Nodes.push_back(node);
        for (const auto& connection : sourceSystem->Connections)
            if (selected.contains(connection.OutputNode) && selected.contains(connection.InputNode))
                target->Connections.push_back(connection);
        AppendFragmentAuthoring(target->Authoring, sourceSystem->Authoring, selected);
        return copied;
    }
} // namespace KeireEditor
