#include "KeireClientInternal/Editor/VfxEffectPanelInternal.h"

namespace KeireEditor
{
    bool VfxEffectPanel::DrawGraphValueEditor(Keire::UiFrame& ui, const std::string_view label,
                                              const Keire::VfxValueType type, Keire::VfxParameterValue& value)
    {
        if (!Keire::VfxValueMatchesType(type, value))
            throw std::logic_error("VFX graph value editor received a mismatched value type.");

        const auto unsignedInteger = [&ui](const std::string_view field, std::uint64_t& candidate)
        {
            auto text = std::to_string(candidate);
            if (!ui.InputText(field, text))
                return false;
            std::uint64_t parsed = 0;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
            if (error != std::errc{} || end != text.data() + text.size())
                return false;
            candidate = parsed;
            return true;
        };
        const auto editColor = [&ui](const std::string_view field, Keire::Color& candidate)
        {
            Keire::UiColor color{candidate.Red, candidate.Green, candidate.Blue, candidate.Alpha};
            if (!ui.ColorEdit(field, color))
                return false;
            candidate = {color.Red, color.Green, color.Blue, color.Alpha};
            return true;
        };

        switch (type)
        {
        case Keire::VfxValueType::Boolean:
            return ui.Checkbox(label, std::get<bool>(value));
        case Keire::VfxValueType::Integer:
            return ui.DragInteger(label, std::get<std::int64_t>(value));
        case Keire::VfxValueType::UnsignedInteger:
            return unsignedInteger(label, std::get<std::uint64_t>(value));
        case Keire::VfxValueType::Scalar:
        {
            double scalar = std::get<float>(value);
            if (!ui.DragScalar(label, scalar, 0.01))
                return false;
            value = static_cast<float>(scalar);
            return true;
        }
        case Keire::VfxValueType::Vector2:
            return ui.DragVector2(label, std::get<Keire::Vector2>(value), 0.01F);
        case Keire::VfxValueType::Vector3:
            return ui.DragVector3(label, std::get<Keire::Vector3>(value), 0.01F);
        case Keire::VfxValueType::Vector4:
            return ui.DragVector4(label, std::get<Keire::Vector4>(value), 0.01F);
        case Keire::VfxValueType::Quaternion:
        {
            auto& quaternion = std::get<Keire::Quaternion>(value);
            Keire::Vector4 components{quaternion.X, quaternion.Y, quaternion.Z, quaternion.W};
            if (!ui.DragVector4(label, components, 0.01F))
                return false;
            quaternion = {components.X, components.Y, components.Z, components.W};
            return true;
        }
        case Keire::VfxValueType::Color:
            return editColor(label, std::get<Keire::Color>(value));
        case Keire::VfxValueType::Matrix:
        {
            auto& matrix = std::get<Keire::Matrix4>(value);
            bool changed = false;
            for (std::size_t row = 0; row < 4; ++row)
            {
                Keire::Vector4 components{matrix.Elements[row * 4], matrix.Elements[row * 4 + 1],
                                          matrix.Elements[row * 4 + 2], matrix.Elements[row * 4 + 3]};
                if (!ui.DragVector4(std::string(label) + " Row " + std::to_string(row + 1), components, 0.01F))
                    continue;
                matrix.Elements[row * 4] = components.X;
                matrix.Elements[row * 4 + 1] = components.Y;
                matrix.Elements[row * 4 + 2] = components.Z;
                matrix.Elements[row * 4 + 3] = components.W;
                changed = true;
            }
            return changed;
        }
        case Keire::VfxValueType::Curve:
            return AuthoringValueEditors::Curve(ui, label, std::get<Keire::Curve1D>(value));
        case Keire::VfxValueType::Gradient:
            return AuthoringValueEditors::Gradient(ui, label, std::get<Keire::ColorGradient>(value));
        case Keire::VfxValueType::ScalarRange:
        {
            auto& range = std::get<Keire::VfxScalarRange>(value);
            double minimum = range.Minimum;
            double maximum = range.Maximum;
            const bool changed = static_cast<int>(ui.DragScalar(std::string(label) + " Min", minimum, 0.01)) |
                                 static_cast<int>(ui.DragScalar(std::string(label) + " Max", maximum, 0.01));
            if (!changed)
                return false;
            range.Minimum = static_cast<float>(std::min(minimum, maximum));
            range.Maximum = static_cast<float>(std::max(minimum, maximum));
            return true;
        }
        case Keire::VfxValueType::IntegerRange:
        {
            auto& range = std::get<Keire::VfxIntegerRange>(value);
            const bool changed = static_cast<int>(ui.DragInteger(std::string(label) + " Min", range.Minimum)) |
                                 static_cast<int>(ui.DragInteger(std::string(label) + " Max", range.Maximum));
            if (changed && range.Maximum < range.Minimum)
                std::swap(range.Minimum, range.Maximum);
            return changed;
        }
        case Keire::VfxValueType::UnsignedIntegerRange:
        {
            auto& range = std::get<Keire::VfxUnsignedIntegerRange>(value);
            const bool changed = static_cast<int>(unsignedInteger(std::string(label) + " Min", range.Minimum)) |
                                 static_cast<int>(unsignedInteger(std::string(label) + " Max", range.Maximum));
            if (changed && range.Maximum < range.Minimum)
                std::swap(range.Minimum, range.Maximum);
            return changed;
        }
        case Keire::VfxValueType::Vector2Range:
        {
            auto& range = std::get<Keire::VfxVector2Range>(value);
            const bool changed = static_cast<int>(ui.DragVector2(std::string(label) + " Min", range.Minimum, 0.01F)) |
                                 static_cast<int>(ui.DragVector2(std::string(label) + " Max", range.Maximum, 0.01F));
            if (!changed)
                return false;
            const auto minimum = range.Minimum;
            const auto maximum = range.Maximum;
            range.Minimum = {std::min(minimum.X, maximum.X), std::min(minimum.Y, maximum.Y)};
            range.Maximum = {std::max(minimum.X, maximum.X), std::max(minimum.Y, maximum.Y)};
            return true;
        }
        case Keire::VfxValueType::Vector3Range:
        {
            auto& range = std::get<Keire::VfxVector3Range>(value);
            const bool changed = static_cast<int>(ui.DragVector3(std::string(label) + " Min", range.Minimum, 0.01F)) |
                                 static_cast<int>(ui.DragVector3(std::string(label) + " Max", range.Maximum, 0.01F));
            if (!changed)
                return false;
            const auto minimum = range.Minimum;
            const auto maximum = range.Maximum;
            range.Minimum = {std::min(minimum.X, maximum.X), std::min(minimum.Y, maximum.Y),
                             std::min(minimum.Z, maximum.Z)};
            range.Maximum = {std::max(minimum.X, maximum.X), std::max(minimum.Y, maximum.Y),
                             std::max(minimum.Z, maximum.Z)};
            return true;
        }
        case Keire::VfxValueType::Vector4Range:
        {
            auto& range = std::get<Keire::VfxVector4Range>(value);
            const bool changed = static_cast<int>(ui.DragVector4(std::string(label) + " Min", range.Minimum, 0.01F)) |
                                 static_cast<int>(ui.DragVector4(std::string(label) + " Max", range.Maximum, 0.01F));
            if (!changed)
                return false;
            const auto minimum = range.Minimum;
            const auto maximum = range.Maximum;
            range.Minimum = {std::min(minimum.X, maximum.X), std::min(minimum.Y, maximum.Y),
                             std::min(minimum.Z, maximum.Z), std::min(minimum.W, maximum.W)};
            range.Maximum = {std::max(minimum.X, maximum.X), std::max(minimum.Y, maximum.Y),
                             std::max(minimum.Z, maximum.Z), std::max(minimum.W, maximum.W)};
            return true;
        }
        case Keire::VfxValueType::ColorRange:
        {
            auto& range = std::get<Keire::VfxColorRange>(value);
            const bool changed = static_cast<int>(editColor(std::string(label) + " Min", range.Minimum)) |
                                 static_cast<int>(editColor(std::string(label) + " Max", range.Maximum));
            if (!changed)
                return false;
            const auto minimum = range.Minimum;
            const auto maximum = range.Maximum;
            range.Minimum = {std::min(minimum.Red, maximum.Red), std::min(minimum.Green, maximum.Green),
                             std::min(minimum.Blue, maximum.Blue), std::min(minimum.Alpha, maximum.Alpha)};
            range.Maximum = {std::max(minimum.Red, maximum.Red), std::max(minimum.Green, maximum.Green),
                             std::max(minimum.Blue, maximum.Blue), std::max(minimum.Alpha, maximum.Alpha)};
            return true;
        }
        case Keire::VfxValueType::Texture:
        case Keire::VfxValueType::Mesh:
        case Keire::VfxValueType::Asset:
        case Keire::VfxValueType::Texture2DArray:
        case Keire::VfxValueType::Texture3D:
        case Keire::VfxValueType::TextureCube:
        case Keire::VfxValueType::Buffer:
        case Keire::VfxValueType::PointCache:
        case Keire::VfxValueType::SignedDistanceField:
        {
            auto& asset = std::get<Keire::AssetId>(value);
            std::optional<Keire::AssetTypeId> expected;
            if (type == Keire::VfxValueType::Texture)
                expected = Keire::Texture2DAsset::StaticType();
            else if (type == Keire::VfxValueType::Mesh)
                expected = Keire::MeshAsset::StaticType();
            AssetPickerOptions options{
                .Label = label,
                .ExpectedType = expected,
                .Reveal = [this](const Keire::AssetId selected) { m_Controller.RevealVfxEffectAsset(selected); },
            };
            return m_AssetPicker.Draw(ui, m_Controller.VfxEffectAssetRecords(), asset, options);
        }
        case Keire::VfxValueType::ParticleStream:
            return false;
        }
        return false;
    }

