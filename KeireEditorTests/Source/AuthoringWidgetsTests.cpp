#include "Keire/Core.h"
#include "KeireClient/Editor/AuthoringWidgets.h"
#include "KeireClient/Editor/GraphComments.h"

#include <SDL3/SDL.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    class CollapsedGraphUiLayer final : public Keire::Layer
    {
      public:
        explicit CollapsedGraphUiLayer(bool& drawn) : Layer("CollapsedGraphUiLayer"), m_Drawn(drawn) {}

      protected:
        void OnUi(Keire::UiFrame& ui) override
        {
            ui.SetNextWindowSize({640.0F, 420.0F});
            if (auto window = ui.BeginWindow("Collapsed graph regression"); window)
            {
                KeireEditor::NodeGraphCanvasOptions options;
                options.Comments = m_Comments;
                (void)m_Canvas.Draw(ui, "CollapsedGraph", m_Nodes, m_Connections, options);
                m_Drawn = true;
            }
            Owner().RequestExit();
        }

      private:
        bool& m_Drawn;
        KeireEditor::StableNodeGraphCanvas m_Canvas;
        std::array<KeireEditor::NodeGraphNode, 2> m_Nodes{
            KeireEditor::NodeGraphNode{1, "Hidden producer", {80.0F, 100.0F}, {160.0F, 100.0F}},
            KeireEditor::NodeGraphNode{2, "Visible consumer", {380.0F, 100.0F}, {160.0F, 100.0F}}};
        std::array<KeireEditor::NodeGraphConnection, 1> m_Connections{
            KeireEditor::NodeGraphConnection{3, 1, 2, "Boundary cable"}};
        std::array<KeireEditor::NodeGraphComment, 1> m_Comments{KeireEditor::NodeGraphComment{
            .Id = 4,
            .Title = "Collapsed region",
            .Position = {40.0F, 50.0F},
            .Size = {240.0F, 200.0F},
            .Collapsed = true,
            .Members = {1},
        }};
    };

    class CollapsedGraphUiApplication final : public Keire::Application
    {
      public:
        explicit CollapsedGraphUiApplication(bool& drawn) : Application(Specification())
        {
            (void)PushLayer(std::make_unique<CollapsedGraphUiLayer>(drawn));
        }

      private:
        static Keire::ApplicationSpecification Specification()
        {
            Keire::ApplicationSpecification specification;
            specification.MainWindow.Title = "collapsed-graph-regression";
            specification.MainWindow.Visible = false;
            specification.TargetFrameRate = 0;
            specification.Ui.Mode = Keire::UiMode::Headless;
            specification.Ui.LayoutPath.clear();
            return specification;
        }
    };
} // namespace

TEST_CASE("Node menu selection focuses on open and follows live search results")
{
    KeireEditor::NodeMenuSelection selection;
    selection.Open();
    CHECK(selection.ConsumeFocusRequest());
    CHECK_FALSE(selection.ConsumeFocusRequest());

    constexpr std::array results{std::string_view("add"), std::string_view("multiply"),
                                 std::string_view("texture-sample")};
    selection.Synchronize(results);
    CHECK(selection.Selected() == "add");
    selection.MoveNext(results);
    CHECK(selection.Selected() == "multiply");
    selection.MovePrevious(results);
    CHECK(selection.Selected() == "add");
    selection.MovePrevious(results);
    CHECK(selection.Selected() == "texture-sample");

    constexpr std::array narrowed{std::string_view("multiply")};
    selection.Synchronize(narrowed);
    CHECK(selection.Selected() == "multiply");
    selection.Synchronize({});
    CHECK_FALSE(selection.Selected());
}

