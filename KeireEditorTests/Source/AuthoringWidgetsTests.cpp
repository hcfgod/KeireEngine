#include "KeireClient/Editor/AuthoringWidgets.h"

#include <doctest/doctest.h>

#include <optional>
#include <stdexcept>
#include <vector>

TEST_CASE("Stable node graph validation protects local identity and references")
{
    const std::vector<KeireEditor::NodeGraphNode> nodes{{1, "Entry", {0.0F, 0.0F}}, {2, "Locomotion", {200.0F, 0.0F}}};
    const std::vector<KeireEditor::NodeGraphConnection> connections{{3, 1, 2, "Enter"}};
    CHECK_NOTHROW(KeireEditor::StableNodeGraphCanvas::Validate(nodes, connections));

    auto duplicateNodes = nodes;
    duplicateNodes.push_back(nodes.front());
    CHECK_THROWS_AS(KeireEditor::StableNodeGraphCanvas::Validate(duplicateNodes, connections), std::invalid_argument);

    auto missingTarget = connections;
    missingTarget.front().Target = 99;
    CHECK_THROWS_AS(KeireEditor::StableNodeGraphCanvas::Validate(nodes, missingTarget), std::invalid_argument);

    const std::vector<KeireEditor::NodeGraphNode> loopNode{{4, "Feedback", {0.0F, 0.0F}}};
    const std::vector<KeireEditor::NodeGraphConnection> loop{{5, 4, 4, "Feedback"}};
    CHECK_NOTHROW(KeireEditor::StableNodeGraphCanvas::Validate(loopNode, loop));
}

TEST_CASE("Stable node graph local IDs remap zero and collisions without losing source identity")
{
    KeireEditor::StableNodeGraphIdMap ids;
    const Keire::AssetId first(1, 2);
    const Keire::AssetId second(3, 4);
    const auto firstCanvasId = ids.Assign(first, 0);
    const auto secondCanvasId = ids.Assign(second, firstCanvasId);

    CHECK(firstCanvasId != 0);
    CHECK(secondCanvasId != 0);
    CHECK(secondCanvasId != firstCanvasId);
    CHECK(ids.Assign(first, 99) == firstCanvasId);
    CHECK(ids.Find(first) == firstCanvasId);
    CHECK_FALSE(ids.Find(Keire::AssetId(5, 6)).has_value());
}

TEST_CASE("Stable node graph presentation metadata remains optional")
{
    const KeireEditor::NodeGraphNode legacyNode{1, "Legacy", {0.0F, 0.0F}};
    CHECK(legacyNode.Subtitle.empty());

    auto contextualNode = legacyNode;
    contextualNode.Subtitle = "Update Context";
    CHECK(contextualNode != legacyNode);
    CHECK_NOTHROW(KeireEditor::StableNodeGraphCanvas::Validate(std::span{&contextualNode, std::size_t{1}}, {}));

    const KeireEditor::NodeGraphCanvasResult legacyResult{std::nullopt, std::nullopt, true, true};
    CHECK(legacyResult.BackgroundActivated);
    CHECK(legacyResult.Changed);
    CHECK_FALSE(legacyResult.MoveCompletedNode.has_value());
}

TEST_CASE("Stable node graph typed pins preserve legacy connections and validate explicit endpoints")
{
    KeireEditor::NodeGraphNode producer{1, "Producer", {0.0F, 0.0F}};
    producer.Pins.push_back(
        {11, "Particles", KeireEditor::NodeGraphPinDirection::Output, 100, {0.2F, 0.7F, 1.0F, 1.0F}});
    KeireEditor::NodeGraphNode consumer{2, "Consumer", {240.0F, 0.0F}};
    consumer.Pins.push_back(
        {12, "Particles", KeireEditor::NodeGraphPinDirection::Input, 100, {0.2F, 0.7F, 1.0F, 1.0F}});
    const std::vector nodes{producer, consumer};

    const std::vector<KeireEditor::NodeGraphConnection> typed{{3, 1, 2, "Particle stream", 11, 12}};
    CHECK_NOTHROW(KeireEditor::StableNodeGraphCanvas::Validate(nodes, typed));

    const std::vector<KeireEditor::NodeGraphNode> legacyNodes{{21, "Legacy source", {0.0F, 0.0F}},
                                                              {22, "Legacy target", {240.0F, 0.0F}}};
    const std::vector<KeireEditor::NodeGraphConnection> legacyConnection{{23, 21, 22, "Legacy"}};
    CHECK_NOTHROW(KeireEditor::StableNodeGraphCanvas::Validate(legacyNodes, legacyConnection));

    auto halfSpecified = typed;
    halfSpecified.front().TargetPin = 0;
    CHECK_THROWS_AS(KeireEditor::StableNodeGraphCanvas::Validate(nodes, halfSpecified), std::invalid_argument);

    auto duplicatePins = nodes;
    duplicatePins.back().Pins.front().Id = duplicatePins.front().Pins.front().Id;
    CHECK_THROWS_AS(KeireEditor::StableNodeGraphCanvas::Validate(duplicatePins, typed), std::invalid_argument);

    auto reversed = typed;
    reversed.front().Source = 2;
    reversed.front().SourcePin = 12;
    reversed.front().Target = 1;
    reversed.front().TargetPin = 11;
    CHECK_THROWS_AS(KeireEditor::StableNodeGraphCanvas::Validate(nodes, reversed), std::invalid_argument);
}

