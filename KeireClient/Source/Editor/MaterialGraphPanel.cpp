#include "KeireClient/Editor/MaterialGraphPanel.h"

#include "KeireClient/Editor/MaterialGraphPreview.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
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
            Keire::MaterialGraphNodeKind Kind;
            std::string_view Category;
            std::string_view Name;
            Keire::MaterialGraphValueType Type = Keire::MaterialGraphValueType::Scalar;
        };

        [[nodiscard]] const std::vector<NodeEntry>& NodeEntries()
        {
            static const auto entries = []
            {
                std::vector<NodeEntry> result{
                    {Keire::MaterialGraphNodeKind::Parameter, "Parameters", "Scalar Parameter"},
                    {Keire::MaterialGraphNodeKind::Parameter, "Parameters", "Vector2 Parameter",
                     Keire::MaterialGraphValueType::Vector2},
                    {Keire::MaterialGraphNodeKind::Parameter, "Parameters", "Vector3 Parameter",
                     Keire::MaterialGraphValueType::Vector3},
                    {Keire::MaterialGraphNodeKind::Parameter, "Parameters", "Vector4 Parameter",
                     Keire::MaterialGraphValueType::Vector4},
                    {Keire::MaterialGraphNodeKind::Parameter, "Parameters", "Color Parameter",
                     Keire::MaterialGraphValueType::Color},
                    {Keire::MaterialGraphNodeKind::Parameter, "Parameters", "Texture2D Parameter",
                     Keire::MaterialGraphValueType::Texture2D},
                    {Keire::MaterialGraphNodeKind::Constant, "Constants", "Scalar Constant"},
                    {Keire::MaterialGraphNodeKind::Constant, "Constants", "Vector2 Constant",
                     Keire::MaterialGraphValueType::Vector2},
                    {Keire::MaterialGraphNodeKind::Constant, "Constants", "Vector3 Constant",
                     Keire::MaterialGraphValueType::Vector3},
                    {Keire::MaterialGraphNodeKind::Constant, "Constants", "Vector4 Constant",
                     Keire::MaterialGraphValueType::Vector4},
                    {Keire::MaterialGraphNodeKind::Constant, "Constants", "Color Constant",
                     Keire::MaterialGraphValueType::Color},
                };
                for (const auto& descriptor : Keire::MaterialGraphNodeCatalog())
                {
                    if (!descriptor.UserCreatable || descriptor.Kind == Keire::MaterialGraphNodeKind::Parameter ||
                        descriptor.Kind == Keire::MaterialGraphNodeKind::Constant)
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
        constexpr std::array OutputNames{std::string_view("Surface PBR"), std::string_view("Transparent PBR"),
                                         std::string_view("Decal PBR"),   std::string_view("Unlit"),
                                         std::string_view("Hair PBR"),    std::string_view("Eye PBR")};
        constexpr std::array TextureSemanticNames{
            std::string_view("Generic"),   std::string_view("Base Color"),
            std::string_view("Normal"),    std::string_view("Metallic / Roughness"),
            std::string_view("Occlusion"), std::string_view("Emissive"),
            std::string_view("Metallic"),  std::string_view("Roughness"),
        };

        [[nodiscard]] std::string Lower(const std::string_view value)
        {
            std::string result(value);
            std::ranges::transform(result, result.begin(), [](const unsigned char character)
                                   { return static_cast<char>(std::tolower(character)); });
            return result;
        }

        [[nodiscard]] std::string UniqueSymbol(const Keire::MaterialGraphDefinition& definition,
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

        void ChangeOutput(Keire::MaterialGraphDefinition& definition, const Keire::MaterialGraphOutput output)
        {
            if (definition.Output == output)
                return;
            auto master = std::ranges::find(definition.Nodes, Keire::MaterialGraphNodeKind::Master,
                                            &Keire::MaterialGraphNode::Kind);
            if (master == definition.Nodes.end())
                throw std::invalid_argument("Material Graph Master node is unavailable.");
            auto replacement = Keire::CreateDefaultMaterialGraph(output).Nodes.front();
            replacement.Id = master->Id;
            replacement.EditorPosition = master->EditorPosition;
            for (auto& newPin : replacement.Pins)
            {
                auto oldName = std::string_view(newPin.Name);
                if (newPin.Name == "Color")
                    oldName = "BaseColor";
                else if (newPin.Name == "BaseColor")
                    oldName = "Color";
                auto oldPin = std::ranges::find(master->Pins, oldName, &Keire::MaterialGraphPin::Name);
                if (oldPin == master->Pins.end())
                    oldPin = std::ranges::find(master->Pins, newPin.Name, &Keire::MaterialGraphPin::Name);
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
                          [&](const Keire::MaterialGraphConnection& connection)
                          {
                              return connection.Input.Node == masterId &&
                                     std::ranges::find(retained, connection.Input.Pin, &Keire::MaterialGraphPin::Id) ==
                                         retained.end();
                          });
            definition.Output = output;
        }

        [[nodiscard]] const Keire::MaterialGraphNode* FindNode(const Keire::MaterialGraphDefinition& definition,
                                                               const Keire::AssetId id)
        {
            const auto found = std::ranges::find(definition.Nodes, id, &Keire::MaterialGraphNode::Id);
            return found == definition.Nodes.end() ? nullptr : std::addressof(*found);
        }

    } // namespace

    MaterialGraphPanel::~MaterialGraphPanel() noexcept
    {
        m_PreviewCancellation->fetch_add(1, std::memory_order_release);
        if (m_PreviewRender.valid())
            m_PreviewRender.wait();
    }

    void MaterialGraphPanel::Attach(Keire::UiWorkspace& workspace)
    {
        m_Registration = workspace.RegisterPanel({"editor.material-graph", "Material Graph", false});
    }

    void MaterialGraphPanel::Draw(Keire::UiFrame& ui)
    {
        if (auto panel = ui.BeginPanel(m_Registration); panel)
        {
            auto& document = m_Controller.MaterialGraphState();
            if (!document.IsOpen())
            {
                ui.TextColored(m_Controller.MaterialGraphTheme().MutedText,
                               "Open a Material Graph asset to author its shader.");
                return;
            }
            DrawHeader(ui);
            ui.Separator();
            DrawPreview(ui);
            ui.Separator();
            DrawCanvas(ui);
            ui.Separator();
            DrawInspector(ui);
            ui.Separator();
            DrawDiagnostics(ui);
        }
    }

    void MaterialGraphPanel::DrawHeader(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.MaterialGraphState();
        const auto& theme = m_Controller.MaterialGraphTheme();
        const bool compiling = document.CompilationPending();
        ui.TextColored(!compiling && document.Publishable() ? theme.Success : theme.Warning,
                       compiling                ? "LIVE COMPILING + UPDATING SCENE"
                       : document.Publishable() ? "GENERATED SHADER READY"
                                                : "PREVIEW USING LAST GOOD SHADER");
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!document.Dirty() || !document.Publishable() || compiling); disabled)
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
        ui.SameLine();
        const auto undo = document.UndoContext();
        if (auto disabled = ui.BeginDisabled(!undo || !undo->CanUndo()); disabled)
            if (ui.Button("Undo"))
                m_Controller.UndoMaterialGraphEdit();
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!undo || !undo->CanRedo()); disabled)
            if (ui.Button("Redo"))
                m_Controller.RedoMaterialGraphEdit();

        auto preview = document.PreviewSettings();
        auto previewIndex = static_cast<std::size_t>(preview.Mesh);
        ui.SameLine();
        if (auto combo = ui.BeginCombo("Preview Mesh", PreviewNames[previewIndex]); combo)
        {
            for (std::size_t index = 0; index < PreviewNames.size(); ++index)
            {
                if (ui.Selectable(PreviewNames[index], index == previewIndex))
                {
                    preview.Mesh = static_cast<Keire::MaterialGraphPreviewMesh>(index);
                    if (preview.Mesh == Keire::MaterialGraphPreviewMesh::Custom && !preview.CustomMesh)
                        preview.CustomMesh = Keire::MeshAsset::CubeId();
                    document.SetPreviewSettings(preview);
                }
            }
        }
        if (preview.Mesh == Keire::MaterialGraphPreviewMesh::Custom)
        {
            auto mesh = preview.CustomMesh;
            const AssetPickerOptions options{
                .Label = "Custom Preview Mesh",
                .ExpectedType = Keire::MeshAsset::StaticType(),
                .Reveal = [this](const Keire::AssetId selected) { m_Controller.RevealMaterialGraphAsset(selected); },
                .AllowNone = false,
            };
            if (m_AssetPicker.Draw(ui, m_Controller.MaterialGraphAssetRecords(), mesh, options))
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
        if (auto combo = ui.BeginCombo("Material Output", OutputNames[outputIndex]); combo)
            for (std::size_t index = 0; index < OutputNames.size(); ++index)
                if (ui.Selectable(OutputNames[index], index == outputIndex))
                    try
                    {
                        const auto selected = static_cast<Keire::MaterialGraphOutput>(index);
                        (void)document.Edit("Change Material Graph output",
                                            [selected](auto& definition) { ChangeOutput(definition, selected); });
                    }
                    catch (const std::exception& error)
                    {
                        Report(error.what());
                    }
    }

    void MaterialGraphPanel::DrawPreview(Keire::UiFrame& ui)
    {
        const auto& theme = m_Controller.MaterialGraphTheme();
        if (m_PreviewRender.valid() && m_PreviewRender.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            auto result = m_PreviewRender.get();
            if (result.Generation == m_PreviewGeneration)
            {
                if (result.Error.empty())
                {
                    m_PreviewImage = ui.CreateImage(result.Width, result.Height, result.Pixels);
                    if (!result.FinalQuality)
                    {
                        m_PreviewRefinement = true;
                        m_PreviewDirty = true;
                    }
                }
                else
                    Report(std::move(result.Error));
            }
        }
        ui.TextColored(theme.Accent, "LIVE MATERIAL PREVIEW");
        auto preview = m_Controller.MaterialGraphState().PreviewSettings();
        bool previewChanged = false;
        previewChanged |= ui.SliderFloat("Exposure", preview.Exposure, 0.1F, 4.0F);
        previewChanged |= ui.SliderFloat("Environment", preview.EnvironmentIntensity, 0.0F, 4.0F);
        previewChanged |= ui.SliderFloat("Rotation", preview.RotationDegrees, -180.0F, 180.0F);
        if (previewChanged)
            m_Controller.MaterialGraphState().SetPreviewSettings(preview);
        if (m_PreviewProperties.empty() && !m_Controller.MaterialGraphState().LastGoodCompilation())
        {
            ui.TextColored(theme.MutedText, "A preview appears after the graph compiles successfully.");
            return;
        }
        const auto availableWidth = std::clamp(ui.ContentAvailable().Width, 280.0F, 640.0F);
        const auto previewWidth = static_cast<std::uint32_t>(std::floor(availableWidth));
        const auto previewHeight = static_cast<std::uint32_t>(std::floor(availableWidth * 0.625F));
        if (previewWidth != m_PreviewWidth || previewHeight != m_PreviewHeight)
        {
            m_PreviewWidth = previewWidth;
            m_PreviewHeight = previewHeight;
            m_PreviewRefinement = false;
            m_PreviewDirty = true;
            if (++m_PreviewGeneration == 0)
                ++m_PreviewGeneration;
            m_PreviewCancellation->store(m_PreviewGeneration, std::memory_order_release);
        }
        if (m_PreviewDirty && !m_PreviewRender.valid())
        {
            try
            {
                Keire::Ref<const Keire::MeshAsset> customMesh;
                if (m_PreviewSettings.Mesh == Keire::MaterialGraphPreviewMesh::Custom)
                {
                    customMesh = m_Controller.ResolveMaterialGraphPreviewMesh(m_PreviewSettings.CustomMesh);
                    if (!customMesh)
                    {
                        ui.TextColored(theme.MutedText, "Loading the custom preview mesh...");
                        return;
                    }
                }
                const auto& lastGoodDefinition = m_Controller.MaterialGraphState().LastGoodDefinition();
                if (!lastGoodDefinition)
                    return;
                const auto generation = m_PreviewGeneration;
                const bool finalQuality = m_PreviewRefinement;
                const auto width = finalQuality ? m_PreviewWidth : std::max(96U, m_PreviewWidth / 2U);
                const auto height = finalQuality ? m_PreviewHeight : std::max(64U, m_PreviewHeight / 2U);
                auto definition = *lastGoodDefinition;
                auto properties = m_PreviewProperties;
                auto settings = m_PreviewSettings;
                auto cancellation = m_PreviewCancellation;
                m_PreviewRender = std::async(
                    std::launch::async,
                    [generation, width, height, definition = std::move(definition), properties = std::move(properties),
                     settings = std::move(settings), customMesh = std::move(customMesh),
                     cancellation = std::move(cancellation), finalQuality]() mutable
                    {
                        PreviewRenderResult result{
                            .Generation = generation, .Width = width, .Height = height, .FinalQuality = finalQuality};
                        try
                        {
                            result.Pixels = RenderMaterialGraphPreview({
                                .Output = definition.Output,
                                .Mesh = settings.Mesh,
                                .CustomMesh = std::move(customMesh),
                                .Definition = &definition,
                                .Properties = properties,
                                .Width = width,
                                .Height = height,
                                .Exposure = settings.Exposure,
                                .EnvironmentIntensity = settings.EnvironmentIntensity,
                                .RotationDegrees = settings.RotationDegrees,
                                .CancellationRequested = [cancellation, generation]
                                { return cancellation->load(std::memory_order_acquire) != generation; },
                            });
                        }
                        catch (const std::exception& error)
                        {
                            result.Error = error.what();
                        }
                        return result;
                    });
                m_PreviewRefinement = false;
                m_PreviewDirty = false;
            }
            catch (const std::exception& error)
            {
                Report(error.what());
                m_PreviewDirty = false;
            }
        }
        if (m_PreviewDirty || m_PreviewRender.valid())
            ui.TextColored(theme.MutedText, "Updating preview...");
        if (m_PreviewImage)
            ui.Image(m_PreviewImage, {static_cast<float>(m_PreviewWidth), static_cast<float>(m_PreviewHeight)});
        else
            ui.TextColored(theme.Warning, "The live preview is unavailable.");
    }

    void MaterialGraphPanel::UpdatePreview(const Keire::MaterialGraphCompilation& compilation,
                                           const MaterialGraphPreviewSettings& settings)
    {
        m_PreviewProperties = compilation.Properties;
        m_PreviewSettings = settings;
        m_PreviewRefinement = false;
        m_PreviewDirty = true;
        if (++m_PreviewGeneration == 0)
            ++m_PreviewGeneration;
        m_PreviewCancellation->store(m_PreviewGeneration, std::memory_order_release);
    }

    void MaterialGraphPanel::ClearPreview() noexcept
    {
        m_PreviewImage.Reset();
        m_PreviewProperties.clear();
        m_PreviewSettings = {};
        m_PreviewRefinement = false;
        m_PreviewDirty = false;
        if (++m_PreviewGeneration == 0)
            ++m_PreviewGeneration;
        m_PreviewCancellation->store(m_PreviewGeneration, std::memory_order_release);
        m_AssetPicker.Clear();
        m_NodeAssetPicker.Clear();
    }

    void MaterialGraphPanel::DrawInspector(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.MaterialGraphState();
        const auto* node = m_SelectedNode ? FindNode(document.Definition(), *m_SelectedNode) : nullptr;
        if (!node)
        {
            m_InspectorNode.reset();
            ui.TextColored(m_Controller.MaterialGraphTheme().MutedText,
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
        }

        ui.TextColored(m_Controller.MaterialGraphTheme().Accent, "NODE INSPECTOR");
        ui.TextColored(m_Controller.MaterialGraphTheme().MutedText, "Stable ID: " + node->Id.ToString());
        (void)ui.InputText("Display Name", m_InspectorName);
        if (node->Kind == Keire::MaterialGraphNodeKind::Parameter ||
            node->Kind == Keire::MaterialGraphNodeKind::Keyword)
            (void)ui.InputText("Shader Symbol", m_InspectorSymbol);
        if (node->Kind == Keire::MaterialGraphNodeKind::Parameter)
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
        if (node->Kind == Keire::MaterialGraphNodeKind::Custom)
        {
            (void)ui.InputText("Safe Include", m_InspectorInclude);
            (void)ui.InputText("Function", m_InspectorFunction);
        }
        if (ui.Button("Apply Node Properties"))
        {
            try
            {
                const auto nodeId = node->Id;
                const auto oldSymbol = node->Symbol;
                const auto kind = node->Kind;
                Keire::MaterialGraphParameterMetadata metadata;
                metadata.Description = m_InspectorDescription;
                metadata.Category = m_InspectorCategory;
                metadata.SortPriority = static_cast<std::int32_t>(std::round(m_InspectorSortPriority));
                if (m_InspectorHasMinimum)
                    metadata.Minimum = static_cast<float>(m_InspectorMinimum);
                if (m_InspectorHasMaximum)
                    metadata.Maximum = static_cast<float>(m_InspectorMaximum);
                if (m_InspectorHasStep)
                    metadata.Step = static_cast<float>(m_InspectorStep);
                (void)document.Edit("Edit Material Graph node properties",
                                    [nodeId, oldSymbol, kind, name = m_InspectorName, symbol = m_InspectorSymbol,
                                     include = m_InspectorInclude, function = m_InspectorFunction,
                                     metadata = std::move(metadata)](auto& definition)
                                    {
                                        auto candidate =
                                            std::ranges::find(definition.Nodes, nodeId, &Keire::MaterialGraphNode::Id);
                                        if (candidate == definition.Nodes.end())
                                            throw std::invalid_argument("Material Graph node is unavailable.");
                                        candidate->Name = name;
                                        candidate->Symbol = symbol;
                                        candidate->Include = include;
                                        candidate->Function = function;
                                        candidate->ParameterMetadata = metadata;
                                        if (kind == Keire::MaterialGraphNodeKind::Keyword)
                                        {
                                            auto keyword = std::ranges::find(definition.Keywords, oldSymbol,
                                                                             &Keire::MaterialGraphKeyword::Name);
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

        if (auto disabled = ui.BeginDisabled(node->Kind == Keire::MaterialGraphNodeKind::Master ||
                                             node->Kind == Keire::MaterialGraphNodeKind::Keyword);
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
                    if (duplicate.Kind == Keire::MaterialGraphNodeKind::Parameter)
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
        if (auto disabled = ui.BeginDisabled(node->Kind == Keire::MaterialGraphNodeKind::Master); disabled)
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

        const auto applyValue = [&](Keire::MaterialGraphValue value)
        {
            try
            {
                (void)document.EditNode(node->Id, [value = std::move(value)](auto& candidate) mutable
                                        { candidate.Value = std::move(value); });
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
                .Reveal = [this](const Keire::AssetId selected) { m_Controller.RevealMaterialGraphAsset(selected); },
                .AllowNone = true,
            };
            if (m_NodeAssetPicker.Draw(ui, m_Controller.MaterialGraphAssetRecords(), value, options))
                applyValue(value);
            if (!m_NodeAssetPicker.Diagnostic().empty())
                ui.TextColored(m_Controller.MaterialGraphTheme().Warning, m_NodeAssetPicker.Diagnostic());

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
            const auto applyPinValue = [&](const Keire::AssetId pinId, Keire::MaterialGraphValue value)
            {
                try
                {
                    (void)document.EditNode(
                        node->Id,
                        [pinId, value = std::move(value)](auto& candidate) mutable
                        {
                            const auto pin = std::ranges::find(candidate.Pins, pinId, &Keire::MaterialGraphPin::Id);
                            if (pin == candidate.Pins.end())
                                throw std::invalid_argument("Material Graph input pin is unavailable.");
                            pin->DefaultValue = std::move(value);
                        });
                }
                catch (const std::exception& error)
                {
                    Report(error.what());
                }
            };
            for (const auto& pin : node->Pins)
            {
                if (pin.Direction != Keire::MaterialGraphPinDirection::Input)
                    continue;
                const auto connected = std::ranges::any_of(
                    document.Definition().Connections, [&](const auto& connection)
                    { return connection.Input.Node == node->Id && connection.Input.Pin == pin.Id; });
                if (connected)
                {
                    ui.TextColored(m_Controller.MaterialGraphTheme().MutedText, pin.Name + "  |  connected");
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
                        { m_Controller.RevealMaterialGraphAsset(selected); },
                        .AllowNone = true,
                    };
                    if (m_NodeAssetPicker.Draw(ui, m_Controller.MaterialGraphAssetRecords(), value, options))
                        applyPinValue(pin.Id, value);
                }
            }
        }
    }

    void MaterialGraphPanel::DrawCanvas(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.MaterialGraphState();
        auto model = document.BuildCanvasModel();
        (void)ui.InputTextWithHint("##MaterialNodeSearch", "Search nodes and categories...", m_NodeSearch);
        if (auto combo = ui.BeginCombo("Add Node", "Choose..."); combo)
        {
            const auto search = Lower(m_NodeSearch);
            for (const auto& entry : NodeEntries())
            {
                const auto path = std::string(entry.Category) + " / " + std::string(entry.Name);
                if (!search.empty() && Lower(path).find(search) == std::string::npos)
                    continue;
                if (ui.Selectable(path))
                {
                    if (AddNode(entry.Kind, entry.Type))
                        return;
                }
            }
        }
        if (!m_NodeSearch.empty())
        {
            ui.SameLine();
            if (ui.Button("Clear Search"))
                m_NodeSearch.clear();
        }
        ui.SameLine();
        if (ui.Button("Frame All"))
            m_Canvas.Focus(model.Nodes, ui.ContentAvailable());
        ui.SameLine();
        ui.TextColored(m_Controller.MaterialGraphTheme().MutedText,
                       "Drag pins to connect  |  middle-drag to pan  |  wheel to zoom  |  Delete removes selection");

        const auto findCanvasNode = [&](const Keire::AssetId id) -> std::optional<StableNodeId>
        {
            const auto found = std::ranges::find_if(model.NodeIdentities,
                                                    [id](const auto& identity) { return identity.second == id; });
            return found == model.NodeIdentities.end() ? std::nullopt : std::optional<StableNodeId>(found->first);
        };
        const auto findCanvasConnection = [&](const Keire::AssetId id) -> std::optional<StableNodeId>
        {
            const auto found = std::ranges::find_if(model.ConnectionIdentities,
                                                    [id](const auto& identity) { return identity.second == id; });
            return found == model.ConnectionIdentities.end() ? std::nullopt : std::optional<StableNodeId>(found->first);
        };
        m_Canvas.Select(m_SelectedNode ? findCanvasNode(*m_SelectedNode) : std::nullopt);
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
                                                         "A Material Graph connection endpoint is unavailable."};
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
        if (canvas.DeleteNodeRequested)
        {
            const auto node = model.Node(*canvas.DeleteNodeRequested);
            if (node)
                try
                {
                    (void)document.RemoveNode(*node);
                    m_SelectedNode.reset();
                }
                catch (const std::exception& error)
                {
                    Report(error.what());
                }
        }
    }

    void MaterialGraphPanel::DrawDiagnostics(Keire::UiFrame& ui)
    {
        const auto& document = m_Controller.MaterialGraphState();
        const auto& theme = m_Controller.MaterialGraphTheme();
        if (!m_Message.empty())
            ui.TextColored(theme.Warning, m_Message);
        if (document.Compilation().Diagnostics.empty())
        {
            ui.TextColored(theme.Success, "Generated shader diagnostics: clear.");
            return;
        }
        ui.TextColored(theme.Warning, "Generated shader diagnostics (" +
                                          std::to_string(document.Compilation().Diagnostics.size()) + ")");
        for (const auto& diagnostic : document.Compilation().Diagnostics)
        {
            const auto color = diagnostic.Severity == Keire::MaterialGraphDiagnosticSeverity::Error ? theme.Error
                               : diagnostic.Severity == Keire::MaterialGraphDiagnosticSeverity::Warning
                                   ? theme.Warning
                                   : theme.MutedText;
            std::string text = diagnostic.Code + "  " + diagnostic.Message;
            if (diagnostic.Node)
                text += "  [" + diagnostic.Node.ToString() + "]";
            if (diagnostic.GeneratedLine != 0)
                text += "  line " + std::to_string(diagnostic.GeneratedLine);
            ui.TextColored(color, text);
        }
    }

    bool MaterialGraphPanel::AddNode(const Keire::MaterialGraphNodeKind kind, const Keire::MaterialGraphValueType type)
    {
        try
        {
            auto node = Keire::CreateMaterialGraphNode(kind, type);
            if (kind == Keire::MaterialGraphNodeKind::Parameter)
            {
                const auto base = type == Keire::MaterialGraphValueType::Vector2     ? "Vector2"
                                  : type == Keire::MaterialGraphValueType::Vector3   ? "Vector3"
                                  : type == Keire::MaterialGraphValueType::Vector4   ? "Vector4"
                                  : type == Keire::MaterialGraphValueType::Color     ? "BaseColor"
                                  : type == Keire::MaterialGraphValueType::Texture2D ? "Texture"
                                                                                     : "Scalar";
                node.Symbol = UniqueSymbol(m_Controller.MaterialGraphState().Definition(), base);
                node.Name = node.Symbol;
            }
            else if (kind == Keire::MaterialGraphNodeKind::Keyword)
            {
                node.Symbol = UniqueSymbol(m_Controller.MaterialGraphState().Definition(), "FEATURE");
                node.Name = node.Symbol;
            }
            else if (kind == Keire::MaterialGraphNodeKind::Custom)
                node.Include = "Assets/Shaders/MaterialNodes.hlsl";
            const auto size = m_Canvas.Zoom();
            node.EditorPosition = {-m_Canvas.Pan().X + 280.0F / size, -m_Canvas.Pan().Y + 180.0F / size};
            const auto id = node.Id;
            const bool changed =
                kind == Keire::MaterialGraphNodeKind::Keyword
                    ? m_Controller.MaterialGraphState().Edit("Add Material Graph keyword",
                                                             [node = std::move(node)](auto& definition) mutable
                                                             {
                                                                 definition.Keywords.push_back(
                                                                     {.Name = node.Symbol, .DefaultOption = "false"});
                                                                 definition.Nodes.push_back(std::move(node));
                                                             })
                    : m_Controller.MaterialGraphState().AddNode(std::move(node));
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

    void MaterialGraphPanel::Report(std::string message) noexcept
    {
        m_Message = std::move(message);
        m_Controller.ReportMaterialGraphError(m_Message);
    }
} // namespace KeireEditor
