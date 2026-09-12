#include "KeireClient/Editor/ShaderGraphSaveState.h"
#include "KeireClient/Editor/GraphComments.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        const Keire::ShaderGraphNode* FindNode(const Keire::ShaderGraphDefinition& graph, Keire::AssetId id)
        {
            const auto found = std::ranges::find(graph.Nodes, id, &Keire::ShaderGraphNode::Id);
            return found == graph.Nodes.end() ? nullptr : &*found;
        }
    } // namespace
    void ShaderGraphSaveState::Stage(const ShaderGraphDocument& document, const Keire::ShaderGraphNode& node)
    {
        if (!document.IsOpen() || !FindNode(document.Definition(), node.Id))
            throw std::invalid_argument("Stage properties only for a node in the open Shader Graph.");
        if (Draft.Dirty && Draft.Node != node.Id)
            throw std::logic_error("Apply or discard the previous node properties before replacing the draft.");
        Draft.Node = node.Id;
        Draft.Name = node.Name;
        Draft.Symbol = node.Symbol;
        Draft.Include = node.Include.generic_string();
        Draft.Function = node.Function;
        Draft.Description = node.ParameterMetadata.Description;
        Draft.Category = node.ParameterMetadata.Category;
        Draft.HighDynamicRange = node.ParameterMetadata.HighDynamicRange;
        Draft.SortPriority = node.ParameterMetadata.SortPriority;
        Draft.HasMinimum = node.ParameterMetadata.Minimum.has_value();
        Draft.HasMaximum = node.ParameterMetadata.Maximum.has_value();
        Draft.HasStep = node.ParameterMetadata.Step.has_value();
        Draft.Minimum = node.ParameterMetadata.Minimum.value_or(0.0F);
        Draft.Maximum = node.ParameterMetadata.Maximum.value_or(1.0F);
        Draft.Step = node.ParameterMetadata.Step.value_or(0.01F);
        const auto annotation = std::ranges::find(document.Definition().Authoring.NodeAnnotations, node.Id,
                                                  &Keire::GraphNodeAnnotation::Node);
        Draft.Comment = annotation == document.Definition().Authoring.NodeAnnotations.end() ? "" : annotation->Text;
        Draft.CommentPinned = annotation != document.Definition().Authoring.NodeAnnotations.end() && annotation->Pinned;
        Draft.Dirty = true;
    }

    void ShaderGraphSaveState::Apply(ShaderGraphDocument& document)
    {
        if (!Draft.Dirty)
            return;
        const auto* node = Draft.Node ? FindNode(document.Definition(), *Draft.Node) : nullptr;
        if (!node)
            throw std::runtime_error("The edited node is no longer available.");
        if (!std::isfinite(Draft.SortPriority) || Draft.SortPriority < std::numeric_limits<std::int32_t>::min() ||
            Draft.SortPriority > std::numeric_limits<std::int32_t>::max())
            throw std::invalid_argument("Shader property sort priority is outside its supported range.");

        const auto nodeId = node->Id;
        const auto oldSymbol = node->Symbol;
        const auto kind = node->Kind;
        Keire::ShaderGraphParameterMetadata metadata;
        metadata.Description = Draft.Description;
        metadata.Category = Draft.Category;
        metadata.HighDynamicRange = Draft.HighDynamicRange;
        metadata.SortPriority = static_cast<std::int32_t>(std::round(Draft.SortPriority));
        if (Draft.HasMinimum)
            metadata.Minimum = static_cast<float>(Draft.Minimum);
        if (Draft.HasMaximum)
            metadata.Maximum = static_cast<float>(Draft.Maximum);
        if (Draft.HasStep)
            metadata.Step = static_cast<float>(Draft.Step);
        (void)document.Edit(
            "Edit Shader Graph node properties",
            [nodeId, oldSymbol, kind, name = Draft.Name, symbol = Draft.Symbol, include = Draft.Include,
             function = Draft.Function, metadata = std::move(metadata), comment = Draft.Comment,
             pinned = Draft.CommentPinned](auto& definition)
            {
                auto candidate = std::ranges::find(definition.Nodes, nodeId, &Keire::ShaderGraphNode::Id);
                if (candidate == definition.Nodes.end())
                    throw std::invalid_argument("Shader Graph node is unavailable.");
                candidate->Name = name;
                candidate->Symbol = symbol;
                candidate->Include = include;
                candidate->Function = function;
                candidate->ParameterMetadata = metadata;
                SetGraphNodeAnnotation(definition.Authoring, nodeId, comment, pinned);
                if (kind == Keire::ShaderGraphNodeKind::Keyword)
                {
                    auto keyword = std::ranges::find(definition.Keywords, oldSymbol, &Keire::ShaderGraphKeyword::Name);
                    if (keyword != definition.Keywords.end())
                        keyword->Name = symbol;
                }
            });
        Draft.Dirty = false;
    }

    void ShaderGraphSaveState::Request(const ShaderGraphDocument& document)
    {
        if (document.IsOpen())
            RequestedAsset = document.Asset();
    }

    void ShaderGraphSaveState::Update(ShaderGraphDocument& document, const std::function<void()>& save)
    {
        if (!RequestedAsset)
            return;
        if (!document.IsOpen() || *RequestedAsset != document.Asset())
        {
            RequestedAsset.reset();
            return;
        }
        try
        {
            Apply(document);
            if (document.CompilationPending())
                return;
            RequestedAsset.reset();
            save();
        }
        catch (...)
        {
            RequestedAsset.reset();
            throw;
        }
    }

    void ShaderGraphSaveState::Reset() noexcept
    {
        Draft = {};
        RequestedAsset.reset();
    }
} // namespace KeireEditor