TEST_CASE("Node menu recent entries are unique, newest-first, and bounded")
{
    KeireEditor::NodeMenuSelection selection;
    for (std::size_t index = 0; index < KeireEditor::NodeMenuSelection::RecentCapacity + 2; ++index)
        selection.Remember("node-" + std::to_string(index));
    REQUIRE(selection.Recent().size() == KeireEditor::NodeMenuSelection::RecentCapacity);
    CHECK(selection.Recent().front() == "node-7");
    CHECK(selection.Recent().back() == "node-2");

    selection.Remember("node-4");
    REQUIRE(selection.Recent().size() == KeireEditor::NodeMenuSelection::RecentCapacity);
    CHECK(selection.Recent().front() == "node-4");
    CHECK(std::ranges::count(selection.Recent(), std::string("node-4")) == 1);
}

TEST_CASE("Shader Graph context compatibility respects direction and supported coercions")
{
    const auto pin = [](const Keire::ShaderGraphValueType type, const Keire::ShaderGraphPinDirection direction)
    {
        Keire::ShaderGraphPin result;
        result.Type = type;
        result.Direction = direction;
        return result;
    };

    const auto scalarOutput = pin(Keire::ShaderGraphValueType::Scalar, Keire::ShaderGraphPinDirection::Output);
    const auto vectorInput = pin(Keire::ShaderGraphValueType::Vector3, Keire::ShaderGraphPinDirection::Input);
    const auto vectorOutput = pin(Keire::ShaderGraphValueType::Vector3, Keire::ShaderGraphPinDirection::Output);
    const auto scalarInput = pin(Keire::ShaderGraphValueType::Scalar, Keire::ShaderGraphPinDirection::Input);
    const auto textureInput = pin(Keire::ShaderGraphValueType::Texture2D, Keire::ShaderGraphPinDirection::Input);
    const auto colorInput = pin(Keire::ShaderGraphValueType::Color, Keire::ShaderGraphPinDirection::Input);
    const auto vector4Output = pin(Keire::ShaderGraphValueType::Vector4, Keire::ShaderGraphPinDirection::Output);

    CHECK(KeireEditor::ShaderGraphPinsCanConnect(scalarOutput, vectorInput));
    CHECK(KeireEditor::ShaderGraphPinsCanConnect(vectorInput, scalarOutput));
    CHECK_FALSE(KeireEditor::ShaderGraphPinsCanConnect(vectorOutput, scalarInput));
    CHECK_FALSE(KeireEditor::ShaderGraphPinsCanConnect(scalarOutput, textureInput));
    CHECK(KeireEditor::ShaderGraphPinsCanConnect(vector4Output, colorInput));
    CHECK_FALSE(KeireEditor::ShaderGraphPinsCanConnect(vectorOutput, vector4Output));

    Keire::ShaderGraphNode source;
    source.Pins.push_back(scalarOutput);
    Keire::ShaderGraphNode compatible;
    compatible.Pins.push_back(vectorInput);
    Keire::ShaderGraphNode incompatible;
    incompatible.Pins.push_back(textureInput);
    CHECK(KeireEditor::ShaderGraphNodesCanConnect(source, compatible));
    CHECK_FALSE(KeireEditor::ShaderGraphNodesCanConnect(source, incompatible));
}

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

    const KeireEditor::NodeGraphCanvasResult legacyResult{std::nullopt, std::nullopt, std::nullopt, true, true};
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

