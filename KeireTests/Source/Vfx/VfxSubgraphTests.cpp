#include "Keire/Vfx/VfxSubgraph.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>

TEST_CASE("VFX Operator subgraph schema round trips stable typed boundaries")
{
    auto definition = Keire::VfxSubgraphAsset::DefaultDefinition();
    definition.Name = "Reusable Time";

    const auto bytes = Keire::VfxSubgraphAsset::Encode(definition);
    const auto decoded = Keire::VfxSubgraphAsset::Decode(bytes);

    CHECK(decoded->Definition() == definition);
    CHECK(decoded->Definition().SchemaVersion == Keire::VfxSubgraphSchemaVersion);
    REQUIRE(decoded->Definition().Ports.size() == 1);
    CHECK_FALSE(decoded->Definition().Ports.front().Input);
    CHECK(decoded->Definition().Ports.front().Type == Keire::VfxValueType::Scalar);
}

TEST_CASE("VFX subgraphs reject direct recursion and incompatible purpose bodies")
{
    auto recursive = Keire::VfxSubgraphAsset::DefaultDefinition();
    auto call = recursive.Graph.Nodes.front();
    call.Id = Keire::AssetId::Generate();
    for (auto& pin : call.Pins)
        pin.Id = Keire::AssetId::Generate();
    call.Kind = Keire::VfxGraphNodeKind::Subgraph;
    call.Reference = recursive.Id;
    recursive.Graph.Nodes.push_back(call);
    CHECK_THROWS_AS(Keire::ValidateVfxSubgraph(recursive), std::invalid_argument);

    auto block = Keire::VfxSubgraphAsset::DefaultDefinition();
    block.Purpose = Keire::VfxSubgraphPurpose::Block;
    block.ValidContexts = {Keire::VfxContextType::Update};
    CHECK_THROWS_AS(Keire::ValidateVfxSubgraph(block), std::invalid_argument);
}

TEST_CASE("VFX subgraph decoder rejects future and malformed schemas before publication")
{
    auto bytes = Keire::VfxSubgraphAsset::Encode(Keire::VfxSubgraphAsset::DefaultDefinition());
    auto text = std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const auto version = text.find("\"subgraphSchemaVersion\": 1");
    REQUIRE(version != std::string::npos);
    text[version + std::string("\"subgraphSchemaVersion\": ").size()] = '2';
    std::vector<std::byte> future(text.size());
    std::ranges::transform(text, future.begin(), [](const char value) { return static_cast<std::byte>(value); });
    CHECK_THROWS_WITH_AS(static_cast<void>(Keire::VfxSubgraphAsset::Decode(future)),
                         "VFX subgraph asset has an unsupported schema.", std::runtime_error);

    const std::vector<std::byte> malformed{std::byte{'{'}};
    CHECK_THROWS_AS(static_cast<void>(Keire::VfxSubgraphAsset::Decode(malformed)), std::runtime_error);
}

