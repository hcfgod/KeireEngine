#include "KeireClient/Editor/MaterialGraphPanel.h"

#include "KeireClient/Editor/ShaderGraphPreview.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace KeireEditor
{
    namespace
    {
        struct ExpressionEntry
        {
            Keire::ShaderGraphNodeKind Kind;
            std::string_view Category;
            std::string_view Name;
            Keire::ShaderGraphValueType Type = Keire::ShaderGraphValueType::Scalar;
        };

        [[nodiscard]] const std::vector<ExpressionEntry>& ExpressionEntries()
        {
            static const auto entries = []
            {
                std::vector<ExpressionEntry> result{
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
                    if (!descriptor.UserCreatable || descriptor.Kind == Keire::ShaderGraphNodeKind::Master ||
                        descriptor.Kind == Keire::ShaderGraphNodeKind::Parameter ||
                        descriptor.Kind == Keire::ShaderGraphNodeKind::Constant)
                        continue;
                    result.push_back(
                        {descriptor.Kind, descriptor.Category, descriptor.DisplayName, descriptor.DefaultValueType});
                }
                return result;
            }();
            return entries;
        }

        [[nodiscard]] std::string Lower(const std::string_view value)
        {
            std::string result(value);
            std::ranges::transform(result, result.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return result;
        }

        [[nodiscard]] std::string UniqueExpressionSymbol(const Keire::MaterialGraphDefinition& definition,
                                                         const std::string_view base)
        {
            std::set<std::string, std::less<>> symbols;
            for (const auto& node : definition.SurfaceGraph.Nodes)
                if (!node.Symbol.empty())
                    symbols.insert(node.Symbol);
            if (!symbols.contains(base))
                return std::string(base);
            for (std::size_t suffix = 2;; ++suffix)
            {
                auto candidate = std::string(base) + std::to_string(suffix);
                if (!symbols.contains(candidate))
                    return candidate;
            }
        }
    } // namespace

    MaterialGraphPanel::~MaterialGraphPanel() noexcept
    {
        m_PreviewCancellation->fetch_add(1, std::memory_order_release);
        if (m_PreviewRender)
        {
            m_PreviewRender.Cancel();
            (void)m_PreviewRender.Wait();
        }
        if (m_JobScope)
        {
            m_JobScope->Cancel();
            m_JobScope->Wait();
        }
        if (m_OwnJobSystem && m_JobSystem)
            m_JobSystem->Close();
    }

    void MaterialGraphPanel::SetJobSystem(Keire::Ref<Keire::JobSystem> jobs)
    {
        if (m_PreviewRender || m_JobScope)
            throw std::logic_error("Material Graph preview jobs are already configured.");
        if (!jobs)
            throw std::invalid_argument("Material Graph preview job system is unavailable.");
        m_JobSystem = std::move(jobs);
    }

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
            const auto available = ui.ContentAvailable();
            const float authoringHeight = std::max(360.0F, available.Height * 0.72F);
            const float previewPaneWidth = m_ShowPreview && available.Width >= 620.0F ? 248.0F : 0.0F;
            const float graphPaneWidth = std::max(320.0F, available.Width - previewPaneWidth - 8.0F);
            if (auto graph = ui.BeginChild("MaterialGraphAuthoring", {graphPaneWidth, authoringHeight}, false); graph)
                DrawCanvas(ui);
            if (previewPaneWidth > 0.0F)
            {
                ui.SameLine();
                if (auto preview = ui.BeginChild("MaterialGraphPreviewPane", {previewPaneWidth, authoringHeight}, true);
                    preview)
                    DrawPreview(ui);
            }
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
        m_InspectorNode.reset();
        m_NodeCreationPosition.reset();
        m_Canvas.CancelInteractions();
        m_Canvas.Select(std::nullopt);
        m_Canvas.SelectConnection(std::nullopt);
        m_ShaderGraphPicker.Clear();
        m_RawShaderPicker.Clear();
        m_TexturePicker.Clear();
        m_NodeSearch.clear();
        m_InspectorName.clear();
        m_InspectorSymbol.clear();
        m_InspectorInclude.clear();
        m_InspectorFunction.clear();
        m_InspectorDescription.clear();
        m_InspectorCategory.clear();
        m_ShowTemplateParameters = false;
        m_PreviewImage.Reset();
        m_PreviewDefinition.reset();
        m_PreviewError.clear();
        m_PreviewDirty = false;
        if (++m_PreviewGeneration == 0)
            ++m_PreviewGeneration;
        m_PreviewCancellation->store(m_PreviewGeneration, std::memory_order_release);
        m_Message.clear();
    }

    void MaterialGraphPanel::DrawHeader(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.MaterialGraphState();
        const auto& theme = m_Controller.MaterialGraphTheme();
        const auto materialOutput =
            std::ranges::find(document.Definition().SurfaceGraph.Nodes, Keire::ShaderGraphNodeKind::Master,
                              &Keire::ShaderGraphNode::Kind);
        const bool hasSurfaceExpressions = materialOutput != document.Definition().SurfaceGraph.Nodes.end() &&
                                           std::ranges::any_of(document.Definition().SurfaceGraph.Connections,
                                                               [&](const Keire::ShaderGraphConnection& connection)
                                                               { return connection.Input.Node == materialOutput->Id; });
        ui.TextColored(document.Diagnostics().empty() ? theme.Success : theme.Warning,
                       document.Diagnostics().empty() ? hasSurfaceExpressions
                                                            ? "MATERIAL SURFACE READY  |  SAVE TO COMPILE"
                                                            : "MATERIAL GRAPH READY"
                                                      : "MATERIAL GRAPH HAS DIAGNOSTICS");
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
                       "Shader Graph defines the reusable template; Material Graph expressions compile the surface.");
        ui.SameLine();
        if (ui.Button(m_ShowPreview ? "Hide Preview" : "Show Preview"))
            m_ShowPreview = !m_ShowPreview;
    }

    void MaterialGraphPanel::DrawPreview(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.MaterialGraphState();
        const auto& theme = m_Controller.MaterialGraphTheme();
        if (m_PreviewRender && m_PreviewRender.IsComplete())
        {
            (void)m_PreviewRender.Wait();
            try
            {
                m_PreviewRender.RethrowIfFailed();
                std::optional<PreviewRenderResult> result;
                {
                    std::scoped_lock lock(m_PreviewRenderState->Mutex);
                    result = std::move(m_PreviewRenderState->Result);
                }
                if (result && result->Generation == m_PreviewGeneration)
                {
                    if (result->Error.empty())
                    {
                        constexpr std::uint32_t previewSize = 184;
                        m_PreviewImage = ui.CreateImage(previewSize, previewSize, result->Pixels);
                        m_PreviewError.clear();
                    }
                    else
                        m_PreviewError = std::move(result->Error);
                }
            }
            catch (const std::exception& error)
            {
                m_PreviewError = error.what();
            }
            m_PreviewRender = {};
            m_PreviewRenderState.reset();
        }
        ui.TextColored(theme.Accent, "LIVE MATERIAL PREVIEW");
        ui.TextColored(theme.MutedText, "Surface output on a studio-lit sphere");

        if (!m_PreviewDefinition || *m_PreviewDefinition != document.Definition())
        {
            m_PreviewDefinition = document.Definition();
            m_PreviewError.clear();
            m_PreviewDirty = true;
            if (++m_PreviewGeneration == 0)
                ++m_PreviewGeneration;
            m_PreviewCancellation->store(m_PreviewGeneration, std::memory_order_release);
        }
        if (m_PreviewDirty && !m_PreviewRender)
        {
            try
            {
                EnsureJobScope();
                const auto shaderTemplate = m_Controller.ResolveMaterialGraphTemplate(document.Definition().Shader);
                if (!shaderTemplate)
                    throw std::runtime_error("Select a Shader Graph template to enable the live material preview.");
                auto composed = Keire::ComposeMaterialGraphShader(document.Definition(), *shaderTemplate);
                composed =
                    Keire::ExpandShaderGraphFunctions(composed, [this](const Keire::AssetId asset)
                                                      { return m_Controller.ResolveMaterialGraphFunction(asset); });
                const auto generation = m_PreviewGeneration;
                const auto cancellation = m_PreviewCancellation;
                m_PreviewRenderState = std::make_shared<PreviewRenderState>();
                const auto state = m_PreviewRenderState;
                m_PreviewRender = m_JobScope->Submit(
                    {.Name = "Render Material Graph preview",
                     .Priority = Keire::JobPriority::Low,
                     .Class = Keire::JobClass::Compute,
                     .Domain = Keire::JobDomain::Tooling},
                    [generation, definition = std::move(composed), cancellation, state](Keire::JobContext& context)
                    {
                        PreviewRenderResult result{.Generation = generation};
                        const auto stop = context.StopToken();
                        try
                        {
                            constexpr std::uint32_t previewSize = 184;
                            result.Pixels = RenderShaderGraphPreview({
                                .Output = definition.Output,
                                .Mesh = Keire::ShaderGraphPreviewMesh::Sphere,
                                .Definition = &definition,
                                .Width = previewSize,
                                .Height = previewSize,
                                .CancellationRequested =
                                    [cancellation, generation, stop]
                                {
                                    return stop.stop_requested() ||
                                           cancellation->load(std::memory_order_acquire) != generation;
                                },
                            });
                        }
                        catch (const std::exception& error)
                        {
                            result.Error = error.what();
                        }
                        if (!context.StopRequested())
                        {
                            std::scoped_lock lock(state->Mutex);
                            state->Result = std::move(result);
                        }
                    });
                m_PreviewDirty = false;
            }
            catch (const std::exception& error)
            {
                m_PreviewError = error.what();
                m_PreviewDirty = false;
            }
        }

        if (!m_PreviewError.empty())
            ui.TextColoredWrapped(theme.Warning, m_PreviewError);
        if (m_PreviewRender || m_PreviewDirty)
            ui.TextColored(theme.MutedText, "Updating preview...");
        if (m_PreviewImage)
            ui.Image(m_PreviewImage, {184.0F, 184.0F});
        else if (m_PreviewError.empty() && !m_PreviewRender && !m_PreviewDirty)
            ui.TextColored(theme.Warning, "The live preview is unavailable.");
    }

    void MaterialGraphPanel::EnsureJobScope()
    {
        if (m_JobScope)
            return;
        if (!m_JobSystem)
        {
            Keire::JobSystemSpecification specification;
            specification.WorkerCount = 1;
            specification.BlockingWorkerCount = 1;
            m_JobSystem = Keire::CreateRef<Keire::JobSystem>(specification);
            m_OwnJobSystem = true;
        }
        m_JobScope = m_JobSystem->CreateScope("Material Graph previews");
    }

    bool MaterialGraphPanel::DrawExpressionCreationMenu(Keire::UiFrame& ui,
                                                        const std::optional<Keire::Vector2> position)
    {
        (void)ui.InputTextWithHint("##MaterialExpressionSearch", "Search expressions and categories...", m_NodeSearch);
        ui.Separator();
        const auto search = Lower(m_NodeSearch);
        std::size_t visible = 0;
        for (const auto& entry : ExpressionEntries())
        {
            const auto path = std::string(entry.Category) + " / " + std::string(entry.Name);
            if (!search.empty() && Lower(path).find(search) == std::string::npos)
                continue;
            ++visible;
            if (!ui.MenuItem(path))
                continue;
            if (!AddExpressionNode(entry.Kind, entry.Type, position))
                return false;
            m_NodeSearch.clear();
            ui.CloseCurrentPopup();
            return true;
        }
        for (const auto& record : m_Controller.MaterialGraphAssetRecords())
        {
            const bool reusable = record.Type == Keire::MaterialFunctionAsset::StaticType() ||
                                  record.Type == Keire::ShaderFunctionAsset::StaticType() ||
                                  record.Type == Keire::MaterialLayerAsset::StaticType() ||
                                  record.Type == Keire::MaterialLayerBlendAsset::StaticType();
            if (!reusable)
                continue;
            const auto name = record.RelativePath.stem().string();
            const auto path = "Functions & Layers / " + name;
            if (!search.empty() && Lower(path).find(search) == std::string::npos)
                continue;
            ++visible;
            if (!ui.MenuItem(path))
                continue;
            if (!AddFunctionNode(record.Id, name, position))
                return false;
            m_NodeSearch.clear();
            ui.CloseCurrentPopup();
            return true;
        }
        if (visible == 0)
            ui.TextColored(m_Controller.MaterialGraphTheme().MutedText, "No expressions match this search.");
        return false;
    }

    void MaterialGraphPanel::DrawCanvas(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.MaterialGraphState();
        auto model = document.BuildCanvasModel(m_ShowTemplateParameters);
        if (auto combo = ui.BeginCombo("Add Expression", "Search node library..."); combo)
            if (DrawExpressionCreationMenu(ui, std::nullopt))
                return;
        ui.SameLine();
        if (ui.Checkbox("Template Defaults", m_ShowTemplateParameters))
        {
            model = document.BuildCanvasModel(m_ShowTemplateParameters);
            m_Canvas.Focus(model.Nodes, ui.ContentAvailable());
        }
        if (m_ShowTemplateParameters)
        {
            ui.SameLine();
            if (auto combo = ui.BeginCombo("Add Override", "Choose template parameter..."); combo)
                for (const auto& property : document.Definition().Properties)
                    if (ui.Selectable(property.Name, false))
                    {
                        AddValueNode(property);
                        return;
                    }
        }
        ui.SameLine();
        if (ui.Button("Frame All"))
            m_Canvas.Focus(model.Nodes, ui.ContentAvailable());
        ui.SameLine();
        ui.TextColored(m_Controller.MaterialGraphTheme().MutedText,
                       "Right-click to add  |  drag pins to connect  |  middle-drag to pan  |  wheel to zoom");

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
            m_NodeSearch.clear();
            ui.SetNextWindowSize({380.0F, 440.0F}, true);
            ui.OpenPopup("MaterialGraphExpressionPalette");
        }
        bool paletteOpen = false;
        if (auto popup = ui.BeginPopup("MaterialGraphExpressionPalette"); popup)
        {
            paletteOpen = true;
            if (DrawExpressionCreationMenu(ui, m_NodeCreationPosition))
            {
                m_NodeCreationPosition.reset();
                return;
            }
        }
        if (!paletteOpen)
            m_NodeCreationPosition.reset();
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

    bool MaterialGraphPanel::DrawExpressionValueEditor(Keire::UiFrame& ui, const std::string_view label,
                                                       Keire::ShaderGraphValue& value)
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
            ui.TextColored(theme.MutedText, "Select Material Output, an expression, or a template override.");
            return;
        }

        const auto expressionFound =
            std::ranges::find(document.Definition().SurfaceGraph.Nodes, *m_SelectedNode, &Keire::ShaderGraphNode::Id);
        if (expressionFound != document.Definition().SurfaceGraph.Nodes.end())
        {
            const auto expression = *expressionFound;
            const bool materialOutput = expression.Kind == Keire::ShaderGraphNodeKind::Master;
            if (m_InspectorNode != expression.Id)
            {
                m_InspectorNode = expression.Id;
                m_InspectorName = expression.Name;
                m_InspectorSymbol = expression.Symbol;
                m_InspectorInclude = expression.Include.generic_string();
                m_InspectorFunction = expression.Function;
                m_InspectorDescription = expression.ParameterMetadata.Description;
                m_InspectorCategory = expression.ParameterMetadata.Category;
                m_InspectorSortPriority = expression.ParameterMetadata.SortPriority;
                m_InspectorHasMinimum = expression.ParameterMetadata.Minimum.has_value();
                m_InspectorHasMaximum = expression.ParameterMetadata.Maximum.has_value();
                m_InspectorHasStep = expression.ParameterMetadata.Step.has_value();
                m_InspectorMinimum = expression.ParameterMetadata.Minimum.value_or(0.0F);
                m_InspectorMaximum = expression.ParameterMetadata.Maximum.value_or(1.0F);
                m_InspectorStep = expression.ParameterMetadata.Step.value_or(0.01F);
            }
            ui.Text(materialOutput ? "Material Output" : expression.Name);
            const auto* descriptor = Keire::FindShaderGraphNodeDescriptor(
                expression.TypeId.empty() ? Keire::ShaderGraphNodeTypeId(expression.Kind)
                                          : std::string_view(expression.TypeId));
            if (descriptor)
                ui.TextColored(theme.MutedText,
                               std::string(descriptor->Category) + " / " + std::string(descriptor->DisplayName));
            if (expression.Kind == Keire::ShaderGraphNodeKind::Parameter)
                ui.TextColored(theme.Accent, "Instance parameter: " + expression.Symbol);

            if (!materialOutput)
            {
                (void)ui.InputText("Display Name", m_InspectorName);
                if (expression.Kind == Keire::ShaderGraphNodeKind::Parameter ||
                    expression.Kind == Keire::ShaderGraphNodeKind::Keyword)
                    (void)ui.InputText("Parameter Symbol", m_InspectorSymbol);
                if (expression.Kind == Keire::ShaderGraphNodeKind::Parameter)
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
                if (expression.Kind == Keire::ShaderGraphNodeKind::Custom)
                {
                    (void)ui.InputText("Safe Include", m_InspectorInclude);
                    (void)ui.InputText("Function", m_InspectorFunction);
                }
                if (ui.Button("Apply Node Properties"))
                    try
                    {
                        const auto nodeId = expression.Id;
                        const auto oldSymbol = expression.Symbol;
                        const auto kind = expression.Kind;
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
                            "Edit Material Graph node properties",
                            [nodeId, oldSymbol, kind, name = m_InspectorName, symbol = m_InspectorSymbol,
                             include = m_InspectorInclude, function = m_InspectorFunction,
                             metadata = std::move(metadata)](auto& definition)
                            {
                                const auto candidate = std::ranges::find(definition.SurfaceGraph.Nodes, nodeId,
                                                                         &Keire::ShaderGraphNode::Id);
                                if (candidate == definition.SurfaceGraph.Nodes.end())
                                    throw std::invalid_argument("Material expression node is unavailable.");
                                candidate->Name = name;
                                candidate->Symbol = symbol;
                                candidate->Include = include;
                                candidate->Function = function;
                                candidate->ParameterMetadata = metadata;
                                if (kind == Keire::ShaderGraphNodeKind::Keyword)
                                {
                                    const auto keyword = std::ranges::find(definition.SurfaceGraph.Keywords, oldSymbol,
                                                                           &Keire::ShaderGraphKeyword::Name);
                                    if (keyword == definition.SurfaceGraph.Keywords.end())
                                        throw std::invalid_argument("Static parameter declaration is unavailable.");
                                    keyword->Name = symbol;
                                }
                            });
                    }
                    catch (const std::exception& error)
                    {
                        Report(error.what());
                    }
            }

            if (materialOutput)
            {
                auto surface = document.Definition().Surface;
                constexpr std::array modes{std::string_view("Opaque"),       std::string_view("Masked"),
                                           std::string_view("Transparent"),  std::string_view("Additive"),
                                           std::string_view("Modulate"),     std::string_view("Alpha Composite"),
                                           std::string_view("Alpha Holdout")};
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
                ui.TextColored(theme.MutedText,
                               "Connected outputs replace matching branches in the selected Shader Graph template.");
            }
            else if (expression.Kind == Keire::ShaderGraphNodeKind::Parameter ||
                     expression.Kind == Keire::ShaderGraphNodeKind::Constant)
            {
                auto value = expression.Value;
                if (DrawExpressionValueEditor(ui, "Value", value))
                    try
                    {
                        (void)document.EditExpressionNode(expression.Id,
                                                          [value = std::move(value)](auto& candidate) mutable
                                                          { candidate.Value = std::move(value); });
                    }
                    catch (const std::exception& error)
                    {
                        Report(error.what());
                    }
            }
            else if (expression.Kind == Keire::ShaderGraphNodeKind::Keyword)
            {
                const auto keyword = std::ranges::find(document.Definition().SurfaceGraph.Keywords, expression.Symbol,
                                                       &Keire::ShaderGraphKeyword::Name);
                if (keyword != document.Definition().SurfaceGraph.Keywords.end())
                {
                    bool enabled = keyword->DefaultOption == "true";
                    if (ui.Checkbox("Default Enabled", enabled))
                        try
                        {
                            (void)document.Edit(
                                "Change static parameter default",
                                [symbol = expression.Symbol, enabled](auto& definition)
                                {
                                    const auto edited = std::ranges::find(definition.SurfaceGraph.Keywords, symbol,
                                                                          &Keire::ShaderGraphKeyword::Name);
                                    if (edited == definition.SurfaceGraph.Keywords.end())
                                        throw std::invalid_argument("Static parameter declaration is unavailable.");
                                    edited->DefaultOption = enabled ? "true" : "false";
                                });
                        }
                        catch (const std::exception& error)
                        {
                            Report(error.what());
                        }
                }
            }

            for (const auto& pin : expression.Pins)
            {
                if (pin.Direction != Keire::ShaderGraphPinDirection::Input)
                    continue;
                const bool connected = std::ranges::any_of(
                    document.Definition().SurfaceGraph.Connections, [&](const Keire::ShaderGraphConnection& connection)
                    { return connection.Input == Keire::ShaderGraphEndpoint{expression.Id, pin.Id}; });
                if (connected)
                {
                    ui.TextColored(theme.MutedText, pin.Name + "  |  connected");
                    continue;
                }
                auto value = pin.DefaultValue;
                if (DrawExpressionValueEditor(ui, pin.Name + "##" + pin.Id.ToString(), value))
                    try
                    {
                        (void)document.EditExpressionNode(
                            expression.Id,
                            [pinId = pin.Id, value = std::move(value)](auto& candidate) mutable
                            {
                                const auto edited =
                                    std::ranges::find(candidate.Pins, pinId, &Keire::ShaderGraphPin::Id);
                                if (edited == candidate.Pins.end())
                                    throw std::invalid_argument("Material expression input is unavailable.");
                                edited->DefaultValue = std::move(value);
                            });
                    }
                    catch (const std::exception& error)
                    {
                        Report(error.what());
                    }
            }
            if (!materialOutput && ui.Button("Duplicate Expression"))
                try
                {
                    auto duplicate = expression;
                    duplicate.Id = Keire::AssetId::Generate();
                    duplicate.Name += " Copy";
                    duplicate.EditorPosition.X += 32.0F;
                    duplicate.EditorPosition.Y += 32.0F;
                    for (auto& pin : duplicate.Pins)
                        pin.Id = Keire::AssetId::Generate();
                    if (duplicate.Kind == Keire::ShaderGraphNodeKind::Parameter ||
                        duplicate.Kind == Keire::ShaderGraphNodeKind::Keyword)
                        duplicate.Symbol = UniqueExpressionSymbol(document.Definition(), duplicate.Symbol);
                    const auto duplicateId = duplicate.Id;
                    if (document.AddExpressionNode(std::move(duplicate)))
                    {
                        m_SelectedNode = duplicateId;
                        m_InspectorNode.reset();
                    }
                }
                catch (const std::exception& error)
                {
                    Report(error.what());
                }
            if (!materialOutput)
                ui.SameLine();
            if (!materialOutput && ui.Button("Delete Expression"))
                try
                {
                    (void)document.RemoveNode(expression.Id);
                    m_SelectedNode.reset();
                    m_InspectorNode.reset();
                }
                catch (const std::exception& error)
                {
                    Report(error.what());
                }
            return;
        }
        if (*m_SelectedNode == document.Definition().OutputNode)
        {
            ui.Text("Template Parameters");
            ui.Separator();
            ui.TextColored(theme.MutedText,
                           "Compatibility uniform overrides. New materials should use expression Parameters and "
                           "Material Output.");
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

    bool MaterialGraphPanel::AddExpressionNode(const Keire::ShaderGraphNodeKind kind,
                                               const Keire::ShaderGraphValueType type,
                                               const std::optional<Keire::Vector2> position)
    {
        try
        {
            auto node = Keire::CreateShaderGraphNode(kind, type);
            node.EditorPosition = position.value_or(Keire::Vector2{120.0F, 120.0F});
            if (kind == Keire::ShaderGraphNodeKind::Parameter || kind == Keire::ShaderGraphNodeKind::Keyword)
            {
                const auto base = kind == Keire::ShaderGraphNodeKind::Keyword ? "STATIC_FEATURE" : "Parameter";
                node.Symbol = UniqueExpressionSymbol(m_Controller.MaterialGraphState().Definition(), base);
                node.Name = node.Symbol;
                if (kind == Keire::ShaderGraphNodeKind::Parameter)
                    node.ParameterMetadata.Category = "Material";
            }
            const auto id = node.Id;
            if (!m_Controller.MaterialGraphState().AddExpressionNode(std::move(node)))
                return false;
            m_SelectedNode = id;
            return true;
        }
        catch (const std::exception& error)
        {
            Report(error.what());
            return false;
        }
    }

    bool MaterialGraphPanel::AddFunctionNode(const Keire::AssetId asset, const std::string_view name,
                                             const std::optional<Keire::Vector2> position)
    {
        try
        {
            const auto function = m_Controller.ResolveMaterialGraphFunction(asset);
            if (!function)
                throw std::runtime_error("The reusable material graph source is unavailable.");
            auto node = Keire::CreateShaderGraphFunctionCallNode(asset, *function);
            node.Name = std::string(name);
            node.EditorPosition = position.value_or(Keire::Vector2{120.0F, 120.0F});
            const auto id = node.Id;
            if (!m_Controller.MaterialGraphState().AddExpressionNode(std::move(node)))
                return false;
            m_SelectedNode = id;
            return true;
        }
        catch (const std::exception& error)
        {
            Report(error.what());
            return false;
        }
    }

    void MaterialGraphPanel::Report(std::string message) noexcept
    {
        m_Message = std::move(message);
        m_Controller.ReportMaterialGraphError(m_Message);
    }
} // namespace KeireEditor