TEST_CASE("Stable node graph cable routing points are ordered, bounded, and finite")
{
    KeireEditor::NodeGraphNode producer{1, "Producer", {0.0F, 0.0F}};
    producer.Pins.push_back({11, "Output", KeireEditor::NodeGraphPinDirection::Output, 7});
    KeireEditor::NodeGraphNode consumer{2, "Consumer", {240.0F, 0.0F}};
    consumer.Pins.push_back({12, "Input", KeireEditor::NodeGraphPinDirection::Input, 7});
    const std::vector nodes{producer, consumer};
    KeireEditor::NodeGraphConnection routed{3, 1, 2, "", 11, 12};
    routed.RoutingPoints = {{80.0F, 20.0F}, {160.0F, -20.0F}};
    CHECK_NOTHROW(KeireEditor::StableNodeGraphCanvas::Validate(nodes, std::span{&routed, std::size_t{1}}));

    const KeireEditor::NodeGraphRerouteRequest insert{routed.Id, 1, {120.0F, 40.0F}};
    CHECK(insert.Connection == routed.Id);
    CHECK(insert.Index == 1);
    CHECK((insert.GraphPosition == Keire::Vector2{120.0F, 40.0F}));

    auto invalid = routed;
    invalid.RoutingPoints.front().X = std::numeric_limits<float>::quiet_NaN();
    CHECK_THROWS_AS(KeireEditor::StableNodeGraphCanvas::Validate(nodes, std::span{&invalid, std::size_t{1}}),
                    std::invalid_argument);
    invalid = routed;
    invalid.RoutingPoints.resize(65, {});
    CHECK_THROWS_AS(KeireEditor::StableNodeGraphCanvas::Validate(nodes, std::span{&invalid, std::size_t{1}}),
                    std::invalid_argument);
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

TEST_CASE("Stable node graph zoom detail prevents labels from overlapping scaled rows")
{
    const auto overview = KeireEditor::StableNodeGraphCanvas::DetailForZoom(0.5F);
    CHECK_FALSE(overview.NodeSubtitle);
    CHECK(overview.BlockLabels);
    CHECK(overview.PinLabels);
    CHECK_FALSE(overview.ConnectionLabels);

    const auto distant = KeireEditor::StableNodeGraphCanvas::DetailForZoom(0.49F);
    CHECK_FALSE(distant.BlockLabels);
    CHECK_FALSE(distant.PinLabels);

    const auto compact = KeireEditor::StableNodeGraphCanvas::DetailForZoom(0.7F);
    CHECK(compact.NodeSubtitle);
    CHECK(compact.BlockLabels);
    CHECK(compact.PinLabels);
    CHECK(compact.ConnectionLabels);

    const auto readablePins = KeireEditor::StableNodeGraphCanvas::DetailForZoom(0.8F);
    CHECK(readablePins.NodeSubtitle);
    CHECK(readablePins.PinLabels);

    const auto full = KeireEditor::StableNodeGraphCanvas::DetailForZoom(0.9F);
    CHECK(full.NodeSubtitle);
    CHECK(full.BlockLabels);
    CHECK(full.PinLabels);
    CHECK(full.ConnectionLabels);
    CHECK(KeireEditor::StableNodeGraphCanvas::DetailForZoom(std::numeric_limits<float>::quiet_NaN()) == full);
}

TEST_CASE("Stable node graph multi-selection is ordered and marquee selection is deterministic")
{
    KeireEditor::StableNodeGraphCanvas canvas;
    const auto selections = [&canvas] { return std::vector(canvas.Selections().begin(), canvas.Selections().end()); };
    canvas.Select(11);
    canvas.ToggleSelection(22);
    CHECK(selections() == std::vector<KeireEditor::StableNodeId>{11, 22});
    CHECK(canvas.Selection() == 22);
    canvas.ToggleSelection(11);
    CHECK(selections() == std::vector<KeireEditor::StableNodeId>{22});

    const std::array ordered{KeireEditor::StableNodeId{31}, KeireEditor::StableNodeId{32},
                             KeireEditor::StableNodeId{33}};
    canvas.Select(ordered, 31);
    CHECK(selections() == std::vector<KeireEditor::StableNodeId>{32, 33, 31});
    CHECK(canvas.Selection() == 31);

    const std::vector<KeireEditor::NodeGraphNode> nodes{{41, "First", {10.0F, 10.0F}, {80.0F, 60.0F}},
                                                        {42, "Second", {180.0F, 40.0F}, {100.0F, 80.0F}},
                                                        {43, "Outside", {420.0F, 360.0F}, {90.0F, 60.0F}}};
    CHECK(KeireEditor::StableNodeGraphCanvas::MarqueeSelection(nodes, {0.0F, 0.0F}, {220.0F, 100.0F}) ==
          std::vector<KeireEditor::StableNodeId>{41, 42});
    canvas.SelectAll(nodes);
    CHECK(selections() == std::vector<KeireEditor::StableNodeId>{41, 42, 43});
    CHECK(canvas.Selection() == 43);
}

TEST_CASE("Graph comment presentation reserves node identities and preserves nested ownership")
{
    constexpr KeireEditor::StableNodeId nodeCanvas = 77;
    const auto nodeAsset = Keire::AssetId::Generate();
    const Keire::AssetId commentAsset(0, 0x434f4d4d454e5401ULL ^ nodeCanvas);
    Keire::GraphAuthoringMetadata metadata;
    metadata.Comments.push_back({commentAsset, "Collision-safe", {}, {}, {400.0F, 240.0F}});
    const std::array identities{std::pair{nodeCanvas, nodeAsset}};

    const auto model = KeireEditor::BuildNodeGraphCommentModel(metadata, identities);
    REQUIRE(model.Comments.size() == 1);
    CHECK(model.Comments.front().Id != nodeCanvas);
    CHECK(model.Asset(model.Comments.front().Id) == commentAsset);

    KeireEditor::NodeGraphCommentCreateRequest request;
    request.Position = {80.0F, 90.0F};
    request.Size = {220.0F, 120.0F};
    request.Members = {nodeCanvas};
    const KeireEditor::GraphCommentCanvasEdit edit{KeireEditor::GraphCommentCanvasEditKind::Create,
                                                   KeireEditor::CreateGraphComment(request, identities)};
    KeireEditor::ApplyGraphCommentCanvasEdit(metadata, edit);
    REQUIRE(metadata.Comments.size() == 2);
    CHECK(metadata.Comments.back().Parent == commentAsset);
    CHECK(metadata.Comments.front().Members == std::vector<Keire::AssetId>{metadata.Comments.back().Id});
    CHECK(metadata.Comments.back().Members == std::vector<Keire::AssetId>{nodeAsset});
}

TEST_CASE("Graph comments enclose selection with deterministic padding")
{
    const std::vector<KeireEditor::NodeGraphNode> nodes{{11, "First", {100.0F, 80.0F}, {120.0F, 90.0F}},
                                                        {12, "Second", {300.0F, 220.0F}, {160.0F, 100.0F}}};
    constexpr std::array selected{KeireEditor::StableNodeId{11}, KeireEditor::StableNodeId{12}};
    const auto request = KeireEditor::StableNodeGraphCanvas::CommentFromSelection(nodes, selected, {});
    CHECK(request.Members == std::vector<KeireEditor::StableNodeId>{11, 12});
    CHECK((request.Position == Keire::Vector2{60.0F, 16.0F}));
    CHECK((request.Size == Keire::Vector2{440.0F, 344.0F}));
}

TEST_CASE("Collapsed graph comment display bounds are stable and do not overwrite authored size")
{
    KeireEditor::NodeGraphComment comment;
    comment.Size = {480.0F, 260.0F};
    CHECK(KeireEditor::GraphCommentDisplayHeight(comment) == doctest::Approx(260.0F));
    comment.Collapsed = true;
    CHECK(KeireEditor::GraphCommentDisplayHeight(comment) == doctest::Approx(34.0F));
    comment.SummaryInputs = 2;
    comment.SummaryOutputs = 4;
    CHECK(KeireEditor::GraphCommentDisplayHeight(comment) == doctest::Approx(114.0F));
    CHECK((comment.Size == Keire::Vector2{480.0F, 260.0F}));
    comment.Collapsed = false;
    CHECK(KeireEditor::GraphCommentDisplayHeight(comment) == doctest::Approx(260.0F));
}

TEST_CASE("Collapsed graph comments omit member nodes without invalidating canvas rendering")
{
    REQUIRE(SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE));
    bool drawn = false;
    CollapsedGraphUiApplication application(drawn);

    CHECK(application.Run() == 0);
    CHECK(drawn);
}

