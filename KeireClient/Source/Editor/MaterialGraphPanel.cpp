#include "KeireClient/Editor/MaterialGraphPanel.h"

#include <algorithm>
#include <array>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    void MaterialGraphPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.material-graph", "Material Graph", false});
    }

    void MaterialGraphPanel::Draw(Keire::UiFrame& ui)
    {
        if (auto panel = ui.BeginPanel(m_Registration); panel)
        {
            if (!m_Controller.MaterialGraphState().IsOpen())
            {
                ui.TextColored(m_Controller.MaterialGraphTheme().MutedText,
                               "Open a Material Graph asset to author a shader-backed material.");
                return;
            }
            DrawHeader(ui);
            ui.Separator();
            DrawCanvas(ui);
            ui.Separator();
            DrawInspector(ui);
            ui.Separator();
            DrawDiagnostics(ui);
        }
    }

    void MaterialGraphPanel::ResetTransientState() noexcept
    {
        m_SelectedNode.reset();
        m_SelectedConnection.reset();
        m_NodeCreationPosition.reset();
        m_Canvas.CancelInteractions();
        m_Canvas.Select(std::nullopt);
        m_Canvas.SelectConnection(std::nullopt);
        m_ShaderGraphPicker.Clear();
        m_RawShaderPicker.Clear();
        m_TexturePicker.Clear();
        m_Message.clear();
    }

    void MaterialGraphPanel::DrawHeader(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.MaterialGraphState();
        const auto& theme = m_Controller.MaterialGraphTheme();
        ui.TextColored(document.Diagnostics().empty() ? theme.Success : theme.Warning,
                       document.Diagnostics().empty() ? "MATERIAL GRAPH READY" : "MATERIAL GRAPH HAS DIAGNOSTICS");
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!document.Dirty()); disabled)
            if (ui.Button("Save"))
            {
                try
                {
                    m_Controller.SaveMaterialGraphDocument();
                    m_Message = "Saved Material Graph.";
                }
                catch (const std::exception& error)
                {
                    Report(error.what());
                }
            }
        const auto undo = document.UndoContext();
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!undo || !undo->CanUndo()); disabled)
            if (ui.Button("Undo"))
                m_Controller.UndoMaterialGraphEdit();
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!undo || !undo->CanRedo()); disabled)
            if (ui.Button("Redo"))
                m_Controller.RedoMaterialGraphEdit();

        const auto current = document.Definition().Shader;
        auto shaderGraph =
            current.Kind == Keire::MaterialShaderSourceKind::ShaderGraph ? current.Asset : Keire::AssetId{};
        const AssetPickerOptions graphOptions{
            .Label = "Shader Graph",
            .ExpectedType = Keire::ShaderGraphAsset::StaticType(),
            .Reveal = [this](const Keire::AssetId asset) { m_Controller.RevealMaterialGraphAsset(asset); },
            .AllowNone = false,
        };
        if (m_ShaderGraphPicker.Draw(ui, m_Controller.MaterialGraphAssetRecords(), shaderGraph, graphOptions) &&
            shaderGraph)
        {
            try
            {
                Keire::MaterialShaderReference replacement;
                replacement.Kind = Keire::MaterialShaderSourceKind::ShaderGraph;
                replacement.Asset = shaderGraph;
                (void)document.SetShader(std::move(replacement));
            }
            catch (const std::exception& error)
            {
                Report(error.what());
            }
        }

        auto rawShader =
            current.Kind == Keire::MaterialShaderSourceKind::ShaderAsset ? current.Asset : Keire::AssetId{};
        const AssetPickerOptions rawOptions{
            .Label = "Raw Shader",
            .ExpectedType = Keire::ShaderAsset::StaticType(),
            .Reveal = [this](const Keire::AssetId asset) { m_Controller.RevealMaterialGraphAsset(asset); },
            .AllowNone = false,
        };
        if (m_RawShaderPicker.Draw(ui, m_Controller.MaterialGraphAssetRecords(), rawShader, rawOptions) && rawShader)
        {
            try
            {
                Keire::MaterialShaderReference replacement;
                replacement.Kind = Keire::MaterialShaderSourceKind::ShaderAsset;
                replacement.Asset = rawShader;
                (void)document.SetShader(std::move(replacement));
            }
            catch (const std::exception& error)
            {
                Report(error.what());
            }
        }
        ui.TextColored(theme.MutedText,
                       "Shader changes rebuild Material Output inputs from stable reflected property identities.");
    }

    void MaterialGraphPanel::DrawCanvas(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.MaterialGraphState();
        auto model = document.BuildCanvasModel();
        if (auto combo = ui.BeginCombo("Add Value Node", "Choose exposed input..."); combo)
            for (const auto& property : document.Definition().Properties)
                if (ui.Selectable(property.Name, false))
                {
                    AddValueNode(property);
                    return;
                }
        ui.SameLine();
        if (ui.Button("Frame All"))
            m_Canvas.Focus(model.Nodes, ui.ContentAvailable());
        ui.SameLine();
        ui.TextColored(m_Controller.MaterialGraphTheme().MutedText,
                       "Drag values into Material Output  |  middle-drag to pan  |  wheel to zoom  |  Delete removes");

        const auto findCanvas = [](const auto& identities, const Keire::AssetId id) -> std::optional<StableNodeId>
        {
            const auto found =
                std::ranges::find_if(identities, [id](const auto& identity) { return identity.second == id; });
            return found == identities.end() ? std::nullopt : std::optional(found->first);
        };
        m_Canvas.Select(m_SelectedNode ? findCanvas(model.NodeIdentities, *m_SelectedNode) : std::nullopt);
        m_Canvas.SelectConnection(m_SelectedConnection ? findCanvas(model.ConnectionIdentities, *m_SelectedConnection)
                                                       : std::nullopt);
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
                                                         "Material Graph endpoint is unavailable."};
                return document.CheckConnection({*outputNode, *outputPin}, {*inputNode, *inputPin});
            },
        };
        const auto canvas = m_Canvas.Draw(ui, "MaterialGraphCanvas", model.Nodes, model.Connections, options);
        if (canvas.ActivatedNode)
            m_SelectedNode = model.Node(*canvas.ActivatedNode);
        if (canvas.ActivatedConnection)
            m_SelectedConnection = model.Connection(*canvas.ActivatedConnection);
        if (canvas.BackgroundActivated)
        {
            m_SelectedNode.reset();
            m_SelectedConnection.reset();
        }
        if (canvas.MoveCompletedNode)
        {
            const auto node = model.Node(*canvas.MoveCompletedNode);
            const auto canvasNode = std::ranges::find(model.Nodes, *canvas.MoveCompletedNode, &NodeGraphNode::Id);
            if (node && canvasNode != model.Nodes.end())
                try
                {
                    (void)document.MoveNode(*node, canvasNode->Position);
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
            if (const auto connection = model.Connection(*canvas.DeleteConnectionRequested))
                try
                {
                    (void)document.RemoveConnection(*connection);
                    m_SelectedConnection.reset();
                }
                catch (const std::exception& error)
                {
                    Report(error.what());
                }
        if (canvas.DeleteNodeRequested)
            if (const auto node = model.Node(*canvas.DeleteNodeRequested);
                node && *node != document.Definition().OutputNode)
                try
                {
                    (void)document.RemoveNode(*node);
                    m_SelectedNode.reset();
                }
                catch (const std::exception& error)
                {
                    Report(error.what());
                }

        if (canvas.ContextRequested)
        {
            m_NodeCreationPosition = canvas.ContextRequested->GraphPosition;
            ui.OpenPopup("MaterialGraphValuePalette");
        }
        if (auto popup = ui.BeginPopup("MaterialGraphValuePalette"); popup)
            for (const auto& property : document.Definition().Properties)
                if (ui.MenuItem(property.Name))
                {
                    AddValueNode(property, m_NodeCreationPosition);
                    m_NodeCreationPosition.reset();
                    return;
                }
    }

    bool MaterialGraphPanel::DrawValueEditor(Keire::UiFrame& ui, const std::string_view label,
                                             Keire::MaterialPropertyValue& value)
    {
        if (auto* scalar = std::get_if<float>(&value))
        {
            double edited = *scalar;
            if (!ui.DragScalar(label, edited, 0.01))
                return false;
            *scalar = static_cast<float>(edited);
            return true;
        }
        if (auto* vector = std::get_if<Keire::Vector2>(&value))
            return ui.DragVector2(label, *vector);
        if (auto* vector = std::get_if<Keire::Vector3>(&value))
            return ui.DragVector3(label, *vector);
        if (auto* vector = std::get_if<Keire::Vector4>(&value))
            return ui.DragVector4(label, *vector);
        if (auto* color = std::get_if<Keire::Color>(&value))
        {
            Keire::UiColor edited{color->Red, color->Green, color->Blue, color->Alpha};
            if (!ui.ColorEdit(label, edited))
                return false;
            *color = {edited.Red, edited.Green, edited.Blue, edited.Alpha};
            return true;
        }
        auto* texture = std::get_if<Keire::AssetId>(&value);
        if (!texture)
            return false;
        const AssetPickerOptions options{
            .Label = label,
            .ExpectedType = Keire::Texture2DAsset::StaticType(),
            .Reveal = [this](const Keire::AssetId asset) { m_Controller.RevealMaterialGraphAsset(asset); },
            .AllowNone = true,
        };
        return m_TexturePicker.Draw(ui, m_Controller.MaterialGraphAssetRecords(), *texture, options);
    }

    void MaterialGraphPanel::DrawInspector(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.MaterialGraphState();
        const auto& theme = m_Controller.MaterialGraphTheme();
        ui.TextColored(theme.Accent, "MATERIAL GRAPH INSPECTOR");
        if (!m_SelectedNode)
        {
            ui.TextColored(theme.MutedText, "Select Material Output or a value node.");
            return;
        }
        if (*m_SelectedNode == document.Definition().OutputNode)
        {
            ui.Text("Material Output");
            auto surface = document.Definition().Surface;
            constexpr std::array modes{std::string_view("Opaque"), std::string_view("Masked"),
                                       std::string_view("Transparent")};
            const auto mode = static_cast<std::size_t>(surface.AlphaMode);
            if (auto combo = ui.BeginCombo("Surface", modes[mode]); combo)
                for (std::size_t index = 0; index < modes.size(); ++index)
                    if (ui.Selectable(modes[index], index == mode))
                    {
                        surface.AlphaMode = static_cast<Keire::MaterialAlphaMode>(index);
                        (void)document.SetSurface(surface);
                    }
            if (surface.AlphaMode == Keire::MaterialAlphaMode::Mask &&
                ui.SliderFloat("Alpha Cutoff", surface.AlphaCutoff, 0.0F, 1.0F))
                (void)document.SetSurface(surface);
            if (ui.Checkbox("Double Sided", surface.DoubleSided))
                (void)document.SetSurface(surface);
            ui.Separator();
            ui.TextColored(theme.MutedText, "Unconnected shader inputs use these material defaults.");
            for (const auto& property : document.Definition().Properties)
            {
                const bool connected =
                    std::ranges::any_of(document.Definition().Connections,
                                        [&](const auto& connection) { return connection.Input.Pin == property.Pin; });
                if (connected)
                {
                    ui.TextColored(theme.MutedText, property.Name + "  |  connected");
                    continue;
                }
                auto value = property.Value;
                if (DrawValueEditor(ui, property.Name + "##" + property.Pin.ToString(), value))
                    try
                    {
                        (void)document.SetInputValue(property.Pin, std::move(value));
                    }
                    catch (const std::exception& error)
                    {
                        Report(error.what());
                    }
            }
            return;
        }

        const auto node =
            std::ranges::find(document.Definition().Nodes, *m_SelectedNode, &Keire::MaterialGraphValueNode::Id);
        if (node == document.Definition().Nodes.end())
            return;
        ui.Text(node->Name);
        auto value = node->Value;
        if (DrawValueEditor(ui, "Value", value))
            try
            {
                (void)document.EditNode(node->Id, [value = std::move(value)](auto& candidate) mutable
                                        { candidate.Value = std::move(value); });
            }
            catch (const std::exception& error)
            {
                Report(error.what());
            }
        if (ui.Button("Delete Value Node"))
            try
            {
                (void)document.RemoveNode(node->Id);
                m_SelectedNode.reset();
            }
            catch (const std::exception& error)
            {
                Report(error.what());
            }
    }

    void MaterialGraphPanel::DrawDiagnostics(Keire::UiFrame& ui)
    {
        const auto& theme = m_Controller.MaterialGraphTheme();
        if (!m_Message.empty())
            ui.TextColored(theme.Warning, m_Message);
        const auto diagnostics = m_Controller.MaterialGraphState().Diagnostics();
        if (diagnostics.empty())
        {
            ui.TextColored(theme.Success, "Material Graph diagnostics: clear.");
            return;
        }
        for (const auto& diagnostic : diagnostics)
        {
            const auto color = diagnostic.Severity == Keire::MaterialGraphDiagnosticSeverity::Error ? theme.Error
                               : diagnostic.Severity == Keire::MaterialGraphDiagnosticSeverity::Warning
                                   ? theme.Warning
                                   : theme.MutedText;
            ui.TextColored(color, diagnostic.Code + "  " + diagnostic.Message);
        }
    }

    void MaterialGraphPanel::AddValueNode(const Keire::MaterialGraphPropertyBinding& property,
                                          const std::optional<Keire::Vector2> position)
    {
        try
        {
            auto node = Keire::CreateMaterialGraphValueNode(property.Type, property.Value,
                                                            position.value_or(Keire::Vector2{120.0F, 120.0F}));
            node.Name = property.Name;
            const auto id = node.Id;
            if (m_Controller.MaterialGraphState().AddNode(std::move(node)))
                m_SelectedNode = id;
        }
        catch (const std::exception& error)
        {
            Report(error.what());
        }
    }

    void MaterialGraphPanel::Report(std::string message) noexcept
    {
        m_Message = std::move(message);
        m_Controller.ReportMaterialGraphError(m_Message);
    }
} // namespace KeireEditor
