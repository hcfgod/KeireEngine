#include "KeireClient/Editor/EditModeVfxPreview.h"
#include "KeireClient/Editor/VfxEffectDocument.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
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
                for (auto& node : system.Nodes)
                    if (node.Kind == Keire::VfxGraphNodeKind::Module && node.Reference == previous)
                        node.Reference = result.Modules[index].Id;
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
    CHECK(document.Definition().SchemaVersion == 3);
    CHECK(document.Definition().ExecutionSource == Keire::VfxExecutionSource::Graph);
    for (const auto& module : document.Definition().Modules)
    {
        CHECK(std::ranges::any_of(
            document.Definition().Systems,
            [&](const Keire::VfxGraphSystem& system)
            {
                return std::ranges::any_of(
                    system.Nodes, [&](const Keire::VfxGraphNode& node)
                    { return node.Kind == Keire::VfxGraphNodeKind::Module && node.Reference == module.Id; });
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
    };
    CHECK(document.AddNode(document.Definition().Systems.front().Id, custom));
    CHECK(document.Definition().Systems.front().Connections.size() ==
          graphBeforeCustomNode.Systems.front().Connections.size() + 1);
    CHECK(document.RemoveNode(document.Definition().Systems.front().Id, custom.Id));
    CHECK(document.Definition() == graphBeforeCustomNode);

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
    const auto emissionNode = std::ranges::find_if(
        authoredSystem.Nodes, [&](const Keire::VfxGraphNode& node)
        { return node.Kind == Keire::VfxGraphNodeKind::Module && node.Reference == emissionModule->Id; });
    REQUIRE(emissionNode != authoredSystem.Nodes.end());
    const auto emissionRatePin =
        std::ranges::find(emissionNode->Pins, std::string("particlesPerSecond"), &Keire::VfxGraphPin::Semantic);
    REQUIRE(emissionRatePin != emissionNode->Pins.end());
    const auto speedNode =
        std::ranges::find_if(authoredSystem.Nodes, [&](const Keire::VfxGraphNode& node)
                             { return node.Kind == Keire::VfxGraphNodeKind::Parameter && node.Reference == speed.Id; });
    REQUIRE(speedNode != authoredSystem.Nodes.end());
    const auto alternateNode = std::ranges::find_if(
        authoredSystem.Nodes, [&](const Keire::VfxGraphNode& node)
        { return node.Kind == Keire::VfxGraphNodeKind::Parameter && node.Reference == alternate.Id; });
    REQUIRE(alternateNode != authoredSystem.Nodes.end());

    const auto system = authoredSystem.Id;
    const auto emissionNodeId = emissionNode->Id;
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
    };
    const Keire::VfxGraphConnection alternateWriter{
        .Id = Id(81),
        .OutputNode = alternateNodeId,
        .OutputPin = alternatePinId,
        .InputNode = emissionNodeId,
        .InputPin = emissionRatePinId,
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
                },
                {
                    .Id = Id(53),
                    .Type = "Input",
                    .Context = Keire::VfxContextType::Update,
                    .Pins = {{Id(54), "Value", Keire::VfxValueType::Scalar, true}},
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
        const auto node = std::ranges::find_if(
            document.Definition().Systems.front().Nodes, [&](const Keire::VfxGraphNode& candidate)
            { return candidate.Kind == Keire::VfxGraphNodeKind::Module && candidate.Reference == module; });
        REQUIRE(node != document.Definition().Systems.front().Nodes.end());
        const auto pin = std::ranges::find(node->Pins, semantic, &Keire::VfxGraphPin::Semantic);
        REQUIRE(pin != node->Pins.end());
        return *pin;
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
