#include "Keire/Input/Input.h"

#include "Keire/BuildInfo.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/InputInternal.h"
#include "KeireInternal/WindowInternal.h"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <ranges>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace Keire
{
    namespace
    {
        using Json = nlohmann::json;
        using Detail::BooleanInputValue;
        using Detail::InputParameterValue;
        using Detail::VectorInputValue;

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

    } // namespace

    namespace Detail
    {
        class InputRuntimeState;

        struct ActionRuntime
        {
            InputActionPhase Phase = InputActionPhase::Waiting;
            InputValue Value;
            InputValue PendingRelativeValue;
            std::uint64_t StartedFrame = 0;
            std::uint64_t PerformedFrame = 0;
            std::uint64_t CanceledFrame = 0;
            std::uint64_t PressTimestamp = 0;
            std::uint64_t LastReleaseTimestamp = 0;
            std::uint32_t TapCount = 0;
            InputDeviceId ActiveDevice;
            bool Active = false;
            bool HoldPerformed = false;
            bool PendingStarted = false;
            bool PendingPerformed = false;
            bool PendingCanceled = false;
        };

        class InputContextState final : public RefCounted
        {
          public:
            WeakRef<InputRuntimeState> Runtime;
            Keire::AssetHandle<InputActionAsset> AssetHandle;
            AssetId AssetIdValue;
            InputUserId User;
            InputContextRole Role = InputContextRole::Gameplay;
            std::uint64_t ContextId = 0;
            InputActionAssetDefinition Definition;
            std::uint64_t AssetRevision = std::numeric_limits<std::uint64_t>::max();
            std::unordered_set<AssetId> EnabledMaps;
            std::unordered_set<AssetId> ExplicitlyEnabledActions;
            std::unordered_set<AssetId> ExplicitlyDisabledActions;
            std::unordered_set<AssetId> EnabledActions;
            std::unordered_set<AssetId> CaptureBypassMaps;
            std::unordered_map<AssetId, ActionRuntime> Actions;
            std::unordered_map<AssetId, std::string> Overrides;
            std::unordered_set<AssetId> DisabledBindings;
            std::map<std::uint64_t, std::pair<AssetId, std::function<void(const InputActionEvent&)>>> Subscribers;
            std::vector<std::uint64_t> PendingDisconnects;
            std::size_t EmitDepth = 0;
            std::uint64_t NextSubscriber = 1;
            bool Open = true;

            void Rebuild();
            [[nodiscard]] const InputActionDefinition* FindAction(AssetId id) const noexcept;
            [[nodiscard]] const InputBindingDefinition* FindBinding(AssetId id) const noexcept;
            void Emit(const InputActionEvent& event);
            void Disconnect(std::uint64_t id) noexcept
            {
                if (EmitDepth > 0)
                    PendingDisconnects.push_back(id);
                else
                    Subscribers.erase(id);
            }
        };

        class InputSubscriptionState final : public RefCounted
        {
          public:
            explicit InputSubscriptionState(WeakRef<InputContextState> context) : Context(std::move(context)) {}
            void Disconnect(const std::uint64_t id) noexcept
            {
                if (const auto context = Context.Lock())
                    context->Disconnect(id);
            }
            [[nodiscard]] bool Connected(const std::uint64_t id) const noexcept
            {
                if (const auto context = Context.Lock())
                    return context->Open && context->Subscribers.contains(id);
                return false;
            }
            WeakRef<InputContextState> Context;
        };

        struct RebindState final : public RefCounted
        {
            WeakRef<InputRuntimeState> Runtime;
            WeakRef<InputContextState> Context;
            std::vector<AssetId> Targets;
            std::size_t TargetIndex = 0;
            InteractiveRebindOptions Options;
            RebindStatus Status = RebindStatus::Listening;
            std::string Candidate;
            InputDeviceType CandidateDevice = InputDeviceType::Keyboard;
            std::vector<RebindConflict> Conflicts;
            double Remaining = 0.0;
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

            void AddFixedDevice(const InputDeviceId id, const InputDeviceType type, std::string name,
                                std::string hardware)
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
                            Queue({found->second, InputDeviceType::Gamepad, "<Gamepad>/dpad", dpad,
                                   event.gbutton.timestamp, dpad.Magnitude() >= 0.5F});
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

            [[nodiscard]] std::pair<InputValue, InputDeviceId>
            EvaluateAction(const InputContextState& context, const InputActionMapDefinition& map,
                           const InputActionDefinition& action, const InputBindingDefinition** winning) const
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
                                               const std::string_view path = override == context.Overrides.end()
                                                                                 ? binding.Path
                                                                                 : override->second;
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
                const bool hasTransitions = std::ranges::any_of(user->second.Descriptor.Devices,
                                                                [&](const InputDeviceId deviceId)
                                                                {
                                                                    const auto device = Devices.find(deviceId);
                                                                    return device != Devices.end() &&
                                                                           (!device->second.PressedControls.empty() ||
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
                    const std::string_view path =
                        overridden == context.Overrides.end() ? binding.Path : overridden->second;
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
                const auto duration =
                    runtime.PressTimestamp == 0 || LastTimestamp < runtime.PressTimestamp
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
                        const auto pressPoint = interactions.empty()
                                                    ? 0.5F
                                                    : static_cast<float>(InputParameterValue(
                                                          interactions.front().Parameters, "pressPoint", 0.5));
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
                                    static_cast<double>(LastTimestamp - runtime.LastReleaseTimestamp) /
                                            1'000'000'000.0 <=
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
                                const auto duration =
                                    InputParameterValue(interactions.front().Parameters, "duration", 0.4);
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
                                const auto duration =
                                    InputParameterValue(interactions.front().Parameters, "duration", 0.2);
                                if (seconds <= duration)
                                {
                                    const auto delay =
                                        InputParameterValue(interactions.front().Parameters, "delay", 0.75);
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
                if ((!rebind->Options.AllowedDevices.empty() &&
                     !Contains(rebind->Options.AllowedDevices, event.Type)) ||
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
                        if (std::ranges::none_of(map.Bindings,
                                                 [&](const auto& value) { return value.Id == target->Id; }))
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

        void InputContextState::Rebuild()
        {
            if (AssetRevision == AssetHandle.Revision())
                return;
            if (AssetRevision != std::numeric_limits<std::uint64_t>::max())
            {
                if (const auto runtime = Runtime.Lock())
                {
                    for (const auto& map : Definition.ActionMaps)
                    {
                        for (const auto& definition : map.Actions)
                        {
                            auto& action = Actions[definition.Id];
                            if (!action.Active)
                                continue;
                            runtime->Emit(*this, action, map, definition, action.ActiveDevice,
                                          InputActionPhase::Canceled, InputValue{definition.ValueType});
                            action.Active = false;
                        }
                    }
                }
            }
            const auto asset = AssetHandle.Get();
            Definition = asset ? asset->Definition() : InputActionAssetDefinition{};
            AssetRevision = AssetHandle.Revision();
            std::unordered_map<AssetId, ActionRuntime> next;
            for (const auto& map : Definition.ActionMaps)
            {
                for (const auto& action : map.Actions)
                    next.emplace(action.Id, Actions[action.Id]);
            }
            Actions = std::move(next);
            std::erase_if(EnabledMaps,
                          [&](const auto id)
                          {
                              return std::ranges::none_of(Definition.ActionMaps,
                                                          [id](const auto& map) { return map.Id == id; });
                          });
            std::erase_if(ExplicitlyEnabledActions, [&](const auto id) { return !FindAction(id); });
            std::erase_if(ExplicitlyDisabledActions, [&](const auto id) { return !FindAction(id); });

            std::unordered_set<AssetId> nextEnabled = ExplicitlyEnabledActions;
            for (const auto& map : Definition.ActionMaps)
            {
                if (!EnabledMaps.contains(map.Id))
                    continue;
                for (const auto& action : map.Actions)
                {
                    if (!ExplicitlyDisabledActions.contains(action.Id))
                        nextEnabled.insert(action.Id);
                }
            }
            for (const auto action : nextEnabled)
            {
                if (!EnabledActions.contains(action))
                    Actions[action].Phase = InputActionPhase::Waiting;
            }
            EnabledActions = std::move(nextEnabled);
        }

        const InputActionDefinition* InputContextState::FindAction(const AssetId id) const noexcept
        {
            for (const auto& map : Definition.ActionMaps)
            {
                const auto found = std::ranges::find(map.Actions, id, &InputActionDefinition::Id);
                if (found != map.Actions.end())
                    return &*found;
            }
            return nullptr;
        }

        const InputBindingDefinition* InputContextState::FindBinding(const AssetId id) const noexcept
        {
            for (const auto& map : Definition.ActionMaps)
            {
                const auto found = std::ranges::find(map.Bindings, id, &InputBindingDefinition::Id);
                if (found != map.Bindings.end())
                    return &*found;
            }
            return nullptr;
        }

        void InputContextState::Emit(const InputActionEvent& event)
        {
            ++EmitDepth;
            try
            {
                for (const auto& [id, subscriber] : Subscribers)
                {
                    if (subscriber.first == event.Action && !Contains(PendingDisconnects, id))
                        subscriber.second(event);
                }
            }
            catch (...)
            {
                --EmitDepth;
                if (EmitDepth == 0)
                {
                    for (const auto id : PendingDisconnects)
                        Subscribers.erase(id);
                    PendingDisconnects.clear();
                }
                throw;
            }
            --EmitDepth;
            if (EmitDepth == 0)
            {
                for (const auto id : PendingDisconnects)
                    Subscribers.erase(id);
                PendingDisconnects.clear();
            }
        }
    } // namespace Detail

    class InputActionContext::Impl final
    {
      public:
        explicit Impl(Ref<Detail::InputContextState> state)
            : State(std::move(state)), Subscriptions(CreateRef<Detail::InputSubscriptionState>(State))
        {
        }
        Ref<Detail::InputContextState> State;
        Ref<Detail::InputSubscriptionState> Subscriptions;
    };

    class InteractiveRebindOperation::Impl final
    {
      public:
        explicit Impl(Ref<Detail::RebindState> state) : State(std::move(state)) {}
        Ref<Detail::RebindState> State;
    };

    class InputSystem::Impl final
    {
      public:
        explicit Impl(Ref<Detail::InputRuntimeState> state) : State(std::move(state)) {}
        Ref<Detail::InputRuntimeState> State;
    };

    InputActionSubscription::InputActionSubscription(WeakRef<Detail::InputSubscriptionState> state,
                                                     const std::uint64_t id) noexcept
        : m_State(std::move(state)), m_Id(id)
    {
    }

    InputActionSubscription::InputActionSubscription(InputActionSubscription&& other) noexcept
        : m_State(std::move(other.m_State)), m_Id(std::exchange(other.m_Id, 0))
    {
    }

    InputActionSubscription& InputActionSubscription::operator=(InputActionSubscription&& other) noexcept
    {
        if (this != &other)
        {
            Disconnect();
            m_State = std::move(other.m_State);
            m_Id = std::exchange(other.m_Id, 0);
        }
        return *this;
    }

    InputActionSubscription::~InputActionSubscription() { Disconnect(); }
    void InputActionSubscription::Disconnect() noexcept
    {
        if (m_Id)
        {
            if (const auto state = m_State.Lock())
                state->Disconnect(m_Id);
            m_Id = 0;
            m_State.Reset();
        }
    }

    bool InputActionSubscription::Connected() const noexcept
    {
        if (const auto state = m_State.Lock())
            return m_Id && state->Connected(m_Id);
        return false;
    }

    InputCaptureOverride::InputCaptureOverride(WeakRef<Detail::InputContextState> context, const AssetId map) noexcept
        : m_Context(std::move(context)), m_Map(map)
    {
    }

    InputCaptureOverride::InputCaptureOverride(InputCaptureOverride&& other) noexcept
        : m_Context(std::move(other.m_Context)), m_Map(std::exchange(other.m_Map, {}))
    {
    }

    InputCaptureOverride& InputCaptureOverride::operator=(InputCaptureOverride&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_Context = std::move(other.m_Context);
            m_Map = std::exchange(other.m_Map, {});
        }
        return *this;
    }

    InputCaptureOverride::~InputCaptureOverride() { Reset(); }

    void InputCaptureOverride::Reset() noexcept
    {
        if (m_Map)
        {
            if (const auto context = m_Context.Lock())
                context->CaptureBypassMaps.erase(m_Map);
            m_Map = {};
            m_Context.Reset();
        }
    }

    bool InputCaptureOverride::Active() const noexcept
    {
        const auto context = m_Context.Lock();
        return context && context->Open && m_Map && context->CaptureBypassMaps.contains(m_Map);
    }

    InputActionHandle::InputActionHandle(WeakRef<Detail::InputContextState> context, const AssetId action) noexcept
        : m_Context(std::move(context)), m_Action(action)
    {
    }
    InputActionHandle::operator bool() const noexcept
    {
        const auto context = m_Context.Lock();
        return context && context->Open && context->Actions.contains(m_Action);
    }
    InputActionPhase InputActionHandle::Phase() const noexcept
    {
        if (const auto context = m_Context.Lock(); context && context->Open && context->Actions.contains(m_Action))
            return context->Actions.at(m_Action).Phase;
        return InputActionPhase::Disabled;
    }
    InputValue InputActionHandle::Value() const noexcept
    {
        if (const auto context = m_Context.Lock(); context && context->Open && context->Actions.contains(m_Action))
            return context->Actions.at(m_Action).Value;
        return {};
    }
    bool InputActionHandle::WasStartedThisFrame() const noexcept
    {
        const auto context = m_Context.Lock();
        const auto runtime = context ? context->Runtime.Lock() : Ref<Detail::InputRuntimeState>{};
        return context && context->Open && runtime && runtime->Open && context->Actions.contains(m_Action) &&
               context->Actions.at(m_Action).StartedFrame == runtime->Frame;
    }
    bool InputActionHandle::WasPerformedThisFrame() const noexcept
    {
        const auto context = m_Context.Lock();
        const auto runtime = context ? context->Runtime.Lock() : Ref<Detail::InputRuntimeState>{};
        return context && context->Open && runtime && runtime->Open && context->Actions.contains(m_Action) &&
               context->Actions.at(m_Action).PerformedFrame == runtime->Frame;
    }
    bool InputActionHandle::WasCanceledThisFrame() const noexcept
    {
        const auto context = m_Context.Lock();
        const auto runtime = context ? context->Runtime.Lock() : Ref<Detail::InputRuntimeState>{};
        return context && context->Open && runtime && runtime->Open && context->Actions.contains(m_Action) &&
               context->Actions.at(m_Action).CanceledFrame == runtime->Frame;
    }

    InputActionContext::InputActionContext(std::unique_ptr<Impl> implementation) : m_Impl(std::move(implementation)) {}
    InputActionContext::~InputActionContext()
    {
        if (m_Impl && m_Impl->State)
            m_Impl->State->Open = false;
    }
    InputUserId InputActionContext::User() const noexcept { return m_Impl->State->User; }
    AssetId InputActionContext::Asset() const noexcept { return m_Impl->State->AssetIdValue; }
    InputActionAssetDefinition InputActionContext::Definition() const
    {
        const auto runtime = m_Impl->State->Runtime.Lock();
        if (!runtime)
            throw std::logic_error("Input context is no longer attached to a runtime.");
        runtime->RequireOwner("Definition");
        m_Impl->State->Rebuild();
        return m_Impl->State->Definition;
    }
    InputActionHandle InputActionContext::FindAction(const AssetId id) const noexcept
    {
        return m_Impl->State->Open && m_Impl->State->FindAction(id) ? InputActionHandle(m_Impl->State, id)
                                                                    : InputActionHandle{};
    }
    InputActionHandle InputActionContext::FindAction(const std::string_view mapName,
                                                     const std::string_view actionName) const noexcept
    {
        if (!m_Impl->State->Open)
            return {};
        const auto map =
            std::ranges::find(m_Impl->State->Definition.ActionMaps, mapName, &InputActionMapDefinition::Name);
        if (map == m_Impl->State->Definition.ActionMaps.end())
            return {};
        const auto action = std::ranges::find(map->Actions, actionName, &InputActionDefinition::Name);
        return action == map->Actions.end() ? InputActionHandle{} : InputActionHandle(m_Impl->State, action->Id);
    }
    bool InputActionContext::EnableMap(const AssetId id)
    {
        const auto runtime = m_Impl->State->Runtime.Lock();
        if (!runtime)
            throw std::logic_error("Input context is no longer attached to a runtime.");
        runtime->RequireOwner("EnableMap");
        const auto map = std::ranges::find(m_Impl->State->Definition.ActionMaps, id, &InputActionMapDefinition::Id);
        if (map == m_Impl->State->Definition.ActionMaps.end())
            return false;
        m_Impl->State->EnabledMaps.insert(id);
        for (const auto& action : map->Actions)
        {
            m_Impl->State->ExplicitlyDisabledActions.erase(action.Id);
            if (m_Impl->State->EnabledActions.insert(action.Id).second)
                m_Impl->State->Actions[action.Id].Phase = InputActionPhase::Waiting;
        }
        return true;
    }
    bool InputActionContext::EnableMap(const std::string_view name)
    {
        const auto found =
            std::ranges::find(m_Impl->State->Definition.ActionMaps, name, &InputActionMapDefinition::Name);
        return found != m_Impl->State->Definition.ActionMaps.end() && EnableMap(found->Id);
    }
    bool InputActionContext::DisableMap(const AssetId id)
    {
        const auto runtime = m_Impl->State->Runtime.Lock();
        if (!runtime)
            return false;
        runtime->RequireOwner("DisableMap");
        const auto map = std::ranges::find(m_Impl->State->Definition.ActionMaps, id, &InputActionMapDefinition::Id);
        if (map == m_Impl->State->Definition.ActionMaps.end())
            return false;
        bool changed = m_Impl->State->EnabledMaps.erase(id) > 0;
        for (const auto& action : map->Actions)
        {
            m_Impl->State->ExplicitlyEnabledActions.erase(action.Id);
            m_Impl->State->ExplicitlyDisabledActions.erase(action.Id);
            changed |= m_Impl->State->EnabledActions.erase(action.Id) > 0;
            auto& state = m_Impl->State->Actions[action.Id];
            if (state.Active)
                runtime->Emit(*m_Impl->State, state, *map, action, state.ActiveDevice, InputActionPhase::Canceled,
                              InputValue{action.ValueType});
            state.Active = false;
            state.Phase = InputActionPhase::Disabled;
        }
        return changed;
    }
    bool InputActionContext::EnableAction(const AssetId id)
    {
        const auto runtime = m_Impl->State->Runtime.Lock();
        if (!runtime)
            throw std::logic_error("Input context is no longer attached to a runtime.");
        runtime->RequireOwner("EnableAction");
        if (!m_Impl->State->FindAction(id))
            return false;
        m_Impl->State->ExplicitlyEnabledActions.insert(id);
        m_Impl->State->ExplicitlyDisabledActions.erase(id);
        if (m_Impl->State->EnabledActions.insert(id).second)
            m_Impl->State->Actions[id].Phase = InputActionPhase::Waiting;
        return true;
    }
    bool InputActionContext::DisableAction(const AssetId id)
    {
        const auto runtime = m_Impl->State->Runtime.Lock();
        if (!runtime)
            return false;
        runtime->RequireOwner("DisableAction");
        for (const auto& map : m_Impl->State->Definition.ActionMaps)
        {
            const auto action = std::ranges::find(map.Actions, id, &InputActionDefinition::Id);
            if (action == map.Actions.end())
                continue;
            m_Impl->State->ExplicitlyEnabledActions.erase(id);
            if (m_Impl->State->EnabledMaps.contains(map.Id))
                m_Impl->State->ExplicitlyDisabledActions.insert(id);
            const bool changed = m_Impl->State->EnabledActions.erase(id) > 0;
            auto& state = m_Impl->State->Actions[id];
            if (state.Active)
                runtime->Emit(*m_Impl->State, state, map, *action, state.ActiveDevice, InputActionPhase::Canceled,
                              InputValue{action->ValueType});
            state.Active = false;
            state.Phase = InputActionPhase::Disabled;
            return changed;
        }
        return false;
    }
    void InputActionContext::DisableAll()
    {
        m_Impl->State->EnabledMaps.clear();
        m_Impl->State->ExplicitlyEnabledActions.clear();
        m_Impl->State->ExplicitlyDisabledActions.clear();
        std::vector<AssetId> enabled(m_Impl->State->EnabledActions.begin(), m_Impl->State->EnabledActions.end());
        for (const auto action : enabled)
            (void)DisableAction(action);
    }
    bool InputActionContext::MapEnabled(const AssetId id) const noexcept
    {
        return m_Impl->State->Open && m_Impl->State->EnabledMaps.contains(id);
    }
    bool InputActionContext::ActionEnabled(const AssetId id) const noexcept
    {
        return m_Impl->State->Open && m_Impl->State->EnabledActions.contains(id);
    }
    InputCaptureOverride InputActionContext::OverrideUiCapture(const AssetId map)
    {
        const auto runtime = m_Impl->State->Runtime.Lock();
        if (!runtime)
            throw std::logic_error("Input context is no longer attached to a runtime.");
        runtime->RequireOwner("OverrideUiCapture");
        if (std::ranges::none_of(m_Impl->State->Definition.ActionMaps,
                                 [map](const auto& definition) { return definition.Id == map; }))
            throw std::invalid_argument("Input capture override requires a known action map.");
        if (!m_Impl->State->CaptureBypassMaps.insert(map).second)
            throw std::logic_error("Input action map already has an active UI capture override.");
        return InputCaptureOverride(m_Impl->State, map);
    }
    InputCaptureOverride InputActionContext::OverrideUiCapture(const std::string_view map)
    {
        const auto found =
            std::ranges::find(m_Impl->State->Definition.ActionMaps, map, &InputActionMapDefinition::Name);
        if (found == m_Impl->State->Definition.ActionMaps.end())
            throw std::invalid_argument("Input capture override requires a known action map.");
        return OverrideUiCapture(found->Id);
    }
    InputActionSubscription InputActionContext::Subscribe(AssetId action,
                                                          std::function<void(const InputActionEvent&)> callback)
    {
        const auto runtime = m_Impl->State->Runtime.Lock();
        if (!runtime)
            throw std::logic_error("Input context is no longer attached to a runtime.");
        runtime->RequireOwner("Subscribe");
        if (!m_Impl->State->FindAction(action) || !callback)
            throw std::invalid_argument("Input action subscription requires a known action and callback.");
        const auto id = m_Impl->State->NextSubscriber++;
        m_Impl->State->Subscribers.emplace(id, std::pair{action, std::move(callback)});
        return InputActionSubscription(m_Impl->Subscriptions, id);
    }

    void InputActionContext::SaveBindingOverrides(const std::string_view profile) const
    {
        const auto runtime = m_Impl->State->Runtime.Lock();
        if (!runtime)
            throw std::logic_error("Input context is no longer attached to a runtime.");
        runtime->RequireOwner("SaveBindingOverrides");
        const auto path = Detail::InputBindingProfilePath(runtime->Specification.BindingOverrideDirectory, profile);
        Json overrides = Json::array();
        for (const auto& [binding, value] : m_Impl->State->Overrides)
            overrides.push_back({{"binding", binding.ToString()}, {"path", value}});
        for (const auto binding : m_Impl->State->DisabledBindings)
            overrides.push_back({{"binding", binding.ToString()}, {"disabled", true}});
        const Json document{{"schemaVersion", 1},
                            {"asset", m_Impl->State->AssetIdValue.ToString()},
                            {"overrides", std::move(overrides)}};
        std::filesystem::create_directories(path.parent_path());
        Detail::WriteTextFileAtomically(path, document.dump(2) + '\n');
    }

    std::size_t InputActionContext::LoadBindingOverrides(const std::string_view profile)
    {
        const auto runtime = m_Impl->State->Runtime.Lock();
        if (!runtime)
            throw std::logic_error("Input context is no longer attached to a runtime.");
        runtime->RequireOwner("LoadBindingOverrides");
        const auto path = Detail::InputBindingProfilePath(runtime->Specification.BindingOverrideDirectory, profile);
        if (!std::filesystem::exists(path))
            return 0;
        std::ifstream stream(path, std::ios::binary);
        Json document;
        stream >> document;
        if (!stream || document.value("schemaVersion", 0) != 1 ||
            AssetId::Parse(document.at("asset").get<std::string>()) != m_Impl->State->AssetIdValue)
            throw std::runtime_error("Input binding override profile is malformed or belongs to another asset.");
        std::size_t applied = 0;
        for (const auto& item : document.at("overrides"))
        {
            const auto binding = AssetId::Parse(item.at("binding").get<std::string>());
            if (!m_Impl->State->FindBinding(binding))
                continue;
            if (item.value("disabled", false))
                m_Impl->State->DisabledBindings.insert(binding);
            else
                m_Impl->State->Overrides[binding] = item.at("path").get<std::string>();
            ++applied;
        }
        return applied;
    }
    void InputActionContext::ClearBindingOverrides()
    {
        const auto runtime = m_Impl->State->Runtime.Lock();
        if (!runtime)
            throw std::logic_error("Input context is no longer attached to a runtime.");
        runtime->RequireOwner("ClearBindingOverrides");
        m_Impl->State->Overrides.clear();
        m_Impl->State->DisabledBindings.clear();
    }

    InteractiveRebindOperation::InteractiveRebindOperation(std::unique_ptr<Impl> implementation)
        : m_Impl(std::move(implementation))
    {
    }
    InteractiveRebindOperation::~InteractiveRebindOperation() { Cancel(); }
    RebindStatus InteractiveRebindOperation::Status() const noexcept { return m_Impl->State->Status; }
    AssetId InteractiveRebindOperation::TargetBinding() const noexcept
    {
        if (m_Impl->State->Targets.empty())
            return {};
        return m_Impl->State->Targets[std::min(m_Impl->State->TargetIndex, m_Impl->State->Targets.size() - 1)];
    }
    std::string InteractiveRebindOperation::CandidatePath() const { return m_Impl->State->Candidate; }
    std::vector<RebindConflict> InteractiveRebindOperation::Conflicts() const { return m_Impl->State->Conflicts; }
    double InteractiveRebindOperation::RemainingSeconds() const noexcept { return m_Impl->State->Remaining; }
    void InteractiveRebindOperation::Apply(const RebindConflictResolution resolution)
    {
        const auto runtime = m_Impl->State->Runtime.Lock();
        if (!runtime)
            throw std::logic_error("Interactive rebind is no longer attached to a runtime.");
        runtime->RequireOwner("ApplyRebind");
        if (m_Impl->State->Status != RebindStatus::Candidate)
            throw std::logic_error("Interactive rebind has no candidate to apply.");
        const auto context = m_Impl->State->Context.Lock();
        if (!context)
            throw std::logic_error("Interactive rebind context is no longer available.");
        if (resolution == RebindConflictResolution::Cancel)
        {
            Cancel();
            return;
        }
        if (resolution == RebindConflictResolution::Replace)
        {
            for (const auto& conflict : m_Impl->State->Conflicts)
                context->DisabledBindings.insert(conflict.Binding);
        }
        const auto target = m_Impl->State->Targets[m_Impl->State->TargetIndex];
        context->Overrides[target] = m_Impl->State->Candidate;
        context->DisabledBindings.erase(target);
        if (++m_Impl->State->TargetIndex < m_Impl->State->Targets.size())
        {
            m_Impl->State->Candidate.clear();
            m_Impl->State->Conflicts.clear();
            m_Impl->State->Remaining = m_Impl->State->Options.TimeoutSeconds;
            m_Impl->State->Status = RebindStatus::Listening;
        }
        else
            m_Impl->State->Status = RebindStatus::Completed;
    }
    void InteractiveRebindOperation::Cancel() noexcept
    {
        if (m_Impl &&
            (m_Impl->State->Status == RebindStatus::Listening || m_Impl->State->Status == RebindStatus::Candidate))
            m_Impl->State->Status = RebindStatus::Cancelled;
    }

    InputSystem::InputSystem(InputSystemSpecification specification, const Ref<WindowSystem>& windows,
                             Ref<AssetSystem> assets, Ref<EventBus> events)
        : m_Impl(std::make_unique<Impl>(CreateRef<Detail::InputRuntimeState>(std::move(specification), windows,
                                                                             std::move(assets), std::move(events))))
    {
    }
    InputSystem::~InputSystem() { Close(); }
    std::vector<InputDeviceDescriptor> InputSystem::Devices() const
    {
        m_Impl->State->RequireOwner("Devices");
        std::vector<InputDeviceDescriptor> result;
        result.reserve(m_Impl->State->Devices.size());
        for (const auto& [id, device] : m_Impl->State->Devices)
        {
            (void)id;
            result.push_back(device.Descriptor);
        }
        std::ranges::sort(result, {}, &InputDeviceDescriptor::Id);
        return result;
    }
    std::optional<InputDeviceId> InputSystem::CurrentDevice(const InputDeviceType type) const noexcept
    {
        if (!m_Impl || !m_Impl->State || !m_Impl->State->Open ||
            std::this_thread::get_id() != m_Impl->State->OwnerThread)
            return std::nullopt;
        const auto index = static_cast<std::size_t>(type);
        if (index >= m_Impl->State->LastActiveDevices.size())
            return std::nullopt;
        const auto preferred = m_Impl->State->LastActiveDevices[index];
        if (const auto found = m_Impl->State->Devices.find(preferred); found != m_Impl->State->Devices.end() &&
                                                                       found->second.Descriptor.Connected &&
                                                                       found->second.Descriptor.Type == type)
            return preferred;
        const auto fallback =
            std::ranges::find_if(m_Impl->State->Devices, [&](const auto& item)
                                 { return item.second.Descriptor.Connected && item.second.Descriptor.Type == type; });
        return fallback == m_Impl->State->Devices.end() ? std::nullopt : std::optional(fallback->first);
    }
    std::optional<InputControlSnapshot> InputSystem::ReadControl(const InputDeviceId device,
                                                                 const std::string_view path) const noexcept
    {
        if (!m_Impl || !m_Impl->State || !m_Impl->State->Open || !device || path.empty() ||
            std::this_thread::get_id() != m_Impl->State->OwnerThread)
            return std::nullopt;
        if (m_Impl->State->GameplayPlayback)
        {
            const auto recorded = m_Impl->State->PlaybackControls.find({device, std::string(path)});
            if (recorded != m_Impl->State->PlaybackControls.end())
                return recorded->second;
            return InputControlSnapshot{device, {}, m_Impl->State->Frame};
        }
        const auto found = m_Impl->State->Devices.find(device);
        if (found == m_Impl->State->Devices.end() || !found->second.Descriptor.Connected)
            return std::nullopt;
        const auto control = found->second.Controls.find(std::string(path));
        return InputControlSnapshot{device, control == found->second.Controls.end() ? InputValue{} : control->second,
                                    m_Impl->State->Frame, found->second.PressedControls.contains(std::string(path)),
                                    found->second.ReleasedControls.contains(std::string(path))};
    }
    std::vector<InputUserDescriptor> InputSystem::Users() const
    {
        m_Impl->State->RequireOwner("Users");
        std::vector<InputUserDescriptor> result;
        result.reserve(m_Impl->State->Users.size());
        for (const auto& [id, user] : m_Impl->State->Users)
        {
            (void)id;
            result.push_back(user.Descriptor);
        }
        std::ranges::sort(result, {}, &InputUserDescriptor::Id);
        return result;
    }
    InputUserId InputSystem::CreateUser(std::string name) { return m_Impl->State->CreateUser(std::move(name)); }
    bool InputSystem::RemoveUser(const InputUserId user)
    {
        m_Impl->State->RequireOwner("RemoveUser");
        const auto found = m_Impl->State->Users.find(user);
        if (found == m_Impl->State->Users.end())
            return false;
        const auto devices = found->second.Descriptor.Devices;
        m_Impl->State->Users.erase(found);
        for (const auto device : devices)
            m_Impl->State->Devices.at(device).Descriptor.Paired = m_Impl->State->DevicePaired(device);
        if (m_Impl->State->Events)
            (void)m_Impl->State->Events->Dispatch(InputUserLeftEvent{user});
        return true;
    }
    bool InputSystem::PairDevice(const InputUserId user, const InputDeviceId device, const bool shared)
    {
        m_Impl->State->RequireOwner("PairDevice");
        return m_Impl->State->Pair(user, device, shared);
    }
    bool InputSystem::UnpairDevice(const InputUserId user, const InputDeviceId device)
    {
        m_Impl->State->RequireOwner("UnpairDevice");
        const auto found = m_Impl->State->Users.find(user);
        if (found == m_Impl->State->Users.end() || !Contains(found->second.Descriptor.Devices, device))
            return false;
        std::erase(found->second.Descriptor.Devices, device);
        m_Impl->State->Devices.at(device).Descriptor.Paired = m_Impl->State->DevicePaired(device);
        m_Impl->State->SelectScheme(found->second);
        return true;
    }
    bool InputSystem::SetControlScheme(const InputUserId user, std::string scheme, const bool locked)
    {
        m_Impl->State->RequireOwner("SetControlScheme");
        if (scheme.empty() || scheme.size() > 128)
            throw std::invalid_argument("Input control scheme names must contain between 1 and 128 bytes.");
        const auto found = m_Impl->State->Users.find(user);
        if (found == m_Impl->State->Users.end())
            return false;
        found->second.Descriptor.ControlScheme = std::move(scheme);
        found->second.SchemeLocked = locked;
        return true;
    }
    bool InputSystem::ClearControlSchemeLock(const InputUserId user)
    {
        m_Impl->State->RequireOwner("ClearControlSchemeLock");
        const auto found = m_Impl->State->Users.find(user);
        if (found == m_Impl->State->Users.end())
            return false;
        found->second.SchemeLocked = false;
        m_Impl->State->SelectScheme(found->second);
        return true;
    }
    bool InputSystem::SetGamepadRumble(const InputDeviceId device, const float lowFrequency, const float highFrequency,
                                       const TimeStep duration)
    {
        m_Impl->State->RequireOwner("SetGamepadRumble");
        if (!std::isfinite(lowFrequency) || !std::isfinite(highFrequency) || lowFrequency < 0.0F ||
            lowFrequency > 1.0F || highFrequency < 0.0F || highFrequency > 1.0F || !std::isfinite(duration.Seconds()) ||
            duration.Seconds() < 0.0 || duration.Seconds() > 60.0)
        {
            throw std::invalid_argument("Gamepad rumble strengths must be normalized and duration must be bounded.");
        }
        const auto found = m_Impl->State->Devices.find(device);
        if (found == m_Impl->State->Devices.end() || found->second.Descriptor.Type != InputDeviceType::Gamepad ||
            !found->second.Descriptor.Connected || !found->second.Gamepad)
        {
            return false;
        }
        constexpr float maximumMotorValue = static_cast<float>(std::numeric_limits<std::uint16_t>::max());
        const auto low = static_cast<std::uint16_t>(std::lround(lowFrequency * maximumMotorValue));
        const auto high = static_cast<std::uint16_t>(std::lround(highFrequency * maximumMotorValue));
        const auto milliseconds = static_cast<std::uint32_t>(std::ceil(duration.Milliseconds()));
        return SDL_RumbleGamepad(found->second.Gamepad, low, high, milliseconds);
    }
    Ref<InputActionContext> InputSystem::CreateActionContext(const AssetId asset, const InputUserId user,
                                                             const InputContextRole role)
    {
        m_Impl->State->RequireOwner("CreateActionContext");
        if (!m_Impl->State->Users.contains(user))
            throw std::invalid_argument("Input action context requires a known input user.");
        auto state = CreateRef<Detail::InputContextState>();
        state->Runtime = m_Impl->State;
        state->AssetHandle = m_Impl->State->Assets->Load<InputActionAsset>(asset, AssetPriority::Critical);
        state->AssetIdValue = asset;
        state->User = user;
        state->Role = role;
        state->ContextId = m_Impl->State->NextContext++;
        state->Rebuild();
        m_Impl->State->Contexts.push_back(state);
        return CreateRef<InputActionContext>(std::make_unique<InputActionContext::Impl>(std::move(state)));
    }
    Ref<InputActionContext> InputSystem::CreateActionContext(InputActionAssetDefinition definition,
                                                             const InputUserId user, const InputContextRole role)
    {
        m_Impl->State->RequireOwner("CreateActionContext");
        if (!m_Impl->State->Users.contains(user))
            throw std::invalid_argument("Input action context requires a known input user.");
        InputActionAsset::Validate(definition);
        auto state = CreateRef<Detail::InputContextState>();
        state->Runtime = m_Impl->State;
        state->User = user;
        state->Role = role;
        state->ContextId = m_Impl->State->NextContext++;
        state->Definition = std::move(definition);
        state->AssetRevision = state->AssetHandle.Revision();
        for (const auto& map : state->Definition.ActionMaps)
            for (const auto& action : map.Actions)
                state->Actions.emplace(action.Id, Detail::ActionRuntime{});
        m_Impl->State->Contexts.push_back(state);
        return CreateRef<InputActionContext>(std::make_unique<InputActionContext::Impl>(std::move(state)));
    }
    Ref<InteractiveRebindOperation> InputSystem::BeginInteractiveRebind(const Ref<InputActionContext>& context,
                                                                        const AssetId binding,
                                                                        InteractiveRebindOptions options)
    {
        m_Impl->State->RequireOwner("BeginInteractiveRebind");
        if (!context || !context->m_Impl->State->FindBinding(binding) || options.MagnitudeThreshold <= 0.0F ||
            options.MagnitudeThreshold > 1.0F || options.TimeoutSeconds <= 0.0 || options.TimeoutSeconds > 60.0)
            throw std::invalid_argument("Interactive rebind options or target binding are invalid.");
        if (const auto active = m_Impl->State->ActiveRebind.Lock();
            active && (active->Status == RebindStatus::Listening || active->Status == RebindStatus::Candidate))
            throw std::logic_error("Only one interactive rebind operation may be active.");
        auto state = CreateRef<Detail::RebindState>();
        state->Runtime = m_Impl->State;
        state->Context = context->m_Impl->State;
        state->Options = std::move(options);
        const auto* target = context->m_Impl->State->FindBinding(binding);
        if (target && !target->Composite.empty() && state->Options.SequenceCompositeParts)
        {
            for (const auto& map : context->m_Impl->State->Definition.ActionMaps)
            {
                const auto root = std::ranges::find(map.Bindings, binding, &InputBindingDefinition::Id);
                if (root == map.Bindings.end())
                    continue;
                for (auto part = std::next(root); part != map.Bindings.end() && !part->CompositePart.empty(); ++part)
                    state->Targets.push_back(part->Id);
                break;
            }
        }
        if (state->Targets.empty())
            state->Targets.push_back(binding);
        state->Remaining = state->Options.TimeoutSeconds;
        m_Impl->State->ActiveRebind = state;
        return CreateRef<InteractiveRebindOperation>(
            std::make_unique<InteractiveRebindOperation::Impl>(std::move(state)));
    }
    FixedTickInputSnapshot InputSystem::CaptureFixedTick(const std::uint64_t tick)
    {
        m_Impl->State->RequireOwner("CaptureFixedTick");
        FixedTickInputSnapshot snapshot;
        snapshot.Tick = tick;
        constexpr std::uint64_t offsetBasis = 14695981039346656037ULL;
        constexpr std::uint64_t prime = 1099511628211ULL;
        snapshot.InputMapFingerprint = offsetBasis;
        const auto hashByte = [&](const std::uint8_t byte)
        {
            snapshot.InputMapFingerprint ^= byte;
            snapshot.InputMapFingerprint *= prime;
        };
        const auto hash64 = [&](const std::uint64_t value)
        {
            for (std::size_t byte = 0; byte < sizeof(value); ++byte)
                hashByte(static_cast<std::uint8_t>(value >> (byte * 8U)));
        };

        for (auto iterator = m_Impl->State->Contexts.begin(); iterator != m_Impl->State->Contexts.end();)
        {
            const auto context = iterator->Lock();
            if (!context)
            {
                iterator = m_Impl->State->Contexts.erase(iterator);
                continue;
            }
            ++iterator;
            if (context->Role != InputContextRole::Gameplay)
                continue;
            context->Rebuild();
            hash64(context->ContextId);
            hash64(context->AssetIdValue.High());
            hash64(context->AssetIdValue.Low());
            for (const auto& map : context->Definition.ActionMaps)
            {
                hash64(map.Id.High());
                hash64(map.Id.Low());
                for (const auto& action : map.Actions)
                {
                    hash64(action.Id.High());
                    hash64(action.Id.Low());
                    hashByte(static_cast<std::uint8_t>(action.Type));
                    hashByte(static_cast<std::uint8_t>(action.ValueType));
                    auto& runtime = context->Actions[action.Id];
                    const bool relative = Detail::InputRuntimeState::IsRelativeAction(*context, map, action);
                    snapshot.Actions.push_back(
                        {context->ContextId, context->AssetIdValue, map.Id, action.Id, context->User, runtime.Phase,
                         relative ? runtime.PendingRelativeValue : runtime.Value, runtime.PendingStarted,
                         runtime.PendingPerformed, runtime.PendingCanceled});
                    if (relative)
                        runtime.PendingRelativeValue = InputValue{action.ValueType};
                    runtime.PendingStarted = false;
                    runtime.PendingPerformed = false;
                    runtime.PendingCanceled = false;
                }
            }
        }
        std::ranges::sort(
            snapshot.Actions,
            [](const FixedTickInputAction& left, const FixedTickInputAction& right)
            {
                return std::tuple{left.Context, left.ContextAsset, left.Map, left.Action, left.User.Value()} <
                       std::tuple{right.Context, right.ContextAsset, right.Map, right.Action, right.User.Value()};
            });
        for (const auto& [deviceId, device] : m_Impl->State->Devices)
        {
            if (!device.Descriptor.Connected)
                continue;
            for (const auto& [path, value] : device.Controls)
            {
                const bool pressed = device.PressedControls.contains(path);
                const bool released = device.ReleasedControls.contains(path);
                if (value.Magnitude() <= 0.0001F && !pressed && !released)
                    continue;
                snapshot.Controls.push_back({deviceId, path, value, pressed, released});
            }
        }
        std::ranges::sort(
            snapshot.Controls, [](const auto& left, const auto& right)
            { return std::tuple{left.Device.Value(), left.Path} < std::tuple{right.Device.Value(), right.Path}; });
        return snapshot;
    }

    void InputSystem::ApplyFixedTick(const FixedTickInputSnapshot& snapshot)
    {
        m_Impl->State->RequireOwner("ApplyFixedTick");
        m_Impl->State->PlaybackControls.clear();
        for (const auto& control : snapshot.Controls)
        {
            m_Impl->State->PlaybackControls.emplace(std::pair{control.Device, control.Path},
                                                    InputControlSnapshot{control.Device, control.Value,
                                                                         m_Impl->State->Frame, control.Pressed,
                                                                         control.Released});
        }
        for (const auto& weak : m_Impl->State->Contexts)
        {
            const auto context = weak.Lock();
            if (!context || context->Role != InputContextRole::Gameplay)
                continue;
            for (const auto& map : context->Definition.ActionMaps)
            {
                for (const auto& definition : map.Actions)
                {
                    auto& runtime = context->Actions[definition.Id];
                    runtime.Phase = context->EnabledActions.contains(definition.Id) ? InputActionPhase::Waiting
                                                                                    : InputActionPhase::Disabled;
                    runtime.Value = InputValue{definition.ValueType};
                    runtime.Active = false;
                    runtime.PendingStarted = false;
                    runtime.PendingPerformed = false;
                    runtime.PendingCanceled = false;
                }
            }
        }

        for (const auto& recorded : snapshot.Actions)
        {
            const auto context = std::ranges::find_if(m_Impl->State->Contexts,
                                                      [&](const WeakRef<Detail::InputContextState>& weak)
                                                      {
                                                          const auto candidate = weak.Lock();
                                                          return candidate &&
                                                                 candidate->Role == InputContextRole::Gameplay &&
                                                                 candidate->ContextId == recorded.Context &&
                                                                 candidate->User == recorded.User &&
                                                                 candidate->AssetIdValue == recorded.ContextAsset;
                                                      });
            if (context == m_Impl->State->Contexts.end())
                continue;
            const auto state = context->Lock();
            if (!state)
                continue;
            const auto map =
                std::ranges::find(state->Definition.ActionMaps, recorded.Map, &InputActionMapDefinition::Id);
            if (map == state->Definition.ActionMaps.end())
                continue;
            const auto definition = std::ranges::find(map->Actions, recorded.Action, &InputActionDefinition::Id);
            if (definition == map->Actions.end())
                continue;
            auto& runtime = state->Actions[recorded.Action];
            runtime.Phase = recorded.Phase;
            runtime.Value = recorded.Value;
            runtime.Active = recorded.Value.Magnitude() >= 0.5F;
            if (recorded.Started)
                m_Impl->State->Emit(*state, runtime, *map, *definition, {}, InputActionPhase::Started, recorded.Value);
            if (recorded.Performed)
                m_Impl->State->Emit(*state, runtime, *map, *definition, {}, InputActionPhase::Performed,
                                    recorded.Value);
            if (recorded.Canceled)
                m_Impl->State->Emit(*state, runtime, *map, *definition, {}, InputActionPhase::Canceled, recorded.Value);
            runtime.Phase = recorded.Phase;
            runtime.Value = recorded.Value;
        }
    }

    void InputSystem::SetGameplayPlayback(const bool enabled)
    {
        m_Impl->State->RequireOwner("SetGameplayPlayback");
        m_Impl->State->GameplayPlayback = enabled;
        if (!enabled)
            m_Impl->State->PlaybackControls.clear();
    }

    std::uint64_t InputSystem::Frame() const noexcept { return m_Impl->State->Frame; }
    bool InputSystem::IsOpen() const noexcept { return m_Impl && m_Impl->State->Open; }
    void InputSystem::AdvanceFrame(const TimeStep delta, const UiCaptureState capture, const bool suspended)
    {
        m_Impl->State->Advance(delta, capture, suspended);
    }
    void InputSystem::Close() noexcept
    {
        if (m_Impl && m_Impl->State)
            m_Impl->State->Close();
    }
} // namespace Keire
