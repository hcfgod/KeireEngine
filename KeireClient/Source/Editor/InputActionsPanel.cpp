#include "KeireClient/Editor/EditorPanels.h"

#include "KeireClient/Editor/InputActionsDocument.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace KeireEditor
{
    namespace
    {
        template <typename Range, typename Projection>
        [[nodiscard]] std::string UniqueInputName(const Range& values, std::string base, Projection projection)
        {
            std::string candidate = base;
            for (std::size_t copy = 2; std::ranges::any_of(values, [&](const auto& value)
                                                           { return std::invoke(projection, value) == candidate; });
                 ++copy)
            {
                candidate = base + " " + std::to_string(copy);
            }
            return candidate;
        }

        void DrawEmptyState(Keire::UiFrame& ui, const std::string_view heading, const std::string_view message,
                            const std::string_view hint)
        {
            ui.TextColored({0.31F, 0.63F, 1.0F, 1.0F}, heading);
            ui.Separator();
            ui.Text(message);
            if (!hint.empty())
                ui.Text(hint);
        }
    } // namespace
    void InputActionsPanel::Draw(Keire::UiFrame& ui)
    {
        auto panel = ui.BeginPanel(m_Registration);
        if (!panel)
            return;

        auto& document = m_Controller.InputActionsState();
        const auto& theme = m_Controller.InputActionsTheme();
        const auto database = m_Controller.InputAssetDatabase();
        const auto inputContext = m_Controller.InputActionContext();
        if (ui.WindowFocused())
            m_Controller.ActivateInputHistory();
        if (!document.Asset())
        {
            DrawEmptyState(ui, "INPUT ACTIONS", "No input action asset is open.",
                           "Select a .keireinput asset and choose Edit Input Actions in the Inspector.");
            return;
        }

        auto inputDocument = document.Definition();
        auto selectedMap = document.SelectedMap();
        auto selectedScheme = document.SelectedScheme();
        auto selectedAction = document.SelectedAction();
        auto selectedBinding = document.SelectedBinding();
        const auto originalBytes = Keire::InputActionAsset::Encode(inputDocument);

        const auto record = database ? database->Find(document.Asset()) : std::nullopt;
        ui.TextColored(theme.Accent, "INPUT ACTIONS");
        ui.SameLine();
        ui.Text(record ? record->RelativePath.generic_string() + (document.Dirty() ? " *" : "") : "Missing asset");
        ui.Separator();
        if (ui.Shortcut({Keire::UiKey::S, true}) && document.Dirty())
        {
            try
            {
                m_Controller.SaveInputActionsDocument();
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportInputActionsError(m_Message);
            }
        }
        if (ui.Button("Save"))
        {
            try
            {
                m_Controller.SaveInputActionsDocument();
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportInputActionsError(m_Message);
            }
        }
        ui.SameLine();
        if (ui.Button("Revert"))
        {
            try
            {
                m_Controller.ReloadInputActionsDocument(document.Asset());
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportInputActionsError(m_Message);
            }
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!document.UndoContext() || !document.UndoContext()->CanUndo()); disabled)
        {
            if (ui.Button("Undo"))
                m_Controller.UndoInputActions();
        }
        ui.SameLine();
        if (auto disabled = ui.BeginDisabled(!document.UndoContext() || !document.UndoContext()->CanRedo()); disabled)
        {
            if (ui.Button("Redo"))
                m_Controller.RedoInputActions();
        }
        ui.SameLine();
        if (ui.Button("Validate"))
        {
            try
            {
                Keire::InputActionAsset::Validate(inputDocument);
                m_Message = "Validation passed.";
            }
            catch (const std::exception& error)
            {
                m_Message = error.what();
                m_Controller.ReportInputActionsError(m_Message);
            }
        }
        ui.SameLine();
        (void)ui.Checkbox("Live Monitor", m_LiveMonitor);
        (void)ui.InputText("Search", m_Search);
        if (!m_Message.empty())
            ui.TextColored(theme.MutedText, m_Message);
        ui.Separator();

        auto findMap = [&]() -> Keire::InputActionMapDefinition*
        {
            const auto found =
                std::ranges::find(inputDocument.ActionMaps, selectedMap, &Keire::InputActionMapDefinition::Id);
            return found == inputDocument.ActionMaps.end() ? nullptr : &*found;
        };
        auto map = findMap();
        if (auto maps = ui.BeginChild("InputMaps", {230.0F, 0.0F}, true); maps)
        {
            ui.TextColored(theme.Accent, "ACTION MAPS");
            for (const auto& candidate : inputDocument.ActionMaps)
            {
                if (!m_Search.empty() && candidate.Name.find(m_Search) == std::string::npos)
                    continue;
                if (ui.Selectable(candidate.Name, candidate.Id == selectedMap))
                {
                    selectedMap = candidate.Id;
                    selectedScheme = {};
                    selectedAction = {};
                    selectedBinding = {};
                }
            }
            ui.Separator();
            ui.TextColored(theme.MutedText, "CONTROL SCHEMES");
            for (const auto& scheme : inputDocument.ControlSchemes)
            {
                if (ui.Selectable(scheme.Name + "  [" + scheme.BindingGroup + "]", scheme.Id == selectedScheme))
                {
                    selectedScheme = scheme.Id;
                    selectedMap = {};
                    selectedAction = {};
                    selectedBinding = {};
                }
            }
            if (ui.Button("+ Map"))
            {
                m_Controller.RecordInputActionsUndo();
                Keire::InputActionMapDefinition added;
                added.Id = Keire::AssetId::Generate();
                added.Name =
                    UniqueInputName(inputDocument.ActionMaps, "New Map", &Keire::InputActionMapDefinition::Name);
                selectedMap = added.Id;
                selectedScheme = {};
                inputDocument.ActionMaps.push_back(std::move(added));
            }
            ui.SameLine();
            if (ui.Button("+ Scheme"))
            {
                m_Controller.RecordInputActionsUndo("Add Control Scheme");
                Keire::InputControlSchemeDefinition added;
                added.Id = Keire::AssetId::Generate();
                added.Name = UniqueInputName(inputDocument.ControlSchemes, "New Scheme",
                                             &Keire::InputControlSchemeDefinition::Name);
                added.BindingGroup = UniqueInputName(inputDocument.ControlSchemes, "NewScheme",
                                                     &Keire::InputControlSchemeDefinition::BindingGroup);
                added.Devices.push_back({"Keyboard", false});
                selectedScheme = added.Id;
                selectedMap = {};
                selectedAction = {};
                selectedBinding = {};
                inputDocument.ControlSchemes.push_back(std::move(added));
            }
            if (selectedMap && ui.Button("Duplicate Map"))
            {
                const auto source =
                    std::ranges::find(inputDocument.ActionMaps, selectedMap, &Keire::InputActionMapDefinition::Id);
                if (source != inputDocument.ActionMaps.end())
                {
                    m_Controller.RecordInputActionsUndo("Duplicate Action Map");
                    auto copy = *source;
                    copy.Id = Keire::AssetId::Generate();
                    copy.Name = UniqueInputName(inputDocument.ActionMaps, copy.Name + " Copy",
                                                &Keire::InputActionMapDefinition::Name);
                    std::vector<std::pair<Keire::AssetId, Keire::AssetId>> actionIds;
                    for (auto& action : copy.Actions)
                    {
                        const auto previous = action.Id;
                        action.Id = Keire::AssetId::Generate();
                        actionIds.emplace_back(previous, action.Id);
                    }
                    for (auto& binding : copy.Bindings)
                    {
                        binding.Id = Keire::AssetId::Generate();
                        const auto action = std::ranges::find_if(actionIds, [&](const auto& identities)
                                                                 { return identities.first == binding.Action; });
                        if (action != actionIds.end())
                            binding.Action = action->second;
                    }
                    selectedMap = copy.Id;
                    inputDocument.ActionMaps.push_back(std::move(copy));
                }
            }
            ui.SameLine();
            if (selectedMap && ui.Button("Delete Map"))
            {
                m_Controller.RecordInputActionsUndo("Delete Action Map");
                std::erase_if(inputDocument.ActionMaps,
                              [&](const auto& actionMap) { return actionMap.Id == selectedMap; });
                selectedMap = inputDocument.ActionMaps.empty() ? Keire::AssetId{} : inputDocument.ActionMaps.front().Id;
                selectedAction = {};
                selectedBinding = {};
            }
        }
        ui.SameLine();
        map = findMap();
        if (auto actions = ui.BeginChild("InputActions", {430.0F, 0.0F}, true); actions)
        {
            ui.TextColored(theme.Accent, map ? map->Name : "ACTIONS");
            if (!map)
                ui.TextColored(theme.MutedText, "Select or create an action map.");
            else
            {
                if (inputContext)
                    (void)inputContext->EnableMap(map->Id);
                for (const auto& action : map->Actions)
                {
                    auto actionId = ui.PushId(action.Id.ToString());
                    if (ui.Selectable(action.Name, action.Id == selectedAction))
                    {
                        selectedAction = action.Id;
                        selectedBinding = {};
                    }
                    for (const auto& binding : map->Bindings)
                    {
                        if (binding.Action != action.Id)
                            continue;
                        auto bindingId = ui.PushId(binding.Id.ToString());
                        const auto detail = !binding.Composite.empty() ? std::string("[") + binding.Composite + "]"
                                            : !binding.CompositePart.empty()
                                                ? binding.CompositePart + ": " + binding.Path
                                            : binding.Name.empty() ? binding.Path
                                                                   : binding.Name + ": " + binding.Path;
                        const auto label = "   " + detail;
                        if (ui.Selectable(label, binding.Id == selectedBinding))
                        {
                            selectedAction = action.Id;
                            selectedBinding = binding.Id;
                        }
                    }
                }
                if (ui.Button("+ Action"))
                {
                    m_Controller.RecordInputActionsUndo();
                    Keire::InputActionDefinition action;
                    action.Id = Keire::AssetId::Generate();
                    action.Name = UniqueInputName(map->Actions, "New Action", &Keire::InputActionDefinition::Name);
                    selectedAction = action.Id;
                    map->Actions.push_back(std::move(action));
                }
                ui.SameLine();
                if (auto disabled = ui.BeginDisabled(!selectedAction); disabled)
                {
                    if (ui.Button("+ Binding"))
                    {
                        m_Controller.RecordInputActionsUndo();
                        Keire::InputBindingDefinition binding;
                        binding.Id = Keire::AssetId::Generate();
                        binding.Action = selectedAction;
                        binding.Path = "<Keyboard>/space";
                        selectedBinding = binding.Id;
                        map->Bindings.push_back(std::move(binding));
                    }
                    ui.SameLine();
                    if (ui.Button("+ 1D Axis"))
                    {
                        m_Controller.RecordInputActionsUndo("Add 1D Composite");
                        if (const auto action =
                                std::ranges::find(map->Actions, selectedAction, &Keire::InputActionDefinition::Id);
                            action != map->Actions.end())
                        {
                            action->Type = Keire::InputActionType::Value;
                            action->ValueType = Keire::InputValueType::Axis1D;
                        }
                        Keire::InputBindingDefinition root;
                        root.Id = Keire::AssetId::Generate();
                        root.Action = selectedAction;
                        root.Name = "Axis";
                        root.Composite = "Axis1D";
                        map->Bindings.push_back(root);
                        for (const auto& [part, path] :
                             {std::pair{"Negative", "<Keyboard>/a"}, std::pair{"Positive", "<Keyboard>/d"}})
                        {
                            Keire::InputBindingDefinition child;
                            child.Id = Keire::AssetId::Generate();
                            child.Action = selectedAction;
                            child.Name = part;
                            child.Path = path;
                            child.CompositePart = part;
                            map->Bindings.push_back(std::move(child));
                        }
                        selectedBinding = root.Id;
                    }
                    ui.SameLine();
                    if (ui.Button("+ 2D Vector"))
                    {
                        m_Controller.RecordInputActionsUndo("Add 2D Composite");
                        if (const auto action =
                                std::ranges::find(map->Actions, selectedAction, &Keire::InputActionDefinition::Id);
                            action != map->Actions.end())
                        {
                            action->Type = Keire::InputActionType::Value;
                            action->ValueType = Keire::InputValueType::Axis2D;
                        }
                        Keire::InputBindingDefinition root;
                        root.Id = Keire::AssetId::Generate();
                        root.Action = selectedAction;
                        root.Name = "Vector";
                        root.Composite = "Vector2";
                        map->Bindings.push_back(root);
                        for (const auto& [part, path] :
                             {std::pair{"Up", "<Keyboard>/w"}, std::pair{"Down", "<Keyboard>/s"},
                              std::pair{"Left", "<Keyboard>/a"}, std::pair{"Right", "<Keyboard>/d"}})
                        {
                            Keire::InputBindingDefinition child;
                            child.Id = Keire::AssetId::Generate();
                            child.Action = selectedAction;
                            child.Name = part;
                            child.Path = path;
                            child.CompositePart = part;
                            map->Bindings.push_back(std::move(child));
                        }
                        selectedBinding = root.Id;
                    }
                }
                if (selectedBinding && ui.Button("Delete Binding"))
                {
                    m_Controller.RecordInputActionsUndo("Delete Binding");
                    auto binding =
                        std::ranges::find(map->Bindings, selectedBinding, &Keire::InputBindingDefinition::Id);
                    if (binding != map->Bindings.end())
                    {
                        if (!binding->Composite.empty())
                        {
                            auto end = std::next(binding);
                            while (end != map->Bindings.end() && !end->CompositePart.empty())
                                ++end;
                            map->Bindings.erase(binding, end);
                        }
                        else
                            map->Bindings.erase(binding);
                    }
                    selectedBinding = {};
                }
                else if (selectedAction && ui.Button("Duplicate Action"))
                {
                    const auto source =
                        std::ranges::find(map->Actions, selectedAction, &Keire::InputActionDefinition::Id);
                    if (source != map->Actions.end())
                    {
                        m_Controller.RecordInputActionsUndo("Duplicate Action");
                        auto copy = *source;
                        const auto sourceId = copy.Id;
                        copy.Id = Keire::AssetId::Generate();
                        copy.Name =
                            UniqueInputName(map->Actions, copy.Name + " Copy", &Keire::InputActionDefinition::Name);
                        selectedAction = copy.Id;
                        map->Actions.push_back(copy);
                        for (const auto& binding : std::vector<Keire::InputBindingDefinition>(map->Bindings))
                        {
                            if (binding.Action != sourceId)
                                continue;
                            auto bindingCopy = binding;
                            bindingCopy.Id = Keire::AssetId::Generate();
                            bindingCopy.Action = copy.Id;
                            if (!bindingCopy.Name.empty())
                                bindingCopy.Name = UniqueInputName(map->Bindings, bindingCopy.Name + " Copy",
                                                                   &Keire::InputBindingDefinition::Name);
                            map->Bindings.push_back(std::move(bindingCopy));
                        }
                    }
                }
                ui.SameLine();
                if (selectedAction && ui.Button("Delete Action"))
                {
                    m_Controller.RecordInputActionsUndo("Delete Action");
                    std::erase_if(map->Bindings, [&](const auto& binding) { return binding.Action == selectedAction; });
                    std::erase_if(map->Actions, [&](const auto& action) { return action.Id == selectedAction; });
                    selectedAction = {};
                    selectedBinding = {};
                }
            }
        }
        ui.SameLine();
        map = findMap();
        if (auto properties = ui.BeginChild("InputProperties", {}, true); properties)
        {
            ui.TextColored(theme.Accent, "PROPERTIES");
            const auto drawBehaviors = [&](const std::string_view heading,
                                           std::vector<Keire::InputBehaviorDefinition>& behaviors,
                                           const bool interactions)
            {
                ui.Separator();
                ui.TextColored(theme.Accent, heading);
                for (std::size_t index = 0; index < behaviors.size(); ++index)
                {
                    auto behaviorId = ui.PushId(std::string(heading) + std::to_string(index));
                    auto& behavior = behaviors[index];
                    ui.Text(behavior.Name);
                    ui.SameLine();
                    if (ui.Button("Remove"))
                    {
                        m_Controller.RecordInputActionsUndo("Remove " + behavior.Name);
                        behaviors.erase(behaviors.begin() + static_cast<std::ptrdiff_t>(index));
                        --index;
                        continue;
                    }
                    for (auto& parameter : behavior.Parameters)
                    {
                        if (behavior.Name == "Invert" && (parameter.Name == "x" || parameter.Name == "y"))
                        {
                            bool enabled = parameter.Value != 0.0;
                            if (ui.Checkbox("Invert " + std::string(parameter.Name), enabled))
                            {
                                m_Controller.RecordInputActionsUndo("Change Invert Axis");
                                parameter.Value = enabled ? 1.0 : 0.0;
                            }
                            continue;
                        }
                        float minimum = -1000.0F;
                        float maximum = 1000.0F;
                        if (parameter.Name == "pressPoint" || parameter.Name == "minimum" ||
                            parameter.Name == "maximum")
                        {
                            minimum = 0.0F;
                            maximum = 1.0F;
                        }
                        else if (parameter.Name == "duration" || parameter.Name == "delay")
                        {
                            minimum = 0.01F;
                            maximum = behavior.Name == "Hold" ? 60.0F : 10.0F;
                        }
                        else if (parameter.Name == "count")
                        {
                            minimum = 2.0F;
                            maximum = 16.0F;
                        }
                        float value = static_cast<float>(parameter.Value);
                        if (ui.SliderFloat(parameter.Name, value, minimum, maximum))
                        {
                            m_Controller.RecordInputActionsUndo("Change " + behavior.Name + " Parameter");
                            parameter.Value = parameter.Name == "count" ? std::round(value) : value;
                        }
                    }
                }
                if (behaviors.size() >= 8)
                    return;
                if (auto add = ui.BeginCombo("Add " + std::string(heading), "Choose..."); add)
                {
                    const auto addBehavior =
                        [&](const std::string_view name, std::initializer_list<Keire::InputParameter> parameters)
                    {
                        if (std::ranges::find(behaviors, name, &Keire::InputBehaviorDefinition::Name) !=
                            behaviors.end())
                            return;
                        m_Controller.RecordInputActionsUndo("Add " + std::string(name));
                        behaviors.push_back({std::string(name), parameters});
                    };
                    if (interactions)
                    {
                        if (ui.MenuItem("Press"))
                            addBehavior("Press", {{"pressPoint", 0.5}});
                        if (ui.MenuItem("Tap"))
                            addBehavior("Tap", {{"duration", 0.2}, {"pressPoint", 0.5}});
                        if (ui.MenuItem("Hold"))
                            addBehavior("Hold", {{"duration", 0.4}, {"pressPoint", 0.5}});
                        if (ui.MenuItem("Multi Tap"))
                            addBehavior("MultiTap",
                                        {{"duration", 0.2}, {"delay", 0.75}, {"count", 2.0}, {"pressPoint", 0.5}});
                    }
                    else
                    {
                        if (ui.MenuItem("Deadzone"))
                            addBehavior("Deadzone", {{"minimum", 0.125}, {"maximum", 0.925}});
                        if (ui.MenuItem("Scale"))
                            addBehavior("Scale", {{"x", 1.0}, {"y", 1.0}});
                        if (ui.MenuItem("Invert"))
                            addBehavior("Invert", {{"x", 1.0}, {"y", 1.0}});
                        if (ui.MenuItem("Normalize"))
                            addBehavior("Normalize", {});
                    }
                }
            };
            if (!map)
            {
                const auto scheme = std::ranges::find(inputDocument.ControlSchemes, selectedScheme,
                                                      &Keire::InputControlSchemeDefinition::Id);
                if (scheme == inputDocument.ControlSchemes.end())
                {
                    ui.TextColored(theme.MutedText, "Select an action map, action, binding, or control scheme.");
                    return;
                }
                auto name = scheme->Name;
                if (ui.InputText("Scheme Name", name))
                {
                    m_Controller.RecordInputActionsUndo("Rename Control Scheme");
                    scheme->Name = std::move(name);
                }
                auto group = scheme->BindingGroup;
                if (ui.InputText("Binding Group", group))
                {
                    m_Controller.RecordInputActionsUndo("Change Binding Group");
                    const auto previous = scheme->BindingGroup;
                    scheme->BindingGroup = std::move(group);
                    for (auto& actionMap : inputDocument.ActionMaps)
                        for (auto& binding : actionMap.Bindings)
                            std::ranges::replace(binding.Groups, previous, scheme->BindingGroup);
                }
                ui.Separator();
                ui.TextColored(theme.Accent, "DEVICES");
                for (const std::string_view family : {"Keyboard", "Mouse", "Gamepad"})
                {
                    auto device = std::ranges::find(scheme->Devices, family, &Keire::InputDeviceRequirement::Device);
                    bool required = device != scheme->Devices.end();
                    if (ui.Checkbox(std::string(family), required))
                    {
                        if (required)
                        {
                            m_Controller.RecordInputActionsUndo("Add Scheme Device");
                            scheme->Devices.push_back({std::string(family), false});
                        }
                        else if (scheme->Devices.size() > 1)
                        {
                            m_Controller.RecordInputActionsUndo("Remove Scheme Device");
                            scheme->Devices.erase(device);
                        }
                        else
                            m_Message = "A control scheme requires at least one device family.";
                    }
                    device = std::ranges::find(scheme->Devices, family, &Keire::InputDeviceRequirement::Device);
                    if (device != scheme->Devices.end())
                    {
                        ui.SameLine();
                        bool optional = device->Optional;
                        if (ui.Checkbox("Optional##" + std::string(family), optional))
                        {
                            m_Controller.RecordInputActionsUndo("Change Device Requirement");
                            device->Optional = optional;
                        }
                    }
                }
                ui.Separator();
                if (ui.Button("Delete Control Scheme"))
                {
                    m_Controller.RecordInputActionsUndo("Delete Control Scheme");
                    const auto removedGroup = scheme->BindingGroup;
                    inputDocument.ControlSchemes.erase(scheme);
                    for (auto& actionMap : inputDocument.ActionMaps)
                        for (auto& binding : actionMap.Bindings)
                            std::erase(binding.Groups, removedGroup);
                    selectedScheme = {};
                    if (!inputDocument.ActionMaps.empty())
                        selectedMap = inputDocument.ActionMaps.front().Id;
                }
                return;
            }
            if (!selectedAction)
            {
                auto name = map->Name;
                if (ui.InputText("Map Name", name))
                {
                    m_Controller.RecordInputActionsUndo();
                    map->Name = std::move(name);
                }
                bool alwaysReceive = map->CapturePolicy == Keire::InputCapturePolicy::AlwaysReceive;
                if (ui.Checkbox("Always Receive", alwaysReceive))
                {
                    m_Controller.RecordInputActionsUndo();
                    map->CapturePolicy = alwaysReceive ? Keire::InputCapturePolicy::AlwaysReceive
                                                       : Keire::InputCapturePolicy::RespectUiCapture;
                }
            }
            else
            {
                auto action = std::ranges::find(map->Actions, selectedAction, &Keire::InputActionDefinition::Id);
                if (action != map->Actions.end())
                {
                    auto name = action->Name;
                    if (ui.InputText("Action Name", name))
                    {
                        m_Controller.RecordInputActionsUndo();
                        action->Name = std::move(name);
                    }
                    const auto actionTypeName = [](const Keire::InputActionType type)
                    {
                        switch (type)
                        {
                        case Keire::InputActionType::Button:
                            return "Button";
                        case Keire::InputActionType::Value:
                            return "Value";
                        case Keire::InputActionType::PassThrough:
                            return "Pass Through";
                        }
                        return "Unknown";
                    };
                    if (auto combo = ui.BeginCombo("Action Type", actionTypeName(action->Type)); combo)
                    {
                        for (const auto type : {Keire::InputActionType::Button, Keire::InputActionType::Value,
                                                Keire::InputActionType::PassThrough})
                        {
                            if (ui.Selectable(actionTypeName(type), action->Type == type) && action->Type != type)
                            {
                                m_Controller.RecordInputActionsUndo("Change Action Type");
                                action->Type = type;
                                if (type == Keire::InputActionType::Button)
                                {
                                    action->ValueType = Keire::InputValueType::Boolean;
                                    std::erase_if(map->Bindings,
                                                  [&](const auto& binding)
                                                  {
                                                      return binding.Action == action->Id &&
                                                             (!binding.Composite.empty() ||
                                                              !binding.CompositePart.empty());
                                                  });
                                }
                                if (type == Keire::InputActionType::PassThrough)
                                {
                                    action->Interactions.clear();
                                    for (auto& binding : map->Bindings)
                                        if (binding.Action == action->Id)
                                            binding.Interactions.clear();
                                }
                            }
                        }
                    }
                    const auto valueTypeName = [](const Keire::InputValueType type)
                    {
                        switch (type)
                        {
                        case Keire::InputValueType::Boolean:
                            return "Boolean";
                        case Keire::InputValueType::Axis1D:
                            return "Axis (1D)";
                        case Keire::InputValueType::Axis2D:
                            return "Vector (2D)";
                        }
                        return "Unknown";
                    };
                    if (auto disabled = ui.BeginDisabled(action->Type == Keire::InputActionType::Button); disabled)
                    {
                        if (auto combo = ui.BeginCombo("Value Type", valueTypeName(action->ValueType)); combo)
                        {
                            for (const auto type : {Keire::InputValueType::Boolean, Keire::InputValueType::Axis1D,
                                                    Keire::InputValueType::Axis2D})
                            {
                                if (ui.Selectable(valueTypeName(type), action->ValueType == type) &&
                                    action->ValueType != type)
                                {
                                    m_Controller.RecordInputActionsUndo("Change Action Value Type");
                                    action->ValueType = type;
                                    std::erase_if(map->Bindings,
                                                  [&](const auto& binding)
                                                  {
                                                      if (binding.Action != action->Id)
                                                          return false;
                                                      return !binding.Composite.empty() ||
                                                             !binding.CompositePart.empty();
                                                  });
                                }
                            }
                        }
                    }
                    if (action->Type != Keire::InputActionType::PassThrough)
                        drawBehaviors("INTERACTIONS", action->Interactions, true);
                    drawBehaviors("PROCESSORS", action->Processors, false);
                }
                if (selectedBinding)
                {
                    auto binding =
                        std::ranges::find(map->Bindings, selectedBinding, &Keire::InputBindingDefinition::Id);
                    if (binding != map->Bindings.end())
                    {
                        auto name = binding->Name;
                        if (ui.InputText("Binding Name", name))
                        {
                            m_Controller.RecordInputActionsUndo();
                            binding->Name = std::move(name);
                        }
                        if (!binding->Composite.empty())
                        {
                            ui.TextColored(theme.MutedText, "Composite");
                            ui.Text(binding->Composite == "Axis1D" ? "1D Axis" : "2D Vector");
                        }
                        else
                        {
                            if (!binding->CompositePart.empty())
                            {
                                ui.TextColored(theme.MutedText, "Composite Part");
                                ui.Text(binding->CompositePart);
                            }
                            if (auto controls = ui.BeginCombo(
                                    "Control Browser", binding->Path.empty() ? "Choose a control..." : binding->Path);
                                controls)
                            {
                                for (const std::string_view path :
                                     {"<Keyboard>/space", "<Keyboard>/enter", "<Keyboard>/escape", "<Keyboard>/w",
                                      "<Keyboard>/a", "<Keyboard>/s", "<Keyboard>/d", "<Mouse>/position",
                                      "<Mouse>/delta", "<Mouse>/leftButton", "<Mouse>/rightButton", "<Mouse>/scroll",
                                      "<Gamepad>/leftStick", "<Gamepad>/rightStick", "<Gamepad>/buttonSouth",
                                      "<Gamepad>/buttonEast", "<Gamepad>/leftTrigger", "<Gamepad>/rightTrigger"})
                                {
                                    if (ui.Selectable(path, binding->Path == path))
                                    {
                                        m_Controller.RecordInputActionsUndo("Choose Control Path");
                                        binding->Path = path;
                                    }
                                }
                            }
                            auto path = binding->Path;
                            if (ui.InputText("Control Path", path))
                            {
                                m_Controller.RecordInputActionsUndo("Change Control Path");
                                binding->Path = std::move(path);
                            }
                        }
                        ui.Separator();
                        ui.TextColored(theme.Accent, "BINDING GROUPS");
                        for (const auto& scheme : inputDocument.ControlSchemes)
                        {
                            bool included =
                                std::ranges::find(binding->Groups, scheme.BindingGroup) != binding->Groups.end();
                            if (ui.Checkbox(scheme.Name + "##" + scheme.Id.ToString(), included))
                            {
                                m_Controller.RecordInputActionsUndo("Change Binding Group");
                                if (included)
                                    binding->Groups.push_back(scheme.BindingGroup);
                                else
                                    std::erase(binding->Groups, scheme.BindingGroup);
                            }
                        }
                        if (action != map->Actions.end() && action->Type != Keire::InputActionType::PassThrough)
                            drawBehaviors("BINDING INTERACTIONS", binding->Interactions, true);
                        drawBehaviors("BINDING PROCESSORS", binding->Processors, false);
                        if (binding->Composite.empty() && !m_Rebind && ui.Button("Listen"))
                        {
                            try
                            {
                                if (!inputContext)
                                    throw std::runtime_error("The runtime input context is not ready.");
                                const auto input = m_Controller.InputSystem();
                                if (!input)
                                    throw std::runtime_error("The input system is not available.");
                                m_RebindContext = input->CreateActionContext(inputDocument, inputContext->User(),
                                                                             Keire::InputContextRole::EditorControl);
                                m_Rebind = input->BeginInteractiveRebind(m_RebindContext, binding->Id);
                                m_Message = "Listening for a control...";
                            }
                            catch (const std::exception& error)
                            {
                                m_RebindContext.Reset();
                                m_Message = error.what();
                                m_Controller.ReportInputActionsError(m_Message);
                            }
                        }
                        if (m_Rebind)
                        {
                            const auto status = m_Rebind->Status();
                            if (status == Keire::RebindStatus::Listening)
                            {
                                ui.Text("Listening... " + std::to_string(m_Rebind->RemainingSeconds()) + "s");
                                ui.ProgressBar(static_cast<float>(1.0 - m_Rebind->RemainingSeconds() / 5.0),
                                               {0.0F, 4.0F}, {});
                            }
                            else if (status == Keire::RebindStatus::Candidate)
                            {
                                ui.TextColored(theme.Success, "Candidate: " + m_Rebind->CandidatePath());
                                const auto conflicts = m_Rebind->Conflicts();
                                if (!conflicts.empty())
                                    ui.TextColored(theme.Warning,
                                                   std::to_string(conflicts.size()) + " binding conflict(s).");
                                if (ui.Button(conflicts.empty() ? "Accept" : "Replace"))
                                {
                                    const auto targetBinding = m_Rebind->TargetBinding();
                                    const auto candidatePath = m_Rebind->CandidatePath();
                                    m_Controller.RecordInputActionsUndo();
                                    if (!conflicts.empty())
                                    {
                                        std::erase_if(map->Bindings,
                                                      [&](const auto& candidate)
                                                      {
                                                          return std::ranges::any_of(
                                                              conflicts, [&](const auto& conflict)
                                                              { return conflict.Binding == candidate.Id; });
                                                      });
                                    }
                                    binding = std::ranges::find(map->Bindings, targetBinding,
                                                                &Keire::InputBindingDefinition::Id);
                                    if (binding != map->Bindings.end())
                                        binding->Path = candidatePath;
                                    m_Rebind->Apply(conflicts.empty() ? Keire::RebindConflictResolution::KeepBoth
                                                                      : Keire::RebindConflictResolution::Replace);
                                    if (m_Rebind->Status() == Keire::RebindStatus::Completed)
                                    {
                                        m_Rebind.Reset();
                                        m_RebindContext.Reset();
                                    }
                                }
                                ui.SameLine();
                                if (!conflicts.empty() && ui.Button("Keep Both"))
                                {
                                    const auto targetBinding = m_Rebind->TargetBinding();
                                    const auto candidatePath = m_Rebind->CandidatePath();
                                    m_Controller.RecordInputActionsUndo();
                                    const auto target = std::ranges::find(map->Bindings, targetBinding,
                                                                          &Keire::InputBindingDefinition::Id);
                                    if (target != map->Bindings.end())
                                        target->Path = candidatePath;
                                    m_Rebind->Apply(Keire::RebindConflictResolution::KeepBoth);
                                    if (m_Rebind->Status() == Keire::RebindStatus::Completed)
                                    {
                                        m_Rebind.Reset();
                                        m_RebindContext.Reset();
                                    }
                                }
                                ui.SameLine();
                                if (ui.Button("Cancel"))
                                {
                                    m_Rebind->Cancel();
                                    m_Rebind.Reset();
                                    m_RebindContext.Reset();
                                }
                            }
                            else
                            {
                                m_Rebind.Reset();
                                m_RebindContext.Reset();
                            }
                        }
                    }
                }
                if (m_LiveMonitor && inputContext)
                {
                    ui.Separator();
                    ui.TextColored(theme.Accent, "LIVE VALUE");
                    const auto handle = inputContext->FindAction(selectedAction);
                    if (handle)
                    {
                        const auto value = handle.Value();
                        ui.Text("Phase " + std::to_string(static_cast<int>(handle.Phase())) + "  [" +
                                std::to_string(value.X) + ", " + std::to_string(value.Y) + "]");
                    }
                    if (const auto input = m_Controller.InputSystem())
                    {
                        ui.TextColored(theme.MutedText, std::to_string(input->Devices().size()) + " device(s), " +
                                                            std::to_string(input->Users().size()) + " user(s)");
                    }
                }
            }
        }

        document.SetSelection(selectedMap, selectedScheme, selectedAction, selectedBinding);
        if (Keire::InputActionAsset::Encode(inputDocument) != originalBytes)
            document.ReplaceDefinition(std::move(inputDocument));
    }

    void InputActionsPanel::ResetTransientState() noexcept
    {
        m_Rebind.Reset();
        m_RebindContext.Reset();
    }
} // namespace KeireEditor