TEST_CASE("Stable node graph connection evaluation supports warnings without weakening hard pin checks")
{
    KeireEditor::NodeGraphNode producer{1, "Producer", {0.0F, 0.0F}};
    producer.Pins.push_back({11, "Scalar", KeireEditor::NodeGraphPinDirection::Output, 7});
    producer.Pins.push_back({13, "Other output", KeireEditor::NodeGraphPinDirection::Output, 7});
    KeireEditor::NodeGraphNode consumer{2, "Consumer", {240.0F, 0.0F}};
    consumer.Pins.push_back({12, "Scalar", KeireEditor::NodeGraphPinDirection::Input, 7});
    consumer.Pins.push_back({14, "Vector", KeireEditor::NodeGraphPinDirection::Input, 8});
    const std::vector nodes{producer, consumer};

    const KeireEditor::NodeGraphConnectionRequest valid{1, 11, 2, 12};
    CHECK(KeireEditor::StableNodeGraphCanvas::EvaluateConnection(nodes, valid).Status ==
          KeireEditor::NodeGraphConnectionValidationStatus::Accept);

    bool validatorCalled = false;
    const auto warning = KeireEditor::StableNodeGraphCanvas::EvaluateConnection(
        nodes, valid,
        [&validatorCalled](const KeireEditor::NodeGraphConnectionRequest&)
        {
            validatorCalled = true;
            return KeireEditor::NodeGraphConnectionValidation{
                KeireEditor::NodeGraphConnectionValidationStatus::AcceptWithWarning,
                "This connection leaves another context incomplete."};
        });
    CHECK(validatorCalled);
    CHECK(warning.CanConnect());
    CHECK(warning.Status == KeireEditor::NodeGraphConnectionValidationStatus::AcceptWithWarning);
    CHECK_FALSE(warning.Diagnostic.empty());

    validatorCalled = false;
    const KeireEditor::NodeGraphConnectionRequest mismatchedType{1, 11, 2, 14};
    const auto rejected = KeireEditor::StableNodeGraphCanvas::EvaluateConnection(
        nodes, mismatchedType,
        [&validatorCalled](const KeireEditor::NodeGraphConnectionRequest&)
        {
            validatorCalled = true;
            return KeireEditor::NodeGraphConnectionValidation{};
        });
    CHECK_FALSE(validatorCalled);
    CHECK_FALSE(rejected.CanConnect());
    CHECK(rejected.Status == KeireEditor::NodeGraphConnectionValidationStatus::Reject);

    const KeireEditor::NodeGraphConnectionRequest sameDirection{1, 11, 1, 13};
    CHECK_FALSE(KeireEditor::StableNodeGraphCanvas::EvaluateConnection(nodes, sameDirection).CanConnect());
}

