#include "Keire/Vfx/VfxSubgraph.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <memory>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::size_t MaximumDocumentBytes = 4U * 1024U * 1024U;
        constexpr std::size_t MaximumPorts = 256;
        constexpr std::size_t MaximumContexts = 8;
        constexpr std::size_t MaximumNameBytes = 256;

        [[nodiscard]] std::string_view PurposeName(const VfxSubgraphPurpose purpose)
        {
            switch (purpose)
            {
            case VfxSubgraphPurpose::Operator:
                return "operator";
            case VfxSubgraphPurpose::Block:
                return "block";
            case VfxSubgraphPurpose::System:
                return "system";
            }
            throw std::invalid_argument("VFX subgraph purpose is invalid.");
        }

        [[nodiscard]] VfxSubgraphPurpose ParsePurpose(const std::string_view value)
        {
            if (value == "operator")
                return VfxSubgraphPurpose::Operator;
            if (value == "block")
                return VfxSubgraphPurpose::Block;
            if (value == "system")
                return VfxSubgraphPurpose::System;
            throw std::runtime_error("VFX subgraph purpose is invalid.");
        }

        void CollectIdentities(const VfxSubgraphDefinition& definition, std::set<AssetId>& used)
        {
            used.insert(definition.Id);
            used.insert(definition.Graph.Id);
            for (const auto& module : definition.Modules)
                used.insert(module.Id);
            for (const auto& parameter : definition.Parameters)
                used.insert(parameter.Id);
            for (const auto& node : definition.Graph.Nodes)
            {
                used.insert(node.Id);
                for (const auto& pin : node.Pins)
                    used.insert(pin.Id);
                for (const auto& block : node.Blocks)
                {
                    used.insert(block.Id);
                    for (const auto& pin : block.Pins)
                        used.insert(pin.Id);
                }
            }
            for (const auto& connection : definition.Graph.Connections)
                used.insert(connection.Id);
            for (const auto& port : definition.Ports)
                used.insert(port.Id);
        }

        [[nodiscard]] AssetId DerivedIdentity(const AssetId seed, std::uint64_t salt, std::set<AssetId>& used)
        {
            AssetId candidate(seed.High() ^ 0x5355424752415048ULL, seed.Low() + salt);
            while (!candidate || used.contains(candidate))
                candidate = AssetId(candidate.High(), candidate.Low() + 1);
            used.insert(candidate);
            return candidate;
        }

        [[nodiscard]] VfxEffectDefinition Carrier(const VfxSubgraphDefinition& definition, const bool executable)
        {
            std::set<AssetId> used;
            CollectIdentities(definition, used);
            auto carrier = VfxEffectAsset::DefaultDefinition();
            const auto defaultModules = carrier.Modules;
            carrier.EmitterId = DerivedIdentity(definition.Id, 1, used);
            carrier.Name = definition.Name;
            carrier.Systems = {definition.Graph};
            carrier.Blackboard = definition.Parameters;
            carrier.Modules = definition.Modules;
            std::uint64_t salt = 2;
            for (const auto& module : defaultModules)
            {
                const bool required = std::holds_alternative<VfxEmissionRateModule>(module.Payload) ||
                                      std::holds_alternative<VfxRendererModule>(module.Payload);
                if (!required)
                    continue;
                auto filler = module;
                filler.Id = DerivedIdentity(definition.Id, salt++, used);
                filler.Enabled = true;
                carrier.Modules.push_back(std::move(filler));
            }
            carrier.ExecutionSource = executable ? VfxExecutionSource::Graph : VfxExecutionSource::LegacyModules;
            carrier.CompatibilityMode = VfxCompatibilityMode::NativeSchema4;
            return carrier;
        }

        [[nodiscard]] const VfxGraphPin* FindPortPin(const VfxGraphSystem& graph, const VfxSubgraphPort& port)
        {
            const auto node = std::ranges::find(graph.Nodes, port.Node, &VfxGraphNode::Id);
            if (node == graph.Nodes.end())
                return nullptr;
            if (!port.Block)
            {
                const auto pin = std::ranges::find(node->Pins, port.Pin, &VfxGraphPin::Id);
                return pin == node->Pins.end() ? nullptr : std::addressof(*pin);
            }
            const auto block = std::ranges::find(node->Blocks, port.Block, &VfxGraphBlock::Id);
            if (block == node->Blocks.end())
                return nullptr;
            const auto pin = std::ranges::find(block->Pins, port.Pin, &VfxGraphPin::Id);
            return pin == block->Pins.end() ? nullptr : std::addressof(*pin);
        }

        [[nodiscard]] Json EncodePorts(const std::span<const VfxSubgraphPort> ports)
        {
            auto result = Json::array();
            for (const auto& port : ports)
                result.push_back({{"id", port.Id.ToString()},
                                  {"name", port.Name},
                                  {"type", static_cast<std::uint8_t>(port.Type)},
                                  {"input", port.Input},
                                  {"node", port.Node.ToString()},
                                  {"block", port.Block ? Json(port.Block.ToString()) : Json(nullptr)},
                                  {"pin", port.Pin.ToString()}});
            return result;
        }

        [[nodiscard]] std::vector<VfxSubgraphPort> DecodePorts(const Json& document)
        {
            const auto& ports = document.at("subgraphPorts");
            if (!ports.is_array() || ports.size() > MaximumPorts)
                throw std::runtime_error("VFX subgraph ports are malformed or exceed their limit.");
            std::vector<VfxSubgraphPort> result;
            result.reserve(ports.size());
            for (const auto& encoded : ports)
            {
                const auto type = encoded.at("type").get<std::uint32_t>();
                if (type > static_cast<std::uint32_t>(VfxValueType::SignedDistanceField))
                    throw std::runtime_error("VFX subgraph port type is invalid.");
                VfxSubgraphPort port;
                port.Id = AssetId::Parse(encoded.at("id").get<std::string>());
                port.Name = encoded.at("name").get<std::string>();
                port.Type = static_cast<VfxValueType>(type);
                port.Input = encoded.at("input").get<bool>();
                port.Node = AssetId::Parse(encoded.at("node").get<std::string>());
                if (const auto& block = encoded.at("block"); !block.is_null())
                    port.Block = AssetId::Parse(block.get<std::string>());
                port.Pin = AssetId::Parse(encoded.at("pin").get<std::string>());
                result.push_back(std::move(port));
            }
            return result;
        }

        [[nodiscard]] std::vector<VfxContextType> DecodeContexts(const Json& document)
        {
            const auto& contexts = document.at("subgraphValidContexts");
            if (!contexts.is_array() || contexts.size() > MaximumContexts)
                throw std::runtime_error("VFX subgraph Context list is malformed or exceeds its limit.");
            std::vector<VfxContextType> result;
            result.reserve(contexts.size());
            for (const auto& encoded : contexts)
            {
                const auto value = encoded.get<std::uint32_t>();
                if (value > static_cast<std::uint32_t>(VfxContextType::Event))
                    throw std::runtime_error("VFX subgraph Context is invalid.");
                result.push_back(static_cast<VfxContextType>(value));
            }
            return result;
        }
    } // namespace

    void ValidateVfxSubgraph(const VfxSubgraphDefinition& definition)
    {
        if (definition.SchemaVersion != VfxSubgraphSchemaVersion || !definition.Id || definition.Name.empty() ||
            definition.Name.size() > MaximumNameBytes || definition.Purpose > VfxSubgraphPurpose::System ||
            !definition.Graph.Id || definition.Graph.Name.empty() || definition.Ports.size() > MaximumPorts ||
            definition.ValidContexts.size() > MaximumContexts)
            throw std::invalid_argument("VFX subgraph header is invalid.");

        auto carrier = Carrier(definition, definition.Purpose == VfxSubgraphPurpose::System);
        if (definition.Purpose == VfxSubgraphPurpose::System)
            ValidateVfxEffect(carrier);
        else
            ValidateVfxEffectAuthoring(carrier);

        std::set<AssetId> portIds;
        std::set<std::pair<bool, std::string>> portNames;
        for (const auto& port : definition.Ports)
        {
            const auto* pin = FindPortPin(definition.Graph, port);
            if (!port.Id || !portIds.insert(port.Id).second || port.Name.empty() ||
                port.Name.size() > MaximumNameBytes || !portNames.emplace(port.Input, port.Name).second || !pin ||
                pin->Type != port.Type || pin->Input != port.Input)
                throw std::invalid_argument("VFX subgraph contains an invalid typed boundary port.");
        }

        std::set<VfxContextType> contexts;
        for (const auto context : definition.ValidContexts)
            if (context > VfxContextType::Event || !contexts.insert(context).second)
                throw std::invalid_argument("VFX subgraph contains an invalid or duplicate Context declaration.");

        if (definition.Purpose == VfxSubgraphPurpose::Operator)
        {
            if (!definition.ValidContexts.empty() ||
                std::ranges::any_of(
                    definition.Graph.Nodes, [](const auto& node)
                    { return node.Kind == VfxGraphNodeKind::Context || node.Kind == VfxGraphNodeKind::Module; }))
                throw std::invalid_argument("Operator VFX subgraphs may contain only value-graph nodes.");
        }
        else if (definition.Purpose == VfxSubgraphPurpose::Block)
        {
            const bool hasBlock = std::ranges::any_of(definition.Graph.Nodes,
                                                      [](const VfxGraphNode& node) { return !node.Blocks.empty(); });
            if (!hasBlock || definition.ValidContexts.empty() ||
                std::ranges::any_of(
                    definition.Graph.Nodes, [&](const VfxGraphNode& node)
                    { return node.Kind == VfxGraphNodeKind::Context && !contexts.contains(node.Context); }))
                throw std::invalid_argument("Block VFX subgraph Context declarations do not match its Blocks.");
        }

        const bool recursiveNode =
            std::ranges::any_of(definition.Graph.Nodes, [&](const VfxGraphNode& node)
                                { return node.Kind == VfxGraphNodeKind::Subgraph && node.Reference == definition.Id; });
        const bool recursiveBlock = std::ranges::any_of(
            definition.Graph.Nodes,
            [&](const VfxGraphNode& node)
            {
                return std::ranges::any_of(
                    node.Blocks, [&](const VfxGraphBlock& block)
                    { return block.TypeId.View() == "keire.block.subgraph" && block.Reference == definition.Id; });
            });
        if (recursiveNode || recursiveBlock)
            throw std::invalid_argument("VFX subgraph directly references itself.");
    }

    std::vector<AssetId> VfxSubgraphDependencies(const VfxSubgraphDefinition& definition)
    {
        auto carrier = Carrier(definition, false);
        auto result = VfxEffectDependencies(carrier);
        std::erase(result, definition.Id);
        return result;
    }

    VfxGraphNode CreateVfxSubgraphNode(const VfxSubgraphDefinition& definition, const Vector2 editorPosition)
    {
        ValidateVfxSubgraph(definition);
        if (definition.Purpose == VfxSubgraphPurpose::Block)
            throw std::invalid_argument("Block VFX Subgraphs must be created as ordered Blocks.");
        VfxGraphNode result;
        result.Id = AssetId::Generate();
        result.Type = definition.Name;
        result.EditorPosition = editorPosition;
        result.Kind = VfxGraphNodeKind::Subgraph;
        result.Reference = definition.Id;
        result.TypeId.Value = "keire.subgraph";
        result.Pins.reserve(definition.Ports.size());
        for (const auto& port : definition.Ports)
        {
            const auto* pin = FindPortPin(definition.Graph, port);
            result.Pins.push_back({AssetId::Generate(), port.Name, port.Type, port.Input, port.Name,
                                   port.Input && pin ? pin->DefaultValue : std::nullopt});
        }
        return result;
    }

    VfxGraphBlock CreateVfxSubgraphBlock(const VfxSubgraphDefinition& definition)
    {
        ValidateVfxSubgraph(definition);
        if (definition.Purpose != VfxSubgraphPurpose::Block)
            throw std::invalid_argument("Only Block VFX Subgraphs can be created as ordered Blocks.");
        VfxGraphBlock result;
        result.Id = AssetId::Generate();
        result.TypeId.Value = "keire.block.subgraph";
        result.Type = definition.Name;
        result.Reference = definition.Id;
        result.Pins.reserve(definition.Ports.size());
        for (const auto& port : definition.Ports)
        {
            const auto* pin = FindPortPin(definition.Graph, port);
            result.Pins.push_back({AssetId::Generate(), port.Name, port.Type, port.Input, port.Name,
                                   port.Input && pin ? pin->DefaultValue : std::nullopt});
        }
        return result;
    }

    VfxSubgraphAsset::VfxSubgraphAsset(VfxSubgraphDefinition definition) : m_Definition(std::move(definition))
    {
        ValidateVfxSubgraph(m_Definition);
    }

    std::size_t VfxSubgraphAsset::ResidentBytes() const noexcept
    {
        std::size_t result = sizeof(*this) + m_Definition.Name.size() +
                             m_Definition.Ports.size() * sizeof(VfxSubgraphPort) +
                             m_Definition.Parameters.size() * sizeof(VfxBlackboardParameter) +
                             m_Definition.Modules.size() * sizeof(VfxModuleDefinition);
        for (const auto& port : m_Definition.Ports)
            result += port.Name.size();
        return result;
    }

    VfxSubgraphDefinition VfxSubgraphAsset::DefaultDefinition()
    {
        VfxSubgraphDefinition result;
        result.Id = AssetId(0x5646585355424752ULL, 1);
        result.Name = "Time Operator";
        result.Graph.Id = AssetId(0x5646585355424752ULL, 2);
        result.Graph.Name = "Operator";
        result.Graph.Nodes.push_back(CreateVfxGraphOperatorNode("keire.operator.time"));
        const auto& node = result.Graph.Nodes.front();
        const auto output = std::ranges::find(node.Pins, false, &VfxGraphPin::Input);
        if (output == node.Pins.end())
            throw std::logic_error("Default VFX time Operator has no output pin.");
        result.Ports.push_back(
            {AssetId(0x5646585355424752ULL, 3), "Time", output->Type, false, node.Id, {}, output->Id});
        return result;
    }

    Ref<VfxSubgraphAsset> VfxSubgraphAsset::Default() { return CreateRef<VfxSubgraphAsset>(DefaultDefinition()); }

    Ref<VfxSubgraphAsset> VfxSubgraphAsset::Decode(const std::span<const std::byte> bytes)
    {
        if (bytes.empty() || bytes.size() > MaximumDocumentBytes)
            throw std::runtime_error("VFX subgraph asset is empty or exceeds the 4 MiB safety limit.");
        try
        {
            const auto document = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                              reinterpret_cast<const char*>(bytes.data() + bytes.size()));
            if (!document.is_object() || document.value("subgraphSchemaVersion", 0U) != VfxSubgraphSchemaVersion)
                throw std::runtime_error("VFX subgraph asset has an unsupported schema.");
            const auto carrier = VfxEffectAsset::Decode(bytes)->Definition();
            if (carrier.Systems.size() != 1)
                throw std::runtime_error("VFX subgraph must contain exactly one graph body.");

            VfxSubgraphDefinition definition;
            definition.Id = AssetId::Parse(document.at("subgraphId").get<std::string>());
            definition.Name = document.at("subgraphName").get<std::string>();
            definition.Purpose = ParsePurpose(document.at("subgraphPurpose").get<std::string>());
            definition.Graph = carrier.Systems.front();
            definition.Parameters = carrier.Blackboard;
            definition.Ports = DecodePorts(document);
            definition.ValidContexts = DecodeContexts(document);
            std::set<AssetId> moduleIds;
            const auto& encodedModules = document.at("subgraphModuleIds");
            if (!encodedModules.is_array() || encodedModules.size() > carrier.Modules.size())
                throw std::runtime_error("VFX subgraph module identity list is malformed.");
            for (const auto& encoded : encodedModules)
                moduleIds.insert(AssetId::Parse(encoded.get<std::string>()));
            for (const auto& module : carrier.Modules)
                if (moduleIds.contains(module.Id))
                    definition.Modules.push_back(module);
            if (definition.Modules.size() != moduleIds.size())
                throw std::runtime_error("VFX subgraph module identity is unavailable.");
            return CreateRef<VfxSubgraphAsset>(std::move(definition));
        }
        catch (const Json::exception& error)
        {
            throw std::runtime_error(std::string("VFX subgraph asset JSON is malformed: ") + error.what());
        }
    }

    std::vector<std::byte> VfxSubgraphAsset::Encode(const VfxSubgraphDefinition& definition)
    {
        ValidateVfxSubgraph(definition);
        const auto encodedCarrier =
            VfxEffectAsset::Encode(Carrier(definition, definition.Purpose == VfxSubgraphPurpose::System));
        auto document = Json::parse(reinterpret_cast<const char*>(encodedCarrier.data()),
                                    reinterpret_cast<const char*>(encodedCarrier.data() + encodedCarrier.size()));
        document["subgraphSchemaVersion"] = VfxSubgraphSchemaVersion;
        document["subgraphId"] = definition.Id.ToString();
        document["subgraphName"] = definition.Name;
        document["subgraphPurpose"] = PurposeName(definition.Purpose);
        document["subgraphPorts"] = EncodePorts(definition.Ports);
        auto contexts = Json::array();
        for (const auto context : definition.ValidContexts)
            contexts.push_back(static_cast<std::uint8_t>(context));
        document["subgraphValidContexts"] = std::move(contexts);
        auto modules = Json::array();
        for (const auto& module : definition.Modules)
            modules.push_back(module.Id.ToString());
        document["subgraphModuleIds"] = std::move(modules);
        const auto text = document.dump(2);
        std::vector<std::byte> result(text.size());
        std::memcpy(result.data(), text.data(), text.size());
        return result;
    }

    AssetImporterRegistration CreateVfxSubgraphAssetImporter()
    {
        AssetImporterRegistration result;
        result.Name = "Keire.VfxSubgraph";
        result.Version = VfxSubgraphSchemaVersion;
        result.Type = VfxSubgraphAsset::StaticType();
        result.Extensions = {".keirevfxsubgraph"};
        result.Import = [](const std::span<const std::byte> bytes)
        { return VfxSubgraphAsset::Encode(VfxSubgraphAsset::Decode(bytes)->Definition()); };
        result.ContextualImport = [](const AssetImportContext&, const std::span<const std::byte> bytes)
        {
            const auto asset = VfxSubgraphAsset::Decode(bytes);
            AssetImportOutput output;
            output.Bytes = VfxSubgraphAsset::Encode(asset->Definition());
            output.AssetDependencies = VfxSubgraphDependencies(asset->Definition());
            return output;
        };
        return result;
    }

    AssetDecoderRegistration CreateVfxSubgraphAssetDecoder()
    {
        return {VfxSubgraphAsset::StaticType(), VfxSubgraphAsset::Default(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return VfxSubgraphAsset::Decode(bytes); }};
    }
} // namespace Keire
