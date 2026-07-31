#include "KeireClient/Editor/EditModeVfxPreview.h"
#include "KeireClient/Editor/VfxEffectDocument.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace
{
    [[nodiscard]] constexpr Keire::AssetId Id(const std::uint64_t value) noexcept
    {
        return Keire::AssetId(0x564658444f435445ULL, value);
    }

    [[nodiscard]] Keire::VfxEffectDefinition Definition()
    {
        auto result = Keire::VfxEffectAsset::DefaultDefinition();
        result.EmitterId = Id(1);
        result.Name = "Sparks";
        for (std::size_t index = 0; index < result.Modules.size(); ++index)
        {
            const auto previous = result.Modules[index].Id;
            result.Modules[index].Id = Id(index + 2);
            for (auto& system : result.Systems)
            {
                for (auto& node : system.Nodes)
                {
                    if (node.Kind == Keire::VfxGraphNodeKind::Module && node.Reference == previous)
                        node.Reference = result.Modules[index].Id;
                    for (auto& block : node.Blocks)
                        if (block.Reference == previous)
                            block.Reference = result.Modules[index].Id;
                }
            }
        }
        return result;
    }

    [[nodiscard]] Keire::AssetId RendererId(const Keire::VfxEffectDefinition& definition)
    {
        const auto found =
            std::ranges::find_if(definition.Modules, [](const Keire::VfxModuleDefinition& module)
                                 { return std::holds_alternative<Keire::VfxRendererModule>(module.Payload); });
        if (found == definition.Modules.end())
            throw std::logic_error("Expected renderer module was unavailable.");
        return found->Id;
    }
} // namespace

TEST_CASE("VFX effect document coordinates stable modules preview undo save discard and reload")
{
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "VFX Effect"});
    Keire::VfxEffectDefinition preview;
    std::vector<std::byte> persisted;
    std::size_t previewCount = 0;
    std::size_t stopCount = 0;
    std::size_t persistCount = 0;
    KeireEditor::VfxEffectDocument document(
        {.Preview =
             [&](const Keire::AssetId asset, const Keire::VfxEffectDefinition& definition)
         {
             CHECK(asset == Id(100));
             preview = definition;
             ++previewCount;
         },
         .StopPreview =
             [&](const Keire::AssetId asset)
         {
             CHECK(asset == Id(100));
             ++stopCount;
         },
         .Persist =
             [&](const Keire::AssetId asset, const std::span<const std::byte> bytes)
         {
             CHECK(asset == Id(100));
             persisted.assign(bytes.begin(), bytes.end());
             ++persistCount;
         }});

    const auto authored = Definition();
    document.Open(Id(100), Keire::VfxEffectAsset::Encode(authored), 1, undo);
    CHECK_FALSE(document.Dirty());
    CHECK(preview == authored);
    CHECK(previewCount == 1);
    CHECK(persistCount == 0);

    const Keire::VfxModuleDefinition burst{
        .Id = Id(20),
        .Payload = Keire::VfxBurstModule{.Time = 0.1F, .Count = 4, .Cycles = 1, .Interval = 0.1F},
    };
    CHECK(document.AddModule(burst));
    CHECK(document.Definition().Modules.back().Id == Id(20));
    CHECK(document.EditModule(Id(20), [](Keire::VfxModuleDefinition& module)
                              { std::get<Keire::VfxBurstModule>(module.Payload).Count = 8; }));
    CHECK(std::get<Keire::VfxBurstModule>(document.Definition().Modules.back().Payload).Count == 8);
    CHECK(document.MoveModule(Id(20), 0));
    CHECK(document.Definition().Modules.front().Id == Id(20));
    CHECK(document.Definition().EmitterId == Id(1));
    CHECK(preview == document.Definition());

    CHECK(document.Undo());
    CHECK(document.Definition().Modules.back().Id == Id(20));
    CHECK(document.Redo());
    CHECK(document.Definition().Modules.front().Id == Id(20));
    CHECK(document.RemoveModule(Id(20)));
    CHECK(std::ranges::find(document.Definition().Modules, Id(20), &Keire::VfxModuleDefinition::Id) ==
          document.Definition().Modules.end());
    CHECK(document.Undo());
    CHECK(document.Definition().Modules.front().Id == Id(20));

    document.Save();
    CHECK_FALSE(document.Dirty());
    CHECK(persistCount == 1);
    CHECK(Keire::VfxEffectAsset::Decode(persisted)->Definition() == document.Definition());
    const auto previewCountAfterSave = previewCount;
    CHECK(document.Reload(document.Definition(), 2) == KeireEditor::AssetDocumentReloadResult::Unchanged);
    CHECK(document.Revision() == 2);
    CHECK(previewCount == previewCountAfterSave);

    auto external = document.Definition();
    external.Name = "External Sparks";
    CHECK(document.Reload(external, 3) == KeireEditor::AssetDocumentReloadResult::Applied);
    CHECK(document.Definition().Name == "External Sparks");
    CHECK_FALSE(document.Dirty());

    CHECK(document.Edit("Rename VFX effect",
                        [](Keire::VfxEffectDefinition& definition) { definition.Name = "Local Sparks"; }));
    auto newerExternal = external;
    newerExternal.Name = "Newer External Sparks";
    CHECK(document.Reload(newerExternal, 4) == KeireEditor::AssetDocumentReloadResult::LocalChanges);
    document.Discard();
    CHECK(document.Definition().Name == "External Sparks");
    CHECK_FALSE(document.Dirty());

    document.Close();
    CHECK(stopCount == 1);
    CHECK_FALSE(document.IsOpen());
    undoService->Close();
}

