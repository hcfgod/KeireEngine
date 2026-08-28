#pragma once

#include "Keire/Input/Input.h"

#include "KeireInternal/Input/InputContextState.h"
#include "KeireInternal/InputInternal.h"
#include "KeireInternal/WindowInternal.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Keire::Detail
{
    template <typename Range, typename Value> [[nodiscard]] bool Contains(const Range& range, const Value& value)
    {
        return std::ranges::find(range, value) != std::ranges::end(range);
    }

    struct RawControlEvent
    {
        InputDeviceId Device;
        InputDeviceType Type = InputDeviceType::Keyboard;
        std::string Path;
        InputValue Value;
        std::uint64_t Timestamp = 0;
        bool Press = false;
    };

    struct DeviceState
    {
        InputDeviceDescriptor Descriptor;
        SDL_JoystickID NativeId = 0;
        SDL_Gamepad* Gamepad = nullptr;
        std::unordered_map<std::string, InputValue> Controls;
        std::unordered_map<std::string, InputValue> PendingControls;
        std::unordered_set<std::string> PressedControls;
        std::unordered_set<std::string> ReleasedControls;
    };

    struct UserState
    {
        InputUserDescriptor Descriptor;
        bool SchemeLocked = false;
    };

    class InputRuntimeState final : public RefCounted
    {
      public:
        InputRuntimeState(InputSystemSpecification value, Ref<WindowSystem> windows, Ref<AssetSystem> assets,
                          Ref<EventBus> events)
            : Specification(std::move(value)), Windowing(std::move(windows)), Assets(std::move(assets)),
              Events(std::move(events)), OwnerThread(std::this_thread::get_id())
        {
            if (Specification.MaximumUsers == 0 || Specification.MaximumUsers > 16 ||
                Specification.EventCapacity == 0 || Specification.EventCapacity > 65536)
                throw std::invalid_argument("Input limits must be within the supported bounded ranges.");
            if (!Assets || !Windowing)
                throw std::invalid_argument("Enabled input requires AssetSystem and WindowSystem owners.");
            if (Specification.BindingOverrideDirectory.empty())
                Specification.BindingOverrideDirectory = "Library/UserSettings/InputBindings";
            EventsBuffer.reserve(Specification.EventCapacity);
            GamepadSubsystemInitialized = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
            AddFixedDevice(InputDeviceId(1), InputDeviceType::Keyboard, "Keyboard", "keyboard");
            AddFixedDevice(InputDeviceId(2), InputDeviceType::Mouse, "Mouse", "mouse");
            NextDevice = 3;
            Sink = WindowSystemInternalAccess::AddEventSink(*Windowing, this, ProcessNativeEvent);
            if (GamepadSubsystemInitialized)
            {
                int gamepadCount = 0;
                SDL_JoystickID* gamepads = SDL_GetGamepads(&gamepadCount);
                for (int index = 0; index < gamepadCount; ++index)
                    AddGamepad(gamepads[index]);
                SDL_free(gamepads);
            }
        }

        ~InputRuntimeState() override { Close(); }

        void RequireOwner(const char* operation) const
        {
            if (std::this_thread::get_id() != OwnerThread)
                throw std::logic_error(std::string("InputSystem::") + operation + " must run on its owner thread.");
            if (!Open)
                throw std::logic_error(std::string("InputSystem::") + operation + " called after shutdown.");
        }

        void AddFixedDevice(const InputDeviceId id, const InputDeviceType type, std::string name, std::string hardware)
        {
            DeviceState device;
            device.Descriptor = {id, type, std::move(name), std::move(hardware), true, false};
            Devices.emplace(id, std::move(device));
            LastActiveDevices[static_cast<std::size_t>(type)] = id;
        }

        static void ProcessNativeEvent(void* context, const SDL_Event& event) noexcept
        {
            try
            {
                static_cast<InputRuntimeState*>(context)->Process(event);
            }
            catch (...)
            {
                static_cast<InputRuntimeState*>(context)->BackendFailure = true;
            }
        }

        void Queue(RawControlEvent event)
        {
            if (EventsBuffer.size() < Specification.EventCapacity)
                EventsBuffer.push_back(std::move(event));
            else
                ++DroppedEvents;
        }

        void QueuePulse(const InputDeviceId device, const InputDeviceType type, const std::string_view path,
                        const std::uint64_t timestamp)
        {
            if (EventsBuffer.size() + 2 > Specification.EventCapacity)
            {
                DroppedEvents += 2;
                return;
            }
            EventsBuffer.push_back({device, type, std::string(path), BooleanInputValue(true), timestamp, true});
            EventsBuffer.push_back({device, type, std::string(path), BooleanInputValue(false), timestamp, false});
        }

        void Process(const SDL_Event& event)
        {
            if (!Open)
                return;
            switch (event.type)
            {
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
            {
                if (event.key.repeat)
                    return;
                const auto path = Detail::KeyboardInputPath(event.key.scancode);
                if (!path.empty())
                    Queue({InputDeviceId(1), InputDeviceType::Keyboard, path, BooleanInputValue(event.key.down),
                           event.key.timestamp, event.key.down});
                break;
            }
            case SDL_EVENT_MOUSE_MOTION:
                Queue({InputDeviceId(2), InputDeviceType::Mouse, "<Mouse>/position",
                       VectorInputValue(event.motion.x, event.motion.y), event.motion.timestamp, false});
                Queue({InputDeviceId(2), InputDeviceType::Mouse, "<Mouse>/delta",
                       VectorInputValue(event.motion.xrel, event.motion.yrel), event.motion.timestamp,
                       std::abs(event.motion.xrel) + std::abs(event.motion.yrel) > 0.0F});
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                std::string path;
                if (event.button.button == SDL_BUTTON_LEFT)
                    path = "<Mouse>/leftButton";
                else if (event.button.button == SDL_BUTTON_RIGHT)
                    path = "<Mouse>/rightButton";
                else if (event.button.button == SDL_BUTTON_MIDDLE)
                    path = "<Mouse>/middleButton";
                else if (event.button.button == SDL_BUTTON_X1)
                    path = "<Mouse>/backButton";
                else if (event.button.button == SDL_BUTTON_X2)
                    path = "<Mouse>/forwardButton";
                if (!path.empty())
                    Queue({InputDeviceId(2), InputDeviceType::Mouse, std::move(path),
                           BooleanInputValue(event.button.down), event.button.timestamp, event.button.down});
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL:
            {
                Queue({InputDeviceId(2), InputDeviceType::Mouse, "<Mouse>/scroll",
                       VectorInputValue(event.wheel.x, event.wheel.y), event.wheel.timestamp,
                       std::abs(event.wheel.x) + std::abs(event.wheel.y) > 0.0F});
                const auto queueWheelButton = [&](const std::string_view path)
                { QueuePulse(InputDeviceId(2), InputDeviceType::Mouse, path, event.wheel.timestamp); };
                if (event.wheel.y > 0.0F)
                    queueWheelButton("<Mouse>/wheelUp");
                else if (event.wheel.y < 0.0F)
                    queueWheelButton("<Mouse>/wheelDown");
                break;
            }
            case SDL_EVENT_GAMEPAD_ADDED:
                AddGamepad(event.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                RemoveGamepad(event.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
            {
                const auto found = NativeDevices.find(event.gbutton.which);
                const auto path = Detail::GamepadButtonInputPath(event.gbutton.button);
                if (found != NativeDevices.end() && !path.empty())
                {
                    Queue({found->second, InputDeviceType::Gamepad, path, BooleanInputValue(event.gbutton.down),
                           event.gbutton.timestamp, event.gbutton.down});
                    if (path.starts_with("<Gamepad>/dpad/"))
                    {
                        auto& device = Devices.at(found->second);
                        device.PendingControls[path] = BooleanInputValue(event.gbutton.down);
                        const auto direction = [&](const std::string_view directionPath)
                        {
                            const auto pending = device.PendingControls.find(std::string(directionPath));
                            if (pending != device.PendingControls.end())
                                return pending->second.Magnitude();
                            const auto current = device.Controls.find(std::string(directionPath));
                            return current == device.Controls.end() ? 0.0F : current->second.Magnitude();
                        };
                        const auto dpad =
                            VectorInputValue(direction("<Gamepad>/dpad/right") - direction("<Gamepad>/dpad/left"),
                                             direction("<Gamepad>/dpad/up") - direction("<Gamepad>/dpad/down"));
                        device.PendingControls["<Gamepad>/dpad"] = dpad;
                        Queue({found->second, InputDeviceType::Gamepad, "<Gamepad>/dpad", dpad, event.gbutton.timestamp,
                               dpad.Magnitude() >= 0.5F});
                    }
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                ProcessGamepadAxis(event.gaxis);
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                for (auto& [id, device] : Devices)
                {
                    (void)device;
                    if (!Contains(PendingDeviceCancellations, id))
                        PendingDeviceCancellations.push_back(id);
                }
                FocusResetPending = true;
                break;
            default:
                break;
            }
        }

        void AddGamepad(const SDL_JoystickID nativeId)
        {
            if (NativeDevices.contains(nativeId))
                return;
            SDL_Gamepad* handle = SDL_OpenGamepad(nativeId);
            if (!handle)
                return;
            const char* name = SDL_GetGamepadName(handle);
            std::array<char, 33> guid{};
            SDL_GUIDToString(SDL_GetGamepadGUIDForID(nativeId), guid.data(), static_cast<int>(guid.size()));
            const auto reconnect =
                std::ranges::find_if(Devices,
                                     [&](const auto& item)
                                     {
                                         return item.second.Descriptor.Type == InputDeviceType::Gamepad &&
                                                !item.second.Descriptor.Connected &&
                                                item.second.Descriptor.HardwareKey == guid.data();
                                     });
            if (reconnect != Devices.end())
            {
                auto& device = reconnect->second;
                device.Descriptor.Name = name && *name ? name : "Gamepad";
                device.Descriptor.Connected = true;
                device.NativeId = nativeId;
                device.Gamepad = handle;
                NativeDevices.emplace(nativeId, reconnect->first);
                if (const auto owners = ReconnectUsers.find(reconnect->first); owners != ReconnectUsers.end())
                {
                    for (const auto userId : owners->second)
                    {
                        if (auto user = Users.find(userId); user != Users.end())
                        {
                            user->second.Descriptor.Devices.push_back(reconnect->first);
                            SelectScheme(user->second);
                        }
                    }
                    ReconnectUsers.erase(owners);
                }
                device.Descriptor.Paired = DevicePaired(reconnect->first);
                if (Events)
                    (void)Events->Dispatch(InputDeviceConnectedEvent{device.Descriptor});
                return;
            }
            DeviceState device;
            device.Descriptor.Id = InputDeviceId(NextDevice++);
            device.Descriptor.Type = InputDeviceType::Gamepad;
            device.Descriptor.Name = name && *name ? name : "Gamepad";
            device.Descriptor.HardwareKey = guid.data();
            device.Descriptor.Connected = true;
            device.NativeId = nativeId;
            device.Gamepad = handle;
            const auto id = device.Descriptor.Id;
            Devices.emplace(id, std::move(device));
            NativeDevices.emplace(nativeId, id);
            if (Events)
                (void)Events->Dispatch(InputDeviceConnectedEvent{Devices.at(id).Descriptor});
        }

        void RemoveGamepad(const SDL_JoystickID nativeId)
        {
            const auto native = NativeDevices.find(nativeId);
            if (native == NativeDevices.end())
                return;
            const auto id = native->second;
            auto& device = Devices.at(id);
            if (device.Gamepad)
                SDL_CloseGamepad(device.Gamepad);
            device.Gamepad = nullptr;
            device.Descriptor.Connected = false;
            device.Descriptor.Paired = false;
            device.Controls.clear();
            device.PendingControls.clear();
            for (auto& [userId, user] : Users)
            {
                if (Contains(user.Descriptor.Devices, id))
                {
                    ReconnectUsers[id].push_back(userId);
                    std::erase(user.Descriptor.Devices, id);
                }
                SelectScheme(user);
            }
            if (Events)
                (void)Events->Dispatch(InputDeviceDisconnectedEvent{id, InputDeviceType::Gamepad});
            NativeDevices.erase(native);
            PendingDeviceCancellations.push_back(id);
            if (const auto rebind = ActiveRebind.Lock();
                rebind && (rebind->Status == RebindStatus::Listening || rebind->Status == RebindStatus::Candidate))
                rebind->Status = RebindStatus::Cancelled;
        }

        void ProcessGamepadAxis(const SDL_GamepadAxisEvent& event)
        {
            const auto found = NativeDevices.find(event.which);
            if (found == NativeDevices.end())
                return;
            auto& device = Devices.at(found->second);
            const auto path = Detail::GamepadAxisInputPath(event.axis);
            if (path.empty())
                return;
            const auto key = std::string(path);
            const auto [pending, inserted] = device.PendingControls.try_emplace(key);
            if (inserted)
            {
                if (const auto current = device.Controls.find(key); current != device.Controls.end())
                    pending->second = current->second;
            }
            const auto value = Detail::GamepadAxisInputValue(event.axis, event.value, pending->second);
            pending->second = value;
            Queue({found->second, InputDeviceType::Gamepad, std::string(path), value, event.timestamp,
                   value.Magnitude() >= 0.5F});
        }

        void SelectScheme(UserState& user)
        {
            if (user.SchemeLocked)
                return;
            const bool gamepad =
                std::ranges::any_of(user.Descriptor.Devices, [&](const auto id)
                                    { return Devices.at(id).Descriptor.Type == InputDeviceType::Gamepad; });
            const bool keyboard = Contains(user.Descriptor.Devices, InputDeviceId(1));
            const bool mouse = Contains(user.Descriptor.Devices, InputDeviceId(2));
            user.Descriptor.ControlScheme = gamepad ? "Gamepad" : keyboard && mouse ? "KeyboardMouse" : "";
        }

        void SelectSchemeForInput(const RawControlEvent& event)
        {
            if (!event.Press && event.Value.Magnitude() < 0.2F)
                return;
            for (auto& [id, user] : Users)
            {
                (void)id;
                if (user.SchemeLocked || !Contains(user.Descriptor.Devices, event.Device))
                    continue;
                if (event.Type == InputDeviceType::Gamepad)
                    user.Descriptor.ControlScheme = "Gamepad";
                else if (Contains(user.Descriptor.Devices, InputDeviceId(1)) &&
                         Contains(user.Descriptor.Devices, InputDeviceId(2)))
                    user.Descriptor.ControlScheme = "KeyboardMouse";
            }
        }

        [[nodiscard]] bool DevicePaired(const InputDeviceId device) const
        {
            return std::ranges::any_of(Users, [&](const auto& item)
                                       { return Contains(item.second.Descriptor.Devices, device); });
        }

        void AutoJoin(const RawControlEvent& event)
        {
            if (!Specification.AutoJoin || !event.Press || DevicePaired(event.Device) ||
                Users.size() >= Specification.MaximumUsers)
                return;
            const auto userId = CreateUser({});
            if (event.Type == InputDeviceType::Keyboard || event.Type == InputDeviceType::Mouse)
            {
                (void)Pair(userId, InputDeviceId(1), false);
                (void)Pair(userId, InputDeviceId(2), false);
            }
            else
                (void)Pair(userId, event.Device, false);
            if (Events)
                (void)Events->Dispatch(InputUserJoinedEvent{userId, event.Device});
        }

        InputUserId CreateUser(std::string name)
        {
            RequireOwner("CreateUser");
            if (Users.size() >= Specification.MaximumUsers)
                throw std::runtime_error("Input user limit has been reached.");
            const InputUserId id(NextUser++);
            if (name.empty())
                name = "Player " + std::to_string(id.Value());
            Users.emplace(id, UserState{{id, std::move(name), {}, {}}});
            return id;
        }

        bool Pair(const InputUserId userId, const InputDeviceId deviceId, const bool shared)
        {
            auto user = Users.find(userId);
            auto device = Devices.find(deviceId);
            if (user == Users.end() || device == Devices.end() || !device->second.Descriptor.Connected ||
                Contains(user->second.Descriptor.Devices, deviceId))
                return false;
            if (DevicePaired(deviceId) && !(shared && Specification.AllowSharedDevices))
                return false;
            user->second.Descriptor.Devices.push_back(deviceId);
            device->second.Descriptor.Paired = true;
            SelectScheme(user->second);
            return true;
        }

        void Advance(const TimeStep delta, const UiCaptureState capture, const bool suspended)
        {
            RequireOwner("AdvanceFrame");
            if (BackendFailure)
                throw std::runtime_error("The SDL input backend reported an unexpected failure.");
            ++Frame;
            CurrentCapture = capture;
            Suspended = suspended;
            DeltaSeconds = delta.Seconds();
            LastTimestamp += static_cast<std::uint64_t>(std::max(0.0, delta.Seconds()) * 1'000'000'000.0);
            for (auto& [id, device] : Devices)
            {
                (void)id;
                device.PressedControls.clear();
                device.ReleasedControls.clear();
            }
            if (const auto mouse = Devices.find(InputDeviceId(2)); mouse != Devices.end())
            {
                mouse->second.Controls["<Mouse>/delta"] = VectorInputValue(0.0F, 0.0F);
                mouse->second.Controls["<Mouse>/scroll"] = VectorInputValue(0.0F, 0.0F);
            }
            for (const auto device : PendingDeviceCancellations)
                CancelDeviceActions(device);
            PendingDeviceCancellations.clear();
            for (const auto& event : EventsBuffer)
            {
                AutoJoin(event);
                SelectSchemeForInput(event);
                if (event.Press || event.Value.Magnitude() >= 0.2F)
                    LastActiveDevices[static_cast<std::size_t>(event.Type)] = event.Device;
                auto found = Devices.find(event.Device);
                if (found != Devices.end())
                {
                    auto& device = found->second;
                    auto& control = device.Controls[event.Path];
                    if (event.Path == "<Mouse>/delta" || event.Path == "<Mouse>/scroll")
                    {
                        control.Type = event.Value.Type;
                        control.X += event.Value.X;
                        control.Y += event.Value.Y;
                    }
                    else
                    {
                        const bool wasPressed = control.Magnitude() >= 0.5F;
                        const bool isPressed = event.Value.Magnitude() >= 0.5F;
                        if (!wasPressed && isPressed)
                            device.PressedControls.insert(event.Path);
                        else if (wasPressed && !isPressed)
                            device.ReleasedControls.insert(event.Path);
                        control = event.Value;
                    }
                }
                CaptureRebind(event);
                LastTimestamp = std::max(LastTimestamp, event.Timestamp);
            }
            if (FocusResetPending)
            {
                for (auto& [id, device] : Devices)
                {
                    (void)id;
                    for (auto& [path, value] : device.Controls)
                    {
                        if (value.Magnitude() >= 0.5F)
                            device.ReleasedControls.insert(path);
                        value = InputValue{value.Type};
                    }
                }
                FocusResetPending = false;
            }
            for (auto& [id, device] : Devices)
            {
                (void)id;
                device.PendingControls.clear();
            }
            for (auto iterator = Contexts.begin(); iterator != Contexts.end();)
            {
                if (const auto context = iterator->Lock())
                {
                    context->Rebuild();
                    if (!GameplayPlayback || context->Role == InputContextRole::EditorControl)
                        Evaluate(*context);
                    ++iterator;
                }
                else
                    iterator = Contexts.erase(iterator);
            }
            TickRebind(delta.Seconds());
            EventsBuffer.clear();
        }

        [[nodiscard]] bool UserOwns(const InputUserId user, const InputDeviceId device) const
        {
            const auto found = Users.find(user);
            return found != Users.end() && Contains(found->second.Descriptor.Devices, device);
        }

        [[nodiscard]] bool GroupAllowed(const UserState& user, const InputBindingDefinition& binding) const
        {
            return binding.Groups.empty() || user.Descriptor.ControlScheme.empty() ||
                   Contains(binding.Groups, user.Descriptor.ControlScheme);
        }

        [[nodiscard]] std::pair<InputValue, InputDeviceId> ReadBinding(const InputContextState& context,
                                                                       const InputBindingDefinition& binding) const
        {
            if (context.DisabledBindings.contains(binding.Id))
                return {};
            const auto override = context.Overrides.find(binding.Id);
            const std::string_view path = override == context.Overrides.end() ? binding.Path : override->second;
            const auto user = Users.find(context.User);
            if (user == Users.end() || !GroupAllowed(user->second, binding))
                return {};
            for (const auto deviceId : user->second.Descriptor.Devices)
            {
                const auto device = Devices.find(deviceId);
                if (device == Devices.end() || !device->second.Descriptor.Connected)
                    continue;
                const auto control = device->second.Controls.find(std::string(path));
                if (control == device->second.Controls.end())
                    continue;
                if (Suspended || (context.Definition.ActionMaps.empty()))
                    return {};
                return {Detail::ApplyInputProcessors(control->second, binding.Processors), deviceId};
            }
            return {};
        }

        [[nodiscard]] std::pair<InputValue, InputDeviceId> EvaluateAction(const InputContextState& context,
                                                                          const InputActionMapDefinition& map,
                                                                          const InputActionDefinition& action,
                                                                          const InputBindingDefinition** winning) const
        {
            InputValue result{action.ValueType};
            InputDeviceId device;
            float best = 0.0F;
            const auto applyCapture = [&](const InputBindingDefinition& binding, InputValue value)
            {
                if (map.CapturePolicy == InputCapturePolicy::RespectUiCapture &&
                    !context.CaptureBypassMaps.contains(map.Id))
                {
                    const bool keyboard = binding.Path.starts_with("<Keyboard>");
                    const bool pointer = binding.Path.starts_with("<Mouse>");
                    if ((keyboard && CurrentCapture.Keyboard) || (pointer && CurrentCapture.Pointer))
                        value = {action.ValueType};
                }
                return value;
            };
            for (auto binding = map.Bindings.begin(); binding != map.Bindings.end(); ++binding)
            {
                if (binding->Action != action.Id || !binding->CompositePart.empty())
                    continue;
                if (!binding->Composite.empty())
                {
                    const auto user = Users.find(context.User);
                    if (context.DisabledBindings.contains(binding->Id) || user == Users.end() ||
                        !GroupAllowed(user->second, *binding))
                    {
                        continue;
                    }
                    InputValue composite{action.ValueType};
                    InputDeviceId compositeDevice;
                    float strongestPart = 0.0F;
                    for (auto part = std::next(binding); part != map.Bindings.end() && !part->CompositePart.empty();
                         ++part)
                    {
                        if (part->Action != action.Id)
                            continue;
                        auto [value, source] = ReadBinding(context, *part);
                        value = applyCapture(*part, value);
                        const auto magnitude = value.Magnitude();
                        if (part->CompositePart == "Negative")
                            composite.X -= magnitude;
                        else if (part->CompositePart == "Positive")
                            composite.X += magnitude;
                        else if (part->CompositePart == "Up")
                            composite.Y += magnitude;
                        else if (part->CompositePart == "Down")
                            composite.Y -= magnitude;
                        else if (part->CompositePart == "Left")
                            composite.X -= magnitude;
                        else if (part->CompositePart == "Right")
                            composite.X += magnitude;
                        if (magnitude > strongestPart)
                        {
                            strongestPart = magnitude;
                            compositeDevice = source;
                        }
                    }
                    composite = Detail::ApplyInputProcessors(composite, binding->Processors);
                    const auto magnitude = composite.Magnitude();
                    if (magnitude > best)
                    {
                        result = composite;
                        device = compositeDevice;
                        best = magnitude;
                        *winning = &*binding;
                    }
                    continue;
                }
                auto [value, source] = ReadBinding(context, *binding);
                value = applyCapture(*binding, value);
                const auto magnitude = value.Magnitude();
                if (magnitude > best)
                {
                    result = value;
                    result.Type = action.ValueType;
                    device = source;
                    best = magnitude;
                    *winning = &*binding;
                }
            }
            return {Detail::ApplyInputProcessors(result, action.Processors), device};
        }

        [[nodiscard]] static bool IsRelativeAction(const InputContextState& context,
                                                   const InputActionMapDefinition& map,
                                                   const InputActionDefinition& action)
        {
            return std::ranges::any_of(map.Bindings,
                                       [&](const InputBindingDefinition& binding)
                                       {
                                           if (binding.Action != action.Id)
                                               return false;
                                           const auto override = context.Overrides.find(binding.Id);
                                           const std::string_view path =
                                               override == context.Overrides.end() ? binding.Path : override->second;
                                           return path == "<Mouse>/delta" || path == "<Mouse>/scroll";
                                       });
        }

        struct ActionTransitions final
        {
            const InputBindingDefinition* Binding = nullptr;
            InputDeviceId Device;
            bool Pressed = false;
            bool Released = false;
        };

        [[nodiscard]] ActionTransitions ReadActionTransitions(const InputContextState& context,
                                                              const InputActionMapDefinition& map,
                                                              const InputActionDefinition& action) const
        {
            ActionTransitions result;
            const auto user = Users.find(context.User);
            if (user == Users.end() || Suspended)
                return result;
            const bool hasTransitions =
                std::ranges::any_of(user->second.Descriptor.Devices,
                                    [&](const InputDeviceId deviceId)
                                    {
                                        const auto device = Devices.find(deviceId);
                                        return device != Devices.end() && (!device->second.PressedControls.empty() ||
                                                                           !device->second.ReleasedControls.empty());
                                    });
            if (!hasTransitions)
                return result;
            const InputBindingDefinition* compositeRoot = nullptr;
            bool compositePartsAllowed = true;
            for (const auto& binding : map.Bindings)
            {
                if (!binding.Composite.empty())
                {
                    compositePartsAllowed = binding.Action == action.Id &&
                                            !context.DisabledBindings.contains(binding.Id) &&
                                            GroupAllowed(user->second, binding);
                    compositeRoot = compositePartsAllowed ? &binding : nullptr;
                    continue;
                }
                if (binding.CompositePart.empty())
                {
                    compositeRoot = nullptr;
                    compositePartsAllowed = true;
                }
                else if (!compositePartsAllowed)
                    continue;
                if (binding.Action != action.Id || context.DisabledBindings.contains(binding.Id) ||
                    !GroupAllowed(user->second, binding))
                {
                    continue;
                }
                const auto overridden = context.Overrides.find(binding.Id);
                const std::string_view path = overridden == context.Overrides.end() ? binding.Path : overridden->second;
                const std::string pathKey(path);
                if (map.CapturePolicy == InputCapturePolicy::RespectUiCapture &&
                    !context.CaptureBypassMaps.contains(map.Id))
                {
                    const bool keyboard = path.starts_with("<Keyboard>");
                    const bool pointer = path.starts_with("<Mouse>");
                    if ((keyboard && CurrentCapture.Keyboard) || (pointer && CurrentCapture.Pointer))
                        continue;
                }
                for (const auto deviceId : user->second.Descriptor.Devices)
                {
                    const auto device = Devices.find(deviceId);
                    if (device == Devices.end() || !device->second.Descriptor.Connected)
                        continue;
                    const bool pressed = device->second.PressedControls.contains(pathKey);
                    const bool released = device->second.ReleasedControls.contains(pathKey);
                    if (!pressed && !released)
                        continue;
                    result.Binding = compositeRoot ? compositeRoot : &binding;
                    result.Device = deviceId;
                    result.Pressed = result.Pressed || pressed;
                    result.Released = result.Released || released;
                }
            }
            return result;
        }

        void Emit(InputContextState& context, ActionRuntime& runtime, const InputActionMapDefinition& map,
                  const InputActionDefinition& action, const InputDeviceId device, const InputActionPhase phase,
                  const InputValue value)
        {
            runtime.Phase = phase;
            runtime.Value = value;
            runtime.ActiveDevice = phase == InputActionPhase::Canceled ? InputDeviceId{} : device;
            if (phase == InputActionPhase::Started)
            {
                runtime.StartedFrame = Frame;
                runtime.PendingStarted = true;
            }
            else if (phase == InputActionPhase::Performed)
            {
                runtime.PerformedFrame = Frame;
                runtime.PendingPerformed = true;
            }
            else if (phase == InputActionPhase::Canceled)
            {
                runtime.CanceledFrame = Frame;
                runtime.PendingCanceled = true;
            }
            const auto duration = runtime.PressTimestamp == 0 || LastTimestamp < runtime.PressTimestamp
                                      ? 0.0
                                      : static_cast<double>(LastTimestamp - runtime.PressTimestamp) / 1'000'000'000.0;
            context.Emit({context.AssetIdValue, map.Id, action.Id, context.User, device, phase, value, Frame,
                          LastTimestamp, duration});
        }

        void Evaluate(InputContextState& context)
        {
            for (const auto& map : context.Definition.ActionMaps)
            {
                for (const auto& action : map.Actions)
                {
                    const bool enabled = context.EnabledActions.contains(action.Id);
                    auto& runtime = context.Actions[action.Id];
                    if (!enabled)
                    {
                        if (runtime.Active)
                            Emit(context, runtime, map, action, {}, InputActionPhase::Canceled,
                                 InputValue{action.ValueType});
                        runtime.Active = false;
                        runtime.Phase = InputActionPhase::Disabled;
                        continue;
                    }
                    const InputBindingDefinition* winning = nullptr;
                    auto [value, device] = EvaluateAction(context, map, action, &winning);
                    const auto transitions = ReadActionTransitions(context, map, action);
                    if (!winning && transitions.Binding)
                        winning = transitions.Binding;
                    if (IsRelativeAction(context, map, action))
                    {
                        runtime.PendingRelativeValue.Type = action.ValueType;
                        runtime.PendingRelativeValue.X += value.X;
                        runtime.PendingRelativeValue.Y += value.Y;
                    }
                    const auto& interactions =
                        winning && !winning->Interactions.empty() ? winning->Interactions : action.Interactions;
                    std::string_view interaction;
                    if (!interactions.empty())
                        interaction = interactions.front().Name;
                    const auto pressPoint =
                        interactions.empty() ? 0.5F
                                             : static_cast<float>(InputParameterValue(interactions.front().Parameters,
                                                                                      "pressPoint", 0.5));
                    const bool active = value.Magnitude() >= pressPoint;
                    if (action.Type == InputActionType::PassThrough)
                    {
                        if (!runtime.Value.NearlyEquals(value))
                            Emit(context, runtime, map, action, device, InputActionPhase::Performed, value);
                        runtime.Active = active;
                        continue;
                    }
                    if (!runtime.Active && !active && transitions.Pressed && transitions.Released)
                    {
                        const InputValue pressedValue{action.ValueType, 1.0F, 0.0F};
                        runtime.Active = true;
                        runtime.PressTimestamp = LastTimestamp;
                        runtime.HoldPerformed = false;
                        Emit(context, runtime, map, action, transitions.Device, InputActionPhase::Started,
                             pressedValue);
                        if (interaction.empty() || interaction == "Press" || interaction == "Tap")
                        {
                            Emit(context, runtime, map, action, transitions.Device, InputActionPhase::Performed,
                                 pressedValue);
                        }
                        else if (interaction == "MultiTap")
                        {
                            const auto delay = InputParameterValue(interactions.front().Parameters, "delay", 0.75);
                            if (runtime.LastReleaseTimestamp == 0 ||
                                static_cast<double>(LastTimestamp - runtime.LastReleaseTimestamp) / 1'000'000'000.0 <=
                                    delay)
                            {
                                ++runtime.TapCount;
                            }
                            else
                                runtime.TapCount = 1;
                            if (runtime.TapCount >= static_cast<std::uint32_t>(InputParameterValue(
                                                        interactions.front().Parameters, "count", 2.0)))
                            {
                                Emit(context, runtime, map, action, transitions.Device, InputActionPhase::Performed,
                                     pressedValue);
                                runtime.TapCount = 0;
                            }
                            runtime.LastReleaseTimestamp = LastTimestamp;
                        }
                        Emit(context, runtime, map, action, transitions.Device, InputActionPhase::Canceled,
                             InputValue{action.ValueType});
                        runtime.Active = false;
                        runtime.HoldPerformed = false;
                        continue;
                    }
                    if (!runtime.Active && active)
                    {
                        runtime.Active = true;
                        runtime.PressTimestamp = LastTimestamp;
                        runtime.HoldPerformed = false;
                        Emit(context, runtime, map, action, device, InputActionPhase::Started, value);
                        if (interaction.empty() || interaction == "Press")
                            Emit(context, runtime, map, action, device, InputActionPhase::Performed, value);
                    }
                    else if (runtime.Active && active)
                    {
                        if (interaction == "Hold" && !runtime.HoldPerformed)
                        {
                            const auto seconds =
                                static_cast<double>(LastTimestamp - runtime.PressTimestamp) / 1'000'000'000.0;
                            const auto duration = InputParameterValue(interactions.front().Parameters, "duration", 0.4);
                            if (seconds >= duration)
                            {
                                runtime.HoldPerformed = true;
                                Emit(context, runtime, map, action, device, InputActionPhase::Performed, value);
                            }
                        }
                        else if (action.Type == InputActionType::Value &&
                                 (interaction.empty() || interaction == "Press" ||
                                  (interaction == "Hold" && runtime.HoldPerformed)) &&
                                 !runtime.Value.NearlyEquals(value))
                            Emit(context, runtime, map, action, device, InputActionPhase::Performed, value);
                        else
                            runtime.Value = value;
                    }
                    else if (runtime.Active && !active)
                    {
                        const auto seconds =
                            static_cast<double>(LastTimestamp - runtime.PressTimestamp) / 1'000'000'000.0;
                        if (interaction == "Tap" &&
                            seconds <= InputParameterValue(interactions.front().Parameters, "duration", 0.2))
                            Emit(context, runtime, map, action, device, InputActionPhase::Performed, runtime.Value);
                        else if (interaction == "MultiTap")
                        {
                            const auto duration = InputParameterValue(interactions.front().Parameters, "duration", 0.2);
                            if (seconds <= duration)
                            {
                                const auto delay = InputParameterValue(interactions.front().Parameters, "delay", 0.75);
                                if (runtime.LastReleaseTimestamp == 0 ||
                                    static_cast<double>(LastTimestamp - runtime.LastReleaseTimestamp) /
                                            1'000'000'000.0 <=
                                        delay)
                                    ++runtime.TapCount;
                                else
                                    runtime.TapCount = 1;
                                if (runtime.TapCount >= static_cast<std::uint32_t>(InputParameterValue(
                                                            interactions.front().Parameters, "count", 2.0)))
                                {
                                    Emit(context, runtime, map, action, device, InputActionPhase::Performed,
                                         runtime.Value);
                                    runtime.TapCount = 0;
                                }
                            }
                            else
                                runtime.TapCount = 0;
                            runtime.LastReleaseTimestamp = LastTimestamp;
                        }
                        Emit(context, runtime, map, action, device, InputActionPhase::Canceled,
                             InputValue{action.ValueType});
                        runtime.Active = false;
                        runtime.HoldPerformed = false;
                    }
                    else
                    {
                        runtime.Value = value;
                        runtime.Phase = InputActionPhase::Waiting;
                    }
                }
            }
        }

        void CaptureRebind(const RawControlEvent& event)
        {
            const auto rebind = ActiveRebind.Lock();
            if (!rebind || rebind->Status != RebindStatus::Listening)
                return;
            if (event.Press && event.Path == "<Keyboard>/escape")
            {
                rebind->Status = RebindStatus::Cancelled;
                return;
            }
            if ((!rebind->Options.AllowedDevices.empty() && !Contains(rebind->Options.AllowedDevices, event.Type)) ||
                Contains(rebind->Options.ExcludedControls, event.Path) ||
                event.Value.Magnitude() < rebind->Options.MagnitudeThreshold)
                return;
            rebind->Candidate = event.Path;
            rebind->CandidateDevice = event.Type;
            rebind->Status = RebindStatus::Candidate;
            if (const auto context = rebind->Context.Lock())
            {
                const auto* target = context->FindBinding(rebind->Targets[rebind->TargetIndex]);
                if (!target)
                    return;
                for (const auto& map : context->Definition.ActionMaps)
                {
                    if (std::ranges::none_of(map.Bindings, [&](const auto& value) { return value.Id == target->Id; }))
                        continue;
                    for (const auto& binding : map.Bindings)
                    {
                        const auto override = context->Overrides.find(binding.Id);
                        const auto& path = override == context->Overrides.end() ? binding.Path : override->second;
                        if (binding.Id != target->Id && path == rebind->Candidate)
                            rebind->Conflicts.push_back({binding.Id, binding.Action, path});
                    }
                }
            }
        }

        void TickRebind(const double delta)
        {
            const auto rebind = ActiveRebind.Lock();
            if (!rebind || rebind->Status != RebindStatus::Listening)
                return;
            rebind->Remaining = std::max(0.0, rebind->Remaining - delta);
            if (rebind->Remaining == 0.0)
                rebind->Status = RebindStatus::TimedOut;
        }

        void CancelDeviceActions(const InputDeviceId device)
        {
            for (const auto& weak : Contexts)
            {
                if (const auto context = weak.Lock())
                {
                    for (const auto& map : context->Definition.ActionMaps)
                    {
                        for (const auto& definition : map.Actions)
                        {
                            auto& action = context->Actions[definition.Id];
                            if (!action.Active || (device && action.ActiveDevice != device))
                                continue;
                            Emit(*context, action, map, definition, device, InputActionPhase::Canceled,
                                 InputValue{definition.ValueType});
                            action.Active = false;
                        }
                    }
                }
            }
        }

        void Close() noexcept
        {
            if (!Open)
                return;
            if (Sink && Windowing)
                WindowSystemInternalAccess::RemoveEventSink(*Windowing, Sink);
            Sink = 0;
            for (auto& [id, device] : Devices)
            {
                (void)id;
                if (device.Gamepad)
                    SDL_CloseGamepad(device.Gamepad);
                device.Gamepad = nullptr;
            }
            for (const auto& weak : Contexts)
            {
                if (const auto context = weak.Lock())
                    context->Open = false;
            }
            if (GamepadSubsystemInitialized)
            {
                SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
                GamepadSubsystemInitialized = false;
            }
            Open = false;
        }

        InputSystemSpecification Specification;
        Ref<WindowSystem> Windowing;
        Ref<AssetSystem> Assets;
        Ref<EventBus> Events;
        std::thread::id OwnerThread;
        std::unordered_map<InputDeviceId, DeviceState> Devices;
        std::array<InputDeviceId, 3> LastActiveDevices{};
        std::unordered_map<SDL_JoystickID, InputDeviceId> NativeDevices;
        std::unordered_map<InputUserId, UserState> Users;
        std::unordered_map<InputDeviceId, std::vector<InputUserId>> ReconnectUsers;
        std::vector<RawControlEvent> EventsBuffer;
        std::vector<InputDeviceId> PendingDeviceCancellations;
        std::vector<WeakRef<InputContextState>> Contexts;
        std::map<std::pair<InputDeviceId, std::string>, InputControlSnapshot> PlaybackControls;
        WeakRef<RebindState> ActiveRebind;
        WindowSystemInternalAccess::EventSinkToken Sink = 0;
        std::uint32_t NextDevice = 1;
        std::uint32_t NextUser = 1;
        std::uint64_t Frame = 0;
        std::uint64_t LastTimestamp = 0;
        std::uint64_t DroppedEvents = 0;
        std::uint64_t NextContext = 1;
        double DeltaSeconds = 0.0;
        UiCaptureState CurrentCapture;
        bool Suspended = false;
        bool BackendFailure = false;
        bool GameplayPlayback = false;
        bool GamepadSubsystemInitialized = false;
        bool FocusResetPending = false;
        bool Open = true;
    };
} // namespace Keire::Detail
