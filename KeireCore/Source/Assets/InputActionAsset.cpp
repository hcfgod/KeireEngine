#include "Keire/Assets/InputActionAsset.h"

#include "Keire/Assets/AssetPipeline.h"
#include "Keire/Assets/AssetSystem.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ranges>
#include <stdexcept>
#include <unordered_set>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        constexpr std::size_t MaximumDocumentBytes = 4ULL * 1024ULL * 1024U;

        [[nodiscard]] AssetId Id(const std::string_view value) { return AssetId::Parse(value); }

        [[nodiscard]] const char* ToString(const InputActionType value) noexcept
        {
            switch (value)
            {
            case InputActionType::Button:
                return "Button";
            case InputActionType::Value:
                return "Value";
            case InputActionType::PassThrough:
                return "PassThrough";
            }
            return "Button";
        }

        [[nodiscard]] const char* ToString(const InputValueType value) noexcept
        {
            switch (value)
            {
            case InputValueType::Boolean:
                return "Boolean";
            case InputValueType::Axis1D:
                return "Axis1D";
            case InputValueType::Axis2D:
                return "Axis2D";
            }
            return "Boolean";
        }

        [[nodiscard]] InputActionType ParseActionType(const std::string_view value)
        {
            if (value == "Button")
                return InputActionType::Button;
            if (value == "Value")
                return InputActionType::Value;
            if (value == "PassThrough")
                return InputActionType::PassThrough;
            throw std::runtime_error("Input action contains an unsupported action type.");
        }

        [[nodiscard]] InputValueType ParseValueType(const std::string_view value)
        {
            if (value == "Boolean")
                return InputValueType::Boolean;
            if (value == "Axis1D")
                return InputValueType::Axis1D;
            if (value == "Axis2D")
                return InputValueType::Axis2D;
            throw std::runtime_error("Input action contains an unsupported value type.");
        }

        [[nodiscard]] std::vector<InputBehaviorDefinition> ParseBehaviors(const Json& values)
        {
            if (!values.is_array())
                throw std::runtime_error("Input behavior list must be an array.");
            std::vector<InputBehaviorDefinition> result;
            result.reserve(values.size());
            for (const auto& value : values)
            {
                InputBehaviorDefinition behavior;
                behavior.Name = value.at("name").get<std::string>();
                if (value.contains("parameters"))
                {
                    if (!value["parameters"].is_object())
                        throw std::runtime_error("Input behavior parameters must be an object.");
                    for (const auto& [name, parameter] : value["parameters"].items())
                        behavior.Parameters.push_back({name, parameter.get<double>()});
                }
                result.push_back(std::move(behavior));
            }
            return result;
        }

        [[nodiscard]] Json EncodeBehaviors(const std::vector<InputBehaviorDefinition>& values)
        {
            Json result = Json::array();
            for (const auto& behavior : values)
            {
                Json parameters = Json::object();
                for (const auto& parameter : behavior.Parameters)
                    parameters[parameter.Name] = parameter.Value;
                result.push_back({{"name", behavior.Name}, {"parameters", std::move(parameters)}});
            }
            return result;
        }

        [[nodiscard]] bool ValidControlPath(const std::string_view value) noexcept
        {
            if (value.size() < 4 || value.front() != '<')
                return false;
            const auto separator = value.find('/', 2);
            return separator != std::string_view::npos && separator > 2 && value[separator - 1] == '>' &&
                   separator + 1 < value.size() && value.find_first_of("\r\n\t") == std::string_view::npos;
        }

        void ValidateBehaviors(const std::vector<InputBehaviorDefinition>& values, const bool interactions)
        {
            static const std::unordered_set<std::string> Interactions{"Press", "Tap", "Hold", "MultiTap"};
            static const std::unordered_set<std::string> Processors{"Deadzone", "Scale", "Invert", "Normalize"};
            std::unordered_set<std::string> names;
            if (values.size() > 8)
                throw std::invalid_argument("Input actions support at most eight interactions or processors.");
            for (const auto& value : values)
            {
                if (value.Name.empty() || !(interactions ? Interactions : Processors).contains(value.Name) ||
                    !names.insert(value.Name).second)
                    throw std::invalid_argument("Input behavior is unsupported, empty, or duplicated.");
                std::unordered_set<std::string> parameters;
                if (value.Parameters.size() > 16)
                    throw std::invalid_argument("Input behaviors support at most sixteen parameters.");
                for (const auto& parameter : value.Parameters)
                {
                    if (parameter.Name.empty() || !std::isfinite(parameter.Value) ||
                        !parameters.insert(parameter.Name).second)
                        throw std::invalid_argument("Input behavior parameter is invalid or duplicated.");
                }
                const auto parameter = [&](const std::string_view name, const double fallback)
                {
                    const auto found = std::ranges::find(value.Parameters, name, &InputParameter::Name);
                    return found == value.Parameters.end() ? fallback : found->Value;
                };
                const auto only = [&](const std::initializer_list<std::string_view> allowed)
                {
                    return std::ranges::all_of(value.Parameters, [&](const auto& item)
                                               { return std::ranges::find(allowed, item.Name) != allowed.end(); });
                };
                if ((value.Name == "Press" && (!only({"pressPoint"}) || parameter("pressPoint", 0.5) <= 0.0 ||
                                               parameter("pressPoint", 0.5) > 1.0)) ||
                    (value.Name == "Tap" && (!only({"duration", "pressPoint"}) || parameter("duration", 0.2) <= 0.0 ||
                                             parameter("duration", 0.2) > 10.0 || parameter("pressPoint", 0.5) <= 0.0 ||
                                             parameter("pressPoint", 0.5) > 1.0)) ||
                    (value.Name == "Hold" &&
                     (!only({"duration", "pressPoint"}) || parameter("duration", 0.4) <= 0.0 ||
                      parameter("duration", 0.4) > 60.0 || parameter("pressPoint", 0.5) <= 0.0 ||
                      parameter("pressPoint", 0.5) > 1.0)) ||
                    (value.Name == "MultiTap" &&
                     (!only({"duration", "delay", "count", "pressPoint"}) || parameter("duration", 0.2) <= 0.0 ||
                      parameter("duration", 0.2) > 10.0 || parameter("delay", 0.75) <= 0.0 ||
                      parameter("delay", 0.75) > 10.0 || parameter("count", 2.0) < 2.0 ||
                      parameter("count", 2.0) > 16.0 || parameter("pressPoint", 0.5) <= 0.0 ||
                      parameter("pressPoint", 0.5) > 1.0 ||
                      std::floor(parameter("count", 2.0)) != parameter("count", 2.0))) ||
                    (value.Name == "Deadzone" && (!only({"minimum", "maximum"}) || parameter("minimum", 0.125) < 0.0 ||
                                                  parameter("minimum", 0.125) >= parameter("maximum", 0.925) ||
                                                  parameter("maximum", 0.925) > 1.0)) ||
                    (value.Name == "Scale" && (!only({"x", "y"}) || std::abs(parameter("x", 1.0)) > 1000.0 ||
                                               std::abs(parameter("y", 1.0)) > 1000.0)) ||
                    (value.Name == "Invert" && !only({"x", "y"})) ||
                    (value.Name == "Normalize" && !value.Parameters.empty()))
                    throw std::invalid_argument("Input behavior parameters are unsupported or out of range.");
            }
        }

        [[nodiscard]] std::size_t StringBytes(const InputActionAssetDefinition& definition) noexcept
        {
            std::size_t result = definition.Name.size();
            for (const auto& scheme : definition.ControlSchemes)
            {
                result += scheme.Name.size() + scheme.BindingGroup.size();
                for (const auto& device : scheme.Devices)
                    result += device.Device.size();
            }
            for (const auto& map : definition.ActionMaps)
            {
                result += map.Name.size();
                for (const auto& action : map.Actions)
                    result += action.Name.size();
                for (const auto& binding : map.Bindings)
                {
                    result += binding.Name.size() + binding.Path.size() + binding.Composite.size() +
                              binding.CompositePart.size();
                    for (const auto& group : binding.Groups)
                        result += group.size();
                }
            }
            return result;
        }

        InputActionDefinition Action(const std::string_view id, std::string name, const InputActionType type,
                                     const InputValueType valueType)
        {
            return {Id(id), std::move(name), type, valueType};
        }

        InputBindingDefinition Binding(const std::string_view id, const AssetId action, std::string path,
                                       std::vector<std::string> groups, std::string part = {})
        {
            InputBindingDefinition result;
            result.Id = Id(id);
            result.Action = action;
            result.Path = std::move(path);
            result.CompositePart = std::move(part);
            result.Groups = std::move(groups);
            return result;
        }
    } // namespace

    InputActionAsset::InputActionAsset(InputActionAssetDefinition definition) : m_Definition(std::move(definition))
    {
        if (m_Definition.Name.empty() && m_Definition.ActionMaps.empty() && m_Definition.ControlSchemes.empty())
        {
            m_Definition.SchemaVersion = 1;
            return;
        }
        Validate(m_Definition);
        m_ResidentBytes = sizeof(*this) + StringBytes(m_Definition);
    }

    std::size_t InputActionAsset::ResidentBytes() const noexcept { return m_ResidentBytes; }

    const InputActionMapDefinition* InputActionAsset::FindMap(const AssetId id) const noexcept
    {
        const auto found = std::ranges::find(m_Definition.ActionMaps, id, &InputActionMapDefinition::Id);
        return found == m_Definition.ActionMaps.end() ? nullptr : &*found;
    }

    const InputActionMapDefinition* InputActionAsset::FindMap(const std::string_view name) const noexcept
    {
        const auto found = std::ranges::find(m_Definition.ActionMaps, name, &InputActionMapDefinition::Name);
        return found == m_Definition.ActionMaps.end() ? nullptr : &*found;
    }

    const InputActionDefinition* InputActionAsset::FindAction(const AssetId id) const noexcept
    {
        for (const auto& map : m_Definition.ActionMaps)
        {
            const auto found = std::ranges::find(map.Actions, id, &InputActionDefinition::Id);
            if (found != map.Actions.end())
                return &*found;
        }
        return nullptr;
    }

    Ref<InputActionAsset> InputActionAsset::Decode(const std::span<const std::byte> bytes)
    {
        if (bytes.size() > MaximumDocumentBytes)
            throw std::runtime_error("Input action asset exceeds the 4 MiB safety limit.");
        try
        {
            const auto document = Json::parse(reinterpret_cast<const char*>(bytes.data()),
                                              reinterpret_cast<const char*>(bytes.data() + bytes.size()));
            if (!document.is_object() || document.value("schemaVersion", 0) != 1)
                throw std::runtime_error("Input action asset has an unsupported schema.");

            InputActionAssetDefinition definition;
            definition.Name = document.at("name").get<std::string>();
            for (const auto& sourceScheme : document.at("controlSchemes"))
            {
                InputControlSchemeDefinition scheme;
                scheme.Id = Id(sourceScheme.at("id").get<std::string>());
                scheme.Name = sourceScheme.at("name").get<std::string>();
                scheme.BindingGroup = sourceScheme.at("bindingGroup").get<std::string>();
                for (const auto& sourceDevice : sourceScheme.at("devices"))
                    scheme.Devices.push_back(
                        {sourceDevice.at("device").get<std::string>(), sourceDevice.value("optional", false)});
                definition.ControlSchemes.push_back(std::move(scheme));
            }
            for (const auto& sourceMap : document.at("actionMaps"))
            {
                InputActionMapDefinition map;
                map.Id = Id(sourceMap.at("id").get<std::string>());
                map.Name = sourceMap.at("name").get<std::string>();
                const auto capture = sourceMap.value("capturePolicy", std::string("RespectUiCapture"));
                if (capture == "RespectUiCapture")
                    map.CapturePolicy = InputCapturePolicy::RespectUiCapture;
                else if (capture == "AlwaysReceive")
                    map.CapturePolicy = InputCapturePolicy::AlwaysReceive;
                else
                    throw std::runtime_error("Input action map contains an unsupported capture policy.");
                for (const auto& sourceAction : sourceMap.at("actions"))
                {
                    InputActionDefinition action;
                    action.Id = Id(sourceAction.at("id").get<std::string>());
                    action.Name = sourceAction.at("name").get<std::string>();
                    action.Type = ParseActionType(sourceAction.at("type").get<std::string>());
                    action.ValueType = ParseValueType(sourceAction.at("valueType").get<std::string>());
                    action.Interactions = ParseBehaviors(sourceAction.value("interactions", Json::array()));
                    action.Processors = ParseBehaviors(sourceAction.value("processors", Json::array()));
                    map.Actions.push_back(std::move(action));
                }
                for (const auto& sourceBinding : sourceMap.at("bindings"))
                {
                    InputBindingDefinition binding;
                    binding.Id = Id(sourceBinding.at("id").get<std::string>());
                    binding.Action = Id(sourceBinding.at("action").get<std::string>());
                    binding.Name = sourceBinding.value("name", std::string{});
                    binding.Path = sourceBinding.value("path", std::string{});
                    binding.Composite = sourceBinding.value("composite", std::string{});
                    binding.CompositePart = sourceBinding.value("part", std::string{});
                    binding.Groups = sourceBinding.value("groups", std::vector<std::string>{});
                    binding.Interactions = ParseBehaviors(sourceBinding.value("interactions", Json::array()));
                    binding.Processors = ParseBehaviors(sourceBinding.value("processors", Json::array()));
                    map.Bindings.push_back(std::move(binding));
                }
                definition.ActionMaps.push_back(std::move(map));
            }
            return CreateRef<InputActionAsset>(std::move(definition));
        }
        catch (const Json::exception& error)
        {
            throw std::runtime_error(std::string("Input action asset JSON is malformed: ") + error.what());
        }
    }

    std::vector<std::byte> InputActionAsset::Encode(const InputActionAssetDefinition& definition)
    {
        Validate(definition);
        Json schemes = Json::array();
        for (const auto& scheme : definition.ControlSchemes)
        {
            Json devices = Json::array();
            for (const auto& device : scheme.Devices)
                devices.push_back({{"device", device.Device}, {"optional", device.Optional}});
            schemes.push_back({{"id", scheme.Id.ToString()},
                               {"name", scheme.Name},
                               {"bindingGroup", scheme.BindingGroup},
                               {"devices", std::move(devices)}});
        }
        Json maps = Json::array();
        for (const auto& map : definition.ActionMaps)
        {
            Json actions = Json::array();
            for (const auto& action : map.Actions)
                actions.push_back({{"id", action.Id.ToString()},
                                   {"name", action.Name},
                                   {"type", ToString(action.Type)},
                                   {"valueType", ToString(action.ValueType)},
                                   {"interactions", EncodeBehaviors(action.Interactions)},
                                   {"processors", EncodeBehaviors(action.Processors)}});
            Json bindings = Json::array();
            for (const auto& binding : map.Bindings)
                bindings.push_back({{"id", binding.Id.ToString()},
                                    {"action", binding.Action.ToString()},
                                    {"name", binding.Name},
                                    {"path", binding.Path},
                                    {"composite", binding.Composite},
                                    {"part", binding.CompositePart},
                                    {"groups", binding.Groups},
                                    {"interactions", EncodeBehaviors(binding.Interactions)},
                                    {"processors", EncodeBehaviors(binding.Processors)}});
            maps.push_back(
                {{"id", map.Id.ToString()},
                 {"name", map.Name},
                 {"capturePolicy",
                  map.CapturePolicy == InputCapturePolicy::AlwaysReceive ? "AlwaysReceive" : "RespectUiCapture"},
                 {"actions", std::move(actions)},
                 {"bindings", std::move(bindings)}});
        }
        const Json document{{"schemaVersion", 1},
                            {"name", definition.Name},
                            {"controlSchemes", std::move(schemes)},
                            {"actionMaps", std::move(maps)}};
        const auto text = document.dump(2) + '\n';
        std::vector<std::byte> result(text.size());
        std::memcpy(result.data(), text.data(), text.size());
        return result;
    }

    void InputActionAsset::Validate(const InputActionAssetDefinition& definition)
    {
        if (definition.SchemaVersion != 1 || definition.Name.empty() || definition.Name.size() > 256)
            throw std::invalid_argument("Input action asset name or schema is invalid.");
        if (definition.ControlSchemes.size() > 16 || definition.ActionMaps.size() > 128)
            throw std::invalid_argument("Input action asset exceeds the supported map or scheme limit.");
        std::unordered_set<AssetId> identities;
        std::unordered_set<std::string> schemeNames;
        std::unordered_set<std::string> groups;
        for (const auto& scheme : definition.ControlSchemes)
        {
            if (!scheme.Id || scheme.Name.empty() || scheme.Name.size() > 128 || scheme.BindingGroup.empty() ||
                scheme.BindingGroup.size() > 128 || scheme.Devices.empty() || scheme.Devices.size() > 3 ||
                !identities.insert(scheme.Id).second || !schemeNames.insert(scheme.Name).second ||
                !groups.insert(scheme.BindingGroup).second)
                throw std::invalid_argument("Input control scheme is invalid or duplicated.");
            for (const auto& device : scheme.Devices)
            {
                if ((device.Device != "Keyboard" && device.Device != "Mouse" && device.Device != "Gamepad") ||
                    std::ranges::count(scheme.Devices, device.Device, &InputDeviceRequirement::Device) != 1)
                    throw std::invalid_argument("Input control scheme contains an unsupported device family.");
            }
        }
        std::unordered_set<std::string> mapNames;
        for (const auto& map : definition.ActionMaps)
        {
            if (!map.Id || map.Name.empty() || map.Name.size() > 128 || map.Actions.size() > 1024 ||
                map.Bindings.size() > 4096 || !identities.insert(map.Id).second || !mapNames.insert(map.Name).second)
                throw std::invalid_argument("Input action map is invalid or duplicated.");
            std::unordered_set<AssetId> actions;
            std::unordered_set<std::string> actionNames;
            for (const auto& action : map.Actions)
            {
                if (!action.Id || action.Name.empty() || action.Name.size() > 128 ||
                    !identities.insert(action.Id).second || !actions.insert(action.Id).second ||
                    !actionNames.insert(action.Name).second ||
                    (action.Type == InputActionType::Button && action.ValueType != InputValueType::Boolean) ||
                    (action.Type == InputActionType::PassThrough && !action.Interactions.empty()))
                    throw std::invalid_argument("Input action is invalid, duplicated, or type-incompatible.");
                ValidateBehaviors(action.Interactions, true);
                ValidateBehaviors(action.Processors, false);
            }
            std::unordered_set<std::string> bindingNames;
            AssetId compositeAction;
            std::string compositeType;
            for (auto bindingIterator = map.Bindings.begin(); bindingIterator != map.Bindings.end(); ++bindingIterator)
            {
                const auto& binding = *bindingIterator;
                const bool compositeRoot = !binding.Composite.empty();
                const auto action = std::ranges::find(map.Actions, binding.Action, &InputActionDefinition::Id);
                if (!binding.Id || binding.Name.size() > 128 || binding.Path.size() > 512 ||
                    !identities.insert(binding.Id).second || !actions.contains(binding.Action) ||
                    (compositeRoot && binding.Composite != "Axis1D" && binding.Composite != "Vector2") ||
                    (!compositeRoot && !ValidControlPath(binding.Path)) ||
                    (!binding.CompositePart.empty() && compositeRoot) || (compositeRoot && !binding.Path.empty()) ||
                    action == map.Actions.end())
                    throw std::invalid_argument("Input binding is invalid or references an unknown action.");
                if (compositeRoot)
                {
                    compositeAction = binding.Action;
                    compositeType = binding.Composite;
                    if ((compositeType == "Axis1D" && action->ValueType != InputValueType::Axis1D) ||
                        (compositeType == "Vector2" && action->ValueType != InputValueType::Axis2D))
                        throw std::invalid_argument("Input composite type is incompatible with its action value.");
                    std::unordered_set<std::string> parts;
                    std::size_t partCount = 0;
                    for (auto part = std::next(bindingIterator);
                         part != map.Bindings.end() && !part->CompositePart.empty(); ++part)
                    {
                        ++partCount;
                        parts.insert(part->CompositePart);
                    }
                    const bool completeAxis = compositeType == "Axis1D" && partCount == 2 &&
                                              parts == std::unordered_set<std::string>{"Negative", "Positive"};
                    const bool completeVector = compositeType == "Vector2" && partCount == 4 &&
                                                parts == std::unordered_set<std::string>{"Up", "Down", "Left", "Right"};
                    if (!completeAxis && !completeVector)
                        throw std::invalid_argument("Input composite parts must be complete, contiguous, and unique.");
                }
                else if (!binding.CompositePart.empty())
                {
                    const bool axisPart = binding.CompositePart == "Negative" || binding.CompositePart == "Positive";
                    const bool vectorPart = binding.CompositePart == "Up" || binding.CompositePart == "Down" ||
                                            binding.CompositePart == "Left" || binding.CompositePart == "Right";
                    if (binding.Action != compositeAction || (compositeType == "Axis1D" && !axisPart) ||
                        (compositeType == "Vector2" && !vectorPart))
                        throw std::invalid_argument("Input composite part is dangling or unsupported.");
                }
                else
                {
                    compositeAction = {};
                    compositeType.clear();
                }
                if (!binding.Name.empty() && !bindingNames.insert(binding.Name).second)
                    throw std::invalid_argument("Named input bindings must be unique within their map.");
                for (const auto& group : binding.Groups)
                {
                    if (!groups.contains(group))
                        throw std::invalid_argument("Input binding references an unknown control-scheme group.");
                }
                ValidateBehaviors(binding.Interactions, true);
                ValidateBehaviors(binding.Processors, false);
                if (action->Type == InputActionType::PassThrough && !binding.Interactions.empty())
                    throw std::invalid_argument("Pass-through input bindings cannot use interactions.");
            }
        }
    }

    InputActionAssetDefinition InputActionAsset::DefaultDefinition()
    {
        InputActionAssetDefinition result;
        result.Name = "DefaultInput";
        result.ControlSchemes = {
            {Id("d0117eb9-bf9b-49c4-95fc-82c4046ab101"),
             "Keyboard & Mouse",
             "KeyboardMouse",
             {{"Keyboard", false}, {"Mouse", false}}},
            {Id("2e8eb4db-4578-4e0d-a235-5e26686daf02"), "Gamepad", "Gamepad", {{"Gamepad", false}}}};
        InputActionMapDefinition player{Id("a6b6db76-6436-4aa4-b96a-a72a0f987101"), "Player"};
        player.Actions = {
            Action("c220a08c-d4dd-433f-b9f4-c2b5b6757101", "Move", InputActionType::Value, InputValueType::Axis2D),
            Action("622ffac4-6370-489e-8276-74376ef57102", "Look", InputActionType::Value, InputValueType::Axis2D),
            Action("1a7b9aa8-1d99-4ea0-95ec-bce2b1227103", "Fire", InputActionType::Button, InputValueType::Boolean)};
        const auto move = player.Actions[0].Id;
        auto moveRoot = Binding("04cde53d-42c8-48ab-b3a5-e1a0d3007201", move, {}, {"KeyboardMouse"});
        moveRoot.Composite = "Vector2";
        moveRoot.Name = "WASD";
        player.Bindings = {
            moveRoot,
            Binding("bc2c4422-4938-4b1c-a03a-d07551277202", move, "<Keyboard>/w", {"KeyboardMouse"}, "Up"),
            Binding("c61a405e-a213-49c6-a5a6-b062e2bc7203", move, "<Keyboard>/s", {"KeyboardMouse"}, "Down"),
            Binding("f8d7929e-b3dd-4317-bfd7-e5ab482a7204", move, "<Keyboard>/a", {"KeyboardMouse"}, "Left"),
            Binding("d3464d73-3634-4657-9b63-135fc6a07205", move, "<Keyboard>/d", {"KeyboardMouse"}, "Right"),
            Binding("733a3e68-7ba4-4081-82b8-e4a6281f7206", move, "<Gamepad>/leftStick", {"Gamepad"}),
            Binding("36f15973-bc70-470f-a758-c30c23587207", player.Actions[1].Id, "<Mouse>/delta", {"KeyboardMouse"}),
            Binding("6fd6c682-f65b-4270-9743-d69de10a7208", player.Actions[1].Id, "<Gamepad>/rightStick", {"Gamepad"}),
            Binding("e3afdb73-a3ec-43a7-96bb-871fe3007209", player.Actions[2].Id, "<Mouse>/leftButton",
                    {"KeyboardMouse"}),
            Binding("2e63c258-6aa8-4d2f-99f4-c034be7d7210", player.Actions[2].Id, "<Gamepad>/rightTrigger",
                    {"Gamepad"})};

        InputActionMapDefinition ui{Id("782ef248-38bd-4931-831c-ce2bfa887201"), "UI",
                                    InputCapturePolicy::AlwaysReceive};
        ui.Actions = {
            Action("9a4e4ed3-a7e6-48d0-931d-1939715b7301", "Navigate", InputActionType::Value, InputValueType::Axis2D),
            Action("103bbf24-6584-4bee-b7b1-e510cba27302", "Submit", InputActionType::Button, InputValueType::Boolean),
            Action("606e853c-92ae-4f51-9f3f-27231a317303", "Cancel", InputActionType::Button, InputValueType::Boolean),
            Action("72ee241d-b1d5-4f0e-a411-1324a0d27304", "Point", InputActionType::PassThrough,
                   InputValueType::Axis2D),
            Action("f39d8912-2ae8-472c-a833-ab329eb97305", "Click", InputActionType::Button, InputValueType::Boolean),
            Action("ed633def-e7f8-4be5-a380-702f73fb7306", "Scroll", InputActionType::PassThrough,
                   InputValueType::Axis2D)};
        auto navigation = Binding("a327a4af-8f4e-4419-9acc-bdff662f7401", ui.Actions[0].Id, {}, {"KeyboardMouse"});
        navigation.Composite = "Vector2";
        navigation.Name = "Arrows";
        ui.Bindings = {
            navigation,
            Binding("c40b3ad2-f560-47e5-b026-9ee42d677402", ui.Actions[0].Id, "<Keyboard>/upArrow", {"KeyboardMouse"},
                    "Up"),
            Binding("cf3ea809-e942-4572-9c86-caa99fde7403", ui.Actions[0].Id, "<Keyboard>/downArrow", {"KeyboardMouse"},
                    "Down"),
            Binding("1e3832f8-55e1-4038-a465-921d4e4d7404", ui.Actions[0].Id, "<Keyboard>/leftArrow", {"KeyboardMouse"},
                    "Left"),
            Binding("8f113c2f-ddb5-49d0-9b99-574da92c7405", ui.Actions[0].Id, "<Keyboard>/rightArrow",
                    {"KeyboardMouse"}, "Right"),
            Binding("27a255f1-3bce-4a2e-b110-17768a637406", ui.Actions[0].Id, "<Gamepad>/dpad", {"Gamepad"}),
            Binding("bd446985-61d9-4f93-b4b0-486005177407", ui.Actions[1].Id, "<Keyboard>/enter", {"KeyboardMouse"}),
            Binding("26f89329-20a7-491f-a41b-1bf753367408", ui.Actions[1].Id, "<Gamepad>/buttonSouth", {"Gamepad"}),
            Binding("35954b9f-4cc9-4aa6-9903-51637f477409", ui.Actions[2].Id, "<Keyboard>/escape", {"KeyboardMouse"}),
            Binding("dcc6340f-437c-4465-a049-4f8a829b7410", ui.Actions[2].Id, "<Gamepad>/buttonEast", {"Gamepad"}),
            Binding("9a9af445-0099-45b5-9410-06c404167411", ui.Actions[3].Id, "<Mouse>/position", {"KeyboardMouse"}),
            Binding("ea824390-c6b7-49f7-a2c8-477fac827412", ui.Actions[4].Id, "<Mouse>/leftButton", {"KeyboardMouse"}),
            Binding("c0efb3dc-edce-4f34-8896-512098f77413", ui.Actions[5].Id, "<Mouse>/scroll", {"KeyboardMouse"})};
        result.ActionMaps = {std::move(player), std::move(ui)};
        return result;
    }

    InputActionAssetDefinition InputActionAsset::GameplayDefinition()
    {
        auto result = DefaultDefinition();
        result.Name = "GameplayInput";
        result.ActionMaps.resize(1);
        return result;
    }

    InputActionAssetDefinition InputActionAsset::UiDefinition()
    {
        auto result = DefaultDefinition();
        result.Name = "UiInput";
        result.ActionMaps.erase(result.ActionMaps.begin());
        return result;
    }

    AssetDecoderRegistration CreateInputActionAssetDecoder()
    {
        return {InputActionAsset::StaticType(), CreateRef<InputActionAsset>(),
                [](const std::span<const std::byte> bytes) -> Ref<Asset> { return InputActionAsset::Decode(bytes); }};
    }

    AssetImporterRegistration CreateInputActionAssetImporter()
    {
        return {"Keire.InputActions",
                1,
                InputActionAsset::StaticType(),
                {".keireinput"},
                [](const std::span<const std::byte> bytes)
                {
                    const auto parsed = InputActionAsset::Decode(bytes);
                    return InputActionAsset::Encode(parsed->Definition());
                }};
    }
} // namespace Keire