    bool VfxEffectPanel::DrawGraphPropertyEditor(Keire::UiFrame& ui, Keire::VfxGraphProperty& property)
    {
        if (property.Name == "Scope")
        {
            auto* scope = std::get_if<std::uint64_t>(&property.Value);
            if (!scope)
                throw std::logic_error("VFX Random Scope setting is malformed.");
            const auto current = *scope == static_cast<std::uint64_t>(Keire::VfxRandomScope::PerParticle)
                                     ? std::string_view("Per Particle")
                                 : *scope == static_cast<std::uint64_t>(Keire::VfxRandomScope::PerVfxComponent)
                                     ? std::string_view("Per VFX Component")
                                     : std::string_view("Per Particle Strip (Unavailable)");
            bool changed = false;
            if (auto combo = ui.BeginCombo(property.Name, current); combo)
            {
                if (ui.MenuItem("Per Particle", *scope == 0))
                {
                    *scope = static_cast<std::uint64_t>(Keire::VfxRandomScope::PerParticle);
                    changed = true;
                }
                if (ui.MenuItem("Per VFX Component",
                                *scope == static_cast<std::uint64_t>(Keire::VfxRandomScope::PerVfxComponent)))
                {
                    *scope = static_cast<std::uint64_t>(Keire::VfxRandomScope::PerVfxComponent);
                    changed = true;
                }
                (void)ui.MenuItem("Per Particle Strip (requires strip simulation)", false, false);
            }
            return changed;
        }
        if (property.Name == "Condition")
        {
            auto* condition = std::get_if<std::string>(&property.Value);
            if (!condition)
                throw std::logic_error("VFX Compare Condition setting is malformed.");
            static constexpr std::array conditions{
                std::string_view("Less"),      std::string_view("Less Or Equal"),    std::string_view("Equal"),
                std::string_view("Not Equal"), std::string_view("Greater Or Equal"), std::string_view("Greater")};
            bool changed = false;
            if (auto combo = ui.BeginCombo(property.Name, *condition); combo)
            {
                for (const auto candidate : conditions)
                {
                    if (ui.MenuItem(candidate, *condition == candidate))
                    {
                        *condition = candidate;
                        changed = true;
                    }
                }
            }
            return changed;
        }

        return std::visit(
            Overloaded{
                [&ui, &property](bool& candidate) { return ui.Checkbox(property.Name, candidate); },
                [&ui, &property](std::int64_t& candidate) { return ui.DragInteger(property.Name, candidate); },
                [&ui, &property](std::uint64_t& candidate)
                {
                    auto text = std::to_string(candidate);
                    if (!ui.InputText(property.Name, text))
                        return false;
                    std::uint64_t parsed = 0;
                    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
                    if (error != std::errc{} || end != text.data() + text.size())
                        return false;
                    candidate = parsed;
                    return true;
                },
                [&ui, &property](float& candidate)
                {
                    double scalar = candidate;
                    if (!ui.DragScalar(property.Name, scalar, 0.01))
                        return false;
                    candidate = static_cast<float>(scalar);
                    return true;
                },
                [&ui, &property](std::string& candidate) { return ui.InputText(property.Name, candidate); },
                [&ui, &property](Keire::Vector2& candidate) { return ui.DragVector2(property.Name, candidate, 0.01F); },
                [&ui, &property](Keire::Vector3& candidate) { return ui.DragVector3(property.Name, candidate, 0.01F); },
                [&ui, &property](Keire::Vector4& candidate) { return ui.DragVector4(property.Name, candidate, 0.01F); },
                [&ui, &property](Keire::Quaternion& candidate)
                {
                    Keire::Vector4 components{candidate.X, candidate.Y, candidate.Z, candidate.W};
                    if (!ui.DragVector4(property.Name, components, 0.01F))
                        return false;
                    candidate = {components.X, components.Y, components.Z, components.W};
                    return true;
                },
                [&ui, &property](Keire::Color& candidate)
                {
                    Keire::UiColor color{candidate.Red, candidate.Green, candidate.Blue, candidate.Alpha};
                    if (!ui.ColorEdit(property.Name, color))
                        return false;
                    candidate = {color.Red, color.Green, color.Blue, color.Alpha};
                    return true;
                },
                [&ui, &property](Keire::Matrix4& candidate)
                {
                    bool changed = false;
                    for (std::size_t row = 0; row < 4; ++row)
                    {
                        Keire::Vector4 components{candidate.Elements[row * 4], candidate.Elements[row * 4 + 1],
                                                  candidate.Elements[row * 4 + 2], candidate.Elements[row * 4 + 3]};
                        if (!ui.DragVector4(property.Name + " Row " + std::to_string(row + 1), components, 0.01F))
                            continue;
                        candidate.Elements[row * 4] = components.X;
                        candidate.Elements[row * 4 + 1] = components.Y;
                        candidate.Elements[row * 4 + 2] = components.Z;
                        candidate.Elements[row * 4 + 3] = components.W;
                        changed = true;
                    }
                    return changed;
                },
                [this, &ui, &property](Keire::AssetId& candidate)
                {
                    AssetPickerOptions options{
                        .Label = property.Name,
                        .Reveal = [this](const Keire::AssetId selected)
                        { m_Controller.RevealVfxEffectAsset(selected); },
                    };
                    return m_AssetPicker.Draw(ui, m_Controller.VfxEffectAssetRecords(), candidate, options);
                },
            },
            property.Value);
    }

