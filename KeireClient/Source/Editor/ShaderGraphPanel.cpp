#include "KeireClient/Editor/ShaderGraphPanel.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace KeireEditor
{
    namespace
    {
        struct NodeEntry
        {
            Keire::ShaderGraphNodeKind Kind;
            std::string_view Category;
            std::string_view Name;
            Keire::ShaderGraphValueType Type = Keire::ShaderGraphValueType::Scalar;
        };

        [[nodiscard]] const std::vector<NodeEntry>& NodeEntries()
        {
            static const auto entries = []
            {
                std::vector<NodeEntry> result{
                    {Keire::ShaderGraphNodeKind::Parameter, "Parameters", "Scalar Parameter"},
                    {Keire::ShaderGraphNodeKind::Parameter, "Parameters", "Vector2 Parameter",
                     Keire::ShaderGraphValueType::Vector2},
                    {Keire::ShaderGraphNodeKind::Parameter, "Parameters", "Vector3 Parameter",
                     Keire::ShaderGraphValueType::Vector3},
                    {Keire::ShaderGraphNodeKind::Parameter, "Parameters", "Vector4 Parameter",
                     Keire::ShaderGraphValueType::Vector4},
                    {Keire::ShaderGraphNodeKind::Parameter, "Parameters", "Color Parameter",
                     Keire::ShaderGraphValueType::Color},
                    {Keire::ShaderGraphNodeKind::Parameter, "Parameters", "Texture2D Parameter",
                     Keire::ShaderGraphValueType::Texture2D},
                    {Keire::ShaderGraphNodeKind::Constant, "Constants", "Scalar Constant"},
                    {Keire::ShaderGraphNodeKind::Constant, "Constants", "Vector2 Constant",
                     Keire::ShaderGraphValueType::Vector2},
                    {Keire::ShaderGraphNodeKind::Constant, "Constants", "Vector3 Constant",
                     Keire::ShaderGraphValueType::Vector3},
                    {Keire::ShaderGraphNodeKind::Constant, "Constants", "Vector4 Constant",
                     Keire::ShaderGraphValueType::Vector4},
                    {Keire::ShaderGraphNodeKind::Constant, "Constants", "Color Constant",
                     Keire::ShaderGraphValueType::Color},
                };
                for (const auto& descriptor : Keire::ShaderGraphNodeCatalog())
                {
                    if (!descriptor.UserCreatable || descriptor.Kind == Keire::ShaderGraphNodeKind::Parameter ||
                        descriptor.Kind == Keire::ShaderGraphNodeKind::Constant)
                        continue;
                    result.push_back(
                        {descriptor.Kind, descriptor.Category, descriptor.DisplayName, descriptor.DefaultValueType});
                }
                return result;
            }();
            return entries;
        }

        constexpr std::array PreviewNames{std::string_view("Sphere"), std::string_view("Plane"),
                                          std::string_view("Cube"), std::string_view("Custom Mesh")};
        constexpr std::array OutputNames{std::string_view("Lit Surface"),  std::string_view("Transparent Surface"),
                                         std::string_view("Decal"),        std::string_view("Unlit Surface"),
                                         std::string_view("Hair Surface"), std::string_view("Eye Surface"),
                                         std::string_view("Fullscreen")};
        constexpr std::array TextureSemanticNames{
            std::string_view("Generic"),   std::string_view("Base Color"),
            std::string_view("Normal"),    std::string_view("Metallic / Roughness"),
            std::string_view("Occlusion"), std::string_view("Emissive"),
            std::string_view("Metallic"),  std::string_view("Roughness"),
        };

        [[nodiscard]] constexpr std::string_view GraphPurposeName(const Keire::ShaderGraphPurpose purpose) noexcept
        {
            switch (purpose)
            {
            case Keire::ShaderGraphPurpose::Shader:
                return "Shader Graph";
            case Keire::ShaderGraphPurpose::MaterialFunction:
                return "Material Function";
            case Keire::ShaderGraphPurpose::ShaderFunction:
                return "Shader Function";
            case Keire::ShaderGraphPurpose::MaterialLayer:
                return "Material Layer";
            case Keire::ShaderGraphPurpose::MaterialLayerBlend:
                return "Material Layer Blend";
            }
            return "Reusable Graph";
        }

        [[nodiscard]] std::string Lower(const std::string_view value)
        {
            std::string result(value);
            std::ranges::transform(result, result.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return result;
        }

        [[nodiscard]] std::string UniqueSymbol(const Keire::ShaderGraphDefinition& definition,
                                               const std::string_view base)
        {
            std::set<std::string, std::less<>> used;
            for (const auto& node : definition.Nodes)
                if (!node.Symbol.empty())
                    used.insert(node.Symbol);
            if (!used.contains(base))
                return std::string(base);
            for (std::size_t suffix = 2;; ++suffix)
            {
                auto candidate = std::string(base) + std::to_string(suffix);
                if (!used.contains(candidate))
                    return candidate;
            }
        }

        void ChangeOutput(Keire::ShaderGraphDefinition& definition, const Keire::ShaderGraphOutput output)
        {
            if (definition.Output == output)
                return;
            auto master =
                std::ranges::find(definition.Nodes, Keire::ShaderGraphNodeKind::Master, &Keire::ShaderGraphNode::Kind);
            if (master == definition.Nodes.end())
                throw std::invalid_argument("Shader Output node is unavailable.");
            auto replacement = Keire::CreateDefaultShaderGraph(output).Nodes.front();
            replacement.Id = master->Id;
            replacement.EditorPosition = master->EditorPosition;
            for (auto& newPin : replacement.Pins)
            {
                auto oldName = std::string_view(newPin.Name);
                if (newPin.Name == "Color")
                    oldName = "BaseColor";
                else if (newPin.Name == "BaseColor")
                    oldName = "Color";
                auto oldPin = std::ranges::find(master->Pins, oldName, &Keire::ShaderGraphPin::Name);
                if (oldPin == master->Pins.end())
                    oldPin = std::ranges::find(master->Pins, newPin.Name, &Keire::ShaderGraphPin::Name);
                if (oldPin != master->Pins.end() && oldPin->Type == newPin.Type)
                {
                    newPin.Id = oldPin->Id;
                    newPin.DefaultValue = oldPin->DefaultValue;
                }
            }
            const auto masterId = master->Id;
            const auto retained = replacement.Pins;
            *master = std::move(replacement);
            std::erase_if(definition.Connections,
                          [&](const Keire::ShaderGraphConnection& connection)
                          {
                              return connection.Input.Node == masterId &&
                                     std::ranges::find(retained, connection.Input.Pin, &Keire::ShaderGraphPin::Id) ==
                                         retained.end();
                          });
            definition.Output = output;
        }

        [[nodiscard]] const Keire::ShaderGraphNode* FindNode(const Keire::ShaderGraphDefinition& definition,
                                                             const Keire::AssetId id)
        {
            const auto found = std::ranges::find(definition.Nodes, id, &Keire::ShaderGraphNode::Id);
            return found == definition.Nodes.end() ? nullptr : std::addressof(*found);
        }

    } // namespace

    void ShaderGraphPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.shader-graph", "Shader Graph", false});
    }

    void ShaderGraphPanel::Draw(Keire::UiFrame& ui)
    {
        if (auto panel = ui.BeginPanel(m_Registration); panel)
        {
            auto& document = m_Controller.ShaderGraphState();
            if (!document.IsOpen())
            {
                ui.TextColored(m_Controller.ShaderGraphTheme().MutedText,
                               "Open a Shader Graph asset to author its shader.");
                return;
            }
            DrawHeader(ui);
            ui.Separator();
            const auto available = ui.ContentAvailable();
            const float authoringHeight = std::max(360.0F, available.Height * 0.72F);
            const float previewPaneWidth =
                m_ShowPreview && !document.ReusableGraph() && available.Width >= 620.0F ? 248.0F : 0.0F;
            const float graphPaneWidth = std::max(320.0F, available.Width - previewPaneWidth - 8.0F);
            if (auto graph = ui.BeginChild("ShaderGraphAuthoring", {graphPaneWidth, authoringHeight}, false); graph)
                DrawCanvas(ui);
            if (previewPaneWidth > 0.0F)
            {
                ui.SameLine();
                if (auto preview = ui.BeginChild("ShaderGraphPreviewPane", {previewPaneWidth, authoringHeight}, true);
                    preview)
                    DrawPreview(ui);
            }
            ui.Separator();
            DrawInspector(ui);
            ui.Separator();
            DrawDiagnostics(ui);
        }
    }

    void ShaderGraphPanel::ResetTransientState() noexcept
    {
        m_SelectedNode.reset();
        m_SelectedNodes.clear();
        m_SelectedConnection.reset();
        m_FrameNode.reset();
        m_InspectorNode.reset();
        m_NodeCreationPosition.reset();
        m_GraphContext.reset();
        m_FunctionExtractionSelection.clear();
        m_NodeSearch.clear();
        m_NodeMenuOpen = false;
        m_OpenFunctionExtractionPopup = false;
        m_Bookmarks.Clear();
        m_Canvas.CancelInteractions();
        m_Canvas.Select(std::nullopt);
        m_Canvas.SelectConnection(std::nullopt);
        m_AssetPicker.Clear();
        m_NodeAssetPicker.Clear();
        m_Message.clear();
        m_InspectorComment.clear();
        m_InspectorCommentPinned = false;
    }

    void ShaderGraphPanel::DrawHeader(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.ShaderGraphState();
        const auto& theme = m_Controller.ShaderGraphTheme();
        const bool compiling = document.CompilationPending();
        const bool reusable = document.ReusableGraph();
        ui.TextColored(!compiling && document.Publishable() ? theme.Success : theme.Warning,
                       compiling                ? "LIVE COMPILING + UPDATING SCENE"
                       : document.Publishable() ? reusable ? "REUSABLE GRAPH VALID" : "GENERATED SHADER READY"
                       : reusable               ? "REUSABLE GRAPH HAS ERRORS"
                                                : "PREVIEW USING LAST GOOD SHADER");
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!document.Dirty() || !document.Publishable() || compiling); disabled)
            if (ui.Button("Save"))
            {
                try
                {
                    m_Controller.SaveShaderGraphDocument();
                    m_Message = "Saved " + std::string(GraphPurposeName(document.Definition().Purpose)) + ".";
                }
                catch (const std::exception& error)
                {
                    Report(error.what());
                }
            }
        ui.SameLine();
        const auto undo = document.UndoContext();
        if (auto disabled = ui.BeginDisabled(!undo || !undo->CanUndo()); disabled)
            if (ui.Button("Undo"))
                m_Controller.UndoShaderGraphEdit();
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!undo || !undo->CanRedo()); disabled)
            if (ui.Button("Redo"))
                m_Controller.RedoShaderGraphEdit();

        if (reusable)
        {
            ui.SameLine();
            ui.TextColored(theme.MutedText, GraphPurposeName(document.Definition().Purpose));
            const auto& statistics = document.Compilation().Statistics;
            ui.SameLine();
            ui.TextColored(theme.MutedText, std::to_string(statistics.NodeCount) + " nodes  |  " +
                                                std::to_string(statistics.ConnectionCount) + " connections");
            return;
        }

        ui.SameLine();
        if (ui.Button(m_ShowPreview ? "Hide Preview" : "Show Preview"))
            m_ShowPreview = !m_ShowPreview;

        auto preview = document.PreviewSettings();
        auto previewIndex = static_cast<std::size_t>(preview.Mesh);
        ui.SameLine();
        if (auto combo = ui.BeginCombo("Preview Mesh", PreviewNames[previewIndex]); combo)
        {
            for (std::size_t index = 0; index < PreviewNames.size(); ++index)
            {
                if (ui.Selectable(PreviewNames[index], index == previewIndex))
                {
                    preview.Mesh = static_cast<Keire::ShaderGraphPreviewMesh>(index);
                    if (preview.Mesh == Keire::ShaderGraphPreviewMesh::Custom && !preview.CustomMesh)
                        preview.CustomMesh = Keire::MeshAsset::CubeId();
                    document.SetPreviewSettings(preview);
                }
            }
        }
        if (preview.Mesh == Keire::ShaderGraphPreviewMesh::Custom)
        {
            auto mesh = preview.CustomMesh;
            const AssetPickerOptions options{
                .Label = "Custom Preview Mesh",
                .ExpectedType = Keire::MeshAsset::StaticType(),
                .Reveal = [this](const Keire::AssetId selected) { m_Controller.RevealShaderGraphAsset(selected); },
                .AllowNone = false,
            };
            if (m_AssetPicker.Draw(ui, m_Controller.ShaderGraphAssetRecords(), mesh, options))
            {
                preview.CustomMesh = mesh;
                document.SetPreviewSettings(preview);
            }
            if (!m_AssetPicker.Diagnostic().empty())
                ui.TextColored(theme.Warning, m_AssetPicker.Diagnostic());
        }
        const auto& statistics = document.Compilation().Statistics;
        ui.SameLine();
        ui.TextColored(theme.MutedText, std::to_string(statistics.ReachableNodeCount) + "/" +
                                            std::to_string(statistics.NodeCount) + " active nodes  |  " +
                                            std::to_string(statistics.TextureSampleCount) + " texture samples  |  ~" +
                                            std::to_string(statistics.EstimatedAluInstructions) + " ALU  |  " +
                                            std::to_string(statistics.VariantCount) + " variants");

        auto output = document.Definition().Output;
        auto outputIndex = static_cast<std::size_t>(output);
        if (auto combo = ui.BeginCombo("Shader Target", OutputNames[outputIndex]); combo)
            for (std::size_t index = 0; index < OutputNames.size(); ++index)
                if (ui.Selectable(OutputNames[index], index == outputIndex))
                    try
                    {
                        const auto selected = static_cast<Keire::ShaderGraphOutput>(index);
                        (void)document.Edit("Change Shader Graph output",
                                            [selected](auto& definition) { ChangeOutput(definition, selected); });
                    }
                    catch (const std::exception& error)
                    {
                        Report(error.what());
                    }
    }

    void ShaderGraphPanel::DrawInspector(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.ShaderGraphState();
        if (DrawMultiSelectionInspector(ui))
            return;
        const auto* node = m_SelectedNode ? FindNode(document.Definition(), *m_SelectedNode) : nullptr;
        if (!node)
        {
            m_InspectorNode.reset();
            ui.TextColored(m_Controller.ShaderGraphTheme().MutedText,
                           "Select a node to edit its authoring properties.");
            return;
        }
        if (m_InspectorNode != node->Id)
        {
            m_InspectorNode = node->Id;
            m_InspectorName = node->Name;
            m_InspectorSymbol = node->Symbol;
            m_InspectorInclude = node->Include.generic_string();
            m_InspectorFunction = node->Function;
            m_InspectorDescription = node->ParameterMetadata.Description;
            m_InspectorCategory = node->ParameterMetadata.Category;
            m_InspectorSortPriority = node->ParameterMetadata.SortPriority;
            m_InspectorHasMinimum = node->ParameterMetadata.Minimum.has_value();
            m_InspectorHasMaximum = node->ParameterMetadata.Maximum.has_value();
            m_InspectorHasStep = node->ParameterMetadata.Step.has_value();
            m_InspectorMinimum = node->ParameterMetadata.Minimum.value_or(0.0F);
            m_InspectorMaximum = node->ParameterMetadata.Maximum.value_or(1.0F);
            m_InspectorStep = node->ParameterMetadata.Step.value_or(0.01F);
            const auto annotation = std::ranges::find(document.Definition().Authoring.NodeAnnotations, node->Id,
                                                      &Keire::GraphNodeAnnotation::Node);
            m_InspectorComment =
                annotation == document.Definition().Authoring.NodeAnnotations.end() ? std::string{} : annotation->Text;
            m_InspectorCommentPinned =
                annotation != document.Definition().Authoring.NodeAnnotations.end() && annotation->Pinned;
        }

        ui.TextColored(m_Controller.ShaderGraphTheme().Accent, "NODE INSPECTOR");
        ui.TextColored(m_Controller.ShaderGraphTheme().MutedText, "Stable ID: " + node->Id.ToString());
        (void)ui.InputText("Display Name", m_InspectorName);
        if (node->Kind == Keire::ShaderGraphNodeKind::Parameter || node->Kind == Keire::ShaderGraphNodeKind::Keyword)
            (void)ui.InputText("Shader Symbol", m_InspectorSymbol);
        if (node->Kind == Keire::ShaderGraphNodeKind::Parameter)
        {
            (void)ui.InputText("Description", m_InspectorDescription);
            (void)ui.InputText("Parameter Group", m_InspectorCategory);
            (void)ui.DragScalar("Sort Priority", m_InspectorSortPriority, 1.0, -10'000.0, 10'000.0);
            (void)ui.Checkbox("Override Minimum", m_InspectorHasMinimum);
            if (m_InspectorHasMinimum)
                (void)ui.DragScalar("Minimum", m_InspectorMinimum, 0.01);
            (void)ui.Checkbox("Override Maximum", m_InspectorHasMaximum);
            if (m_InspectorHasMaximum)
                (void)ui.DragScalar("Maximum", m_InspectorMaximum, 0.01);
            (void)ui.Checkbox("Override Step", m_InspectorHasStep);
            if (m_InspectorHasStep)
                (void)ui.DragScalar("Step", m_InspectorStep, 0.001, 0.0001, 1'000.0);
        }
        if (node->Kind == Keire::ShaderGraphNodeKind::Custom)
        {
            (void)ui.InputText("Safe Include", m_InspectorInclude);
            (void)ui.InputText("Function", m_InspectorFunction);
        }
        (void)ui.InputTextMultiline("Node Comment", m_InspectorComment, 3);
        (void)ui.Checkbox("Pin Comment Bubble", m_InspectorCommentPinned);
        if (ui.Button("Apply Node Properties"))
        {
            try
            {
                const auto nodeId = node->Id;
                const auto oldSymbol = node->Symbol;
                const auto kind = node->Kind;
                Keire::ShaderGraphParameterMetadata metadata;
                metadata.Description = m_InspectorDescription;
                metadata.Category = m_InspectorCategory;
                metadata.SortPriority = static_cast<std::int32_t>(std::round(m_InspectorSortPriority));
                if (m_InspectorHasMinimum)
                    metadata.Minimum = static_cast<float>(m_InspectorMinimum);
                if (m_InspectorHasMaximum)
                    metadata.Maximum = static_cast<float>(m_InspectorMaximum);
                if (m_InspectorHasStep)
                    metadata.Step = static_cast<float>(m_InspectorStep);
                (void)document.Edit(
                    "Edit Shader Graph node properties",
                    [nodeId, oldSymbol, kind, name = m_InspectorName, symbol = m_InspectorSymbol,
                     include = m_InspectorInclude, function = m_InspectorFunction, metadata = std::move(metadata),
                     comment = m_InspectorComment, pinned = m_InspectorCommentPinned](auto& definition)
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
                            auto keyword =
                                std::ranges::find(definition.Keywords, oldSymbol, &Keire::ShaderGraphKeyword::Name);
                            if (keyword != definition.Keywords.end())
                                keyword->Name = symbol;
                        }
                    });
            }
            catch (const std::exception& error)
            {
                Report(error.what());
            }
            return;
        }
        if (auto disabled = ui.BeginDisabled(node->Kind == Keire::ShaderGraphNodeKind::Master ||
                                             node->Kind == Keire::ShaderGraphNodeKind::Keyword);
            disabled)
            if (ui.Button("Duplicate Node"))
            {
                try
                {
                    auto duplicate = *node;
                    duplicate.Id = Keire::AssetId::Generate();
                    duplicate.Name += " Copy";
                    duplicate.EditorPosition.X += 32.0F;
                    duplicate.EditorPosition.Y += 32.0F;
                    for (auto& pin : duplicate.Pins)
                        pin.Id = Keire::AssetId::Generate();
                    if (duplicate.Kind == Keire::ShaderGraphNodeKind::Parameter)
                        duplicate.Symbol = UniqueSymbol(document.Definition(), duplicate.Symbol);
                    const auto duplicateId = duplicate.Id;
                    if (document.AddNode(std::move(duplicate)))
                        m_SelectedNode = duplicateId;
                }
                catch (const std::exception& error)
                {
                    Report(error.what());
                }
                return;
            }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(node->Kind == Keire::ShaderGraphNodeKind::Master); disabled)
            if (ui.Button("Delete Node"))
            {
                try
                {
                    (void)document.RemoveNode(node->Id);
                    m_SelectedNode.reset();
                    m_InspectorNode.reset();
                }
                catch (const std::exception& error)
                {
                    Report(error.what());
                }
                return;
            }
        const auto applyValue = [&](Keire::ShaderGraphValue value)
        {
            try
            {
                (void)document.EditNode(node->Id, [value](auto& candidate) { candidate.Value = value; });
            }
            catch (const std::exception& error)
            {
                Report(error.what());
            }
        };
        if (const auto scalar = std::get_if<float>(&node->Value))
        {
            double value = *scalar;
            if (ui.DragScalar("Default Value", value, 0.01))
                applyValue(static_cast<float>(value));
        }
        else if (const auto vector2 = std::get_if<Keire::Vector2>(&node->Value))
        {
            auto value = *vector2;
            if (ui.DragVector2("Default Value", value))
                applyValue(value);
        }
        else if (const auto vector3 = std::get_if<Keire::Vector3>(&node->Value))
        {
            auto value = *vector3;
            if (ui.DragVector3("Default Value", value))
                applyValue(value);
        }
        else if (const auto vector4 = std::get_if<Keire::Vector4>(&node->Value))
        {
            auto value = *vector4;
            if (ui.DragVector4("Default Value", value))
                applyValue(value);
        }
        else if (const auto color = std::get_if<Keire::Color>(&node->Value))
        {
            Keire::UiColor value{color->Red, color->Green, color->Blue, color->Alpha};
            if (ui.ColorEdit("Default Value", value))
                applyValue(Keire::Color{value.Red, value.Green, value.Blue, value.Alpha});
        }
        else if (const auto asset = std::get_if<Keire::AssetId>(&node->Value))
        {
            auto value = *asset;
            const AssetPickerOptions options{
                .Label = "Default Texture",
                .ExpectedType = Keire::Texture2DAsset::StaticType(),
                .Reveal = [this](const Keire::AssetId selected) { m_Controller.RevealShaderGraphAsset(selected); },
                .AllowNone = true,
            };
            if (m_NodeAssetPicker.Draw(ui, m_Controller.ShaderGraphAssetRecords(), value, options))
                applyValue(value);
            if (!m_NodeAssetPicker.Diagnostic().empty())
                ui.TextColored(m_Controller.ShaderGraphTheme().Warning, m_NodeAssetPicker.Diagnostic());
            auto semanticIndex = static_cast<std::size_t>(node->TextureSemantic);
            if (auto combo = ui.BeginCombo("Texture Semantic", TextureSemanticNames[semanticIndex]); combo)
                for (std::size_t index = 0; index < TextureSemanticNames.size(); ++index)
                    if (ui.Selectable(TextureSemanticNames[index], semanticIndex == index))
                    {
                        const auto semantic = static_cast<Keire::ShaderTextureSemantic>(index);
                        try
                        {
                            (void)document.EditNode(node->Id, [semantic](auto& candidate)
                                                    { candidate.TextureSemantic = semantic; });
                        }
                        catch (const std::exception& error)
                        {
                            Report(error.what());
                        }
                    }
        }
        if (auto inputs = ui.BeginTreeNode("Input Defaults"); inputs)
        {
            const auto applyPinValue = [&](const Keire::AssetId pinId, Keire::ShaderGraphValue value)
            {
                try
                {
                    (void)document.EditNode(
                        node->Id,
                        [pinId, value](auto& candidate)
                        {
                            const auto pin = std::ranges::find(candidate.Pins, pinId, &Keire::ShaderGraphPin::Id);
                            if (pin == candidate.Pins.end())
                                throw std::invalid_argument("Shader Graph input pin is unavailable.");
                            pin->DefaultValue = value;
                        });
                }
                catch (const std::exception& error)
                {
                    Report(error.what());
                }
            };
            for (const auto& pin : node->Pins)
            {
                if (pin.Direction != Keire::ShaderGraphPinDirection::Input)
                    continue;
                const auto connected = std::ranges::any_of(
                    document.Definition().Connections, [&](const auto& connection)
                    { return connection.Input.Node == node->Id && connection.Input.Pin == pin.Id; });
                if (connected)
                {
                    ui.TextColored(m_Controller.ShaderGraphTheme().MutedText, pin.Name + "  |  connected");
                    continue;
                }
                const auto label = pin.Name + "##" + pin.Id.ToString();
                if (const auto scalar = std::get_if<float>(&pin.DefaultValue))
                {
                    double value = *scalar;
                    if (ui.DragScalar(label, value, 0.01))
                        applyPinValue(pin.Id, static_cast<float>(value));
                }
                else if (const auto vector2 = std::get_if<Keire::Vector2>(&pin.DefaultValue))
                {
                    auto value = *vector2;
                    if (ui.DragVector2(label, value))
                        applyPinValue(pin.Id, value);
                }
                else if (const auto vector3 = std::get_if<Keire::Vector3>(&pin.DefaultValue))
                {
                    auto value = *vector3;
                    if (ui.DragVector3(label, value))
                        applyPinValue(pin.Id, value);
                }
                else if (const auto vector4 = std::get_if<Keire::Vector4>(&pin.DefaultValue))
                {
                    auto value = *vector4;
                    if (ui.DragVector4(label, value))
                        applyPinValue(pin.Id, value);
                }
                else if (const auto color = std::get_if<Keire::Color>(&pin.DefaultValue))
                {
                    Keire::UiColor value{color->Red, color->Green, color->Blue, color->Alpha};
                    if (ui.ColorEdit(label, value))
                        applyPinValue(pin.Id, Keire::Color{value.Red, value.Green, value.Blue, value.Alpha});
                }
                else if (const auto asset = std::get_if<Keire::AssetId>(&pin.DefaultValue))
                {
                    auto value = *asset;
                    const AssetPickerOptions options{
                        .Label = label,
                        .ExpectedType = Keire::Texture2DAsset::StaticType(),
                        .Reveal = [this](const Keire::AssetId selected)
                        { m_Controller.RevealShaderGraphAsset(selected); },
                        .AllowNone = true,
                    };
                    if (m_NodeAssetPicker.Draw(ui, m_Controller.ShaderGraphAssetRecords(), value, options))
                        applyPinValue(pin.Id, value);
                }
            }
        }
    }
    bool ShaderGraphPanel::DrawNodeCreationMenu(Keire::UiFrame& ui, const std::optional<Keire::Vector2> graphPosition,
                                                const Keire::ShaderGraphNode* compatibleNode,
                                                const Keire::ShaderGraphPin* compatiblePin)
    {
        if (m_NodeMenuSelection.ConsumeFocusRequest())
            ui.RequestKeyboardFocus();
        (void)ui.InputTextWithHint("##ShaderNodeSearch", "Search nodes and categories...", m_NodeSearch);
        ui.Separator();
        const auto search = Lower(m_NodeSearch);
        const auto& entries = NodeEntries();
        const auto compatible = [&](const Keire::ShaderGraphNode& candidate)
        {
            if (compatiblePin)
                return std::ranges::any_of(candidate.Pins, [&](const Keire::ShaderGraphPin& pin)
                                           { return ShaderGraphPinsCanConnect(*compatiblePin, pin); });
            return !compatibleNode || ShaderGraphNodesCanConnect(*compatibleNode, candidate);
        };
        const auto entryCompatible = [&](const NodeEntry& entry)
        {
            try
            {
                return compatible(Keire::CreateShaderGraphNode(entry.Kind, entry.Type));
            }
            catch (...)
            {
                return false;
            }
        };
        std::vector<const Keire::AssetSourceRecord*> reusableGraphs;
        for (const auto& record : m_Controller.ShaderGraphAssetRecords())
        {
            const bool reusable = record.Type == Keire::ShaderSubgraphAsset::StaticType() ||
                                  record.Type == Keire::MaterialFunctionAsset::StaticType() ||
                                  record.Type == Keire::ShaderFunctionAsset::StaticType() ||
                                  record.Type == Keire::MaterialLayerAsset::StaticType() ||
                                  record.Type == Keire::MaterialLayerBlendAsset::StaticType();
            if (!reusable || record.Id == m_Controller.ShaderGraphState().Asset())
                continue;
            if (!compatibleNode && !compatiblePin)
            {
                reusableGraphs.push_back(&record);
                continue;
            }
            try
            {
                const auto function = m_Controller.ResolveShaderGraphFunction(record.Id);
                if (function && compatible(Keire::CreateShaderGraphFunctionCallNode(record.Id, *function)))
                    reusableGraphs.push_back(&record);
            }
            catch (...)
            {
            }
        }
        std::vector<std::string> paths;
        paths.reserve(entries.size());
        for (const auto& entry : entries)
            paths.push_back(std::string(entry.Category) + " / " + std::string(entry.Name));
        std::vector<std::size_t> visible;
        if (!search.empty())
        {
            for (std::size_t index = 0; index < entries.size(); ++index)
                if (entryCompatible(entries[index]) && Lower(paths[index]).find(search) != std::string::npos)
                    visible.push_back(index);
        }
        else
        {
            const auto append = [&](const std::size_t index)
            {
                if (std::ranges::find(visible, index) == visible.end())
                    visible.push_back(index);
            };
            for (const auto& recent : m_NodeMenuSelection.Recent())
            {
                const auto found = std::ranges::find(paths, recent);
                if (found != paths.end())
                {
                    const auto index = static_cast<std::size_t>(found - paths.begin());
                    if (entryCompatible(entries[index]))
                        append(index);
                }
            }
            constexpr std::array common{std::string_view("Scalar Parameter"),
                                        std::string_view("Color Parameter"),
                                        std::string_view("Texture2D Parameter"),
                                        std::string_view("Add"),
                                        std::string_view("Multiply"),
                                        std::string_view("Texture Sample")};
            for (const auto name : common)
            {
                const auto found = std::ranges::find(entries, name, &NodeEntry::Name);
                if (found != entries.end())
                {
                    const auto index = static_cast<std::size_t>(found - entries.begin());
                    if (entryCompatible(entries[index]))
                        append(index);
                }
            }
        }

        std::vector<std::string_view> visibleIds;
        visibleIds.reserve(visible.size());
        for (const auto index : visible)
            visibleIds.push_back(paths[index]);
        m_NodeMenuSelection.Synchronize(visibleIds);
        if (ui.Shortcut({.Key = Keire::UiKey::Up, .Global = true}))
            m_NodeMenuSelection.MovePrevious(visibleIds);
        if (ui.Shortcut({.Key = Keire::UiKey::Down, .Global = true}))
            m_NodeMenuSelection.MoveNext(visibleIds);
        const bool activateSelected = ui.Shortcut({.Key = Keire::UiKey::Enter, .Global = true});

        const auto addEntry = [&](const std::size_t index, const std::string_view label)
        {
            const bool activated = ui.MenuItem(label, m_NodeMenuSelection.IsSelected(paths[index])) ||
                                   (activateSelected && m_NodeMenuSelection.IsSelected(paths[index]));
            if (!activated)
                return false;
            if (!AddNode(entries[index].Kind, entries[index].Type, graphPosition))
                return false;
            m_NodeMenuSelection.Remember(paths[index]);
            m_NodeSearch.clear();
            ui.CloseCurrentPopup();
            return true;
        };

        if (search.empty())
            ui.TextColored(m_Controller.ShaderGraphTheme().MutedText, "RECENT & COMMON");
        for (const auto index : visible)
            if (addEntry(index, paths[index]))
                return true;
        bool visibleFunction = false;
        if (!search.empty())
        {
            for (const auto* record : reusableGraphs)
            {
                const auto name = record->RelativePath.stem().string();
                const auto path = "Functions & Layers / " + name;
                if (Lower(path).find(search) == std::string::npos)
                    continue;
                visibleFunction = true;
                if (ui.MenuItem(path) && AddFunctionNode(record->Id, name, graphPosition))
                {
                    m_NodeSearch.clear();
                    ui.CloseCurrentPopup();
                    return true;
                }
            }
        }
        const bool hasCompatibleEntry = std::ranges::any_of(entries, entryCompatible);
        if ((!search.empty() && visible.empty() && !visibleFunction) ||
            (search.empty() && !hasCompatibleEntry && reusableGraphs.empty()))
            ui.TextColored(m_Controller.ShaderGraphTheme().MutedText, "No nodes match this search.");

        if (search.empty())
        {
            ui.Separator();
            std::vector<std::string_view> categories;
            for (const auto& entry : entries)
                if (entryCompatible(entry) && std::ranges::find(categories, entry.Category) == categories.end())
                    categories.push_back(entry.Category);
            for (const auto category : categories)
            {
                if (auto categoryMenu = ui.BeginMenu(category); categoryMenu)
                {
                    for (std::size_t index = 0; index < entries.size(); ++index)
                        if (entries[index].Category == category && entryCompatible(entries[index]) &&
                            addEntry(index, entries[index].Name))
                            return true;
                }
            }
            if (!reusableGraphs.empty())
                if (auto functions = ui.BeginMenu("Functions & Layers"); functions)
                    for (const auto* record : reusableGraphs)
                    {
                        const auto name = record->RelativePath.stem().string();
                        if (ui.MenuItem(name) && AddFunctionNode(record->Id, name, graphPosition))
                        {
                            ui.CloseCurrentPopup();
                            return true;
                        }
                    }
        }
        return false;
    }
    void ShaderGraphPanel::DrawCanvas(Keire::UiFrame& ui)
    {
        const bool openFunctionExtractionPopup = std::exchange(m_OpenFunctionExtractionPopup, false);
        auto& document = m_Controller.ShaderGraphState();
        auto model = document.BuildCanvasModel();
        if (m_FrameNode)
        {
            const auto identity =
                std::ranges::find(model.NodeIdentities, *m_FrameNode, &std::pair<StableNodeId, Keire::AssetId>::second);
            const auto node = identity == model.NodeIdentities.end()
                                  ? model.Nodes.end()
                                  : std::ranges::find(model.Nodes, identity->first, &NodeGraphNode::Id);
            if (node != model.Nodes.end())
            {
                const std::array framed{*node};
                m_Canvas.Focus(framed, ui.ContentAvailable());
            }
            m_FrameNode.reset();
        }
        ApplyNodeGraphAnnotations(document.Definition().Authoring, model.NodeIdentities, model.Nodes);
        auto comments = BuildNodeGraphCommentModel(document.Definition().Authoring, model.NodeIdentities);
        bool nodeMenuOpen = false;
        if (auto combo = ui.BeginCombo("Add Node", "Choose..."); combo)
        {
            nodeMenuOpen = true;
            if (!m_NodeMenuOpen)
            {
                m_NodeSearch.clear();
                m_NodeMenuSelection.Open();
            }
            if (DrawNodeCreationMenu(ui, std::nullopt))
            {
                m_NodeMenuOpen = false;
                return;
            }
        }
        ui.SameLine();
        if (ui.Button("Frame All"))
            m_Canvas.Focus(model.Nodes, ui.ContentAvailable());
        ui.SameLine();
        if (DrawArrangeMenu(ui, model.Nodes, model.Connections, model.NodeIdentities, model.ConnectionIdentities))
            return;
        ui.SameLine();
        (void)DrawGraphBookmarkMenu(ui, m_Bookmarks, m_Canvas);
        ui.SameLine();
        ui.TextColored(
            m_Controller.ShaderGraphTheme().MutedText,
            "Right-click canvas or items for actions  |  drag pins to connect  |  double-click cable routes");
        const auto findCanvasConnection = [&](const Keire::AssetId id) -> std::optional<StableNodeId>
        {
            const auto found = std::ranges::find_if(model.ConnectionIdentities,
                                                    [id](const auto& identity) { return identity.second == id; });
            return found == model.ConnectionIdentities.end() ? std::nullopt : std::optional<StableNodeId>(found->first);
        };
        const auto findDefinitionNode = [&](const StableNodeId canvasId) -> const Keire::ShaderGraphNode*
        {
            const auto id = model.Node(canvasId);
            if (!id)
                return nullptr;
            const auto found = std::ranges::find(document.Definition().Nodes, *id, &Keire::ShaderGraphNode::Id);
            return found == document.Definition().Nodes.end() ? nullptr : &*found;
        };
        const auto findDefinitionPin = [&](const Keire::ShaderGraphNode& node,
                                           const StableNodeId canvasId) -> const Keire::ShaderGraphPin*
        {
            const auto id = model.Pin(canvasId);
            if (!id)
                return nullptr;
            const auto found = std::ranges::find(node.Pins, *id, &Keire::ShaderGraphPin::Id);
            return found == node.Pins.end() ? nullptr : &*found;
        };
        SynchronizeGraphSelection(m_Canvas, model.NodeIdentities, m_SelectedNodes, m_SelectedNode);
        m_Canvas.SelectConnection(m_SelectedConnection ? findCanvasConnection(*m_SelectedConnection) : std::nullopt);
        const NodeGraphCanvasOptions options{
            .Editable = true,
            .InteractiveConnections = true,
            .ValidateConnection =
                [&](const NodeGraphConnectionRequest& request)
            {
                const auto outputNode = model.Node(request.SourceNode);
                const auto outputPin = model.Pin(request.SourcePin);
                const auto inputNode = model.Node(request.TargetNode);
                const auto inputPin = model.Pin(request.TargetPin);
                if (!outputNode || !outputPin || !inputNode || !inputPin)
                    return NodeGraphConnectionValidation{NodeGraphConnectionValidationStatus::Reject,
                                                         "A Shader Graph connection endpoint is unavailable."};
                return document.CheckConnection({*outputNode, *outputPin}, {*inputNode, *inputPin});
            },
            .EditableReroutes = true,
            .MultiSelection = true,
            .Comments = comments.Comments,
        };
        const auto canvas = m_Canvas.Draw(ui, "ShaderGraphCanvas", model.Nodes, model.Connections, options);
        DrawComments(ui, document, model, comments, canvas);
        m_SelectedNodes = ResolveGraphSelection(canvas.SelectedNodes, model.NodeIdentities);
        m_SelectedNode = m_SelectedNodes.empty() ? std::nullopt : std::optional(m_SelectedNodes.back());
        if (HandleClipboard(canvas, model.NodeIdentities))
            return;
        if (!canvas.DuplicateNodesRequested.empty())
            return DuplicateSelection(canvas.DuplicateNodesRequested, model.NodeIdentities);
        const auto setRouting = [&](const StableNodeId canvasConnection, std::vector<Keire::Vector2> routing)
        {
            const auto connection = model.Connection(canvasConnection);
            if (!connection)
                return;
            try
            {
                (void)document.SetConnectionRouting(*connection, std::move(routing));
            }
            catch (const std::exception& error)
            {
                Report(error.what());
            }
        };
        if (canvas.AddRerouteRequested)
        {
            const auto connection =
                std::ranges::find(model.Connections, canvas.AddRerouteRequested->Connection, &NodeGraphConnection::Id);
            if (connection != model.Connections.end() &&
                canvas.AddRerouteRequested->Index <= connection->RoutingPoints.size())
            {
                auto routing = connection->RoutingPoints;
                routing.insert(routing.begin() + static_cast<std::ptrdiff_t>(canvas.AddRerouteRequested->Index),
                               canvas.AddRerouteRequested->GraphPosition);
                setRouting(connection->Id, std::move(routing));
            }
        }
        if (canvas.MoveRerouteRequested)
        {
            const auto connection =
                std::ranges::find(model.Connections, canvas.MoveRerouteRequested->Connection, &NodeGraphConnection::Id);
            if (connection != model.Connections.end() &&
                canvas.MoveRerouteRequested->Index < connection->RoutingPoints.size())
            {
                auto routing = connection->RoutingPoints;
                routing[canvas.MoveRerouteRequested->Index] = canvas.MoveRerouteRequested->GraphPosition;
                setRouting(connection->Id, std::move(routing));
            }
        }
        if (canvas.DeleteRerouteRequested)
        {
            const auto connection = std::ranges::find(model.Connections, canvas.DeleteRerouteRequested->Connection,
                                                      &NodeGraphConnection::Id);
            if (connection != model.Connections.end() &&
                canvas.DeleteRerouteRequested->Index < connection->RoutingPoints.size())
            {
                auto routing = connection->RoutingPoints;
                routing.erase(routing.begin() + static_cast<std::ptrdiff_t>(canvas.DeleteRerouteRequested->Index));
                setRouting(connection->Id, std::move(routing));
            }
        }
        if (canvas.ActivatedConnection)
            m_SelectedConnection = model.Connection(*canvas.ActivatedConnection);
        if (canvas.BackgroundActivated)
        {
            m_SelectedNode.reset();
            m_SelectedConnection.reset();
        }
        if (canvas.ContextRequested)
        {
            m_GraphContext = canvas.ContextRequested;
            m_NodeSearch.clear();
            if (canvas.ContextRequested->Kind == NodeGraphContextTargetKind::Comment)
                m_GraphContext.reset();
            else if (canvas.ContextRequested->Kind == NodeGraphContextTargetKind::Background)
            {
                m_NodeCreationPosition = canvas.ContextRequested->GraphPosition;
                m_NodeMenuSelection.Open();
                ui.SetNextWindowSize({380.0F, 440.0F}, true);
                ui.OpenPopup("ShaderGraphNodePalette");
            }
            else
            {
                m_NodeCreationPosition.reset();
                ui.OpenPopup("ShaderGraphItemContext");
            }
        }
        if (!canvas.MoveCompletedNodes.empty())
        {
            std::vector<std::pair<Keire::AssetId, Keire::Vector2>> moves;
            for (const auto moved : canvas.MoveCompletedNodes)
                if (const auto node = model.Node(moved);
                    node && std::ranges::find(model.Nodes, moved, &NodeGraphNode::Id) != model.Nodes.end())
                    moves.emplace_back(*node, std::ranges::find(model.Nodes, moved, &NodeGraphNode::Id)->Position);
            try
            {
                (void)document.MoveNodes(moves);
            }
            catch (const std::exception& error)
            {
                Report(error.what());
            }
        }
        if (canvas.ConnectionRequested)
        {
            const auto outputNode = model.Node(canvas.ConnectionRequested->SourceNode);
            const auto outputPin = model.Pin(canvas.ConnectionRequested->SourcePin);
            const auto inputNode = model.Node(canvas.ConnectionRequested->TargetNode);
            const auto inputPin = model.Pin(canvas.ConnectionRequested->TargetPin);
            if (outputNode && outputPin && inputNode && inputPin)
                try
                {
                    (void)document.AddConnection({{}, {*outputNode, *outputPin}, {*inputNode, *inputPin}});
                }
                catch (const std::exception& error)
                {
                    Report(error.what());
                }
        }
        if (canvas.DeleteConnectionRequested)
        {
            const auto connection = model.Connection(*canvas.DeleteConnectionRequested);
            if (connection)
                try
                {
                    (void)document.RemoveConnection(*connection);
                    m_SelectedConnection.reset();
                }
                catch (const std::exception& error)
                {
                    Report(error.what());
                }
        }
        if (!canvas.DeleteNodesRequested.empty())
        {
            std::vector<Keire::AssetId> nodes;
            for (const auto selected : canvas.DeleteNodesRequested)
                if (const auto node = model.Node(selected))
                    nodes.push_back(*node);
            try
            {
                (void)document.RemoveNodes(nodes);
                m_SelectedNode.reset();
                m_SelectedNodes.clear();
            }
            catch (const std::exception& error)
            {
                Report(error.what());
            }
        }
        if (!canvas.ProtectedNodes.empty())
            Report("Shader Output is protected and was not deleted.");
        if (auto popup = ui.BeginPopup("ShaderGraphItemContext"); popup)
        {
            if (!m_GraphContext)
            {
                ui.TextColored(m_Controller.ShaderGraphTheme().MutedText, "The graph item is no longer available.");
            }
            else if (m_GraphContext->Kind == NodeGraphContextTargetKind::Node ||
                     m_GraphContext->Kind == NodeGraphContextTargetKind::Pin)
            {
                const auto* node = findDefinitionNode(m_GraphContext->Node);
                const auto* pin = node && m_GraphContext->Kind == NodeGraphContextTargetKind::Pin
                                      ? findDefinitionPin(*node, m_GraphContext->Pin)
                                      : nullptr;
                if (!node || (m_GraphContext->Kind == NodeGraphContextTargetKind::Pin && !pin))
                {
                    ui.TextColored(m_Controller.ShaderGraphTheme().MutedText, "The graph item is no longer available.");
                }
                else
                {
                    ui.TextColored(m_Controller.ShaderGraphTheme().Accent, pin ? pin->Name : node->Name);
                    ui.TextColored(m_Controller.ShaderGraphTheme().MutedText, pin ? node->Name : "SHADER GRAPH NODE");
                    ui.Separator();
                    if (ui.MenuItem("Inspect Node"))
                    {
                        m_SelectedNode = node->Id;
                        m_SelectedConnection.reset();
                    }
                    if (DrawClipboardContextMenu(ui, model.NodeIdentities, true,
                                                 node->Kind != Keire::ShaderGraphNodeKind::Master))
                        return;
                    if (auto addMenu = ui.BeginMenu("Add Compatible Node"); addMenu)
                    {
                        if (DrawNodeCreationMenu(ui, m_GraphContext->GraphPosition, pin ? nullptr : node, pin))
                            return;
                    }
                    const bool connected = std::ranges::any_of(
                        document.Definition().Connections,
                        [&](const Keire::ShaderGraphConnection& connection)
                        {
                            if (pin)
                                return connection.Output.Pin == pin->Id || connection.Input.Pin == pin->Id;
                            return connection.Output.Node == node->Id || connection.Input.Node == node->Id;
                        });
                    if (ui.MenuItem(pin ? "Unlink Pin" : "Unlink All Cables", false, connected))
                    {
                        const auto nodeId = node->Id;
                        const auto pinId = pin ? pin->Id : Keire::AssetId{};
                        try
                        {
                            (void)document.Edit(pin ? "Disconnect Shader Graph pin" : "Disconnect Shader Graph node",
                                                [nodeId, pinId](auto& definition)
                                                {
                                                    std::erase_if(definition.Connections,
                                                                  [&](const Keire::ShaderGraphConnection& connection)
                                                                  {
                                                                      return pinId
                                                                                 ? connection.Output.Pin == pinId ||
                                                                                       connection.Input.Pin == pinId
                                                                                 : connection.Output.Node == nodeId ||
                                                                                       connection.Input.Node == nodeId;
                                                                  });
                                                });
                            m_SelectedConnection.reset();
                        }
                        catch (const std::exception& error)
                        {
                            Report(error.what());
                        }
                    }
                    if (!pin)
                    {
                        ui.Separator();
                        if (ui.MenuItem("Create Comment from Selection"))
                            CreateComment(ui, document, model, m_GraphContext->GraphPosition, true);
                        const bool extractable = CanExtractSelection(document.Definition());
                        if (ui.MenuItem("Extract Selection to Shader Function...", false, extractable))
                        {
                            m_ExtractionName = "ExtractedShaderFunction";
                            m_FunctionExtractionSelection = m_SelectedNodes;
                            m_OpenFunctionExtractionPopup = true;
                        }
                        std::vector<Keire::AssetId> removableNodes;
                        for (const auto selected : m_SelectedNodes)
                        {
                            const auto* selectedNode = FindNode(document.Definition(), selected);
                            if (selectedNode && selectedNode->Kind != Keire::ShaderGraphNodeKind::Master)
                                removableNodes.push_back(selected);
                        }
                        if (removableNodes.empty() && node->Kind != Keire::ShaderGraphNodeKind::Master)
                            removableNodes.push_back(node->Id);
                        if (ui.MenuItem(m_SelectedNodes.size() > 1 ? "Delete Nodes" : "Delete Node", false,
                                        !removableNodes.empty()))
                            try
                            {
                                (void)document.RemoveNodes(removableNodes);
                                m_SelectedNode.reset();
                                m_SelectedNodes.clear();
                                m_Canvas.Select(std::nullopt);
                            }
                            catch (const std::exception& error)
                            {
                                Report(error.what());
                            }
                    }
                }
            }
            else if (m_GraphContext->Kind == NodeGraphContextTargetKind::Connection)
            {
                const auto id = model.Connection(m_GraphContext->Connection);
                const auto connection =
                    id ? std::ranges::find(document.Definition().Connections, *id, &Keire::ShaderGraphConnection::Id)
                       : document.Definition().Connections.end();
                if (connection == document.Definition().Connections.end())
                {
                    ui.TextColored(m_Controller.ShaderGraphTheme().MutedText,
                                   "The graph cable is no longer available.");
                }
                else
                {
                    ui.TextColored(m_Controller.ShaderGraphTheme().Accent, "SHADER GRAPH CABLE");
                    ui.Separator();
                    if (ui.MenuItem("Select Source Node"))
                        m_SelectedNode = connection->Output.Node;
                    if (ui.MenuItem("Select Target Node"))
                        m_SelectedNode = connection->Input.Node;
                    if (ui.MenuItem("Unlink Cable"))
                        try
                        {
                            (void)document.RemoveConnection(connection->Id);
                            m_SelectedConnection.reset();
                        }
                        catch (const std::exception& error)
                        {
                            Report(error.what());
                        }
                }
            }
        }
        if (openFunctionExtractionPopup)
            ui.OpenPopup("ExtractShaderGraphFunction");
        if (DrawFunctionExtractionPopup(ui))
            return;
        bool contextMenuOpen = false;
        if (auto popup = ui.BeginPopup("ShaderGraphNodePalette"); popup)
        {
            contextMenuOpen = true;
            if (DrawNodeCreationMenu(ui, m_NodeCreationPosition))
            {
                m_NodeCreationPosition.reset();
                m_NodeMenuOpen = false;
                return;
            }
            ui.Separator();
            if (DrawClipboardContextMenu(ui, model.NodeIdentities, false))
                return;
            if (ui.MenuItem("Create Empty Comment"))
                CreateComment(ui, document, model, *m_NodeCreationPosition, false);
            if (ui.MenuItem("Frame All Nodes"))
                m_Canvas.Focus(model.Nodes, ui.ContentAvailable());
        }
        if (!contextMenuOpen)
            m_NodeCreationPosition.reset();
        m_NodeMenuOpen = nodeMenuOpen || contextMenuOpen;
    }
    bool ShaderGraphPanel::AddNode(const Keire::ShaderGraphNodeKind kind, const Keire::ShaderGraphValueType type,
                                   const std::optional<Keire::Vector2> graphPosition)
    {
        try
        {
            auto node = Keire::CreateShaderGraphNode(kind, type);
            if (kind == Keire::ShaderGraphNodeKind::Parameter)
            {
                const auto base = type == Keire::ShaderGraphValueType::Vector2     ? "Vector2"
                                  : type == Keire::ShaderGraphValueType::Vector3   ? "Vector3"
                                  : type == Keire::ShaderGraphValueType::Vector4   ? "Vector4"
                                  : type == Keire::ShaderGraphValueType::Color     ? "BaseColor"
                                  : type == Keire::ShaderGraphValueType::Texture2D ? "Texture"
                                                                                   : "Scalar";
                node.Symbol = UniqueSymbol(m_Controller.ShaderGraphState().Definition(), base);
                node.Name = node.Symbol;
            }
            else if (kind == Keire::ShaderGraphNodeKind::Keyword)
            {
                node.Symbol = UniqueSymbol(m_Controller.ShaderGraphState().Definition(), "FEATURE");
                node.Name = node.Symbol;
            }
            else if (kind == Keire::ShaderGraphNodeKind::Custom)
                node.Include = "Assets/Shaders/MaterialNodes.hlsl";
            if (graphPosition)
                node.EditorPosition = *graphPosition;
            else
            {
                const auto size = m_Canvas.Zoom();
                node.EditorPosition = {-m_Canvas.Pan().X + 280.0F / size, -m_Canvas.Pan().Y + 180.0F / size};
            }
            const auto id = node.Id;
            const bool changed =
                kind == Keire::ShaderGraphNodeKind::Keyword
                    ? m_Controller.ShaderGraphState().Edit("Add Shader Graph keyword",
                                                           [node = std::move(node)](auto& definition) mutable
                                                           {
                                                               definition.Keywords.push_back(
                                                                   {.Name = node.Symbol, .DefaultOption = "false"});
                                                               definition.Nodes.push_back(std::move(node));
                                                           })
                    : m_Controller.ShaderGraphState().AddNode(std::move(node));
            if (changed)
                m_SelectedNode = id;
            return changed;
        }
        catch (const std::exception& error)
        {
            Report(error.what());
            return false;
        }
    }
} // namespace KeireEditor