TEST_CASE("Graph comment drag retains its presentation through the release frame")
{
    KeireEditor::NodeGraphComment comment;
    comment.Position = {40.0F, 30.0F};
    Keire::Vector2 retainedPosition{180.0F, 120.0F};

    const auto release = KeireEditor::ApplyGraphCommentDragFrame(comment, retainedPosition, {}, 1.0F, false, true);
    CHECK(release.Active);
    CHECK((comment.Position == Keire::Vector2{180.0F, 120.0F}));

    const auto drag =
        KeireEditor::ApplyGraphCommentDragFrame(comment, retainedPosition, {20.0F, -10.0F}, 2.0F, true, false);
    CHECK(drag.Active);
    CHECK((drag.Delta == Keire::Vector2{10.0F, -5.0F}));
    CHECK((comment.Position == Keire::Vector2{190.0F, 115.0F}));

    const auto idle =
        KeireEditor::ApplyGraphCommentDragFrame(comment, retainedPosition, {100.0F, 100.0F}, 1.0F, false, false);
    CHECK_FALSE(idle.Active);
    CHECK((comment.Position == Keire::Vector2{190.0F, 115.0F}));
}

TEST_CASE("Graph comment collapse arrow hit testing does not mutate presentation state")
{
    KeireEditor::NodeGraphComment comment;
    comment.Id = 91;
    comment.Position = {100.0F, 80.0F};
    comment.Size = {320.0F, 220.0F};
    std::array comments{comment};
    const Keire::UiItemRect canvas{{20.0F, 30.0F}, {820.0F, 630.0F}};

    const auto target =
        KeireEditor::FindGraphCommentCollapseToggleAtPointer(comments, canvas, {}, 1.0F, {125.0F, 115.0F});
    CHECK(target == comment.Id);
    CHECK_FALSE(comments.front().Collapsed);

    comments.front().Collapsed = true;
    const auto expandedTarget =
        KeireEditor::FindGraphCommentCollapseToggleAtPointer(comments, canvas, {}, 1.0F, {125.0F, 115.0F});
    CHECK(expandedTarget == comment.Id);
    CHECK(comments.front().Collapsed);
}