TEST_CASE("VFX Operator subgraphs expand into executable CPU and GPU value programs")
{
    auto subgraphDefinition = Keire::VfxSubgraphAsset::DefaultDefinition();
    const auto subgraph = Keire::CreateRef<Keire::VfxSubgraphAsset>(subgraphDefinition);
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    auto call = Keire::CreateVfxSubgraphNode(subgraphDefinition);
    call.Context = Keire::VfxContextType::Update;
    const auto callId = call.Id;
    const auto callOutput = call.Pins.front().Id;
    definition.Systems.front().Nodes.push_back(std::move(call));

    auto& update = *std::ranges::find(definition.Systems.front().Nodes, Keire::VfxContextType::Update,
                                      &Keire::VfxGraphNode::Context);
    const auto block =
        std::ranges::find_if(update.Blocks,
                             [](const Keire::VfxGraphBlock& candidate)
                             {
                                 return std::ranges::any_of(candidate.Pins, [](const Keire::VfxGraphPin& pin)
                                                            { return pin.Type == Keire::VfxValueType::Scalar; });
                             });
    REQUIRE(block != update.Blocks.end());
    const auto input = std::ranges::find(block->Pins, Keire::VfxValueType::Scalar, &Keire::VfxGraphPin::Type);
    REQUIRE(input != block->Pins.end());
    definition.Systems.front().Connections.push_back(
        {Keire::AssetId::Generate(), callId, callOutput, update.Id, input->Id, {}, block->Id, {}});

    const auto source = Keire::CreateRef<Keire::VfxEffectAsset>(definition);
    const Keire::VfxSubgraphResolver resolver = [subgraph](const Keire::AssetId id)
    { return id == subgraph->Definition().Id ? subgraph : Keire::Ref<const Keire::VfxSubgraphAsset>{}; };
    const auto expanded = Keire::ExpandVfxSubgraphs(definition, resolver);
    CHECK_FALSE(Keire::HasVfxSubgraphCalls(expanded));
    CHECK(Keire::ExpandVfxSubgraphs(definition, resolver) == expanded);
    CHECK(Keire::CompileVfxEffect(expanded, Keire::VfxBackend::Cpu).Valid);
    CHECK(Keire::CompileVfxEffect(expanded, Keire::VfxBackend::Gpu).Valid);

    Keire::VfxWorldSpecification specification;
    specification.MaximumEffects = 1;
    specification.MaximumParticles = 16;
    specification.SubgraphResolver = resolver;
    Keire::VfxWorld world(std::move(specification));
    CHECK(world.Activate({source}));
}

TEST_CASE("VFX Block and System subgraphs expand as ordered work and independent systems")
{
    auto definition = Keire::VfxEffectAsset::DefaultDefinition();
    const auto baseline = Keire::CompileVfxEffect(definition, Keire::VfxBackend::Cpu);
    REQUIRE(baseline.Valid);
    const auto& sourceUpdate = *std::ranges::find(definition.Systems.front().Nodes, Keire::VfxContextType::Update,
                                                  &Keire::VfxGraphNode::Context);
    REQUIRE_FALSE(sourceUpdate.Blocks.empty());
    const auto sourceModule =
        std::ranges::find(definition.Modules, sourceUpdate.Blocks.front().Reference, &Keire::VfxModuleDefinition::Id);
    REQUIRE(sourceModule != definition.Modules.end());

    Keire::VfxSubgraphDefinition blockDefinition;
    blockDefinition.Id = Keire::AssetId::Generate();
    blockDefinition.Name = "Reusable Update Block";
    blockDefinition.Purpose = Keire::VfxSubgraphPurpose::Block;
    blockDefinition.Graph.Id = Keire::AssetId::Generate();
    blockDefinition.Graph.Name = "Update Block";
    auto blockContext = sourceUpdate;
    blockContext.Id = Keire::AssetId::Generate();
    for (auto& pin : blockContext.Pins)
        pin.Id = Keire::AssetId::Generate();
    blockContext.Blocks = {sourceUpdate.Blocks.front()};
    blockContext.Blocks.front().Id = Keire::AssetId::Generate();
    for (auto& pin : blockContext.Blocks.front().Pins)
        pin.Id = Keire::AssetId::Generate();
    blockDefinition.Graph.Nodes = {std::move(blockContext)};
    blockDefinition.Modules = {*sourceModule};
    blockDefinition.ValidContexts = {Keire::VfxContextType::Update};
    const auto blockAsset = Keire::CreateRef<Keire::VfxSubgraphAsset>(blockDefinition);

    Keire::VfxSubgraphDefinition systemDefinition;
    systemDefinition.Id = Keire::AssetId::Generate();
    systemDefinition.Name = "Reusable Particle System";
    systemDefinition.Purpose = Keire::VfxSubgraphPurpose::System;
    systemDefinition.Graph = definition.Systems.front();
    systemDefinition.Graph.Id = Keire::AssetId::Generate();
    systemDefinition.Modules = definition.Modules;
    const auto systemAsset = Keire::CreateRef<Keire::VfxSubgraphAsset>(systemDefinition);

    auto& update = *std::ranges::find(definition.Systems.front().Nodes, Keire::VfxContextType::Update,
                                      &Keire::VfxGraphNode::Context);
    update.Blocks.push_back(Keire::CreateVfxSubgraphBlock(blockDefinition));
    definition.Systems.front().Nodes.push_back(Keire::CreateVfxSubgraphNode(systemDefinition));
    const std::map<Keire::AssetId, Keire::Ref<const Keire::VfxSubgraphAsset>> assets{
        {blockDefinition.Id, blockAsset}, {systemDefinition.Id, systemAsset}};
    const auto expanded = Keire::ExpandVfxSubgraphs(definition,
                                                    [&assets](const Keire::AssetId id)
                                                    {
                                                        const auto found = assets.find(id);
                                                        return found == assets.end()
                                                                   ? Keire::Ref<const Keire::VfxSubgraphAsset>{}
                                                                   : found->second;
                                                    });

    REQUIRE(expanded.Systems.size() == 2);
    const auto programs = Keire::CompileVfxEffectSystems(expanded, Keire::VfxBackend::Cpu);
    REQUIRE(programs.size() == 2);
    CHECK(std::ranges::all_of(programs, &Keire::VfxCompiledProgram::Valid));
    CHECK(programs.front().Operations.size() > baseline.Operations.size());
}