    void VfxEffectPanel::DrawGraphInspector(Keire::UiFrame& ui)
    {
        auto& document = m_Controller.VfxEffectState();
        const auto& definition = document.Definition();
        const auto& theme = m_Controller.VfxEffectTheme();
        const auto system = std::ranges::find(definition.Systems, m_SelectedSystem, &Keire::VfxGraphSystem::Id);
        if (system == definition.Systems.end())
        {
            ui.TextColored(theme.MutedText, "No graph system selected.");
            return;
        }
        if (DrawGraphMultiSelectionInspector(ui))
            return;

        auto rename = system->Name;
        if (ui.InputText("System Name", rename))
        {
            (void)ApplyAction("Renamed VFX graph system",
                              [&document, graph = system->Id, rename = std::move(rename)]
                              {
                                  return document.EditSystem(graph, [&rename](Keire::VfxGraphSystem& candidate)
                                                             { candidate.Name = rename; });
                              });
            return;
        }
        ui.Separator();

        auto dataType = system->DataType;
        auto particlesPerStrip = static_cast<std::int64_t>(system->ParticlesPerStrip);
        const auto dataTypeChanged = DrawEnum(ui, "Data Type", dataType, ParticleDataTypes);
        const auto stripCountChanged =
            dataType == Keire::VfxParticleDataType::ParticleStrip &&
            ui.DragInteger("Particles Per Strip", particlesPerStrip, 1.0, 1, definition.Capacity);
        if (dataTypeChanged || stripCountChanged)
        {
            particlesPerStrip = std::clamp<std::int64_t>(particlesPerStrip, 1, definition.Capacity);
            (void)ApplyAction("Configured VFX graph system",
                              [&document, graph = system->Id, dataType, particlesPerStrip]
                              {
                                  return document.EditSystem(
                                      graph,
                                      [dataType, particlesPerStrip](Keire::VfxGraphSystem& candidate)
                                      {
                                          candidate.DataType = dataType;
                                          candidate.ParticlesPerStrip = static_cast<std::uint32_t>(particlesPerStrip);
                                      });
                              });
            return;
        }
        ui.TextColored(theme.MutedText, dataType == Keire::VfxParticleDataType::ParticleStrip
                                            ? "Stable strip identity is available to Random and Ribbon output."
                                            : "Independent particle simulation.");
        ui.Separator();

        const auto selectedConnection =
            std::ranges::find(system->Connections, m_SelectedConnection, &Keire::VfxGraphConnection::Id);
        if (selectedConnection != system->Connections.end())
        {
            const auto outputNode =
                std::ranges::find(system->Nodes, selectedConnection->OutputNode, &Keire::VfxGraphNode::Id);
            const auto inputNode =
                std::ranges::find(system->Nodes, selectedConnection->InputNode, &Keire::VfxGraphNode::Id);
            const Keire::VfxGraphBlock* outputBlock = nullptr;
            const Keire::VfxGraphBlock* inputBlock = nullptr;
            const Keire::VfxGraphPin* outputPin = nullptr;
            const Keire::VfxGraphPin* inputPin = nullptr;
            if (outputNode != system->Nodes.end())
            {
                if (selectedConnection->OutputBlock)
                {
                    const auto block = std::ranges::find(outputNode->Blocks, selectedConnection->OutputBlock,
                                                         &Keire::VfxGraphBlock::Id);
                    if (block != outputNode->Blocks.end())
                    {
                        outputBlock = std::addressof(*block);
                        const auto found =
                            std::ranges::find(block->Pins, selectedConnection->OutputPin, &Keire::VfxGraphPin::Id);
                        if (found != block->Pins.end())
                            outputPin = std::addressof(*found);
                    }
                }
                else
                {
                    const auto found =
                        std::ranges::find(outputNode->Pins, selectedConnection->OutputPin, &Keire::VfxGraphPin::Id);
                    if (found != outputNode->Pins.end())
                        outputPin = std::addressof(*found);
                }
            }
            if (inputNode != system->Nodes.end())
            {
                if (selectedConnection->InputBlock)
                {
                    const auto block =
                        std::ranges::find(inputNode->Blocks, selectedConnection->InputBlock, &Keire::VfxGraphBlock::Id);
                    if (block != inputNode->Blocks.end())
                    {
                        inputBlock = std::addressof(*block);
                        const auto found =
                            std::ranges::find(block->Pins, selectedConnection->InputPin, &Keire::VfxGraphPin::Id);
                        if (found != block->Pins.end())
                            inputPin = std::addressof(*found);
                    }
                }
                else
                {
                    const auto found =
                        std::ranges::find(inputNode->Pins, selectedConnection->InputPin, &Keire::VfxGraphPin::Id);
                    if (found != inputNode->Pins.end())
                        inputPin = std::addressof(*found);
                }
            }
            const auto outputLabel =
                outputNode == system->Nodes.end() ? std::string("Missing node") : NodeLabel(definition, *outputNode);
            const auto inputLabel =
                inputNode == system->Nodes.end() ? std::string("Missing node") : NodeLabel(definition, *inputNode);
            const auto outputPinLabel = (outputBlock ? outputBlock->Type + "." : std::string{}) +
                                        (outputPin ? outputPin->Name : std::string("Missing pin"));
            const auto inputPinLabel = (inputBlock ? inputBlock->Type + "." : std::string{}) +
                                       (inputPin ? inputPin->Name : std::string("Missing pin"));

            ui.TextColored(theme.Accent, "CABLE INSPECTOR");
            ui.TextColored(theme.MutedText, "Stable ID: " + selectedConnection->Id.ToString());
            ui.Text(outputLabel + "." + outputPinLabel);
            ui.TextColored(theme.MutedText, "                 ->");
            ui.Text(inputLabel + "." + inputPinLabel);
            if (outputPin)
                ui.TextColored(PinColor(outputPin->Type),
                               "TYPE  |  " + std::string(EnumName(outputPin->Type, GraphValueTypes)));
            ui.Separator();
            if (ui.Button("Select Source"))
            {
                m_SelectedNode = selectedConnection->OutputNode;
                m_SelectedBlock = selectedConnection->OutputBlock;
                m_SelectedConnection = {};
                return;
            }
            ui.SameLine();
            if (ui.Button("Select Target"))
            {
                m_SelectedNode = selectedConnection->InputNode;
                m_SelectedBlock = selectedConnection->InputBlock;
                m_SelectedConnection = {};
                return;
            }
            if (ui.Button("Unlink Cable"))
            {
                (void)ApplyAction("Unlinked VFX graph cable",
                                  [&document, graph = system->Id, cable = selectedConnection->Id]
                                  { return document.RemoveConnection(graph, cable); });
                m_SelectedConnection = {};
                m_GraphCanvas.SelectConnection(std::nullopt);
                return;
            }
            ui.TextColored(theme.MutedText, "Right-click the cable in the graph for the same actions.");
            return;
        }
        if (m_SelectedConnection)
            m_SelectedConnection = {};

        const auto selected = std::ranges::find(system->Nodes, m_SelectedNode, &Keire::VfxGraphNode::Id);
        if (selected == system->Nodes.end())
        {
            ui.TextColored(theme.Accent, "GRAPH INSPECTOR");
            ui.TextColored(theme.MutedText,
                           "Select a node or cable to inspect its executable references, typed pins, and routing.");
            return;
        }

        if (m_SelectedBlock)
        {
            const auto selectedBlock = std::ranges::find(selected->Blocks, m_SelectedBlock, &Keire::VfxGraphBlock::Id);
            if (selectedBlock == selected->Blocks.end())
            {
                m_SelectedBlock = {};
            }
            else
            {
                const auto blockIndex =
                    static_cast<std::size_t>(std::distance(selected->Blocks.begin(), selectedBlock));
                const bool portable = selectedBlock->TypeId.View() == "keire.block.portable-hlsl";
                ui.TextColored(NodeColor(*selected), "CONTEXT BLOCK");
                ui.TextColored(theme.MutedText, std::string(EnumName(selected->Context, ContextTypes)) + " / " +
                                                    std::to_string(blockIndex + 1) + " of " +
                                                    std::to_string(selected->Blocks.size()));
                ui.Text(selectedBlock->Type);
                ui.TextColored(theme.MutedText, "Stable ID: " + selectedBlock->Id.ToString());
                if (selectedBlock->Reference)
                    ui.TextColored(theme.MutedText, "Payload: " + selectedBlock->Reference.ToString());

                auto enabled = selectedBlock->Enabled;
                if (ui.Checkbox("Enabled", enabled))
                {
                    (void)ApplyAction(enabled ? "Enabled VFX Context Block" : "Disabled VFX Context Block",
                                      [&document, graph = system->Id, context = selected->Id, block = selectedBlock->Id,
                                       enabled] { return document.SetBlockEnabled(graph, context, block, enabled); });
                    return;
                }
                if (auto disabled = ui.BeginDisabled(blockIndex == 0); disabled)
                {
                    if (ui.Button("Move Up"))
                    {
                        (void)ApplyAction("Moved VFX Context Block up",
                                          [&document, graph = system->Id, context = selected->Id,
                                           block = selectedBlock->Id, blockIndex]
                                          { return document.MoveBlock(graph, context, block, blockIndex - 1); });
                        return;
                    }
                }
                ui.SameLine();
                if (auto disabled = ui.BeginDisabled(blockIndex + 1 >= selected->Blocks.size()); disabled)
                {
                    if (ui.Button("Move Down"))
                    {
                        (void)ApplyAction("Moved VFX Context Block down",
                                          [&document, graph = system->Id, context = selected->Id,
                                           block = selectedBlock->Id, blockIndex]
                                          { return document.MoveBlock(graph, context, block, blockIndex + 1); });
                        return;
                    }
                }

                if (portable)
                {
                    ui.Separator();
                    ui.TextColored(theme.Accent, "PORTABLE CUSTOM HLSL");
                    const auto sourceProperty = std::ranges::find(selectedBlock->Properties, std::string_view("Source"),
                                                                  [](const Keire::VfxGraphProperty& property)
                                                                  { return std::string_view(property.Name); });
                    if (sourceProperty != selectedBlock->Properties.end())
                    {
                        if (const auto* source = std::get_if<std::string>(&sourceProperty->Value))
                        {
                            auto editedSource = *source;
                            if (ui.InputText("Source", editedSource))
                            {
                                (void)ApplyAction(
                                    "Edited Portable HLSL Block source",
                                    [&document, graph = system->Id, context = selected->Id, block = selectedBlock->Id,
                                     editedSource = std::move(editedSource)]() mutable
                                    {
                                        return document.EditBlock(
                                            graph, context, block,
                                            [&editedSource](Keire::VfxGraphBlock& candidate)
                                            {
                                                const auto property =
                                                    std::ranges::find(candidate.Properties, std::string_view("Source"),
                                                                      [](const Keire::VfxGraphProperty& value)
                                                                      { return std::string_view(value.Name); });
                                                if (property == candidate.Properties.end())
                                                    throw std::invalid_argument(
                                                        "Portable HLSL Block source is unavailable.");
                                                property->Value = std::move(editedSource);
                                            });
                                    });
                                return;
                            }
                        }
                        else
                        {
                            ui.TextColored(theme.Error, "Portable HLSL Block source is malformed.");
                        }
                    }
                    else
                    {
                        ui.TextColored(theme.Error, "Portable HLSL Block source is missing.");
                    }
                    ui.TextColored(theme.MutedText,
                                   "Statements may write Position, Velocity, Rotation, Tint, or Size. Typed inputs "
                                   "use their HLSL semantic identifiers.");
                }

                ui.Separator();
                ui.TextColored(theme.Accent, "TYPED BLOCK INPUTS");
                for (const auto& pin : selectedBlock->Pins)
                {
                    auto id = ui.PushId(pin.Id.ToString());
                    auto candidate = pin;
                    bool pinChanged = false;
                    if (portable)
                    {
                        pinChanged |= ui.InputText("Name", candidate.Name);
                        const auto previousType = candidate.Type;
                        pinChanged |= DrawEnum(ui, "Type", candidate.Type, CustomHlslValueTypes);
                        pinChanged |= ui.InputText("Semantic", candidate.Semantic);
                        if (candidate.Type != previousType || !candidate.DefaultValue)
                            candidate.DefaultValue = Keire::DefaultVfxValue(candidate.Type);
                        pinChanged |= DrawGraphValueEditor(ui, "Fallback", candidate.Type, *candidate.DefaultValue);
                    }
                    else
                    {
                        ui.Text(pin.Name + " : " + std::string(EnumName(pin.Type, GraphValueTypes)));
                        if (!pin.Semantic.empty())
                            ui.TextColored(theme.MutedText, "Semantic: " + pin.Semantic);
                        if (candidate.Input && candidate.DefaultValue)
                        {
                            const bool connected =
                                std::ranges::any_of(system->Connections,
                                                    [&](const Keire::VfxGraphConnection& connection)
                                                    {
                                                        return connection.InputNode == selected->Id &&
                                                               connection.InputBlock == selectedBlock->Id &&
                                                               connection.InputPin == pin.Id;
                                                    });
                            pinChanged |= DrawGraphValueEditor(ui, connected ? "Inline Fallback" : "Inline Value",
                                                               candidate.Type, *candidate.DefaultValue);
                            if (connected)
                                ui.TextColored(theme.MutedText,
                                               "The incoming cable overrides this fallback while connected.");
                        }
                    }
                    if (pinChanged)
                    {
                        (void)ApplyAction("Edited VFX Context Block input",
                                          [&document, graph = system->Id, context = selected->Id,
                                           block = selectedBlock->Id, pin = pin.Id,
                                           candidate = std::move(candidate)]() mutable
                                          {
                                              return document.EditBlockPin(graph, context, block, pin,
                                                                           [&candidate](Keire::VfxGraphPin& value)
                                                                           { value = std::move(candidate); });
                                          });
                        return;
                    }
                    if (portable && ui.Button("Remove Input"))
                    {
                        (void)ApplyAction("Removed Portable HLSL Block input",
                                          [&document, graph = system->Id, context = selected->Id,
                                           block = selectedBlock->Id, pin = pin.Id]
                                          { return document.RemoveBlockPin(graph, context, block, pin); });
                        return;
                    }
                    ui.Separator();
                }
                if (selectedBlock->Pins.empty())
                    ui.TextColored(theme.MutedText, "No typed data inputs.");

                if (portable && ui.Button("+ Add Data Input"))
                {
                    std::size_t index = 1;
                    while (std::ranges::any_of(selectedBlock->Pins, [index](const Keire::VfxGraphPin& pin)
                                               { return pin.Semantic == "Input" + std::to_string(index); }))
                    {
                        ++index;
                    }
                    Keire::VfxGraphPin pin{Keire::AssetId::Generate(),      "Input " + std::to_string(index),
                                           Keire::VfxValueType::Scalar,     true,
                                           "Input" + std::to_string(index), 0.0F};
                    (void)ApplyAction("Added Portable HLSL Block input",
                                      [&document, graph = system->Id, context = selected->Id, block = selectedBlock->Id,
                                       pin = std::move(pin)]() mutable
                                      { return document.AddBlockPin(graph, context, block, std::move(pin)); });
                    return;
                }

                if (selectedBlock->Reference)
                {
                    ui.Separator();
                    const auto module = std::ranges::find(definition.Modules, selectedBlock->Reference,
                                                          &Keire::VfxModuleDefinition::Id);
                    if (module == definition.Modules.end())
                        ui.TextColored(theme.Error, "The referenced Runtime Module payload is missing.");
                    else if (ui.Selectable("Edit Payload: " + std::string(ModuleName(module->Payload)),
                                           module->Id == m_SelectedModule))
                        m_SelectedModule = module->Id;
                }

                ui.Separator();
                if (ui.Button("Remove Block") &&
                    ApplyAction("Removed VFX Context Block",
                                [&document, graph = system->Id, context = selected->Id, block = selectedBlock->Id]
                                { return document.RemoveBlock(graph, context, block); }))
                {
                    m_SelectedBlock = {};
                    m_GraphCanvas.SelectBlock(std::nullopt);
                    return;
                }
                ui.TextColored(theme.MutedText,
                               "Drag this row to reorder it. Right-click for enable, unlink, and remove actions.");
                return;
            }
        }

        auto node = *selected;
        bool changed = false;
        ui.TextColored(NodeColor(node), std::string(VfxGraphNodeKindLabel(node.Kind)));
        ui.TextColored(theme.MutedText, "Stable ID: " + node.Id.ToString());
        if (node.Reference)
            ui.TextColored(theme.MutedText, "Reference: " + node.Reference.ToString());
        if (node.Kind == Keire::VfxGraphNodeKind::Context || node.Kind == Keire::VfxGraphNodeKind::CustomHlsl)
            changed |= ui.InputText("Node Name", node.Type);
        else
            ui.Text("Source: " + NodeLabel(definition, node));
        const Keire::VfxNodeDescriptor* operatorDescriptor = nullptr;
        if (node.Kind == Keire::VfxGraphNodeKind::Operator)
            operatorDescriptor = Keire::FindVfxNodeDescriptor(node.TypeId.View());
        if (operatorDescriptor)
        {
            bool contextChanged = false;
            if (auto combo = ui.BeginCombo("Context", EnumName(node.Context, ContextTypes)); combo)
            {
                for (const auto& candidate : ContextTypes)
                {
                    if (std::ranges::find(operatorDescriptor->ValidContexts, candidate.Type) ==
                        operatorDescriptor->ValidContexts.end())
                    {
                        continue;
                    }
                    if (candidate.Type == Keire::VfxContextType::Event)
                    {
                        (void)ui.MenuItem("Event (requires Event context execution)", false, false);
                        continue;
                    }
                    if (ui.MenuItem(candidate.Name, node.Context == candidate.Type))
                    {
                        node.Context = candidate.Type;
                        contextChanged = true;
                    }
                }
            }
            changed |= contextChanged;
        }
        else if (node.Kind == Keire::VfxGraphNodeKind::Context || node.Kind == Keire::VfxGraphNodeKind::CustomHlsl)
            changed |= DrawEnum(ui, "Context", node.Context, ContextTypes);
        else
            ui.Text("Context: " + std::string(EnumName(node.Context, ContextTypes)));
        changed |= ui.DragVector2("Graph Position", node.EditorPosition, 1.0F);
        if (node.Kind == Keire::VfxGraphNodeKind::CustomHlsl)
        {
            changed |= ui.InputText("Custom HLSL", node.CustomHlsl);
            ui.TextColored(theme.MutedText,
                           "Portable statements write Position, Velocity, Rotation, Tint, or Size. Pin semantics are "
                           "the generated input names.");
        }
        if (node.Kind == Keire::VfxGraphNodeKind::Operator)
        {
            ui.Separator();
            ui.TextColored(theme.Accent, "OPERATOR SETTINGS");
            if (!operatorDescriptor)
            {
                ui.TextColored(theme.Error, "The compiler descriptor for this Operator is unavailable.");
            }
            else
            {
                const auto entry = BuildVfxNodeCatalogEntry(*operatorDescriptor);
                ui.TextColored(theme.MutedText, "Backend: " + VfxNodeCatalogSupportBadge(entry));
                for (auto& property : node.Properties)
                {
                    auto id = ui.PushId(property.Name);
                    changed |= DrawGraphPropertyEditor(ui, property);
                }
                if (node.Properties.empty())
                    ui.TextColored(theme.MutedText, "This Operator has no configurable settings.");
            }
        }
        if (changed)
        {
            const auto nodeId = node.Id;
            (void)ApplyAction("Edited VFX graph node",
                              [&document, graph = system->Id, nodeId, node = std::move(node)]() mutable
                              {
                                  return document.EditNode(
                                      graph, nodeId, [node = std::move(node)](Keire::VfxGraphNode& candidate) mutable
                                      { candidate = std::move(node); });
                              });
            return;
        }

        if (DrawGraphNodeComment(ui, system->Id, *selected))
            return;

        ui.Separator();
        ui.TextColored(theme.Accent, "TYPED PINS");
        const bool customPins = selected->Kind == Keire::VfxGraphNodeKind::CustomHlsl;
        for (const auto& pin : selected->Pins)
        {
            auto id = ui.PushId(pin.Id.ToString());
            auto candidate = pin;
            bool pinChanged = false;
            const bool customPinEditable = customPins && pin.Type != Keire::VfxValueType::ParticleStream;
            if (customPinEditable)
            {
                pinChanged |= ui.InputText("Name", candidate.Name);
                const auto previousType = candidate.Type;
                pinChanged |= DrawEnum(ui, "Type", candidate.Type, CustomHlslValueTypes);
                pinChanged |= ui.InputText("Semantic", candidate.Semantic);
                if (candidate.Type != previousType)
                    candidate.DefaultValue = Keire::DefaultVfxValue(candidate.Type);

                if (candidate.Input)
                {
                    if (!candidate.DefaultValue)
                        candidate.DefaultValue = Keire::DefaultVfxValue(candidate.Type);
                    pinChanged |= DrawGraphValueEditor(ui, "Fallback", candidate.Type, *candidate.DefaultValue);
                }
            }
            else
            {
                ui.Text(pin.Name + " : " + std::string(EnumName(pin.Type, GraphValueTypes)) +
                        (pin.Input ? " [Input]" : " [Output]"));
                if (!pin.Semantic.empty())
                    ui.TextColored(theme.MutedText, "Semantic: " + pin.Semantic);
                if (selected->Kind == Keire::VfxGraphNodeKind::Operator && candidate.Input && candidate.DefaultValue)
                {
                    const bool connected = std::ranges::any_of(
                        system->Connections, [&](const Keire::VfxGraphConnection& connection)
                        { return connection.InputNode == selected->Id && connection.InputPin == pin.Id; });
                    pinChanged |= DrawGraphValueEditor(ui, connected ? "Inline Fallback" : "Inline Value",
                                                       candidate.Type, *candidate.DefaultValue);
                    if (connected)
                        ui.TextColored(theme.MutedText, "The incoming cable overrides this fallback while connected.");
                }
            }
            if (pinChanged)
            {
                const bool topologyChanged = candidate.Type != pin.Type || candidate.Input != pin.Input;
                (void)ApplyEdit(
                    "Edit VFX graph pin",
                    [graph = system->Id, nodeId = selected->Id, candidate = std::move(candidate),
                     topologyChanged](Keire::VfxEffectDefinition& draft) mutable
                    {
                        auto graphSystem = std::ranges::find(draft.Systems, graph, &Keire::VfxGraphSystem::Id);
                        if (graphSystem == draft.Systems.end())
                            throw std::invalid_argument("VFX graph system is unavailable.");
                        auto graphNode = std::ranges::find(graphSystem->Nodes, nodeId, &Keire::VfxGraphNode::Id);
                        if (graphNode == graphSystem->Nodes.end())
                            throw std::invalid_argument("VFX graph node is unavailable.");
                        auto graphPin = std::ranges::find(graphNode->Pins, candidate.Id, &Keire::VfxGraphPin::Id);
                        if (graphPin == graphNode->Pins.end())
                            throw std::invalid_argument("VFX graph pin is unavailable.");
                        *graphPin = std::move(candidate);
                        if (topologyChanged)
                            std::erase_if(graphSystem->Connections,
                                          [pin = graphPin->Id](const Keire::VfxGraphConnection& connection)
                                          { return connection.OutputPin == pin || connection.InputPin == pin; });
                    });
                return;
            }

            if (customPinEditable)
            {
                if (ui.Button("Remove Pin"))
                {
                    (void)ApplyAction("Removed VFX graph pin",
                                      [&document, graph = system->Id, nodeId = selected->Id, pinId = pin.Id]
                                      { return document.RemovePin(graph, nodeId, pinId); });
                    return;
                }
            }
            ui.Separator();
        }

        const auto addPin = [&]
        {
            const auto dataInputs =
                std::ranges::count_if(selected->Pins, [](const Keire::VfxGraphPin& pin)
                                      { return pin.Input && pin.Type != Keire::VfxValueType::ParticleStream; });
            Keire::VfxGraphPin pin{Keire::AssetId::Generate(),
                                   "Input",
                                   Keire::VfxValueType::Scalar,
                                   true,
                                   "Input" + std::to_string(dataInputs + 1),
                                   0.0F};
            return ApplyAction("Added VFX graph pin",
                               [&document, graph = system->Id, nodeId = selected->Id, pin = std::move(pin)]() mutable
                               { return document.AddPin(graph, nodeId, std::move(pin)); });
        };
        if (customPins && ui.Button("+ Add Data Input") && addPin())
            return;

        ui.Separator();
        ui.TextColored(theme.Accent, "CONNECTIONS");
        bool hasConnections = false;
        for (const auto& connection : system->Connections)
        {
            if (connection.OutputNode != selected->Id && connection.InputNode != selected->Id)
                continue;
            hasConnections = true;
            auto id = ui.PushId(connection.Id.ToString());
            ui.TextColored(theme.MutedText,
                           connection.OutputNode.ToString() + "  ->  " + connection.InputNode.ToString());
            ui.SameLine();
            if (ui.Button("Remove Link"))
            {
                (void)ApplyAction("Removed VFX graph connection", [&document, graph = system->Id, link = connection.Id]
                                  { return document.RemoveConnection(graph, link); });
                return;
            }
        }
        if (!hasConnections)
            ui.TextColored(theme.MutedText, "No links on this context.");

        ui.Separator();
        if (selected->Kind == Keire::VfxGraphNodeKind::Context)
        {
            ui.TextColored(theme.Accent, "ORDERED CONTEXT BLOCKS");
            for (std::size_t index = 0; index < selected->Blocks.size(); ++index)
            {
                const auto& block = selected->Blocks[index];
                auto id = ui.PushId(block.Id.ToString());
                if (ui.Selectable(std::to_string(index + 1) + ". " + (block.Enabled ? "" : "[Disabled] ") + block.Type,
                                  block.Id == m_SelectedBlock))
                {
                    m_SelectedBlock = block.Id;
                    m_SelectedConnection = {};
                    return;
                }
            }
            if (selected->Blocks.empty())
                ui.TextColored(theme.MutedText, "This Context has no executable Blocks.");
            if (auto add = ui.BeginCombo("Add Block", "Choose..."); add)
            {
                (void)ui.InputTextWithHint("##VfxInspectorBlockSearch", "Search Blocks...", m_NodePaletteSearch);
                ui.Separator();
                if (DrawNodePaletteEntries(ui, system->Id, selected->EditorPosition, m_NodePaletteSearch, selected->Id))
                {
                    return;
                }
            }
        }
        else if (selected->Kind == Keire::VfxGraphNodeKind::Module)
        {
            ui.TextColored(theme.Accent, "REFERENCED RUNTIME MODULE");
            const auto module =
                std::ranges::find(definition.Modules, selected->Reference, &Keire::VfxModuleDefinition::Id);
            if (module == definition.Modules.end())
                ui.TextColored(theme.Error, "The referenced Runtime Module is missing.");
            else if (ui.Selectable(std::string(ModuleName(module->Payload)), module->Id == m_SelectedModule))
                m_SelectedModule = module->Id;
        }
        else if (selected->Kind == Keire::VfxGraphNodeKind::Parameter)
        {
            ui.TextColored(theme.Accent, "REFERENCED BLACKBOARD PROPERTY");
            const auto parameter =
                std::ranges::find(definition.Blackboard, selected->Reference, &Keire::VfxBlackboardParameter::Id);
            if (parameter == definition.Blackboard.end())
                ui.TextColored(theme.Error, "The referenced Blackboard property is missing.");
            else
                ui.Text(parameter->Name + " : " + std::string(EnumName(parameter->Type, ValueTypes)));
        }

        ui.Separator();
        if (auto disabled = ui.BeginDisabled(selected->Kind == Keire::VfxGraphNodeKind::Context); disabled)
        {
            if (ui.Button("Delete Node") &&
                ApplyAction("Removed VFX graph node", [&document, graph = system->Id, nodeId = selected->Id]
                            { return document.RemoveNode(graph, nodeId); }))
            {
                m_SelectedNode = {};
                m_SelectedBlock = {};
                m_SelectedConnection = {};
                m_GraphCanvas.CancelInteractions();
                m_GraphCanvas.Select(std::nullopt);
                m_GraphCanvas.SelectBlock(std::nullopt);
                return;
            }
        }
        if (selected->Kind == Keire::VfxGraphNodeKind::Context)
            ui.TextColored(theme.MutedText, "Executable stage contexts cannot be deleted.");
    }

} // namespace KeireEditor
