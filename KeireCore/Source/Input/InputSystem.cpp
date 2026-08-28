#include "Keire/Input/Input.h"

#include "Keire/BuildInfo.h"
#include "KeireInternal/Assets/AssetInternal.h"
#include "KeireInternal/FileSystem.h"
#include "KeireInternal/InputInternal.h"
#include "KeireInternal/WindowInternal.h"

#include "KeireInternal/Input/InputContextState.h"
#include "KeireInternal/Input/InputRuntimeState.h"

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
    } // namespace

    namespace Detail
    {

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

        bool InputContextState::OccurredThisFrame(const AssetId action,
                                                  const ActionOccurrence occurrence) const noexcept
        {
            const auto runtime = Runtime.Lock();
            const auto found = Actions.find(action);
            if (!Open || !runtime || !runtime->Open || found == Actions.end())
                return false;
            switch (occurrence)
            {
            case ActionOccurrence::Started:
                return found->second.StartedFrame == runtime->Frame;
            case ActionOccurrence::Performed:
                return found->second.PerformedFrame == runtime->Frame;
            case ActionOccurrence::Canceled:
                return found->second.CanceledFrame == runtime->Frame;
            }
            return false;
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
        if (found == m_Impl->State->Users.end() || !Detail::Contains(found->second.Descriptor.Devices, device))
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