TEST_CASE("VFX Subgraph expansion rejects missing dependencies, indirect cycles, and depth overflow")
{
    auto makeCallDefinition = [](const Keire::AssetId id, const Keire::AssetId dependency, std::string name)
    {
        Keire::VfxSubgraphDefinition result;
        result.Id = id;
        result.Name = std::move(name);
        result.Graph.Id = Keire::AssetId::Generate();
        result.Graph.Name = "Nested Operator";
        Keire::VfxGraphNode call;
        call.Id = Keire::AssetId::Generate();
        call.Type = "Nested";
        call.Kind = Keire::VfxGraphNodeKind::Subgraph;
        call.Reference = dependency;
        call.TypeId.Value = "keire.subgraph";
        result.Graph.Nodes.push_back(std::move(call));
        return result;
    };
    const auto firstId = Keire::AssetId::Generate();
    const auto secondId = Keire::AssetId::Generate();
    const auto first = Keire::CreateRef<Keire::VfxSubgraphAsset>(makeCallDefinition(firstId, secondId, "First"));
    const auto second = Keire::CreateRef<Keire::VfxSubgraphAsset>(makeCallDefinition(secondId, firstId, "Second"));
    const std::map<Keire::AssetId, Keire::Ref<const Keire::VfxSubgraphAsset>> assets{{firstId, first},
                                                                                     {secondId, second}};
    const Keire::VfxSubgraphResolver resolver = [&assets](const Keire::AssetId id)
    {
        const auto found = assets.find(id);
        return found == assets.end() ? Keire::Ref<const Keire::VfxSubgraphAsset>{} : found->second;
    };
    auto effect = Keire::VfxEffectAsset::DefaultDefinition();
    effect.Systems.front().Nodes.push_back(Keire::CreateVfxSubgraphNode(first->Definition()));

    CHECK_THROWS_WITH_AS(static_cast<void>(Keire::ExpandVfxSubgraphs(effect, {})),
                         "VFX Subgraph expansion requires a resolver.", std::invalid_argument);
    CHECK_THROWS_WITH_AS(static_cast<void>(Keire::ExpandVfxSubgraphs(
                             effect, [](const Keire::AssetId) { return Keire::Ref<const Keire::VfxSubgraphAsset>{}; })),
                         doctest::Contains("dependency is unavailable"), std::invalid_argument);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(Keire::ExpandVfxSubgraphs(effect, [second](const Keire::AssetId) { return second; })),
        doctest::Contains("wrong identity"), std::invalid_argument);
    CHECK_THROWS_WITH_AS(static_cast<void>(Keire::ExpandVfxSubgraphs(effect, resolver)),
                         doctest::Contains("indirect cycle"), std::invalid_argument);
    CHECK_THROWS_WITH_AS(static_cast<void>(Keire::ExpandVfxSubgraphs(effect, resolver, 1)),
                         doctest::Contains("nesting depth"), std::invalid_argument);
}