TEST_CASE("Graph node annotations map stable identities and preserve pinned presentation")
{
    const auto nodeId = Keire::AssetId::Generate();
    Keire::GraphAuthoringMetadata metadata;
    metadata.NodeAnnotations.push_back({nodeId, "Keep this branch normalized", true, true});
    metadata.NodeAnnotations.push_back({Keire::AssetId::Generate(), "Hidden", false, false});
    const std::array identities{std::pair<KeireEditor::StableNodeId, Keire::AssetId>{42, nodeId}};
    std::array nodes{KeireEditor::NodeGraphNode{.Id = 42}};

    KeireEditor::ApplyNodeGraphAnnotations(metadata, identities, nodes);

    CHECK(nodes.front().Comment == "Keep this branch normalized");
    CHECK(nodes.front().CommentPinned);
}

TEST_CASE("Graph node annotation editing upserts and removes one stable bubble")
{
    const auto node = Keire::AssetId::Generate();
    Keire::GraphAuthoringMetadata metadata;

    KeireEditor::SetGraphNodeAnnotation(metadata, node, "First", false);
    KeireEditor::SetGraphNodeAnnotation(metadata, node, "Updated", true);

    REQUIRE(metadata.NodeAnnotations.size() == 1);
    CHECK(metadata.NodeAnnotations.front() == Keire::GraphNodeAnnotation{node, "Updated", true, true});
    KeireEditor::SetGraphNodeAnnotation(metadata, node, {}, false);
    CHECK(metadata.NodeAnnotations.empty());
}