TEST_CASE("VFX effect document rejects invalid identity module and preview edits transactionally")
{
    Keire::VfxEffectDefinition preview;
    std::size_t previews = 0;
    std::size_t stops = 0;
    KeireEditor::VfxEffectDocument document({.Preview =
                                                 [&](const Keire::AssetId, const Keire::VfxEffectDefinition& definition)
                                             {
                                                 ++previews;
                                                 if (definition.Name == "Reject Preview")
                                                     throw std::runtime_error("Preview rejected.");
                                                 preview = definition;
                                             },
                                             .StopPreview = [&](const Keire::AssetId) { ++stops; },
                                             .Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    const auto authored = Definition();
    document.Open(Id(101), authored, 1);
    const auto baselinePreviews = previews;

    auto duplicate = authored.Modules.front();
    duplicate.Payload = Keire::VfxBurstModule{.Time = 0.1F, .Count = 1};
    CHECK_THROWS_AS((void)document.AddModule(duplicate), std::invalid_argument);
    CHECK(document.Definition() == authored);
    CHECK(previews == baselinePreviews);

    const auto module = authored.Modules.front().Id;
    CHECK_THROWS_AS(
        (void)document.EditModule(module, [](Keire::VfxModuleDefinition& definition) { definition.Id = Id(99); }),
        std::invalid_argument);
    CHECK(document.Definition() == authored);
    CHECK(previews == baselinePreviews);

    CHECK_THROWS_AS((void)document.RemoveModule(RendererId(authored)), std::invalid_argument);
    CHECK(document.Definition() == authored);
    CHECK(previews == baselinePreviews);
    CHECK_THROWS_AS((void)document.MoveModule(Id(99), 0), std::invalid_argument);
    CHECK_THROWS_AS((void)document.MoveModule(module, authored.Modules.size()), std::invalid_argument);

    const std::string malformed = "{\"schemaVersion\":99}";
    CHECK_THROWS_AS(document.Open(Id(102), std::as_bytes(std::span(malformed)), 1), std::runtime_error);
    CHECK(document.Asset() == Id(101));
    CHECK(document.Definition() == authored);

    CHECK_THROWS_AS((void)document.Edit("Rejected VFX preview", [](Keire::VfxEffectDefinition& definition)
                                        { definition.Name = "Reject Preview"; }),
                    std::runtime_error);
    CHECK(document.Definition() == authored);
    CHECK(preview == authored);
    CHECK(previews == baselinePreviews + 2);
    CHECK(document.Diagnostic() == "Preview rejected.");

    document.Close();
    CHECK(stops == 1);
}

TEST_CASE("VFX effect document authors graph systems nodes connections and blackboard parameters")
{
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "VFX Graph"});
    KeireEditor::VfxEffectDocument document({.Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    auto authored = Definition();
    authored.ExecutionSource = Keire::VfxExecutionSource::LegacyModules;
    document.Open(Id(104), authored, 1, undo);

    const Keire::VfxGraphSystem system{.Id = Id(30), .Name = "Secondary System"};
    CHECK(document.AddSystem(system));
    CHECK(document.EditSystem(system.Id, [](Keire::VfxGraphSystem& graph) { graph.Name = "Impact System"; }));
    CHECK(document.Definition().Systems.back().Name == "Impact System");

    const Keire::VfxGraphNode output{
        .Id = Id(31),
        .Type = "Sample Attribute",
        .Context = Keire::VfxContextType::Update,
        .EditorPosition = {100.0F, 80.0F},
        .Pins =
            {
                {Id(32), "Value", Keire::VfxValueType::Scalar, false},
                {Id(33), "Alternate", Keire::VfxValueType::Scalar, false},
            },
        .TypeId = {"keire.context.update"},
    };
    const Keire::VfxGraphNode input{
        .Id = Id(34),
        .Type = "Set Attribute",
        .Context = Keire::VfxContextType::Update,
        .EditorPosition = {420.0F, 80.0F},
        .Pins =
            {
                {Id(35), "Value", Keire::VfxValueType::Scalar, true},
                {Id(36), "Alternate", Keire::VfxValueType::Scalar, true},
            },
        .TypeId = {"keire.context.update"},
    };
    CHECK(document.AddNode(system.Id, output));
    CHECK(document.AddNode(system.Id, input));
    CHECK(document.EditNode(system.Id, input.Id,
                            [](Keire::VfxGraphNode& node)
                            {
                                node.EditorPosition = {480.0F, 120.0F};
                                node.CustomHlsl = "value = input;";
                            }));
    const Keire::VfxGraphPin removablePin{Id(40), "Temporary", Keire::VfxValueType::Scalar, true};
    CHECK(document.AddPin(system.Id, input.Id, removablePin));
    CHECK(document.EditPin(system.Id, input.Id, removablePin.Id,
                           [](Keire::VfxGraphPin& pin) { pin.Name = "Removable"; }));

    const Keire::VfxGraphConnection primary{
        .Id = Id(37),
        .OutputNode = output.Id,
        .OutputPin = output.Pins[0].Id,
        .InputNode = input.Id,
        .InputPin = input.Pins[0].Id,
    };
    const Keire::VfxGraphConnection alternate{
        .Id = Id(38),
        .OutputNode = output.Id,
        .OutputPin = output.Pins[0].Id,
        .InputNode = input.Id,
        .InputPin = input.Pins[1].Id,
    };
    const Keire::VfxGraphConnection removable{
        .Id = Id(41),
        .OutputNode = output.Id,
        .OutputPin = output.Pins[0].Id,
        .InputNode = input.Id,
        .InputPin = removablePin.Id,
    };
    CHECK(document.AddConnection(system.Id, primary));
    CHECK(document.AddConnection(system.Id, alternate));
    CHECK(document.AddConnection(system.Id, removable));
    CHECK(document.EditConnection(system.Id, alternate.Id,
                                  [pin = output.Pins[1].Id](Keire::VfxGraphConnection& connection)
                                  { connection.OutputPin = pin; }));

    const Keire::VfxBlackboardParameter intensity{
        .Id = Id(39),
        .Name = "Intensity",
        .Type = Keire::VfxValueType::Scalar,
        .DefaultValue = 1.0F,
    };
    CHECK(document.AddBlackboardParameter(intensity));
    CHECK(document.EditBlackboardParameter(intensity.Id,
                                           [](Keire::VfxBlackboardParameter& parameter)
                                           {
                                               parameter.Name = "Impact Intensity";
                                               parameter.DefaultValue = 2.0F;
                                               parameter.Exposed = false;
                                           }));
    REQUIRE(document.Definition().Blackboard.size() == 1);
    CHECK(document.Definition().Blackboard.front().Name == "Impact Intensity");
    CHECK(std::get<float>(document.Definition().Blackboard.front().DefaultValue) == doctest::Approx(2.0F));

    CHECK(document.RemoveConnection(system.Id, primary.Id));
    CHECK(document.Definition().Systems.back().Connections.size() == 2);
    CHECK(document.Undo());
    CHECK(document.Definition().Systems.back().Connections.size() == 3);

    CHECK(document.RemovePin(system.Id, input.Id, removablePin.Id));
    CHECK(document.Definition().Systems.back().Nodes.back().Pins.size() == 2);
    CHECK(document.Definition().Systems.back().Connections.size() == 2);
    CHECK(document.Undo());
    CHECK(document.Definition().Systems.back().Nodes.back().Pins.size() == 3);
    CHECK(document.Definition().Systems.back().Connections.size() == 3);
    CHECK(document.RemovePin(system.Id, input.Id, removablePin.Id));

    CHECK(document.RemoveNode(system.Id, output.Id));
    CHECK(document.Definition().Systems.back().Nodes.size() == 1);
    CHECK(document.Definition().Systems.back().Connections.empty());
    CHECK(document.Undo());
    CHECK(document.Definition().Systems.back().Nodes.size() == 2);
    CHECK(document.Definition().Systems.back().Connections.size() == 2);
    CHECK(document.RemoveNode(system.Id, output.Id));
    CHECK(document.Definition().Systems.back().Connections.empty());

    CHECK(document.RemoveBlackboardParameter(intensity.Id));
    CHECK(document.Definition().Blackboard.empty());
    CHECK(document.RemoveSystem(system.Id));
    CHECK(document.RemoveSystem(authored.Systems.front().Id));
    CHECK(document.Definition().Systems.empty());

    document.Close();
    undoService->Close();
}

TEST_CASE("VFX Context Blocks author ordered module and portable HLSL execution transactionally")
{
    auto authored = Definition();
    const auto& authoredSystem = authored.Systems.front();
    const auto authoredUpdate = std::ranges::find_if(
        authoredSystem.Nodes, [](const Keire::VfxGraphNode& node)
        { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == Keire::VfxContextType::Update; });
    REQUIRE(authoredUpdate != authoredSystem.Nodes.end());
    CHECK(std::ranges::none_of(authoredSystem.Nodes, [](const Keire::VfxGraphNode& node)
                               { return node.Kind == Keire::VfxGraphNodeKind::Module; }));
    const auto systemId = authoredSystem.Id;
    const auto updateId = authoredUpdate->Id;
    const auto originalBlockCount = authoredUpdate->Blocks.size();

    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "VFX Context Blocks"});
    KeireEditor::VfxEffectDocument document({.Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    document.Open(Id(399), authored, 1, undo);

    const Keire::VfxModuleDefinition forceModule{
        .Id = Id(400),
        .Payload = Keire::VfxForceModule{.Force = {0.0F, 4.0F, 0.0F}, .GravityMultiplier = 0.25F},
    };
    REQUIRE(document.AddModule(forceModule));
    auto forceBlock = Keire::CreateVfxGraphBlock(forceModule);
    forceBlock.Id = Id(401);
    for (std::size_t index = 0; index < forceBlock.Pins.size(); ++index)
        forceBlock.Pins[index].Id = Id(402 + index);
    REQUIRE(document.AddBlock(systemId, updateId, forceBlock));

    auto portableBlock = Keire::CreateVfxGraphPortableHlslBlock("Size *= 1.0;");
    portableBlock.Id = Id(410);
    REQUIRE(document.AddBlock(systemId, updateId, portableBlock));

    const auto findUpdate = [&document, updateId]() -> const Keire::VfxGraphNode&
    {
        const auto& nodes = document.Definition().Systems.front().Nodes;
        const auto found = std::ranges::find(nodes, updateId, &Keire::VfxGraphNode::Id);
        if (found == nodes.end())
            throw std::logic_error("Expected Update Context was unavailable.");
        return *found;
    };
    const auto findBlock = [&findUpdate](const Keire::AssetId blockId) -> const Keire::VfxGraphBlock&
    {
        const auto& blocks = findUpdate().Blocks;
        const auto found = std::ranges::find(blocks, blockId, &Keire::VfxGraphBlock::Id);
        if (found == blocks.end())
            throw std::logic_error("Expected Context Block was unavailable.");
        return *found;
    };

    REQUIRE(findUpdate().Blocks.size() == originalBlockCount + 2);
    CHECK(findUpdate().Blocks[originalBlockCount].Id == forceBlock.Id);
    CHECK(findUpdate().Blocks[originalBlockCount + 1].Id == portableBlock.Id);
    CHECK(findBlock(forceBlock.Id).Reference == forceModule.Id);
    CHECK(findBlock(portableBlock.Id).TypeId.View() == "keire.block.portable-hlsl");
    REQUIRE(findBlock(portableBlock.Id).Properties.size() == 1);
    CHECK(std::get<std::string>(findBlock(portableBlock.Id).Properties.front().Value) == "Size *= 1.0;");

    REQUIRE(document.SetBlockEnabled(systemId, updateId, forceBlock.Id, false));
    REQUIRE(document.EditModule(forceModule.Id, [](Keire::VfxModuleDefinition& module) { module.Enabled = false; }));
    CHECK_FALSE(findBlock(forceBlock.Id).Enabled);
    REQUIRE(document.EditModule(forceModule.Id, [](Keire::VfxModuleDefinition& module) { module.Enabled = true; }));
    CHECK_FALSE(findBlock(forceBlock.Id).Enabled);
    REQUIRE(document.SetBlockEnabled(systemId, updateId, forceBlock.Id, true));

    REQUIRE(document.MoveBlock(systemId, updateId, portableBlock.Id, 0));
    CHECK(findUpdate().Blocks.front().Id == portableBlock.Id);
    CHECK(document.Undo());
    CHECK(findUpdate().Blocks[originalBlockCount + 1].Id == portableBlock.Id);
    CHECK(document.Redo());
    CHECK(findUpdate().Blocks.front().Id == portableBlock.Id);

    REQUIRE(document.SetBlockEnabled(systemId, updateId, portableBlock.Id, false));
    CHECK_FALSE(findBlock(portableBlock.Id).Enabled);
    CHECK(document.Undo());
    CHECK(findBlock(portableBlock.Id).Enabled);
    CHECK(document.Redo());
    CHECK_FALSE(findBlock(portableBlock.Id).Enabled);
    REQUIRE(document.SetBlockEnabled(systemId, updateId, portableBlock.Id, true));

    const Keire::VfxGraphPin amountPin{
        .Id = Id(411),
        .Name = "Amount",
        .Type = Keire::VfxValueType::Scalar,
        .Input = true,
        .Semantic = "amount",
        .DefaultValue = Keire::VfxParameterValue{1.0F},
    };
    REQUIRE(document.AddBlockPin(systemId, updateId, portableBlock.Id, amountPin));
    REQUIRE(document.EditBlock(systemId, updateId, portableBlock.Id, [](Keire::VfxGraphBlock& block)
                               { block.Properties.front().Value = std::string("Size *= amount;"); }));
    CHECK(std::get<std::string>(findBlock(portableBlock.Id).Properties.front().Value) == "Size *= amount;");

    const Keire::VfxBlackboardParameter amountParameter{
        .Id = Id(412),
        .Name = "Amount",
        .Type = Keire::VfxValueType::Scalar,
        .DefaultValue = 2.0F,
    };
    const Keire::VfxGraphNode amountNode{
        .Id = Id(413),
        .Type = "Amount",
        .Context = Keire::VfxContextType::Update,
        .EditorPosition = {-320.0F, 220.0F},
        .Pins = {{Id(414), "Amount", Keire::VfxValueType::Scalar, false, "value", std::nullopt}},
        .Kind = Keire::VfxGraphNodeKind::Parameter,
        .Reference = amountParameter.Id,
        .TypeId = {"keire.parameter"},
    };
    REQUIRE(document.AddBlackboardParameter(amountParameter));
    REQUIRE(document.AddNode(systemId, amountNode));

    const Keire::VfxGraphEndpoint outputEndpoint{amountNode.Id, {}, amountNode.Pins.front().Id};
    const Keire::VfxGraphEndpoint inputEndpoint{updateId, portableBlock.Id, amountPin.Id};
    const auto accepted = document.CheckConnection(systemId, outputEndpoint, inputEndpoint);
    CHECK(accepted.Status == KeireEditor::VfxGraphConnectionStatus::Accepted);
    CHECK_FALSE(accepted.ReplacesInput);
    const auto missingBlock = document.CheckConnection(systemId, outputEndpoint, {updateId, {}, amountPin.Id});
    CHECK(missingBlock.Status == KeireEditor::VfxGraphConnectionStatus::Rejected);

    const Keire::VfxGraphConnection binding{
        .Id = Id(415),
        .OutputNode = outputEndpoint.Node,
        .OutputPin = outputEndpoint.Pin,
        .InputNode = inputEndpoint.Node,
        .InputPin = inputEndpoint.Pin,
        .OutputBlock = outputEndpoint.Block,
        .InputBlock = inputEndpoint.Block,
    };
    REQUIRE(document.AddConnection(systemId, binding));
    REQUIRE(document.EditBlockPin(systemId, updateId, portableBlock.Id, amountPin.Id,
                                  [](Keire::VfxGraphPin& pin) { pin.Name = "Strength"; }));
    CHECK(findBlock(portableBlock.Id).Pins.front().Name == "Strength");
    CHECK(std::ranges::find(document.Definition().Systems.front().Connections, binding.Id,
                            &Keire::VfxGraphConnection::Id) != document.Definition().Systems.front().Connections.end());

    REQUIRE(document.EditBlock(systemId, updateId, portableBlock.Id, [](Keire::VfxGraphBlock& block)
                               { block.Properties.front().Value = std::string("Size *= 1.0;"); }));
    REQUIRE(document.RemoveBlockPin(systemId, updateId, portableBlock.Id, amountPin.Id));
    CHECK(findBlock(portableBlock.Id).Pins.empty());
    CHECK(std::ranges::find(document.Definition().Systems.front().Connections, binding.Id,
                            &Keire::VfxGraphConnection::Id) == document.Definition().Systems.front().Connections.end());
    CHECK(document.Undo());
    REQUIRE(findBlock(portableBlock.Id).Pins.size() == 1);
    CHECK(findBlock(portableBlock.Id).Pins.front().Id == amountPin.Id);
    CHECK(std::ranges::find(document.Definition().Systems.front().Connections, binding.Id,
                            &Keire::VfxGraphConnection::Id) != document.Definition().Systems.front().Connections.end());

    REQUIRE(document.RemoveBlock(systemId, updateId, portableBlock.Id));
    CHECK(std::ranges::find(findUpdate().Blocks, portableBlock.Id, &Keire::VfxGraphBlock::Id) ==
          findUpdate().Blocks.end());
    CHECK(std::ranges::find(document.Definition().Systems.front().Connections, binding.Id,
                            &Keire::VfxGraphConnection::Id) == document.Definition().Systems.front().Connections.end());
    CHECK(document.Undo());
    CHECK(findBlock(portableBlock.Id).Pins.front().Id == amountPin.Id);
    CHECK(std::ranges::find(document.Definition().Systems.front().Connections, binding.Id,
                            &Keire::VfxGraphConnection::Id) != document.Definition().Systems.front().Connections.end());
    CHECK(document.Redo());
    CHECK(std::ranges::find(findUpdate().Blocks, portableBlock.Id, &Keire::VfxGraphBlock::Id) ==
          findUpdate().Blocks.end());
    CHECK(document.Undo());

    const auto beforeRejectedEdit = document.Definition();
    CHECK_THROWS_AS((void)document.EditBlock(systemId, updateId, portableBlock.Id,
                                             [](Keire::VfxGraphBlock& block) { block.Id = Id(416); }),
                    std::invalid_argument);
    CHECK(document.Definition() == beforeRejectedEdit);
    CHECK_THROWS_AS((void)document.EditBlockPin(systemId, updateId, portableBlock.Id, amountPin.Id,
                                                [](Keire::VfxGraphPin& pin) { pin.Id = Id(417); }),
                    std::invalid_argument);
    CHECK(document.Definition() == beforeRejectedEdit);
    CHECK_THROWS_AS((void)document.MoveBlock(systemId, updateId, portableBlock.Id, findUpdate().Blocks.size()),
                    std::invalid_argument);
    CHECK(document.Definition() == beforeRejectedEdit);

    document.Close();
    undoService->Close();
}

TEST_CASE("VFX Context Blocks remain authorable in a disconnected draft without replacing the last valid preview")
{
    const auto authored = Definition();
    const auto& graph = authored.Systems.front();
    const auto flowConnection = std::ranges::find_if(graph.Connections, [](const Keire::VfxGraphConnection& connection)
                                                     { return !connection.OutputBlock && !connection.InputBlock; });
    REQUIRE(flowConnection != graph.Connections.end());
    const auto update = std::ranges::find_if(
        graph.Nodes, [](const Keire::VfxGraphNode& node)
        { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == Keire::VfxContextType::Update; });
    REQUIRE(update != graph.Nodes.end());

    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "VFX disconnected Context Block draft"});
    Keire::VfxEffectDefinition preview;
    std::size_t previewCount = 0;
    KeireEditor::VfxEffectDocument document({.Preview =
                                                 [&](const Keire::AssetId, const Keire::VfxEffectDefinition& definition)
                                             {
                                                 preview = definition;
                                                 ++previewCount;
                                             },
                                             .Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    document.Open(Id(420), authored, 1, undo);
    REQUIRE(previewCount == 1);

    REQUIRE(document.RemoveConnection(graph.Id, flowConnection->Id));
    CHECK_FALSE(document.Publishable());
    CHECK(previewCount == 1);
    CHECK(preview == authored);

    const Keire::VfxModuleDefinition forceModule{
        .Id = Id(421),
        .Payload = Keire::VfxForceModule{.Force = {1.0F, 0.0F, 0.0F}},
    };
    REQUIRE(document.AddModule(forceModule));
    auto forceBlock = Keire::CreateVfxGraphBlock(forceModule);
    forceBlock.Id = Id(422);
    for (std::size_t index = 0; index < forceBlock.Pins.size(); ++index)
        forceBlock.Pins[index].Id = Id(423 + index);
    REQUIRE(document.AddBlock(graph.Id, update->Id, forceBlock));
    CHECK_FALSE(document.Publishable());
    CHECK(previewCount == 1);
    CHECK(preview == authored);

    CHECK(document.Undo());
    CHECK(document.Undo());
    CHECK(document.Undo());
    CHECK(document.Definition() == authored);
    CHECK(document.Publishable());
    CHECK(previewCount == 2);
    CHECK(preview == authored);

    document.Close();
    undoService->Close();
}

TEST_CASE("VFX graph interactions preserve requested placement and atomically replace occupied inputs")
{
    auto authored = Definition();
    authored.ExecutionSource = Keire::VfxExecutionSource::LegacyModules;
    authored.Systems = {{.Id = Id(300), .Name = "Interaction Graph"}};
    authored.Blackboard = {{Id(320), "Primary", Keire::VfxValueType::Scalar, 0.0F, true},
                           {Id(321), "Alternate", Keire::VfxValueType::Scalar, 0.0F, true},
                           {Id(322), "Vector", Keire::VfxValueType::Vector3, Keire::Vector3{}, true}};

    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "VFX Graph Interactions"});
    KeireEditor::VfxEffectDocument document({.Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    document.Open(Id(301), authored, 1, undo);

    const Keire::VfxGraphNode primarySource{
        .Id = Id(302),
        .Type = "Primary Source",
        .Context = Keire::VfxContextType::Update,
        .EditorPosition = {137.5F, -48.25F},
        .Pins = {{Id(303), "Value", Keire::VfxValueType::Scalar, false}},
        .Kind = Keire::VfxGraphNodeKind::Parameter,
        .Reference = Id(320),
        .TypeId = {"keire.parameter"},
    };
    const Keire::VfxGraphNode alternateSource{
        .Id = Id(305),
        .Type = "Alternate Source",
        .Context = Keire::VfxContextType::Update,
        .EditorPosition = {-220.0F, 84.0F},
        .Pins = {{Id(306), "Value", Keire::VfxValueType::Scalar, false}},
        .Kind = Keire::VfxGraphNodeKind::Parameter,
        .Reference = Id(321),
        .TypeId = {"keire.parameter"},
    };
    const Keire::VfxGraphNode vectorSource{
        .Id = Id(314),
        .Type = "Vector Source",
        .Context = Keire::VfxContextType::Update,
        .EditorPosition = {-220.0F, 180.0F},
        .Pins = {{Id(315), "Vector", Keire::VfxValueType::Vector3, false}},
        .Kind = Keire::VfxGraphNodeKind::Parameter,
        .Reference = Id(322),
        .TypeId = {"keire.parameter"},
    };
    const Keire::VfxGraphNode sink{
        .Id = Id(307),
        .Type = "Sink",
        .Context = Keire::VfxContextType::Update,
        .EditorPosition = {520.0F, 96.0F},
        .Pins = {{Id(308), "Value", Keire::VfxValueType::Scalar, true}},
        .Kind = Keire::VfxGraphNodeKind::CustomHlsl,
        .TypeId = {"keire.operator.portable-hlsl"},
    };

    CHECK(document.AddNode(authored.Systems.front().Id, primarySource));
    REQUIRE(document.Definition().Systems.front().Nodes.size() == 1);
    CHECK(document.Definition().Systems.front().Nodes.front().EditorPosition == primarySource.EditorPosition);
    CHECK(document.Undo());
    CHECK(document.Definition().Systems.front().Nodes.empty());
    CHECK(document.Redo());
    REQUIRE(document.Definition().Systems.front().Nodes.size() == 1);
    CHECK(document.Definition().Systems.front().Nodes.front() == primarySource);

    CHECK(document.AddNode(authored.Systems.front().Id, alternateSource));
    CHECK(document.AddNode(authored.Systems.front().Id, vectorSource));
    CHECK(document.AddNode(authored.Systems.front().Id, sink));
    const auto accepted = document.CheckConnection(authored.Systems.front().Id, primarySource.Id,
                                                   primarySource.Pins[0].Id, sink.Id, sink.Pins[0].Id);
    CHECK(accepted.Status == KeireEditor::VfxGraphConnectionStatus::Accepted);
    CHECK_FALSE(accepted.ReplacesInput);
    CHECK(accepted.Diagnostic.empty());
    const auto wrongDirection = document.CheckConnection(authored.Systems.front().Id, sink.Id, sink.Pins[0].Id,
                                                         primarySource.Id, primarySource.Pins[0].Id);
    CHECK(wrongDirection.Status == KeireEditor::VfxGraphConnectionStatus::Rejected);
    CHECK_FALSE(wrongDirection.ReplacesInput);
    CHECK_FALSE(wrongDirection.Diagnostic.empty());
    const auto wrongType = document.CheckConnection(authored.Systems.front().Id, vectorSource.Id,
                                                    vectorSource.Pins[0].Id, sink.Id, sink.Pins[0].Id);
    CHECK(wrongType.Status == KeireEditor::VfxGraphConnectionStatus::Rejected);
    CHECK_FALSE(wrongType.ReplacesInput);
    CHECK_FALSE(wrongType.Diagnostic.empty());

    const Keire::VfxGraphConnection primary{
        .Id = Id(309),
        .OutputNode = primarySource.Id,
        .OutputPin = primarySource.Pins[0].Id,
        .InputNode = sink.Id,
        .InputPin = sink.Pins[0].Id,
    };
    CHECK(document.AddConnection(authored.Systems.front().Id, primary));
    REQUIRE(document.Definition().Systems.front().Connections.size() == 1);
    CHECK(document.Definition().Systems.front().Connections.front() == primary);
    const auto duplicate = document.CheckConnection(authored.Systems.front().Id, primarySource.Id,
                                                    primarySource.Pins[0].Id, sink.Id, sink.Pins[0].Id);
    CHECK(duplicate.Status == KeireEditor::VfxGraphConnectionStatus::Rejected);
    CHECK_FALSE(duplicate.ReplacesInput);
    CHECK_FALSE(duplicate.Diagnostic.empty());

    const auto beforeInvalidConnection = document.Definition();
    CHECK_THROWS_AS((void)document.AddConnection(authored.Systems.front().Id, {.Id = Id(310),
                                                                               .OutputNode = sink.Id,
                                                                               .OutputPin = sink.Pins[0].Id,
                                                                               .InputNode = primarySource.Id,
                                                                               .InputPin = primarySource.Pins[0].Id}),
                    std::invalid_argument);
    CHECK(document.Definition() == beforeInvalidConnection);
    CHECK_THROWS_AS((void)document.AddConnection(authored.Systems.front().Id, {.Id = Id(311),
                                                                               .OutputNode = vectorSource.Id,
                                                                               .OutputPin = vectorSource.Pins[0].Id,
                                                                               .InputNode = sink.Id,
                                                                               .InputPin = sink.Pins[0].Id}),
                    std::invalid_argument);
    CHECK(document.Definition() == beforeInvalidConnection);
    CHECK_THROWS_AS((void)document.AddConnection(authored.Systems.front().Id, {.Id = Id(312),
                                                                               .OutputNode = primarySource.Id,
                                                                               .OutputPin = primarySource.Pins[0].Id,
                                                                               .InputNode = sink.Id,
                                                                               .InputPin = Id(399)}),
                    std::invalid_argument);
    CHECK(document.Definition() == beforeInvalidConnection);

    const Keire::VfxGraphConnection replacement{
        .Id = Id(313),
        .OutputNode = alternateSource.Id,
        .OutputPin = alternateSource.Pins[0].Id,
        .InputNode = sink.Id,
        .InputPin = sink.Pins[0].Id,
    };
    const auto replacementCheck = document.CheckConnection(authored.Systems.front().Id, alternateSource.Id,
                                                           alternateSource.Pins[0].Id, sink.Id, sink.Pins[0].Id);
    CHECK(replacementCheck.Status == KeireEditor::VfxGraphConnectionStatus::Accepted);
    CHECK(replacementCheck.ReplacesInput);
    CHECK(replacementCheck.Diagnostic.empty());
    CHECK(document.AddConnection(authored.Systems.front().Id, replacement));
    REQUIRE(document.Definition().Systems.front().Connections.size() == 1);
    CHECK(document.Definition().Systems.front().Connections.front() == replacement);
    CHECK(document.Undo());
    REQUIRE(document.Definition().Systems.front().Connections.size() == 1);
    CHECK(document.Definition().Systems.front().Connections.front() == primary);
    CHECK(document.Redo());
    REQUIRE(document.Definition().Systems.front().Connections.size() == 1);
    CHECK(document.Definition().Systems.front().Connections.front() == replacement);

    const auto beforeDuplicateId = document.Definition();
    CHECK_THROWS_AS((void)document.AddConnection(authored.Systems.front().Id, replacement), std::invalid_argument);
    CHECK(document.Definition() == beforeDuplicateId);

    CHECK(document.RemoveConnection(authored.Systems.front().Id, replacement.Id));
    CHECK(document.Definition().Systems.front().Connections.empty());
    CHECK(document.Undo());
    REQUIRE(document.Definition().Systems.front().Connections.size() == 1);
    CHECK(document.Definition().Systems.front().Connections.front() == replacement);
    CHECK(document.Redo());
    CHECK(document.Definition().Systems.front().Connections.empty());

    document.Close();
    undoService->Close();
}

TEST_CASE("VFX graph interaction deletions clean incident cables and round trip through undo and redo")
{
    const Keire::VfxGraphNode source{
        .Id = Id(320),
        .Type = "Source",
        .Context = Keire::VfxContextType::Update,
        .EditorPosition = {80.0F, 120.0F},
        .Pins =
            {
                {Id(321), "First", Keire::VfxValueType::Scalar, false},
                {Id(322), "Second", Keire::VfxValueType::Scalar, false},
            },
        .Kind = Keire::VfxGraphNodeKind::CustomHlsl,
        .TypeId = {"keire.operator.portable-hlsl"},
    };
    const Keire::VfxGraphNode sink{
        .Id = Id(323),
        .Type = "Sink",
        .Context = Keire::VfxContextType::Update,
        .EditorPosition = {480.0F, 120.0F},
        .Pins =
            {
                {Id(324), "First", Keire::VfxValueType::Scalar, true},
                {Id(325), "Second", Keire::VfxValueType::Scalar, true},
            },
        .Kind = Keire::VfxGraphNodeKind::CustomHlsl,
        .TypeId = {"keire.operator.portable-hlsl"},
    };
    const Keire::VfxGraphSystem graph{
        .Id = Id(326),
        .Name = "Deletion Graph",
        .Nodes = {source, sink},
        .Connections =
            {
                {Id(327), source.Id, source.Pins[0].Id, sink.Id, sink.Pins[0].Id},
                {Id(328), source.Id, source.Pins[1].Id, sink.Id, sink.Pins[1].Id},
            },
    };
    auto authored = Definition();
    authored.ExecutionSource = Keire::VfxExecutionSource::LegacyModules;
    authored.Systems = {graph};

    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "VFX Graph Deletions"});
    KeireEditor::VfxEffectDocument document({.Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    document.Open(Id(329), authored, 1, undo);

    CHECK(document.RemovePin(graph.Id, sink.Id, sink.Pins[0].Id));
    REQUIRE(document.Definition().Systems.front().Nodes.back().Pins.size() == 1);
    CHECK(document.Definition().Systems.front().Nodes.back().Pins.front() == sink.Pins[1]);
    REQUIRE(document.Definition().Systems.front().Connections.size() == 1);
    CHECK(document.Definition().Systems.front().Connections.front().Id == Id(328));
    const auto withoutFirstPin = document.Definition();
    CHECK(document.Undo());
    CHECK(document.Definition() == authored);
    CHECK(document.Redo());
    CHECK(document.Definition() == withoutFirstPin);
    CHECK(document.Undo());
    CHECK(document.Definition() == authored);

    CHECK(document.RemoveNode(graph.Id, source.Id));
    REQUIRE(document.Definition().Systems.front().Nodes.size() == 1);
    CHECK(document.Definition().Systems.front().Nodes.front() == sink);
    CHECK(document.Definition().Systems.front().Connections.empty());
    const auto withoutSource = document.Definition();
    CHECK(document.Undo());
    CHECK(document.Definition() == authored);
    CHECK(document.Redo());
    CHECK(document.Definition() == withoutSource);

    document.Close();
    undoService->Close();
}

TEST_CASE("VFX particle-stream unlink remains undoable while invalid drafts preserve the last valid preview")
{
    const auto authored = Definition();
    REQUIRE(authored.ExecutionSource == Keire::VfxExecutionSource::Graph);
    REQUIRE(authored.Systems.size() == 1);
    const auto& graph = authored.Systems.front();
    const auto findPin = [&](const Keire::AssetId nodeId, const Keire::AssetId pinId) -> const Keire::VfxGraphPin*
    {
        const auto node = std::ranges::find(graph.Nodes, nodeId, &Keire::VfxGraphNode::Id);
        if (node == graph.Nodes.end())
            return nullptr;
        const auto pin = std::ranges::find(node->Pins, pinId, &Keire::VfxGraphPin::Id);
        return pin == node->Pins.end() ? nullptr : &*pin;
    };
    const auto flowConnection =
        std::ranges::find_if(graph.Connections,
                             [&](const Keire::VfxGraphConnection& connection)
                             {
                                 const auto* output = findPin(connection.OutputNode, connection.OutputPin);
                                 const auto* input = findPin(connection.InputNode, connection.InputPin);
                                 return output && input && output->Type == Keire::VfxValueType::ParticleStream &&
                                        input->Type == Keire::VfxValueType::ParticleStream;
                             });
    REQUIRE(flowConnection != graph.Connections.end());
    const auto removableContext =
        std::ranges::find_if(graph.Nodes, [](const Keire::VfxGraphNode& node)
                             { return node.Kind == Keire::VfxGraphNodeKind::Context && !node.Blocks.empty(); });
    REQUIRE(removableContext != graph.Nodes.end());
    const auto removableBlock = removableContext->Blocks.front().Id;

    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "VFX Particle Stream Unlink"});
    Keire::VfxEffectDefinition preview;
    std::size_t previewCount = 0;
    std::size_t persistCount = 0;
    KeireEditor::VfxEffectDocument document(
        {.Preview =
             [&](const Keire::AssetId, const Keire::VfxEffectDefinition& definition)
         {
             preview = definition;
             ++previewCount;
         },
         .Persist = [&](const Keire::AssetId, const std::span<const std::byte>) { ++persistCount; }});
    document.Open(Id(330), authored, 1, undo);
    REQUIRE(previewCount == 1);
    CHECK(preview == authored);

    CHECK(document.RemoveConnection(graph.Id, flowConnection->Id));
    CHECK(document.Dirty());
    CHECK(std::ranges::find(document.Definition().Systems.front().Connections, flowConnection->Id,
                            &Keire::VfxGraphConnection::Id) == document.Definition().Systems.front().Connections.end());
    CHECK_FALSE(document.Diagnostic().empty());
    CHECK(std::string(document.Diagnostic()).find("particle-stream") != std::string::npos);
    CHECK(previewCount == 1);
    CHECK(preview == authored);

    CHECK_THROWS_AS(document.Save(), std::invalid_argument);
    CHECK(persistCount == 0);
    CHECK(document.Dirty());
    CHECK(previewCount == 1);
    CHECK(preview == authored);

    const auto unlinked = document.Definition();
    CHECK(document.RemoveBlock(graph.Id, removableContext->Id, removableBlock));
    const auto changedContext =
        std::ranges::find(document.Definition().Systems.front().Nodes, removableContext->Id, &Keire::VfxGraphNode::Id);
    REQUIRE(changedContext != document.Definition().Systems.front().Nodes.end());
    CHECK(std::ranges::find(changedContext->Blocks, removableBlock, &Keire::VfxGraphBlock::Id) ==
          changedContext->Blocks.end());
    CHECK(std::ranges::none_of(
        document.Definition().Systems.front().Connections, [&](const Keire::VfxGraphConnection& connection)
        { return connection.OutputBlock == removableBlock || connection.InputBlock == removableBlock; }));
    CHECK_FALSE(document.Diagnostic().empty());
    CHECK(previewCount == 1);
    CHECK(document.Undo());
    CHECK(document.Definition() == unlinked);
    CHECK_FALSE(document.Diagnostic().empty());
    CHECK(previewCount == 1);

    CHECK(document.Undo());
    CHECK(document.Definition() == authored);
    CHECK_FALSE(document.Dirty());
    CHECK(document.Diagnostic().empty());
    CHECK(previewCount == 2);
    CHECK(preview == authored);

    document.Close();
    undoService->Close();
}