TEST_CASE("Stable node graph Context Blocks preserve nested endpoint identity and independent selection")
{
    KeireEditor::NodeGraphNode producer{1, "Blackboard Amount", {0.0F, 0.0F}};
    producer.Pins.push_back({11, "Amount", KeireEditor::NodeGraphPinDirection::Output, 7});

    KeireEditor::NodeGraphNode context{2, "Update Context", {260.0F, 0.0F}};
    KeireEditor::NodeGraphBlockRow forceBlock;
    forceBlock.Id = 20;
    forceBlock.Label = "Force";
    forceBlock.Pins.push_back({21, "Amount", KeireEditor::NodeGraphPinDirection::Input, 7});
    context.Blocks.push_back(forceBlock);
    const std::vector nodes{producer, context};

    const KeireEditor::NodeGraphConnection nestedConnection{3, 1, 2, "Amount", 11, 21, 0, 20};
    CHECK_NOTHROW(KeireEditor::StableNodeGraphCanvas::Validate(nodes, std::span{&nestedConnection, std::size_t{1}}));

    const KeireEditor::NodeGraphConnectionRequest request{1, 11, 2, 21, 0, 20};
    std::optional<KeireEditor::NodeGraphConnectionRequest> validatedRequest;
    const auto accepted = KeireEditor::StableNodeGraphCanvas::EvaluateConnection(
        nodes, request,
        [&validatedRequest](const KeireEditor::NodeGraphConnectionRequest& candidate)
        {
            validatedRequest = candidate;
            return KeireEditor::NodeGraphConnectionValidation{};
        });
    CHECK(accepted.CanConnect());
    REQUIRE(validatedRequest);
    CHECK(*validatedRequest == request);
    CHECK(validatedRequest->SourceBlock == 0);
    CHECK(validatedRequest->TargetBlock == forceBlock.Id);

    auto missingBlock = request;
    missingBlock.TargetBlock = 0;
    CHECK_FALSE(KeireEditor::StableNodeGraphCanvas::EvaluateConnection(nodes, missingBlock).CanConnect());
    auto unknownBlock = request;
    unknownBlock.TargetBlock = 99;
    CHECK_FALSE(KeireEditor::StableNodeGraphCanvas::EvaluateConnection(nodes, unknownBlock).CanConnect());

    auto duplicateBlocks = nodes;
    duplicateBlocks.front().Blocks.push_back(forceBlock);
    CHECK_THROWS_AS(KeireEditor::StableNodeGraphCanvas::Validate(duplicateBlocks, {}), std::invalid_argument);
    auto duplicatePins = nodes;
    duplicatePins.back().Pins.push_back(
        {forceBlock.Pins.front().Id, "Duplicate", KeireEditor::NodeGraphPinDirection::Input, 7});
    CHECK_THROWS_AS(KeireEditor::StableNodeGraphCanvas::Validate(duplicatePins, {}), std::invalid_argument);

    auto legacyWithBlock = nestedConnection;
    legacyWithBlock.SourcePin = 0;
    legacyWithBlock.TargetPin = 0;
    CHECK_THROWS_AS(KeireEditor::StableNodeGraphCanvas::Validate(nodes, std::span{&legacyWithBlock, std::size_t{1}}),
                    std::invalid_argument);

    KeireEditor::StableNodeGraphCanvas canvas;
    canvas.Select(context.Id);
    const KeireEditor::NodeGraphBlockAddress selectedBlock{context.Id, forceBlock.Id};
    canvas.SelectBlock(selectedBlock);
    CHECK(canvas.Selection() == context.Id);
    CHECK(canvas.BlockSelection() == selectedBlock);
    canvas.SelectConnection(nestedConnection.Id);
    CHECK(canvas.BlockSelection() == selectedBlock);
    CHECK(canvas.ConnectionSelection() == nestedConnection.Id);
    canvas.SelectBlock(std::nullopt);
    CHECK_FALSE(canvas.BlockSelection());

    const KeireEditor::NodeGraphContextRequest blockMenu{
        .Kind = KeireEditor::NodeGraphContextTargetKind::Block,
        .Node = context.Id,
        .Block = forceBlock.Id,
    };
    CHECK(blockMenu.Node == context.Id);
    CHECK(blockMenu.Block == forceBlock.Id);
    const KeireEditor::NodeGraphBlockMoveRequest move{context.Id, forceBlock.Id, 0};
    const KeireEditor::NodeGraphBlockMoveRequest expectedMove{context.Id, forceBlock.Id, 0};
    CHECK(move == expectedMove);
}

TEST_CASE("Stable node graph canvas tracks node and cable selections independently")
{
    KeireEditor::StableNodeGraphCanvas canvas;
    canvas.Select(41);
    CHECK(canvas.Selection() == 41);
    CHECK_FALSE(canvas.ConnectionSelection().has_value());

    canvas.SelectConnection(73);
    CHECK(canvas.Selection() == 41);
    CHECK(canvas.ConnectionSelection() == 73);

    canvas.Select(std::nullopt);
    canvas.SelectConnection(std::nullopt);
    CHECK_FALSE(canvas.Selection().has_value());
    CHECK_FALSE(canvas.ConnectionSelection().has_value());
    CHECK_FALSE(canvas.ConnectionDragActive());
}

TEST_CASE("Stable node graph focus deterministically frames authored nodes")
{
    const std::vector<KeireEditor::NodeGraphNode> nodes{{41, "First", {100.0F, 50.0F}, {100.0F, 50.0F}},
                                                        {42, "Second", {400.0F, 250.0F}, {100.0F, 50.0F}}};
    KeireEditor::StableNodeGraphCanvas canvas;
    canvas.Focus(nodes, {800.0F, 600.0F});
    CHECK(canvas.Zoom() > 0.0F);
    const auto firstPan = canvas.Pan();
    canvas.Focus(nodes, {800.0F, 600.0F});
    CHECK(canvas.Pan() == firstPan);
}