TEST_CASE("VFX undo accounting includes dynamically allocated curve and gradient keys")
{
    const auto authored = Definition();
    const auto initialCurve =
        std::ranges::find_if(authored.Modules, [](const Keire::VfxModuleDefinition& module)
                             { return std::holds_alternative<Keire::VfxSizeOverLifetimeModule>(module.Payload); });
    const auto initialGradient =
        std::ranges::find_if(authored.Modules, [](const Keire::VfxModuleDefinition& module)
                             { return std::holds_alternative<Keire::VfxColorOverLifetimeModule>(module.Payload); });
    REQUIRE(initialCurve != authored.Modules.end());
    REQUIRE(initialGradient != authored.Modules.end());

    std::vector<Keire::CurveKey> curveKeys;
    std::vector<Keire::ColorGradientKey> gradientKeys;
    constexpr std::size_t keyCount = 32;
    curveKeys.reserve(keyCount);
    gradientKeys.reserve(keyCount);
    for (std::size_t index = 0; index < keyCount; ++index)
    {
        const auto time = static_cast<float>(index) / static_cast<float>(keyCount - 1);
        curveKeys.push_back({.Time = time, .Value = 1.0F});
        gradientKeys.push_back({.Time = time, .Value = Keire::Color{}});
    }

    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto controlUndo = undoService->CreateContext({.Name = "VFX control history"});
    KeireEditor::VfxEffectDocument control({.Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    control.Open(Id(350), authored, 1, controlUndo);
    REQUIRE(control.Edit("Control VFX storage", [](Keire::VfxEffectDefinition& definition) { ++definition.Seed; }));
    const auto controlBytes = controlUndo->EstimatedBytes();

    auto expandedUndo = undoService->CreateContext({.Name = "VFX expanded curve history"});
    KeireEditor::VfxEffectDocument expanded({.Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    expanded.Open(Id(351), authored, 1, expandedUndo);
    REQUIRE(expanded.Edit("Expand VFX curve storage",
                          [curveKeys, gradientKeys](Keire::VfxEffectDefinition& definition)
                          {
                              for (auto& module : definition.Modules)
                              {
                                  if (auto* size = std::get_if<Keire::VfxSizeOverLifetimeModule>(&module.Payload))
                                      size->Size = Keire::Curve1D(curveKeys);
                                  if (auto* color = std::get_if<Keire::VfxColorOverLifetimeModule>(&module.Payload))
                                      color->Color = Keire::ColorGradient(gradientKeys);
                              }
                          }));

    const auto initialCurveKeys = std::get<Keire::VfxSizeOverLifetimeModule>(initialCurve->Payload).Size.Keys().size();
    const auto initialGradientKeys =
        std::get<Keire::VfxColorOverLifetimeModule>(initialGradient->Payload).Color.Keys().size();
    const auto additionalBytes = (keyCount - initialCurveKeys) * sizeof(Keire::CurveKey) +
                                 (keyCount - initialGradientKeys) * sizeof(Keire::ColorGradientKey);
    CHECK(expandedUndo->EstimatedBytes() == controlBytes + additionalBytes);

    control.Close();
    expanded.Close();
    undoService->Close();
}

TEST_CASE("VFX graph connection preflight rejects backward particle flow without changing the draft")
{
    const auto authored = Definition();
    const auto& graph = authored.Systems.front();
    const auto initialize = std::ranges::find_if(
        graph.Nodes, [](const Keire::VfxGraphNode& node)
        { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == Keire::VfxContextType::Initialize; });
    const auto update = std::ranges::find_if(
        graph.Nodes, [](const Keire::VfxGraphNode& node)
        { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == Keire::VfxContextType::Update; });
    REQUIRE(initialize != graph.Nodes.end());
    REQUIRE(update != graph.Nodes.end());
    const auto initializeInput =
        std::ranges::find_if(initialize->Pins, [](const Keire::VfxGraphPin& pin)
                             { return pin.Input && pin.Type == Keire::VfxValueType::ParticleStream; });
    const auto updateOutput =
        std::ranges::find_if(update->Pins, [](const Keire::VfxGraphPin& pin)
                             { return !pin.Input && pin.Type == Keire::VfxValueType::ParticleStream; });
    REQUIRE(initializeInput != initialize->Pins.end());
    REQUIRE(updateOutput != update->Pins.end());

    KeireEditor::VfxEffectDocument document({.Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    document.Open(Id(331), authored, 1);
    const auto check =
        document.CheckConnection(graph.Id, update->Id, updateOutput->Id, initialize->Id, initializeInput->Id);

    CHECK(check.Status == KeireEditor::VfxGraphConnectionStatus::Rejected);
    CHECK(check.Diagnostic.find("backwards") != std::string::npos);
    CHECK(document.Definition() == authored);
    CHECK_FALSE(document.Dirty());
    document.Close();
}

TEST_CASE("VFX graph connection preflight rejects cycles and invalid bindings in incomplete drafts")
{
    const auto authored = Definition();
    const auto graphId = authored.Systems.front().Id;
    const Keire::VfxGraphNode firstUpdate{
        .Id = Id(360),
        .Type = "First detached update",
        .Context = Keire::VfxContextType::Update,
        .EditorPosition = {40.0F, 360.0F},
        .Pins =
            {
                {Id(361), "Particles", Keire::VfxValueType::ParticleStream, true, "particles", std::nullopt},
                {Id(362), "Particles", Keire::VfxValueType::ParticleStream, false, "particles", std::nullopt},
            },
        .CustomHlsl = "Size *= 1.0;",
        .Kind = Keire::VfxGraphNodeKind::CustomHlsl,
        .TypeId = {"keire.operator.portable-hlsl"},
    };
    const Keire::VfxGraphNode lastUpdate{
        .Id = Id(363),
        .Type = "Last detached update",
        .Context = Keire::VfxContextType::Update,
        .EditorPosition = {320.0F, 360.0F},
        .Pins =
            {
                {Id(364), "Particles", Keire::VfxValueType::ParticleStream, true, "particles", std::nullopt},
                {Id(365), "Particles", Keire::VfxValueType::ParticleStream, false, "particles", std::nullopt},
            },
        .CustomHlsl = "Size *= 1.0;",
        .Kind = Keire::VfxGraphNodeKind::CustomHlsl,
        .TypeId = {"keire.operator.portable-hlsl"},
    };
    KeireEditor::VfxEffectDocument document({.Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    document.Open(Id(332), authored, 1);
    REQUIRE(document.AddNode(graphId, firstUpdate));
    REQUIRE(document.AddNode(graphId, lastUpdate));
    REQUIRE(document.AddConnection(
        graphId, {Id(366), firstUpdate.Id, firstUpdate.Pins[1].Id, lastUpdate.Id, lastUpdate.Pins[0].Id}));
    const auto cycleDraft = document.Definition();
    const auto cycle =
        document.CheckConnection(graphId, lastUpdate.Id, lastUpdate.Pins[1].Id, firstUpdate.Id, firstUpdate.Pins[0].Id);
    CHECK(cycle.Status == KeireEditor::VfxGraphConnectionStatus::Rejected);
    CHECK(cycle.Diagnostic.find("acyclic") != std::string::npos);
    CHECK(document.Definition() == cycleDraft);
    REQUIRE(document.RemoveNode(graphId, firstUpdate.Id));
    REQUIRE(document.RemoveNode(graphId, lastUpdate.Id));
    REQUIRE(document.Publishable());

    const Keire::VfxBlackboardParameter parameter{
        .Id = Id(333),
        .Name = "Draft Amount",
        .Type = Keire::VfxValueType::Scalar,
        .DefaultValue = 1.0F,
    };
    const Keire::VfxGraphNode parameterNode{
        .Id = Id(334),
        .Type = "Draft Amount",
        .Context = Keire::VfxContextType::Update,
        .EditorPosition = {-320.0F, 140.0F},
        .Pins = {{Id(335), "Value", Keire::VfxValueType::Scalar, false, "value", std::nullopt}},
        .Kind = Keire::VfxGraphNodeKind::Parameter,
        .Reference = parameter.Id,
        .TypeId = {"keire.parameter"},
    };
    auto custom = Keire::CreateVfxGraphPortableHlslBlock("Size *= amount;");
    custom.Id = Id(336);
    custom.Pins = {{Id(338), "Amount", Keire::VfxValueType::Scalar, true, "amount", Keire::VfxParameterValue{1.0F}}};
    const auto updateContext = std::ranges::find_if(
        authored.Systems.front().Nodes, [](const Keire::VfxGraphNode& node)
        { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == Keire::VfxContextType::Update; });
    REQUIRE(updateContext != authored.Systems.front().Nodes.end());
    CHECK(document.AddBlackboardParameter(parameter));
    CHECK(document.AddNode(graphId, parameterNode));
    CHECK(document.AddBlock(graphId, updateContext->Id, custom));
    REQUIRE(document.Publishable());

    const auto validRepair = document.CheckConnection(graphId, {parameterNode.Id, {}, parameterNode.Pins.front().Id},
                                                      {updateContext->Id, custom.Id, custom.Pins.front().Id});
    CHECK(validRepair.Status == KeireEditor::VfxGraphConnectionStatus::Accepted);
    CHECK(validRepair.Diagnostic.empty());

    const auto descriptor = std::ranges::find_if(
        Keire::VfxNodeCatalog(),
        [](const Keire::VfxNodeDescriptor& candidate)
        {
            return candidate.Class == Keire::VfxNodeClass::Operator &&
                   candidate.SupportTier != Keire::VfxNodeSupportTier::Disabled &&
                   std::ranges::any_of(candidate.Pins, [](const Keire::VfxNodePinDescriptor& pin)
                                       { return !pin.Input && pin.Type == Keire::VfxValueType::Scalar; });
        });
    REQUIRE(descriptor != Keire::VfxNodeCatalog().end());
    auto operatorNode = Keire::CreateVfxGraphOperatorNode(descriptor->TypeId.View(), {-20.0F, 260.0F});
    const auto operatorOutput = std::ranges::find_if(operatorNode.Pins, [](const Keire::VfxGraphPin& pin)
                                                     { return !pin.Input && pin.Type == Keire::VfxValueType::Scalar; });
    REQUIRE(operatorOutput != operatorNode.Pins.end());
    CHECK(document.AddNode(graphId, operatorNode));

    const auto invalidBinding = document.CheckConnection(graphId, {operatorNode.Id, {}, operatorOutput->Id},
                                                         {updateContext->Id, custom.Id, custom.Pins.front().Id});
    CHECK(invalidBinding.Status == KeireEditor::VfxGraphConnectionStatus::Rejected);
    CHECK(invalidBinding.Diagnostic.find("Blackboard") != std::string::npos);
    CHECK(document.Definition().Systems.front().Connections == authored.Systems.front().Connections);
    document.Close();
}

TEST_CASE("VFX graph mode adds executable nodes without implicitly splicing particle flow")
{
    auto authored = Definition();
    const auto graphId = authored.Systems.front().Id;
    const Keire::VfxModuleDefinition burst{
        .Id = Id(340),
        .Payload = Keire::VfxBurstModule{.Time = 0.1F, .Count = 4, .Cycles = 1, .Interval = 0.1F},
    };

    KeireEditor::VfxEffectDocument document({.Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    document.Open(Id(341), authored, 1);
    CHECK(document.AddModule(burst));
    const auto beforeModuleNode = document.Definition().Systems.front().Connections;
    const auto moduleNode = Keire::CreateVfxGraphModuleNode(burst, {175.0F, -90.0F});
    CHECK(document.AddNode(graphId, moduleNode));
    CHECK(document.Definition().Systems.front().Connections == beforeModuleNode);
    CHECK(std::ranges::find(document.Definition().Systems.front().Nodes, moduleNode.Id, &Keire::VfxGraphNode::Id) !=
          document.Definition().Systems.front().Nodes.end());
    CHECK_FALSE(document.Publishable());

    const Keire::VfxGraphNode custom{
        .Id = Id(342),
        .Type = "Detached Custom HLSL",
        .Context = Keire::VfxContextType::Update,
        .EditorPosition = {475.0F, -90.0F},
        .Pins =
            {
                {Id(343), "Particles", Keire::VfxValueType::ParticleStream, true, "particles", std::nullopt},
                {Id(344), "Particles", Keire::VfxValueType::ParticleStream, false, "particles", std::nullopt},
            },
        .CustomHlsl = "Size *= 1.0;",
        .Kind = Keire::VfxGraphNodeKind::CustomHlsl,
        .TypeId = {"keire.operator.portable-hlsl"},
    };
    const auto beforeCustomNode = document.Definition().Systems.front().Connections;
    CHECK(document.AddNode(graphId, custom));
    CHECK(document.Definition().Systems.front().Connections == beforeCustomNode);
    CHECK(std::ranges::find(document.Definition().Systems.front().Nodes, custom.Id, &Keire::VfxGraphNode::Id) !=
          document.Definition().Systems.front().Nodes.end());
    CHECK_FALSE(document.Publishable());
    document.Close();
}

TEST_CASE("VFX graph authoring persists Operator contexts inline values settings and Block literals")
{
    KeireEditor::VfxEffectDocument document({.Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    const auto authored = Definition();
    const auto systemId = authored.Systems.front().Id;
    document.Open(Id(360), authored, 1);

    auto range = Keire::CreateVfxGraphOperatorNode("keire.operator.range", {-320.0F, 120.0F});
    const auto rangeId = range.Id;
    CHECK(document.AddNode(systemId, std::move(range)));
    CHECK(document.EditNode(systemId, rangeId,
                            [](Keire::VfxGraphNode& node)
                            {
                                node.Context = Keire::VfxContextType::Initialize;
                                const auto minimum = std::ranges::find(node.Pins, std::string_view("minimum"),
                                                                       [](const Keire::VfxGraphPin& pin)
                                                                       { return std::string_view(pin.Semantic); });
                                const auto maximum = std::ranges::find(node.Pins, std::string_view("maximum"),
                                                                       [](const Keire::VfxGraphPin& pin)
                                                                       { return std::string_view(pin.Semantic); });
                                REQUIRE(minimum != node.Pins.end());
                                REQUIRE(maximum != node.Pins.end());
                                minimum->DefaultValue = 1.0F;
                                maximum->DefaultValue = 20.0F;
                            }));

    auto random = Keire::CreateVfxGraphOperatorNode("keire.operator.random-range", {-80.0F, 120.0F});
    random.Context = Keire::VfxContextType::Initialize;
    const auto randomId = random.Id;
    CHECK(document.AddNode(systemId, std::move(random)));
    CHECK(document.EditNode(systemId, randomId,
                            [](Keire::VfxGraphNode& node)
                            {
                                const auto scope = std::ranges::find(node.Properties, std::string_view("Scope"),
                                                                     [](const Keire::VfxGraphProperty& property)
                                                                     { return std::string_view(property.Name); });
                                const auto constant = std::ranges::find(node.Properties, std::string_view("Constant"),
                                                                        [](const Keire::VfxGraphProperty& property)
                                                                        { return std::string_view(property.Name); });
                                REQUIRE(scope != node.Properties.end());
                                REQUIRE(constant != node.Properties.end());
                                scope->Value = static_cast<std::uint64_t>(Keire::VfxRandomScope::PerVfxComponent);
                                constant->Value = true;
                            }));

    auto compare = Keire::CreateVfxGraphOperatorNode("keire.operator.compare", {160.0F, 120.0F});
    const auto compareId = compare.Id;
    CHECK(document.AddNode(systemId, std::move(compare)));
    CHECK(document.EditNode(systemId, compareId,
                            [](Keire::VfxGraphNode& node)
                            {
                                const auto condition = std::ranges::find(node.Properties, std::string_view("Condition"),
                                                                         [](const Keire::VfxGraphProperty& property)
                                                                         { return std::string_view(property.Name); });
                                REQUIRE(condition != node.Properties.end());
                                condition->Value = std::string("Greater Or Equal");
                            }));

    auto remap = Keire::CreateVfxGraphOperatorNode("keire.operator.remap", {380.0F, 120.0F});
    const auto remapId = remap.Id;
    CHECK(document.AddNode(systemId, std::move(remap)));
    CHECK(document.EditNode(systemId, remapId,
                            [](Keire::VfxGraphNode& node)
                            {
                                const auto clamp = std::ranges::find(node.Properties, std::string_view("Clamp"),
                                                                     [](const Keire::VfxGraphProperty& property)
                                                                     { return std::string_view(property.Name); });
                                REQUIRE(clamp != node.Properties.end());
                                clamp->Value = true;
                            }));

    const auto& definition = document.Definition();
    const auto& system = definition.Systems.front();
    const auto persistedRange = std::ranges::find(system.Nodes, rangeId, &Keire::VfxGraphNode::Id);
    REQUIRE(persistedRange != system.Nodes.end());
    CHECK(persistedRange->Context == Keire::VfxContextType::Initialize);
    CHECK(
        std::get<float>(*std::ranges::find(persistedRange->Pins, std::string_view("minimum"),
                                           [](const Keire::VfxGraphPin& pin) { return std::string_view(pin.Semantic); })
                             ->DefaultValue) == 1.0F);
    CHECK(
        std::get<float>(*std::ranges::find(persistedRange->Pins, std::string_view("maximum"),
                                           [](const Keire::VfxGraphPin& pin) { return std::string_view(pin.Semantic); })
                             ->DefaultValue) == 20.0F);

    const auto persistedRandom = std::ranges::find(system.Nodes, randomId, &Keire::VfxGraphNode::Id);
    REQUIRE(persistedRandom != system.Nodes.end());
    CHECK(std::get<std::uint64_t>(std::ranges::find(persistedRandom->Properties, std::string_view("Scope"),
                                                    [](const Keire::VfxGraphProperty& property)
                                                    { return std::string_view(property.Name); })
                                      ->Value) == static_cast<std::uint64_t>(Keire::VfxRandomScope::PerVfxComponent));
    CHECK(std::get<bool>(std::ranges::find(persistedRandom->Properties, std::string_view("Constant"),
                                           [](const Keire::VfxGraphProperty& property)
                                           { return std::string_view(property.Name); })
                             ->Value));
    CHECK(std::get<std::string>(
              std::ranges::find(system.Nodes, compareId, &Keire::VfxGraphNode::Id)->Properties.front().Value) ==
          "Greater Or Equal");
    CHECK(std::get<bool>(std::ranges::find(system.Nodes, remapId, &Keire::VfxGraphNode::Id)->Properties.front().Value));

    const auto initializeContext = std::ranges::find_if(
        system.Nodes, [](const Keire::VfxGraphNode& node)
        { return node.Kind == Keire::VfxGraphNodeKind::Context && node.Context == Keire::VfxContextType::Initialize; });
    REQUIRE(initializeContext != system.Nodes.end());
    const auto initializeBlock = std::ranges::find_if(
        initializeContext->Blocks,
        [&](const Keire::VfxGraphBlock& block)
        {
            const auto module = std::ranges::find(definition.Modules, block.Reference, &Keire::VfxModuleDefinition::Id);
            return module != definition.Modules.end() &&
                   std::holds_alternative<Keire::VfxInitializeModule>(module->Payload);
        });
    REQUIRE(initializeBlock != initializeContext->Blocks.end());
    const auto lifetime =
        std::ranges::find(initializeBlock->Pins, std::string_view("lifetimeMinimum"),
                          [](const Keire::VfxGraphPin& pin) { return std::string_view(pin.Semantic); });
    REQUIRE(lifetime != initializeBlock->Pins.end());
    const auto initializeContextId = initializeContext->Id;
    const auto initializeBlockId = initializeBlock->Id;
    const auto lifetimeId = lifetime->Id;
    CHECK(document.EditBlockPin(systemId, initializeContextId, initializeBlockId, lifetimeId,
                                [](Keire::VfxGraphPin& pin) { pin.DefaultValue = 3.5F; }));
    const auto& editedSystem = document.Definition().Systems.front();
    const auto editedContext = std::ranges::find(editedSystem.Nodes, initializeContextId, &Keire::VfxGraphNode::Id);
    REQUIRE(editedContext != editedSystem.Nodes.end());
    const auto editedBlock = std::ranges::find(editedContext->Blocks, initializeBlockId, &Keire::VfxGraphBlock::Id);
    REQUIRE(editedBlock != editedContext->Blocks.end());
    CHECK(std::get<float>(*std::ranges::find(editedBlock->Pins, lifetimeId, &Keire::VfxGraphPin::Id)->DefaultValue) ==
          3.5F);

    auto age = Keire::CreateVfxGraphOperatorNode("keire.operator.age");
    const auto ageId = age.Id;
    CHECK(document.AddNode(systemId, std::move(age)));
    CHECK_THROWS_AS((void)document.EditNode(systemId, ageId, [](Keire::VfxGraphNode& node)
                                            { node.Context = Keire::VfxContextType::Spawn; }),
                    std::invalid_argument);
    CHECK(std::ranges::find(document.Definition().Systems.front().Nodes, ageId, &Keire::VfxGraphNode::Id)->Context ==
          Keire::VfxContextType::Update);
    document.Close();
}

TEST_CASE("VFX effect document converts legacy Runtime Modules to an undoable executable graph")
{
    auto legacy = Definition();
    legacy.SchemaVersion = 2;
    legacy.ExecutionSource = Keire::VfxExecutionSource::LegacyModules;

    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "VFX Graph Conversion"});
    KeireEditor::VfxEffectDocument document({.Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    document.Open(Id(106), legacy, 1, undo);

    CHECK(document.ConvertToGraph());
    CHECK(document.Definition().SchemaVersion == Keire::CurrentVfxSchemaVersion);
    CHECK(document.Definition().ExecutionSource == Keire::VfxExecutionSource::Graph);
    CHECK(std::ranges::none_of(document.Definition().Systems,
                               [](const Keire::VfxGraphSystem& system)
                               {
                                   return std::ranges::any_of(system.Nodes, [](const Keire::VfxGraphNode& node)
                                                              { return node.Kind == Keire::VfxGraphNodeKind::Module; });
                               }));
    for (const auto& module : document.Definition().Modules)
    {
        CHECK(std::ranges::any_of(document.Definition().Systems,
                                  [&](const Keire::VfxGraphSystem& system)
                                  {
                                      return std::ranges::any_of(
                                          system.Nodes,
                                          [&](const Keire::VfxGraphNode& node)
                                          {
                                              return node.Kind == Keire::VfxGraphNodeKind::Context &&
                                                     std::ranges::any_of(node.Blocks,
                                                                         [&](const Keire::VfxGraphBlock& block)
                                                                         { return block.Reference == module.Id; });
                                          });
                                  }));
    }
    CHECK_FALSE(document.ConvertToGraph());

    CHECK(document.Undo());
    CHECK(document.Definition() == legacy);
    CHECK(document.Redo());
    CHECK(document.Definition().ExecutionSource == Keire::VfxExecutionSource::Graph);

    const auto graphBeforeCustomNode = document.Definition();
    const Keire::VfxGraphNode custom{
        .Id = Id(82),
        .Type = "Scale Size",
        .Context = Keire::VfxContextType::Update,
        .Pins =
            {
                {Id(83), "Particles", Keire::VfxValueType::ParticleStream, true, "particles"},
                {Id(84), "Particles", Keire::VfxValueType::ParticleStream, false, "particles"},
            },
        .CustomHlsl = "Size *= 1.0;",
        .Kind = Keire::VfxGraphNodeKind::CustomHlsl,
        .TypeId = {"keire.operator.portable-hlsl"},
    };
    CHECK(document.AddNode(document.Definition().Systems.front().Id, custom));
    CHECK(document.Definition().Systems.front().Connections == graphBeforeCustomNode.Systems.front().Connections);
    CHECK(document.Definition().Systems.front().Nodes.size() == graphBeforeCustomNode.Systems.front().Nodes.size() + 1);
    CHECK_FALSE(document.Publishable());
    CHECK(document.GraphDiagnostic().find("main particle stream") != std::string_view::npos);
    CHECK(document.RemoveNode(document.Definition().Systems.front().Id, custom.Id));
    CHECK(document.Definition() == graphBeforeCustomNode);
    CHECK(document.Publishable());

    document.Close();
    undoService->Close();
}

TEST_CASE("VFX document keeps Blackboard references typed and gives each graph input one writer")
{
    const Keire::VfxBlackboardParameter speed{
        .Id = Id(70),
        .Name = "Speed",
        .Type = Keire::VfxValueType::Scalar,
        .DefaultValue = 1.0F,
    };
    const Keire::VfxBlackboardParameter alternate{
        .Id = Id(71),
        .Name = "Alternate",
        .Type = Keire::VfxValueType::Scalar,
        .DefaultValue = 2.0F,
    };

    auto legacy = Definition();
    legacy.SchemaVersion = 2;
    legacy.ExecutionSource = Keire::VfxExecutionSource::LegacyModules;
    legacy.Systems.clear();
    legacy.Blackboard = {speed, alternate};
    const auto authored = Keire::ConvertVfxEffectToGraph(legacy);
    REQUIRE(authored.Systems.size() == 1);

    const auto emissionModule =
        std::ranges::find_if(authored.Modules, [](const Keire::VfxModuleDefinition& module)
                             { return std::holds_alternative<Keire::VfxEmissionRateModule>(module.Payload); });
    REQUIRE(emissionModule != authored.Modules.end());
    const auto& authoredSystem = authored.Systems.front();
    const auto emissionContext =
        std::ranges::find_if(authoredSystem.Nodes,
                             [&](const Keire::VfxGraphNode& node)
                             {
                                 return node.Kind == Keire::VfxGraphNodeKind::Context &&
                                        std::ranges::any_of(node.Blocks, [&](const Keire::VfxGraphBlock& block)
                                                            { return block.Reference == emissionModule->Id; });
                             });
    REQUIRE(emissionContext != authoredSystem.Nodes.end());
    const auto emissionBlock =
        std::ranges::find(emissionContext->Blocks, emissionModule->Id, &Keire::VfxGraphBlock::Reference);
    REQUIRE(emissionBlock != emissionContext->Blocks.end());
    const auto emissionRatePin =
        std::ranges::find(emissionBlock->Pins, std::string("particlesPerSecond"), &Keire::VfxGraphPin::Semantic);
    REQUIRE(emissionRatePin != emissionBlock->Pins.end());
    const auto speedNode =
        std::ranges::find_if(authoredSystem.Nodes, [&](const Keire::VfxGraphNode& node)
                             { return node.Kind == Keire::VfxGraphNodeKind::Parameter && node.Reference == speed.Id; });
    REQUIRE(speedNode != authoredSystem.Nodes.end());
    const auto alternateNode = std::ranges::find_if(
        authoredSystem.Nodes, [&](const Keire::VfxGraphNode& node)
        { return node.Kind == Keire::VfxGraphNodeKind::Parameter && node.Reference == alternate.Id; });
    REQUIRE(alternateNode != authoredSystem.Nodes.end());

    const auto system = authoredSystem.Id;
    const auto emissionNodeId = emissionContext->Id;
    const auto emissionBlockId = emissionBlock->Id;
    const auto emissionRatePinId = emissionRatePin->Id;
    const auto speedNodeId = speedNode->Id;
    const auto speedPinId = speedNode->Pins.front().Id;
    const auto alternateNodeId = alternateNode->Id;
    const auto alternatePinId = alternateNode->Pins.front().Id;

    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "VFX Blackboard Bindings"});
    KeireEditor::VfxEffectDocument document({.Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    document.Open(Id(107), authored, 1, undo);

    const Keire::VfxGraphConnection speedWriter{
        .Id = Id(80),
        .OutputNode = speedNodeId,
        .OutputPin = speedPinId,
        .InputNode = emissionNodeId,
        .InputPin = emissionRatePinId,
        .InputBlock = emissionBlockId,
    };
    const Keire::VfxGraphConnection alternateWriter{
        .Id = Id(81),
        .OutputNode = alternateNodeId,
        .OutputPin = alternatePinId,
        .InputNode = emissionNodeId,
        .InputPin = emissionRatePinId,
        .InputBlock = emissionBlockId,
    };
    CHECK(document.AddConnection(system, speedWriter));
    CHECK(document.AddConnection(system, alternateWriter));
    CHECK(std::ranges::count(document.Definition().Systems.front().Connections, emissionRatePinId,
                             &Keire::VfxGraphConnection::InputPin) == 1);
    CHECK(std::ranges::find(document.Definition().Systems.front().Connections, alternateWriter.Id,
                            &Keire::VfxGraphConnection::Id) != document.Definition().Systems.front().Connections.end());

    CHECK(document.AddConnection(system, speedWriter));
    CHECK(document.EditBlackboardParameter(speed.Id,
                                           [](Keire::VfxBlackboardParameter& parameter)
                                           {
                                               parameter.Name = "Velocity";
                                               parameter.Type = Keire::VfxValueType::Vector3;
                                               parameter.DefaultValue = Keire::Vector3{1.0F, 2.0F, 3.0F};
                                           }));
    const auto& changedSystem = document.Definition().Systems.front();
    const auto changedNode = std::ranges::find(changedSystem.Nodes, speedNodeId, &Keire::VfxGraphNode::Id);
    REQUIRE(changedNode != changedSystem.Nodes.end());
    CHECK(changedNode->Pins.front().Name == "Velocity");
    CHECK(changedNode->Pins.front().Type == Keire::VfxValueType::Vector3);
    CHECK(std::ranges::find(changedSystem.Connections, speedWriter.Id, &Keire::VfxGraphConnection::Id) ==
          changedSystem.Connections.end());

    CHECK(document.Undo());
    CHECK(document.RemoveBlackboardParameter(speed.Id));
    const auto& removedSystem = document.Definition().Systems.front();
    CHECK(std::ranges::find(removedSystem.Nodes, speedNodeId, &Keire::VfxGraphNode::Id) == removedSystem.Nodes.end());
    CHECK(
        std::ranges::none_of(removedSystem.Connections, [&](const Keire::VfxGraphConnection& connection)
                             { return connection.OutputNode == speedNodeId || connection.InputNode == speedNodeId; }));
    CHECK(document.Undo());
    CHECK(std::ranges::find(document.Definition().Systems.front().Nodes, speedNodeId, &Keire::VfxGraphNode::Id) !=
          document.Definition().Systems.front().Nodes.end());

    document.Close();
    undoService->Close();
}

TEST_CASE("VFX graph and blackboard edits preserve stable identities and reject invalid candidates transactionally")
{
    auto authored = Definition();
    authored.ExecutionSource = Keire::VfxExecutionSource::LegacyModules;
    Keire::VfxGraphSystem system{
        .Id = Id(50),
        .Name = "Stable System",
        .Nodes =
            {
                {
                    .Id = Id(51),
                    .Type = "Output",
                    .Context = Keire::VfxContextType::Update,
                    .Pins = {{Id(52), "Value", Keire::VfxValueType::Scalar, false}},
                    .TypeId = {"keire.context.update"},
                },
                {
                    .Id = Id(53),
                    .Type = "Input",
                    .Context = Keire::VfxContextType::Update,
                    .Pins = {{Id(54), "Value", Keire::VfxValueType::Scalar, true}},
                    .TypeId = {"keire.context.update"},
                },
            },
        .Connections =
            {
                {
                    .Id = Id(55),
                    .OutputNode = Id(51),
                    .OutputPin = Id(52),
                    .InputNode = Id(53),
                    .InputPin = Id(54),
                },
            },
    };
    authored.Systems.push_back(system);
    authored.Blackboard.push_back(
        {.Id = Id(56), .Name = "Speed", .Type = Keire::VfxValueType::Scalar, .DefaultValue = 1.0F});

    std::size_t previews = 0;
    KeireEditor::VfxEffectDocument document(
        {.Preview = [&](const Keire::AssetId, const Keire::VfxEffectDefinition&) { ++previews; },
         .Persist = [](const Keire::AssetId, const std::span<const std::byte>) {}});
    document.Open(Id(105), authored, 1);
    const auto baselinePreviews = previews;

    CHECK_THROWS_AS((void)document.EditSystem(system.Id, [](Keire::VfxGraphSystem& graph) { graph.Id = Id(60); }),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        (void)document.EditSystem(system.Id, [](Keire::VfxGraphSystem& graph) { graph.Nodes.front().Id = Id(61); }),
        std::invalid_argument);
    CHECK_THROWS_AS(
        (void)document.EditNode(system.Id, Id(51), [](Keire::VfxGraphNode& node) { node.Pins.front().Id = Id(62); }),
        std::invalid_argument);
    CHECK_THROWS_AS((void)document.EditPin(system.Id, Id(51), Id(52), [](Keire::VfxGraphPin& pin) { pin.Id = Id(69); }),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)document.EditConnection(system.Id, Id(55), [](Keire::VfxGraphConnection& connection)
                                                  { connection.Id = Id(63); }),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)document.EditBlackboardParameter(Id(56), [](Keire::VfxBlackboardParameter& parameter)
                                                           { parameter.Id = Id(64); }),
                    std::invalid_argument);

    auto duplicateParameter = authored.Blackboard.front();
    duplicateParameter.Id = Id(65);
    CHECK_THROWS_AS((void)document.AddBlackboardParameter(duplicateParameter), std::invalid_argument);
    CHECK_THROWS_AS(
        (void)document.AddConnection(
            system.Id,
            {.Id = Id(66), .OutputNode = Id(51), .OutputPin = Id(52), .InputNode = Id(53), .InputPin = Id(67)}),
        std::invalid_argument);
    CHECK_THROWS_AS((void)document.RemoveSystem(Id(68)), std::invalid_argument);
    CHECK_THROWS_AS((void)document.RemoveNode(system.Id, Id(68)), std::invalid_argument);
    CHECK_THROWS_AS((void)document.RemovePin(system.Id, Id(51), Id(68)), std::invalid_argument);
    CHECK_THROWS_AS((void)document.RemoveConnection(system.Id, Id(68)), std::invalid_argument);
    CHECK_THROWS_AS((void)document.RemoveBlackboardParameter(Id(68)), std::invalid_argument);

    CHECK(document.Definition() == authored);
    CHECK(previews == baselinePreviews);
    document.Close();
}

TEST_CASE("Discarding a newly created VFX effect stops preview without persistence")
{
    std::size_t previews = 0;
    std::size_t stops = 0;
    std::size_t persists = 0;
    KeireEditor::VfxEffectDocument document(
        {.Preview = [&](const Keire::AssetId, const Keire::VfxEffectDefinition&) { ++previews; },
         .StopPreview = [&](const Keire::AssetId) { ++stops; },
         .Persist = [&](const Keire::AssetId, const std::span<const std::byte>) { ++persists; }});
    document.Create(Id(102), Definition());
    CHECK(document.Dirty());
    CHECK(previews == 1);
    document.Discard();
    CHECK_FALSE(document.IsOpen());
    CHECK(stops == 1);
    CHECK(persists == 0);
}

TEST_CASE("VFX effect document preserves curve and gradient edits through undo and save")
{
    auto undoService = Keire::CreateRef<Keire::UndoService>();
    auto undo = undoService->CreateContext({.Name = "VFX Curves"});
    std::vector<std::byte> persisted;
    KeireEditor::VfxEffectDocument document(
        {.Persist = [&](const Keire::AssetId, const std::span<const std::byte> bytes)
         { persisted.assign(bytes.begin(), bytes.end()); }});
    const auto authored = Definition();
    const auto size =
        std::ranges::find_if(authored.Modules, [](const Keire::VfxModuleDefinition& module)
                             { return std::holds_alternative<Keire::VfxSizeOverLifetimeModule>(module.Payload); });
    const auto color =
        std::ranges::find_if(authored.Modules, [](const Keire::VfxModuleDefinition& module)
                             { return std::holds_alternative<Keire::VfxColorOverLifetimeModule>(module.Payload); });
    REQUIRE(size != authored.Modules.end());
    REQUIRE(color != authored.Modules.end());

    document.Open(Id(103), authored, 1, undo);
    CHECK(document.EditModule(
        size->Id, [](Keire::VfxModuleDefinition& module)
        { std::get<Keire::VfxSizeOverLifetimeModule>(module.Payload).Size = Keire::Curve1D::Linear(0.5F, 2.0F); }));
    CHECK(document.EditModule(color->Id,
                              [](Keire::VfxModuleDefinition& module)
                              {
                                  std::get<Keire::VfxColorOverLifetimeModule>(module.Payload).Color =
                                      Keire::ColorGradient(
                                          {{0.0F, {1.0F, 0.5F, 0.0F, 1.0F}}, {1.0F, {0.1F, 0.0F, 0.0F, 0.0F}}});
                              }));
    const auto findModulePin = [&](const Keire::AssetId module,
                                   const std::string_view semantic) -> const Keire::VfxGraphPin&
    {
        for (const auto& node : document.Definition().Systems.front().Nodes)
        {
            const auto block = std::ranges::find(node.Blocks, module, &Keire::VfxGraphBlock::Reference);
            if (block == node.Blocks.end())
                continue;
            const auto pin = std::ranges::find(block->Pins, semantic, &Keire::VfxGraphPin::Semantic);
            if (pin != block->Pins.end())
                return *pin;
        }
        throw std::logic_error("Expected Runtime Module Block pin was unavailable.");
    };
    CHECK(std::get<float>(*findModulePin(size->Id, "size").DefaultValue) == doctest::Approx(0.5F));
    CHECK(std::get<Keire::Color>(*findModulePin(color->Id, "color").DefaultValue) ==
          (Keire::Color{1.0F, 0.5F, 0.0F, 1.0F}));
    CHECK(document.Definition().Modules[size - authored.Modules.begin()].Id == size->Id);
    CHECK(document.Definition().Modules[color - authored.Modules.begin()].Id == color->Id);
    CHECK(document.Undo());
    CHECK(std::get<Keire::VfxColorOverLifetimeModule>(
              document.Definition().Modules[color - authored.Modules.begin()].Payload)
              .Color == std::get<Keire::VfxColorOverLifetimeModule>(color->Payload).Color);
    CHECK(document.Redo());

    document.Save();
    REQUIRE_FALSE(persisted.empty());
    CHECK(Keire::VfxEffectAsset::Decode(persisted)->Definition() == document.Definition());
    document.Close();
    undoService->Close();
}

TEST_CASE("Edit-mode VFX emitter collection follows authored scene eligibility and world placement")
{
    auto scene = Keire::CreateRef<Keire::Scene>(Id(200), Keire::SceneAsset::EmptyDefinition("Edit-mode VFX preview"));
    const auto parent = scene->CreateEntity("Parent");
    parent.GetComponent<Keire::TransformComponent>()->SetLocalPosition({10.0F, 0.0F, 0.0F});
    auto entity = scene->CreateEntity("Preview emitter", parent);
    entity.GetComponent<Keire::TransformComponent>()->SetLocalPosition({2.0F, 3.0F, 4.0F});
    const auto emitter = entity.AddComponent<Keire::VfxEmitterComponent>();
    REQUIRE(emitter);
    emitter->SetEffect(Id(201));
    emitter->SetSeedOffset(17);
    emitter->SetSimulationSpeed(1.5F);

    CHECK(KeireEditor::CollectEditModeVfxEmitters(scene).empty());
    emitter->SetEditModePreview(true);

    auto collected = KeireEditor::CollectEditModeVfxEmitters(scene);
    REQUIRE(collected.size() == 1);
    CHECK(collected.front().Entity == entity.Id());
    CHECK(collected.front().Effect == Id(201));
    CHECK(collected.front().SeedOffset == 17);
    CHECK(collected.front().SimulationSpeed == doctest::Approx(1.5F));
    CHECK(collected.front().Position == (Keire::Vector3{12.0F, 3.0F, 4.0F}));

    emitter->SetEnabled(false);
    CHECK(KeireEditor::CollectEditModeVfxEmitters(scene).empty());
    emitter->SetEnabled(true);
    entity.SetActive(false);
    CHECK(KeireEditor::CollectEditModeVfxEmitters(scene).empty());
    entity.SetActive(true);
    emitter->SetEffect({});
    CHECK(KeireEditor::CollectEditModeVfxEmitters(scene).empty());
    emitter->SetEffect(Id(201));

    auto replacement = Keire::CreateRef<Keire::Scene>(Id(202), Keire::SceneAsset::EmptyDefinition("Replacement scene"));
    CHECK(KeireEditor::CollectEditModeVfxEmitters(replacement).empty());
    REQUIRE(scene->DestroyEntity(entity.Id()));
    CHECK(KeireEditor::CollectEditModeVfxEmitters(scene).empty());
}

TEST_CASE("Edit-mode VFX preview restarts only relocated world-space effects")
{
    constexpr Keire::Vector3 position{1.0F, 2.0F, 3.0F};
    constexpr Keire::Quaternion rotation{};
    CHECK_FALSE(KeireEditor::EditModeVfxPreviewRequiresRestart(Keire::VfxSimulationSpace::World, position, rotation,
                                                               {1.00001F, 2.0F, 3.0F}, rotation));
    CHECK(KeireEditor::EditModeVfxPreviewRequiresRestart(Keire::VfxSimulationSpace::World, position, rotation,
                                                         {2.0F, 2.0F, 3.0F}, rotation));
    CHECK_FALSE(KeireEditor::EditModeVfxPreviewRequiresRestart(Keire::VfxSimulationSpace::Local, position, rotation,
                                                               {2.0F, 2.0F, 3.0F}, rotation));

    constexpr Keire::Quaternion sameRotationOppositeSign{0.0F, 0.0F, 0.0F, -1.0F};
    CHECK_FALSE(KeireEditor::EditModeVfxPreviewRequiresRestart(Keire::VfxSimulationSpace::World, position, rotation,
                                                               position, sameRotationOppositeSign));
    CHECK(KeireEditor::EditModeVfxPreviewRequiresRestart(Keire::VfxSimulationSpace::World, position, rotation, position,
                                                         {0.0F, 0.7071068F, 0.0F, 0.7071068F}));

    constexpr Keire::Vector3 firstSmallMove{1.000075F, 2.0F, 3.0F};
    constexpr Keire::Vector3 accumulatedMove{1.00015F, 2.0F, 3.0F};
    CHECK_FALSE(KeireEditor::EditModeVfxPreviewRequiresRestart(Keire::VfxSimulationSpace::World, position, rotation,
                                                               firstSmallMove, rotation));
    CHECK_FALSE(KeireEditor::EditModeVfxPreviewRequiresRestart(Keire::VfxSimulationSpace::World, firstSmallMove,
                                                               rotation, accumulatedMove, rotation));
    CHECK(KeireEditor::EditModeVfxPreviewRequiresRestart(Keire::VfxSimulationSpace::World, position, rotation,
                                                         accumulatedMove, rotation));

    constexpr Keire::Quaternion firstSmallRotation{0.0F, 0.008726535F, 0.0F, 0.999961923F};
    constexpr Keire::Quaternion accumulatedRotation{0.0F, 0.017452406F, 0.0F, 0.999847695F};
    CHECK_FALSE(KeireEditor::EditModeVfxPreviewRequiresRestart(Keire::VfxSimulationSpace::World, position, rotation,
                                                               position, firstSmallRotation));
    CHECK_FALSE(KeireEditor::EditModeVfxPreviewRequiresRestart(Keire::VfxSimulationSpace::World, position,
                                                               firstSmallRotation, position, accumulatedRotation));
    CHECK(KeireEditor::EditModeVfxPreviewRequiresRestart(Keire::VfxSimulationSpace::World, position, rotation, position,
                                                         accumulatedRotation));

    CHECK_FALSE(KeireEditor::EditModeVfxDraftShouldActivate(true, false, false));
    CHECK(KeireEditor::EditModeVfxDraftShouldActivate(true, true, false));
    CHECK_FALSE(KeireEditor::EditModeVfxDraftShouldActivate(false, true, false));
    CHECK(KeireEditor::EditModeVfxDraftShouldActivate(false, false, true));
}

TEST_CASE("VFX draft preview selects one matching scene host without suppressing unrelated emitters")
{
    const Keire::EntityId first(Id(210));
    const Keire::EntityId preferred(Id(211));
    const Keire::EntityId unrelated(Id(212));
    const std::vector<KeireEditor::EditModeVfxEmitterSnapshot> emitters{
        {.Entity = preferred, .Effect = Id(220), .Position = {3.0F, 0.0F, 0.0F}},
        {.Entity = unrelated, .Effect = Id(221), .Position = {2.0F, 0.0F, 0.0F}},
        {.Entity = first, .Effect = Id(220), .Position = {1.0F, 0.0F, 0.0F}},
    };

    const auto selected = KeireEditor::SelectEditModeVfxDraftHost(emitters, Id(220), preferred);
    REQUIRE(selected);
    CHECK(selected->Entity == preferred);
    CHECK(selected->Position == (Keire::Vector3{3.0F, 0.0F, 0.0F}));

    const auto fallback = KeireEditor::SelectEditModeVfxDraftHost(emitters, Id(220), unrelated);
    REQUIRE(fallback);
    CHECK(fallback->Entity == first);
    CHECK_FALSE(KeireEditor::SelectEditModeVfxDraftHost(emitters, Id(222), preferred).has_value());
    CHECK_FALSE(KeireEditor::SelectEditModeVfxDraftHost(emitters, {}, preferred).has_value());
}
